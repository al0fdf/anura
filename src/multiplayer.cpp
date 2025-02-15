/*
	Copyright (C) 2003-2014 by David White <davewx7@gmail.com>

	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	   1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgement in the product documentation would be
	   appreciated but is not required.

	   2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.

	   3. This notice may not be removed or altered from any source
	   distribution.
*/

#include <sstream>
#include <string>

#include <cstdint>
#include <numeric>
#include <stdio.h>

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <deque>
#include <functional>

#include "asserts.hpp"
#include "controls.hpp"
#include "formatter.hpp"
#include "level.hpp"
#include "multiplayer.hpp"
#include "preferences.hpp"
#include "profile_timer.hpp"
#include "random.hpp"
#include "regex_utils.hpp"
#include "unit_test.hpp"

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace multiplayer
{
	namespace
	{
		std::shared_ptr<boost::asio::io_service> asio_service;
		std::shared_ptr<tcp::socket> tcp_socket;
		std::shared_ptr<udp::socket> udp_socket;
		std::shared_ptr<udp::endpoint> udp_endpoint;

		std::vector<std::shared_ptr<udp::endpoint> > udp_endpoint_peers;

		int32_t id;
		int player_slot;

		bool udp_packet_waiting()
		{
			if(!udp_socket) {
				return false;
			}

			boost::asio::socket_base::bytes_readable command(true);
			udp_socket->io_control(command);
			return command.get() != 0;
		}

		bool tcp_packet_waiting()
		{
			if(!tcp_socket) {
				return false;
			}

			boost::asio::socket_base::bytes_readable command(true);
			tcp_socket->io_control(command);
			return command.get() != 0;
		}
	}

	int slot()
	{
		return player_slot;
	}

	Manager::Manager(bool activate)
	{
		if(activate) {
			asio_service.reset(new boost::asio::io_service);
		}
	}

	Manager::~Manager() {
		udp_endpoint.reset();
		tcp_socket.reset();
		udp_socket.reset();
		asio_service.reset();
		player_slot = 0;
	}

	void setup_networked_game(const std::string& server)
	{
		boost::asio::io_service& io_service = *asio_service;
		tcp::resolver resolver(io_service);

		tcp::resolver::query query(server, "17002");

		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;

		tcp_socket.reset(new tcp::socket(io_service));
		tcp::socket& socket = *tcp_socket;
		boost::system::error_code error = boost::asio::error::host_not_found;
		while(error && endpoint_iterator != end) {
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}

		if(error) {
			LOG_INFO("NETWORK ERROR: Can't connect to host: " << server);
			throw multiplayer::Error();
		}

		std::array<char, 4> initial_response;
		size_t len = socket.read_some(boost::asio::buffer(initial_response), error);
		if(error) {
			LOG_INFO("ERROR READING INITIAL RESPONSE");
			throw multiplayer::Error();
		}

		if(len != 4) {
			LOG_INFO("INITIAL RESPONSE HAS THE WRONG SIZE: " << (int)len);
			throw multiplayer::Error();
		}

		memcpy(&id, &initial_response[0], 4);

		LOG_INFO("ID: " << id);

		udp::resolver udp_resolver(io_service);
		udp::resolver::query udp_query(udp::v4(), server, "17001");
		udp_endpoint.reset(new udp::endpoint);
		*udp_endpoint = *udp_resolver.resolve(udp_query);
		udp::endpoint& receiver_endpoint = *udp_endpoint;

		udp_socket.reset(new udp::socket(io_service));
		udp_socket->open(udp::v4());

		std::array<char, 4> udp_msg;
		memcpy(&udp_msg[0], &id, 4);

	//	udp_socket->send_to(boost::asio::buffer(udp_msg), receiver_endpoint);

		LOG_INFO("SENT UDP PACKET");

		udp::endpoint sender_endpoint;
	//	len = udp_socket->receive_from(boost::asio::buffer(udp_msg), sender_endpoint);
		LOG_INFO("GOT UDP PACKET: " << (int)len);

		std::string msg = "greetings!";
		socket.write_some(boost::asio::buffer(msg), error);
		if(error) {
			LOG_INFO("NETWORK ERROR: Could not send data");
			throw multiplayer::Error();
		}
	}

	namespace
	{
		void send_confirm_packet(int nplayer, std::vector<char>& msg, bool has_confirm) {
			msg.resize(6);
			msg[0] = has_confirm ? 'a' : 'A';
			memcpy(&msg[1], &id, 4);
			msg.back() = player_slot;

			if(nplayer == player_slot || nplayer < 0 || static_cast<unsigned>(nplayer) >= udp_endpoint_peers.size()) {
				return;
			}

			LOG_INFO("SENDING CONFIRM TO " << udp_endpoint_peers[nplayer]->port() << ": " << nplayer);
			udp_socket->send_to(boost::asio::buffer(msg), *udp_endpoint_peers[nplayer]);
		}
	}

	void sync_start_time(const Level& lvl, std::function<bool()> idle_fn)
	{
		if(!tcp_socket) {
			return;
		}

		ffl::IntrusivePtr<const Level> lvl_ptr(&lvl);
		ffl::IntrusivePtr<Client> client(new Client(lvl.id(), lvl.players().size()));

		while(!client->PumpStartLevel()) {
			if(idle_fn) {
				const bool res = idle_fn();
				if(!res) {
					LOG_INFO("quitting game...");
					throw multiplayer::Error();
				}
			}
		}
	}

	namespace {
		struct QueuedMessages {
			std::vector<std::function<void()> > send_fn;
		};

		PREF_INT(fakelag, 0, "Number of milliseconds of artificial lag to introduce to multiplayer");
	}

	void send_and_receive()
	{
		if(!udp_socket || controls::num_players() == 1) {
			return;
		}

		static std::deque<QueuedMessages> message_queue;

		//send our ID followed by the send packet.
		std::vector<char> send_buf(5);
		send_buf[0] = 'C';
		memcpy(&send_buf[1], &id, 4);
		controls::write_control_packet(send_buf);

		if(message_queue.empty() == false) {
			QueuedMessages& msg = message_queue.front();
			for(const std::function<void()>& fn : msg.send_fn) {
				fn();
			}

			message_queue.pop_front();
		}

		for(int n = 0; n != udp_endpoint_peers.size(); ++n) {
			if(n == player_slot) {
				continue;
			}

			const unsigned lagframes = g_fakelag/20;

			if(lagframes == 0) {
				udp_socket->send_to(boost::asio::buffer(send_buf), *udp_endpoint_peers[n]);
			} else {
				while(lagframes >= message_queue.size()) {
					message_queue.push_back(QueuedMessages());
				}

				message_queue[lagframes].send_fn.push_back([=]() {
					udp_socket->send_to(boost::asio::buffer(send_buf), *udp_endpoint_peers[n]);
				});
			}
		}

		receive();
	}

	void receive()
	{
		while(udp_packet_waiting()) {
			udp::endpoint sender_endpoint;
			std::array<char, 4096> udp_msg;
			size_t len = udp_socket->receive(boost::asio::buffer(udp_msg));
			if(len == 0 || udp_msg[0] != 'C') {
				continue;
			}

			if(len < 5) {
				LOG_INFO("UDP PACKET TOO SHORT: " << (int)len);
				continue;
			}

			controls::read_control_packet(&udp_msg[5], len - 5);
		}
	}

	Client::Client(const std::string& game_id, int nplayers) : game_id_(game_id), nplayers_(nplayers), completed_(false)
	{
		//find our host and port number within our NAT and tell the server
		//about it, so if two servers from behind the same NAT connect to
		//the server, it can tell them how to connect directly to each other.
		std::string local_host;
		int local_port = 0;

		{
			//send something on the UDP socket, just so that we get a port.
			std::vector<char> send_buf;
			send_buf.push_back('.');
			udp_socket->send_to(boost::asio::buffer(send_buf), *udp_endpoint);

			tcp::endpoint local_endpoint = tcp_socket->local_endpoint();
			local_host = local_endpoint.address().to_string();
			local_port = udp_socket->local_endpoint().port();

			LOG_INFO("LOCAL ENDPOINT: " << local_host << ":" << local_port);
		}

		std::ostringstream s;
		s << "READY/" << game_id_ << "/" << nplayers_ << "/" << local_host << " " << local_port;
		boost::system::error_code error = boost::asio::error::host_not_found;
		tcp_socket->write_some(boost::asio::buffer(s.str()), error);
		if(error) {
			LOG_INFO("ERROR WRITING TO SOCKET");
			throw multiplayer::Error();
		}
	}

	bool Client::PumpStartLevel()
	{
		if(!tcp_packet_waiting()) {
			std::vector<char> send_buf(5);
			send_buf[0] = 'Z';
			memcpy(&send_buf[1], &id, 4);
			udp_socket->send_to(boost::asio::buffer(send_buf), *udp_endpoint);
			return false;
		}

		boost::system::error_code error = boost::asio::error::host_not_found;

		std::array<char, 1024> response;
		size_t len = tcp_socket->read_some(boost::asio::buffer(response), error);
		if(error) {
			LOG_INFO("ERROR READING FROM SOCKET");
			throw multiplayer::Error();
		}

		std::string str(&response[0], &response[0] + len);
		if(std::string(str.begin(), str.begin() + 5) != "START") {
			LOG_INFO("UNEXPECTED RESPONSE: '" << str << "'");
			throw multiplayer::Error();
		}

		const char* ptr = str.c_str() + 6;
		char* end_ptr = nullptr;
		const int nplayers = strtol(ptr, &end_ptr, 10);
		ptr = end_ptr;
		ASSERT_EQ(*ptr, '\n');
		++ptr;

		boost::asio::io_service& io_service = *asio_service;
		udp::resolver udp_resolver(io_service);

		udp_endpoint_peers.clear();

		for(int n = 0; n != nplayers; ++n) {
			const char* end = strchr(ptr, '\n');
			ASSERT_LOG(end != nullptr, "ERROR PARSING RESPONSE: " << str);
			std::string line(ptr, end);
			ptr = end+1;

			if(line == "SLOT") {
				player_slot = n;
				udp_endpoint_peers.push_back(std::shared_ptr<udp::endpoint>());

				LOG_INFO("SLOT: " << player_slot);
				continue;
			}

			static boost::regex pattern("(.*?) (.*?)");

			std::string host, port;
			match_regex(line, pattern, &host, &port);

			LOG_INFO("SLOT " << n << " = " << host << " " << port);

			udp_endpoint_peers.push_back(std::make_shared<udp::endpoint>());
			udp::resolver::query peer_query(udp::v4(), host, port);

			*udp_endpoint_peers.back() = *udp_resolver.resolve(peer_query);

			if(preferences::relay_through_server()) {
				*udp_endpoint_peers.back() = *udp_endpoint;
			}
		}

		std::set<int> confirmed_players;
		confirmed_players.insert(player_slot);

		LOG_INFO("PLAYER " << player_slot << " CONFIRMING...");

		int confirmation_point = 1000;

		for(unsigned m = 0; (m != 1000 && confirmed_players.size() < static_cast<unsigned>(nplayers)) || m < static_cast<unsigned>(confirmation_point) + 50; ++m) {

			std::vector<char> msg;

			for(int n = 0; n != nplayers; ++n) {
				send_confirm_packet(n, msg, confirmed_players.count(n) != 0);
			}

			while(udp_packet_waiting()) {
				std::array<char, 4096> udp_msg;
				udp::endpoint endpoint;
				size_t len = udp_socket->receive_from(boost::asio::buffer(udp_msg), endpoint);
				if(len == 6 && ::toupper(udp_msg[0]) == 'A') {
					confirmed_players.insert(udp_msg[5]);
					if(udp_msg[5] >= 0 && static_cast<unsigned>(udp_msg[5]) < udp_endpoint_peers.size()) {
						if(endpoint.port() != udp_endpoint_peers[udp_msg[5]]->port()) {
							LOG_INFO("REASSIGNING PORT " << endpoint.port() << " TO " << udp_endpoint_peers[udp_msg[5]]->port());
						}
						*udp_endpoint_peers[udp_msg[5]] = endpoint;
					}

					if(confirmed_players.size() >= static_cast<unsigned>(nplayers) && m < static_cast<unsigned>(confirmation_point)) {
						//now all players are confirmed
						confirmation_point = m;
					}
					LOG_INFO("CONFIRMED PLAYER: " << static_cast<int>(udp_msg[5]) << "/ " << player_slot);
				}
			}

			//we haven't had any luck so far, so start port scanning, in case
			//it's on another port.
			if(m > 100 && (m%100) == 0) {
				for(int n = 0; n != nplayers; ++n) {
					if(n == player_slot || confirmed_players.count(n)) {
						continue;
					}

					LOG_INFO("PORTSCANNING FOR PORTS...");

					for(int port_offset=-5; port_offset != 100; ++port_offset) {
						udp::endpoint peer_endpoint;
						const int port = udp_endpoint_peers[n]->port() + port_offset;
						if(port <= 1024 || port >= 65536) {
							continue;
						}

						std::string port_str = formatter() << port;

						udp::resolver::query peer_query(udp::v4(), udp_endpoint_peers[n]->address().to_string(), port_str.c_str());
						peer_endpoint = *udp_resolver.resolve(peer_query);
						udp_socket->send_to(boost::asio::buffer(msg), peer_endpoint);
					}
				}
			}

			profile::delay(10);
		}

		if(confirmed_players.size() < static_cast<unsigned>(nplayers)) {
			LOG_INFO("COULD NOT CONFIRM NETWORK CONNECTION TO ALL PEERS");
			throw multiplayer::Error();
		}

		controls::set_delay(3);

		LOG_INFO("HANDSHAKING...");

		if(player_slot == 0) {
			int ping_id = 0;
			std::map<int, int> ping_sent_at;
			std::map<int, int> ping_player;
			std::map<std::string, int> contents_ping;

			std::map<int, int> player_nresponses;
			std::map<int, int> player_latency;

			int delay = 0;
			int last_send = -1;

			const int game_start = profile::get_tick_time() + 1000;
			std::array<char, 1024> receive_buf;
			while(profile::get_tick_time() < game_start) {
				const int ticks = profile::get_tick_time();
				const int start_in = game_start - ticks;

				if(start_in < 500 && delay == 0) {
					//calculate what the delay should be
					for(int n = 0; n != nplayers; ++n) {
						if(n == player_slot) {
							continue;
						}

						if(player_nresponses[n]) {
							const int avg_latency = player_latency[n]/player_nresponses[n];
							const int delay_time = avg_latency/(20*2) + 2;
							if(delay_time > delay) {
								delay = delay_time;
							}
						}
					}

					if(delay) {
						LOG_INFO("SET DELAY TO " << delay);
						controls::set_delay(delay);
					}
				}

				if(last_send == -1 || ticks >= last_send+10) {
					last_send = ticks;
					for(int n = 0; n != nplayers; ++n) {
						if(n == player_slot) {
							continue;
						}

						char buf[256];

						int start_advisory = start_in;
						const int player_responses = player_nresponses[n];
						if(player_responses) {
							const int avg_latency = player_latency[n]/player_responses;
							start_advisory -= avg_latency/2;
							if(start_advisory < 0) {
								start_advisory = 0;
							}
						}

						LOG_INFO("SENDING ADVISORY TO START IN " << start_in << " - " << (start_in - start_advisory));

						const int buf_len = sprintf(buf, "PXXXX%d %d %d", ping_id, start_advisory, delay);
						memcpy(&buf[1], &id, 4);

						std::string msg(buf, buf + buf_len);
						udp_socket->send_to(boost::asio::buffer(msg), *udp_endpoint_peers[n]);
						ping_sent_at[ping_id] = ticks;
						contents_ping[std::string(msg.begin()+5, msg.end())] = ping_id;
						ping_player[ping_id] = n;
						ping_id++;
					}
				}

				while(udp_packet_waiting()) {
					size_t len = udp_socket->receive(boost::asio::buffer(receive_buf));
					if(len > 5 && receive_buf[0] == 'P') {
						std::string msg(&receive_buf[0], &receive_buf[0] + len);
						std::string msg_content(msg.begin()+5, msg.end());
						ASSERT_LOG(contents_ping.count(msg_content), "UNRECOGNIZED PING: " << msg);
						const int ping = contents_ping[msg_content];
						const int latency = ticks - ping_sent_at[ping];
						const int nplayer = ping_player[ping];

						player_nresponses[nplayer]++;
						player_latency[nplayer] += latency;

						LOG_INFO("RECEIVED PING FROM " << nplayer << " IN " << latency << " AVG LATENCY " << player_latency[nplayer]/player_nresponses[nplayer]);
					} else {
						if(len == 6 && receive_buf[0] == 'A') {
							std::vector<char> msg;
							const int player_num = receive_buf[5];
							send_confirm_packet(player_num, msg, true);
						}
					}
				}

				profile::delay(1);
			}
		} else {
			std::vector<unsigned> start_time;
			std::array<char, 1024> buf;
			for(;;) {
				while(udp_packet_waiting()) {
					size_t len = udp_socket->receive(boost::asio::buffer(buf));
					LOG_INFO("GOT MESSAGE: " << buf[0]);
					if(len > 5 && buf[0] == 'P') {
						memcpy(&buf[1], &id, 4); //write our ID for the return msg.
						const std::string s(&buf[0], &buf[0] + len);

						std::string::const_iterator begin_start_time = std::find(s.begin() + 5, s.end(), ' ');
						ASSERT_LOG(begin_start_time != s.end(), "NO WHITE SPACE FOUND IN PING MESSAGE: " << s);
						std::string::const_iterator begin_delay = std::find(begin_start_time + 1, s.end(), ' ');
						ASSERT_LOG(begin_delay != s.end(), "NO WHITE SPACE FOUND IN PING MESSAGE: " << s);

						const std::string start_in(begin_start_time+1, begin_delay);
						const std::string delay_time(begin_delay+1, s.end());
						const int start_in_num = atoi(start_in.c_str());
						const int delay = atoi(delay_time.c_str());
						start_time.push_back(profile::get_tick_time() + start_in_num);
						while(start_time.size() > 5) {
							start_time.erase(start_time.begin());
						}

						if(delay) {
							LOG_INFO("SET DELAY TO " << delay);
							controls::set_delay(delay);
						}
						udp_socket->send_to(boost::asio::buffer(s), *udp_endpoint_peers[0]);
					} else {
						if(len == 6 && buf[0] == 'A') {
							std::vector<char> msg;
							const int player_num = buf[5];
							send_confirm_packet(player_num, msg, true);
						}
					}
				}

				if(start_time.size() > 0) {
					const int start_time_avg = static_cast<int>(std::accumulate(start_time.begin(), start_time.end(), 0)/start_time.size());
					if(profile::get_tick_time() >= start_time_avg) {
						break;
					}
				}

				profile::delay(1);
			}
		}

		rng::seed_from_int(0);
		completed_ = true;
		return true;
	}


BEGIN_DEFINE_CALLABLE_NOBASE(Client)
DEFINE_FIELD(ready_to_start, "bool")
	return variant::from_bool(obj.completed_);
BEGIN_DEFINE_FN(pump, "() ->commands")
	ffl::IntrusivePtr<Client> client(const_cast<Client*>(&obj));
	return variant(new game_logic::FnCommandCallable("Multiplayer::Client::Pump", [=]() {
		client->PumpStartLevel();
	}));
END_DEFINE_FN
END_DEFINE_CALLABLE(Client)
}

