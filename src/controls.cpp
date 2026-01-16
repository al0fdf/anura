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

#include "logger.hpp"
#include "variant_type.hpp"
#include <SDL2/SDL_keycode.h>
#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#include <SDL2/SDL.h>

#include <assert.h>
#include <cstdint>

#include <stack>
#include <vector>

#include "asserts.hpp"
#include "controls.hpp"
#include "module.hpp"
#include "joystick.hpp"
#include "multiplayer.hpp"
#include "preferences.hpp"
#include "variant.hpp"

PREF_INT(max_control_history, 1024, "Maximum number of frames to keep control history for");

namespace controls
{
	InputEvent::InputEvent(){
		this->code = 0;
		this->type = EventType::KEYBOARD;
		this->strength = 0;
	}
	
	InputEvent::InputEvent(EventType type, int code, float strength){
		this->code = code;
		this->type = type;
		this->strength = strength;
	}
	
	InputEvent::~InputEvent(){
		
	}
	
	InputEvent::EventType InputEvent::get_type(){
		return this->type;
	}
	float InputEvent::get_strength(){
		return this->strength;
	}
	int InputEvent::get_code(){
		return this->code;
	}
	
	variant InputEvent::as_variant(){
		std::map<variant, variant> result = {};
		variant v_type = variant("");
		switch (this->type){
			case EventType::KEYBOARD:
				v_type = variant("keyboard");
				break;
			case EventType::JOYPAD_BUTTON:
				v_type = variant("joy_button");
				break;
			case EventType::JOYPAD_MOTION:
				v_type = variant("joy_motion");
				break;
			default:
				v_type = variant("unknown");
				break;
		}
		result.insert({variant("type"), v_type});
		
		result.insert({variant("code"), variant(this->code)});
		result.insert({variant("strength"), variant(this->strength)});
		return variant(&result);
	}
	
	ActionBindings::ActionBindings(){
	}
	ActionBindings::~ActionBindings(){
		//TODO: Is there anything I need to free here?
	}
	void ActionBindings::parse_action_names(variant node){
		std::map<variant, variant> acts = node.as_map();

		for(auto p = acts.begin(); p!=acts.end();++p){
			std::string act_id = p->first.as_string();
			std::string act_name = p->second.as_string();
			this->action_names.insert({act_id, act_name});
		}
	};
	
	InputEvent InputEvent::from_variant(variant var) {
		InputEvent::EventType type = InputEvent::EventType::KEYBOARD;
		int code = 0;
		float strength = 0.0;
		
		if (var.has_key("type")){
			if(var["type"].as_string() == "keyboard"){
				type = InputEvent::EventType::KEYBOARD;
			} else if (var["type"].as_string() == "joy_button"){
				type = InputEvent::EventType::JOYPAD_BUTTON;
			} else if (var["type"].as_string() == "joy_motion"){
				type = InputEvent::EventType::JOYPAD_MOTION;
			}
		}
		
		if (var.has_key("code")){
			code = var["code"].as_int();
		}
		if (var.has_key("strength")){
			strength = var["strength"].as_float();
		}
		
		return InputEvent(type, code, strength);
	};
	
	void ActionBindings::parse_events(variant node){
		std::map<variant, variant> event_binds = node.as_map(); //Action name to list of list of events
		for(auto p = event_binds.begin(); p != event_binds.end(); ++p){
			std::string action_name = p->first.as_string(); //Action names
			
			
			std::vector<variant> event_sequences = p->second.as_list(); // List of event combo
			EventComboList combo_list = {};
			for(int i=0;i<event_sequences.size();i++){
				std::vector<variant> ffl_event_combo = event_sequences[i].as_list(); //List of events
				EventCombo event_combo = {};
				
				for(int q=0;q<ffl_event_combo.size();q++){
					LOG_INFO(ffl_event_combo[q]);
					InputEvent event = InputEvent::from_variant(ffl_event_combo[q]);
					event_combo.push_back(event);
				}
				combo_list.push_back(event_combo);
			}
			
			for(int j=0;j<combo_list.size();j++){
				this->event_mapping[action_name].push_back(combo_list[j]);
			}
		}
	};
	
	variant ActionBindings::get_events_for_action_ffl(std::string action_name){
		EventComboList event_combos = this->get_events_for_action(action_name); //List of event combos
		std::vector<variant> result = {};
		
		for(int i=0;i<event_combos.size();i++){
			std::vector<variant> combo;   // An 'event combo' i.e. list of events
			for(int j=0;j<event_combos[i].size();j++){
				combo.push_back(event_combos[i][j].as_variant());
			}
			result.push_back(variant(&combo));
		}
		
		return variant(&result);
	}
	
	EventComboList ActionBindings::get_events_for_action(std::string action_name){
		if (this->event_mapping.find(action_name) != this->event_mapping.end()) {
			EventComboList events = this->event_mapping[action_name];
			return events;
		}
		
		EventComboList result = {};
		return result;
	}

	std::map<std::string, std::string> ActionBindings::get_action_names(){
		return action_names;
	}

	void ActionBindings::set_events_for_action(std::string action, EventComboList &combos){
		this->event_mapping[action] = combos;
	}

	bool ActionBindings::has_action(std::string action_name){
		if(this->key_mapping.find(action_name) == this->key_mapping.end()){
			return false;
		}
		return true;
	}
	void ActionBindings::set_are_bindings_default(std::string action_name, bool value){
		if(value == false){
			this->dirty_actions[action_name] = true;
		} else {
			auto itor = this->dirty_actions.find(action_name);
			if (itor != this->dirty_actions.end()){
				this->dirty_actions.erase(itor);
			}
		}
	}

	bool ActionBindings::are_bindings_default_for_action(std::string action_name){
		if (this->dirty_actions.find(action_name) == this->dirty_actions.end()) {
			return true;
		}
		return false;
	}

	void ActionBindings::write_to_preferences(variant_builder *node){
		for(auto p = this->action_names.begin(); p != this->action_names.end(); ++p) {
      		std::string action_name = p->first;
        	if(this->are_bindings_default_for_action(action_name)){
         		continue;
         	}
        	std::string preference_name = "events_";
        	preference_name += action_name;

         	node->add(preference_name, this->get_events_for_action_ffl(action_name));
		}
	}

	void ActionBindings::read_from_preferences(variant node){
		for(auto p = action_names.begin(); p != action_names.end(); ++p) {
      		std::string action_name = p->first;
        	std::string preference_name = "events_";
        	preference_name += action_name;

         	// For each 'keys_action' that is found in preferences.cfg
        	const variant events_node = node[preference_name];
         	if(events_node.is_null() == false) {
          		// Mark action as dirty, i.e. changed from default
          		this->set_are_bindings_default(action_name, false);

            	// Parse the data (key combinations) from variants
             	// to the KeyCombination type.
             	EventComboList events;
              	
            	std::vector<variant> temp = events_node.as_list(); // List of list of events
             	for(int i=0;i<temp.size();i++){
            		EventCombo event_combo = {};
              		std::vector<variant> ffl_event_combo = temp[i].as_list();//List of events
                	for(int j=0;j<ffl_event_combo.size();j++){
               			InputEvent event = InputEvent::from_variant(ffl_event_combo[j]);
                		event_combo.push_back(event);
                 	}
                 	events.push_back(event_combo);
              	}

              	// Assign the new bindings
               	this->set_events_for_action(action_name, events);
			}
		}
	}

	ActionBindings engine_mappings;
	
	std::map<std::string, std::vector<float>> menu_positions;
	std::vector<int> dialog_grid_size = {7, 6};
	const char** control_names()
	{
		static const char* names[] = { "up", "down", "left", "right", "attack", "jump", "tongue", "sprint", nullptr };
		return names;
	}
	