namespace {
struct Peer {
	std::string host, port;
};
}

COMMAND_LINE_UTILITY(hole_punch_test) {
	boost::asio::io_service io_service;
	udp::resolver udp_resolver(io_service);

	std::string server_hostname = "wesnoth.org";
	std::string server_port = "17001";

	size_t narg = 0;

	if(narg < args.size()) {
		server_hostname = args[narg];
		++narg;
	}

	if(narg < args.size()) {
		server_port = args[narg];
		++narg;
	}

	ASSERT_LOG(narg == args.size(), "wrong number of args");

	udp::resolver::query udp_query(udp::v4(), server_hostname.c_str(), server_port.c_str());
	udp::endpoint udp_endpoint;
	udp_endpoint = *udp_resolver.resolve(udp_query);

	udp::socket udp_socket(io_service);
	udp_socket.open(udp::v4());

	udp_socket.send_to(boost::asio::buffer("hello"), udp_endpoint);

	std::vector<Peer> peers;

	std::array<char, 1024> buf;
	for(;;) {
		udp_socket.receive(boost::asio::buffer(buf));
		LOG_INFO("RECEIVED {{{" << &buf[0] << "}}}\n");

		char* beg = &buf[0];
		char* mid = strchr(beg, ' ');
		if(mid) {
			*mid = 0;
			const char* port = mid+1;

			Peer peer;
			peer.host = beg;
			peer.port = port;

			peers.push_back(peer);
		}

		for(int m = 0; m != 10; ++m) {
			for(int n = 0; n != peers.size(); ++n) {
				const std::string host = peers[n].host;
				const std::string port = peers[n].port;
				LOG_INFO("sending to " << host << " " << port);
				udp::resolver::query peer_query(udp::v4(), host, port);
				udp::endpoint peer_endpoint;
				peer_endpoint = *udp_resolver.resolve(peer_query);

				udp_socket.send_to(boost::asio::buffer("peer"), peer_endpoint);
			}

			profile::delay(1000);
		}
	}

	io_service.run();
}