	bool is_action_down(std::string action_name){
		EventComboList combos = engine_mappings.get_events_for_action(action_name);
		
		for(int i=0;i<combos.size();i++){
			EventCombo combo = combos[i];
			bool is_match = true;
			
			for(int j=0;j<combo.size();j++){
				InputEvent ev = combo[j];
				switch(ev.get_type()){
					case InputEvent::EventType::KEYBOARD: 
						{
							int keycode = ev.get_code();
							int scancode = SDL_GetScancodeFromKey(keycode);
							int numkeys;
							const Uint8* states = SDL_GetKeyboardState(&numkeys);
							if (scancode > numkeys){
								is_match = false;
							}
							if(states[scancode] == 0){
								is_match = false;
							}
						}
						break;
					case InputEvent::EventType::JOYPAD_BUTTON:
						{
							int button_index = ev.get_code();
							if(not joystick::button((SDL_GameControllerButton) button_index)){
								is_match = false;
							}
						}
						break;
					case InputEvent::EventType::JOYPAD_MOTION:
						break;
				}
				if(is_match == false){
					break; // Break out of 'for event in combo' loop
				}
			}
			// Wait until all events in the combo have been confirmed
			// for example if an event combo is [Ctrl, P], then both Ctrl
			// and P have to have is_match=true
			if (is_match) {
				return true;
			}
		}
	}
	
	controls::ActionBindings* get_control_mappings(){
		return &engine_mappings;
	}
	
	namespace {

	variant g_user_ctrl_output;

	int npackets_received;
	int ngood_packets;
	int last_packet_size_;

	const int MAX_PLAYERS = 8;

	struct ControlFrame {
		ControlFrame() : keys(0)
		{}
		bool operator==(const ControlFrame& o) const {
			return keys == o.keys && user == o.user;
		}
		bool operator!=(const ControlFrame& o) const {
			return !(*this == o);
		}

		unsigned char keys;
		std::string user;
	};

	std::vector<ControlFrame> controls[MAX_PLAYERS];

	//for each player, the highest confirmed cycle we have
	int32_t highest_confirmed[MAX_PLAYERS];

	//for each player, the highest confirmed cycle of ours that they have
	int32_t remote_highest_confirmed[MAX_PLAYERS];

	std::map<int, int> our_checksums;

	int starting_cycles;
	unsigned nplayers = 1;
	unsigned local_player;

	int delay;

	int first_invalid_cycle_var = -1;

	key_type sdlk[NUM_CONTROLS] = {
		SDLK_UP,
		SDLK_DOWN,
		SDLK_LEFT,
		SDLK_RIGHT,
		SDLK_d,
		SDLK_a,
		SDLK_s,
		SDLK_LSHIFT,
	};

	CONTROL_ITEM g_mouse_controls[3] = {
		NUM_CONTROLS,
		NUM_CONTROLS,
		NUM_CONTROLS,
	};

	//If any of these keys are held, we ignore other keyboard input.
	key_type control_keys[] = {
		SDLK_LCTRL,
		SDLK_RCTRL,
		SDLK_LALT,
		SDLK_RALT,
	};

	int32_t our_highest_confirmed() {
		int32_t res = -1;
		for(int n = 0; n != nplayers; ++n) {
			if(res == -1 || highest_confirmed[n] < res) {
				res = highest_confirmed[n];
			}
		}

		return res;
	}

	std::stack<ControlFrame> local_control_locks;
	}

	class control_backup_scope_impl {
	public:
		std::vector<ControlFrame> controls[MAX_PLAYERS];
		int32_t highest_confirmed[MAX_PLAYERS];
		int starting_cycles;
		std::stack<ControlFrame> lock_stack;
	};

	control_backup_scope::control_backup_scope(int flags) : impl_(new control_backup_scope_impl)
	{
		impl_->lock_stack = local_control_locks;

		if(flags & CLEAR_LOCKS) {
			while(!local_control_locks.empty()) { local_control_locks.pop(); }
		}

		impl_->starting_cycles = starting_cycles;
		for(int n = 0; n != MAX_PLAYERS; ++n) {
			impl_->controls[n] = controls[n];
			impl_->highest_confirmed[n] = highest_confirmed[n];
		}
	}

	control_backup_scope::~control_backup_scope()
	{
		restore_state();
	}

	void control_backup_scope::restore_state()
	{
		if(impl_) {
			starting_cycles = impl_->starting_cycles;
			for(int n = 0; n != MAX_PLAYERS; ++n) {
				controls[n] = impl_->controls[n];
				highest_confirmed[n] = impl_->highest_confirmed[n];
			}

			local_control_locks = impl_->lock_stack;
		}
	}

	void control_backup_scope::cancel()
	{
		impl_.reset();
	}

	int their_highest_confirmed() {
		int32_t res = -1;
		for(int n = 0; n != nplayers; ++n) {
			if(n == local_player) {
				continue;
			}

			if(res == -1 || remote_highest_confirmed[n] < res) {
				res = remote_highest_confirmed[n];
			}
		}

		return res;
	}

	void new_level(int level_starting_cycles, int level_nplayers, int level_local_player)
	{
		LOG_INFO("SET STARTING CYCLES: " << level_starting_cycles);
		starting_cycles = level_starting_cycles;
		nplayers = level_nplayers;
		local_player = level_local_player;
		for(std::vector<ControlFrame>& v : controls) {
			v.clear();
		}

		for(int32_t& highest : highest_confirmed) {
			highest = 0;
		}

		for(int32_t& highest : remote_highest_confirmed) {
			highest = 0;
		}
	}


	local_controls_lock::local_controls_lock(unsigned char state)
	{
		ControlFrame ctrl;
		ctrl.keys = state;
		local_control_locks.push(ctrl);
	}

	local_controls_lock::~local_controls_lock()
	{
		local_control_locks.pop();

		if(local_control_locks.empty()) {
			ignore_current_keypresses();
		}
	}

	const unsigned char* get_local_control_lock()
	{
	    if(local_control_locks.empty() == false){
        	return &local_control_locks.top().keys;
	    }else{
        	return nullptr;
    	    }
	}


	namespace
	{
		//array of keys which we are ignoring. We ignore keys on the end of a dialog.
		//keys will be unignored as soon as they are no longer depressed.
		bool key_ignore[NUM_CONTROLS];
	}

	void ignore_current_keypresses()
	{
		const Uint8 *state = SDL_GetKeyboardState(nullptr);
		for(int n = 0; n < NUM_CONTROLS; ++n) {
			key_ignore[n] = state[SDL_GetScancodeFromKey(sdlk[n])] != 0;
		}
	}

	void read_until(int ncycle)
	{
		if(local_player >= nplayers) {
			return;
		}

		fprintf(stderr, "READ UNTIL: %d, local_player = %d\n", ncycle, local_player);

		while(starting_cycles + controls[local_player].size() <= unsigned(ncycle)) {
			read_local_controls();
		}

		while(starting_cycles + controls[local_player].size() > unsigned(ncycle+1)) {
			unread_local_controls();

			if(controls[local_player].empty() && starting_cycles > unsigned(ncycle+1)) {
				starting_cycles = ncycle+1;
			}
		}
	}

	int local_controls_end()
	{
		if(local_player >= nplayers) {
			return 0;
		}

		return static_cast<int>(controls[local_player].size()) + starting_cycles;
	}

	void read_local_controls()
	{
		if(local_player >= nplayers) {
			return;
		}

		ControlFrame state;
		if(local_control_locks.empty()) {
			bool ignore_keypresses = false;
			const Uint8 *key_state = SDL_GetKeyboardState(nullptr);
			for(const key_type& k : control_keys) {
				if(key_state[SDL_GetScancodeFromKey(k)]) {
					ignore_keypresses = true;
					break;
				}
			}

			Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
			for(int n = 0; n < 3; ++n) {
				if(g_mouse_controls[n] != NUM_CONTROLS && (mouse_buttons&SDL_BUTTON(n+1))) {
					if(!key_ignore[g_mouse_controls[n]]) {
						state.keys |= (1 << g_mouse_controls[n]);
					}
				}
			}

			for(int n = 0; n < NUM_CONTROLS; ++n) {
				if(key_state[SDL_GetScancodeFromKey(sdlk[n])] && !ignore_keypresses) {
					if(!key_ignore[n]) {
						state.keys |= (1 << n);
					}
				} else {
					key_ignore[n] = false;
				}
			}

			//joystick support.
			if(joystick::up()) {
				state.keys |= 0x01;
			}
			if(joystick::down()) {
				state.keys |= 0x02;
			}
			if(joystick::left()) {
				state.keys |= 0x04;
			}
			if(joystick::right()) {
				state.keys |= 0x08;
			}
			// jump
			if(joystick::button(0)) {
				state.keys |= 0x10;
			}
			// tongue
			if(joystick::button(1)) {
				state.keys |= 0x20;
			}
			// attack
			if(joystick::button(2)) {
				state.keys |= 0x40;
			}
			// sprint
			if(joystick::button(3)) {
				state.keys |= 0x80;
			}

			if(g_user_ctrl_output.is_null() == false) {
				state.user = g_user_ctrl_output.write_json();
			}
		} else {
			//we have the controls locked into a specific state.
			state = local_control_locks.top();
		}

		g_user_ctrl_output = variant();

		controls[local_player].push_back(state);
		highest_confirmed[local_player]++;

		//advance networked player's controls based on the assumption that they
		//just did the same thing as last time; incoming packets will correct
		//any assumptions.
		for(int n = 0; n != nplayers; ++n) {
			while(n != local_player && controls[n].size() < controls[local_player].size()) {
				if(controls[n].empty()) {
					controls[n].emplace_back();
				} else {
					controls[n].push_back(controls[n].back());
				}
			}
		}

		if(controls[local_player].size() >= g_max_control_history) {
			const int nerase = static_cast<int>(controls[local_player].size()/2);
			starting_cycles += nerase;
			for(int n = 0; n != nplayers; ++n) {
				ASSERT_LOG(controls[n].size() > nerase, "No controls to erase: " << n << ", " << controls[n].size() << " vs " << nerase);
				controls[n].erase(controls[n].begin(), controls[n].begin() + nerase);
			}
		}
	}

	void unread_local_controls()
	{
		if(local_player >= nplayers || controls[local_player].empty()) {
			return;
		}

		controls[local_player].pop_back();
		highest_confirmed[local_player]--;
	}

	void get_controlStatus(int cycle, int player, bool* output, const std::string** user)
	{
		--cycle;
		cycle -= starting_cycles;

		cycle -= delay;
		if(cycle < 0) {
			for(int n = 0; n != NUM_CONTROLS; ++n) {
				output[n] = false;
			}
			return;
		}

		ASSERT_INDEX_INTO_VECTOR(cycle, controls[player]);

		unsigned char state = controls[player][cycle].keys;

		for(int n = 0; n != NUM_CONTROLS; ++n) {
			output[n] = (state&(1 << n)) ? true : false;
		}

		if(user) {
			*user = &controls[player][cycle].user;
		}
	}

	void set_delay(int value)
	{
		delay = value;
	}

	void read_control_packet(const char* buf, size_t len)
	{
		++npackets_received;

		if(len < 13) {
			LOG_ERROR("CONTROL PACKET TOO SHORT: " << len);
			return;
		}

		const char* end_buf = buf + len;

		unsigned slot = static_cast<unsigned>(*buf++);

		if(slot >= nplayers) {
			LOG_ERROR("BAD SLOT NUMBER: " << slot << "/" << nplayers);
			return;
		}

		if(slot == local_player) {
			LOG_ERROR("NETWORK PLAYER SAYS THEY HAVE THE SAME SLOT AS US!");
			return;
		}

		int32_t current_cycle;
		memcpy(&current_cycle, buf, 4);
		current_cycle = ntohl(current_cycle);
		buf += 4;

		if(current_cycle < highest_confirmed[slot]) {
			LOG_ERROR("DISCARDING PACKET -- OUT OF ORDER: " << current_cycle << " < " << highest_confirmed[slot]);
			return;
		}

		std::cerr << "READ CONTROL PACKET: " << current_cycle << "\n";

		int32_t checksum;
		memcpy(&checksum, buf, 4);
		checksum = ntohl(checksum);
		buf += 4;

		if(checksum && our_checksums[current_cycle-1]) {
			if(checksum == our_checksums[current_cycle-1]) {
				LOG_DEBUG("CHECKSUM MATCH FOR " << current_cycle << ": " << checksum);
			} else {
				LOG_ERROR("CHECKSUM DID NOT MATCH FOR " << current_cycle << ": " << checksum << " VS " << our_checksums[current_cycle-1]);
			}

		}

		int32_t highest_cycle;
		memcpy(&highest_cycle, buf, 4);
		highest_cycle = ntohl(highest_cycle);
		buf += 4;

		if(highest_cycle > remote_highest_confirmed[slot]) {
			remote_highest_confirmed[slot] = highest_cycle;
		}

		int32_t ncycles;
		memcpy(&ncycles, buf, 4);
		ncycles = ntohl(ncycles);
		buf += 4;

		if(*(end_buf-1) != 0) {
			LOG_ERROR("bad packet, no null terminator");
			return;
		}

		int start_cycle = 1 + current_cycle - ncycles;

		//if we already have data up to this point, don't reprocess it.
		if(start_cycle < highest_confirmed[slot]) {
			int diff = highest_confirmed[slot] - start_cycle;
			ncycles -= diff;

			for(int n = 0; n != diff && buf != end_buf; ++n) {
				++buf;
				if(buf == end_buf) {
					LOG_ERROR("bad packet, no null termination");
					return;
				}
				buf += strlen(buf)+1;
			}

			start_cycle = highest_confirmed[slot];
		}

		for(int cycle = start_cycle; cycle <= current_cycle; ++cycle) {
			if(buf == end_buf) {
				LOG_ERROR("bad packet, incorrect number of null terminators");
				return;
			}
			ControlFrame state;
			state.keys = *buf;

			++buf;
			if(buf == end_buf) {
				LOG_ERROR("bad packet, incorrect number of null terminators");
				return;
			}

			state.user = buf;
			buf += state.user.size()+1;

			const int signed_cycle_index = cycle - starting_cycles;
			if(signed_cycle_index < 0) {
				LOG_ERROR("Bad packet, ancient cycle index");
				return;
			}

			const unsigned int cycle_index = static_cast<unsigned int>(signed_cycle_index);

			if(cycle_index < controls[slot].size()) {
				if(controls[slot][cycle_index] != state) {
					LOG_INFO("RECEIVED CORRECTION");
					controls[slot][cycle_index] = state;
					if(first_invalid_cycle_var == -1 || first_invalid_cycle_var > cycle) {
						//mark us as invalid back to this point, so game logic
						//will be recalculated from here.
						first_invalid_cycle_var = cycle;
					}
				}
			} else {
				LOG_INFO("RECEIVED FUTURE PACKET!");
				while(controls[slot].size() <= cycle_index) {
					controls[slot].push_back(state);
				}
			}
		}

		int current_index = current_cycle - starting_cycles;

		//extend the current control out to the end, to keep the assumption that
		//controls don't change unless we get an explicit signal
		if(current_index < static_cast<int>(controls[slot].size()) - 1) {
			for(unsigned n = current_index + 1; n < controls[slot].size(); ++n) {
				controls[slot][n] = controls[slot][current_index];
			}
		}

		//mark our highest confirmed cycle for this player
		highest_confirmed[slot] = current_cycle;

		assert(buf == end_buf);

		++ngood_packets;
	}

	void write_control_packet(std::vector<char>& v)
	{
		if(local_player >= nplayers) {
			LOG_ERROR("NO VALID LOCAL PLAYER");
			return;
		}

		//write our slot to the packet
		v.push_back(local_player);

		//write our current cycle
		int32_t current_cycle = starting_cycles + static_cast<int>(controls[local_player].size()) - 1;
		int32_t current_cycle_net = htonl(current_cycle);
		v.resize(v.size() + 4);
		memcpy(&v[v.size()-4], &current_cycle_net, 4);

		//write our checksum of game state
		int32_t checksum = our_checksums[current_cycle-1];
		int32_t checksum_net = htonl(checksum);
		v.resize(v.size() + 4);
		memcpy(&v[v.size()-4], &checksum_net, 4);

		//write our highest confirmed cycle
		int32_t highest_cycle = htonl(our_highest_confirmed());
		v.resize(v.size() + 4);
		memcpy(&v[v.size()-4], &highest_cycle, 4);

		int32_t ncycles_to_write = 1+current_cycle - their_highest_confirmed();

		last_packet_size_ = ncycles_to_write;
		if(unsigned(ncycles_to_write) > controls[local_player].size()) {
			ncycles_to_write = int32_t(controls[local_player].size());
		}

		int32_t ncycles_to_write_net = htonl(ncycles_to_write);
		v.resize(v.size() + 4);
		memcpy(&v[v.size()-4], &ncycles_to_write_net, 4);

		for(int n = 0; n != ncycles_to_write; ++n) {
			const int index = (static_cast<int>(controls[local_player].size()) - ncycles_to_write) + n;
			v.push_back(controls[local_player][index].keys);
			const char* user = controls[local_player][index].user.c_str();
			v.insert(v.end(), user, user + controls[local_player][index].user.size()+1);
		}

		LOG_INFO("WRITE CONTROL PACKET: " << current_cycle << ": " << v.size() << " highest = " << our_highest_confirmed());
	}

	const variant& user_ctrl_output()
	{
		return g_user_ctrl_output;
	}

	void set_user_ctrl_output(const variant& v)
	{
		g_user_ctrl_output = v;
	}

	int first_invalid_cycle()
	{
		return first_invalid_cycle_var;
	}

	void mark_valid()
	{
		first_invalid_cycle_var = -1;
	}

	unsigned num_players()
	{
		return nplayers;
	}

	int num_errors()
	{
		return npackets_received - ngood_packets;
	}

	int packets_received()
	{
		return npackets_received;
	}

	int cycles_behind()
	{
		if(local_player >= nplayers) {
			return 0;
		}

		return highest_confirmed[local_player] - our_highest_confirmed();
	}

	int last_packet_size()
	{
		return last_packet_size_;
	}

	void set_checksum(int cycle, int sum)
	{
		our_checksums[cycle] = sum;
		while(our_checksums.size() >= 1024) {
			our_checksums.erase(our_checksums.begin());
		}
	}

	void debug_dump_controls()
	{
		std::ostringstream ss;
		ss << "CONTROLS:";
		for(unsigned n = 0; n < nplayers; ++n) {
			ss << " " << n << ":";
			for(unsigned m = 0; m < controls[n].size() && m < static_cast<unsigned>(highest_confirmed[n]); ++m) {
				char c = controls[n][m].keys;
				ss << std::hex << static_cast<int>(c);
			}
		}
		LOG_INFO(ss.str());

		for(unsigned n = 0; n < nplayers; ++n) {
			for(unsigned m = 0; m < controls[n].size() && m < static_cast<unsigned>(highest_confirmed[n]); ++m) {
				ss.clear();
				ss << "CTRL PLAYER " << n << " CYCLE " << m << ": ";
				for(int j = 0; j != NUM_CONTROLS; ++j) {
					ss << (((1 << j)&controls[n][m].keys) ? "1" : "0");
				}
				LOG_INFO(ss.str());
			}
		}
	}

	void set_mouse_to_keycode(CONTROL_ITEM item, int mouse_button)
	{
		--mouse_button;
		if(mouse_button >= 0 && mouse_button < 3) {
			g_mouse_controls[mouse_button] = item;
		}
	}

	CONTROL_ITEM get_mouse_keycode(int mouse_button)
	{
		--mouse_button;
		if(mouse_button >= 0 && mouse_button < 3) {
			return g_mouse_controls[mouse_button];
		}

		return NUM_CONTROLS;
	}

	void set_keycode(CONTROL_ITEM item, key_type key)
	{
		if (item < NUM_CONTROLS) {
			sdlk[item] = key;
		}
	}

	key_type get_keycode(CONTROL_ITEM item)
	{
		if (item < NUM_CONTROLS) {
			return sdlk[item];
		}
		return SDLK_UNKNOWN;
	}
	
	bool read_menu_positions(variant node){
		std::map<variant, variant> read_data = node.as_map();
		if(read_data.size() == 0){
			return false;
		}
		for(auto p = read_data.begin(); p != read_data.end(); ++p) {
      		std::string action_name = p->first.as_string();
        	std::vector<variant> position = p->second.as_list();
         	std::vector<float> result;
          	
          	if(position.size() != 2){
         		LOG_ERROR("Position for action '" << action_name << "' in module.cfg must be a list of two floats. [float x, float y]");
           		continue;
           	}
          	float grid_x = position[0].as_float();
           	float grid_y = position[1].as_float();
         	
            result.push_back(grid_x);
            result.push_back(grid_y);
         	
        	menu_positions[action_name] = result;
		}
		return true;
	}
}
