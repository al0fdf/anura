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

#include "button.hpp"
#include "controls.hpp"
#include "module.hpp"
#include "controls_dialog.hpp"
#include "dialog.hpp"
#include "draw_scene.hpp"
#include "graphical_font_label.hpp"
#include "i18n.hpp"
#include "key_button.hpp"
#include "screen_handling.hpp"
#include "widget_fwd.hpp"
#include <SDL2/SDL_keyboard.h>
#include <map>
#include <string>
#include <vector>

namespace
{
	std::map<std::string, gui::KeyButtonPtr> KeyButtons;
	std::map<std::string, std::string> action_names;
	
	void end_dialog(gui::Dialog* d)
	{
		using namespace controls;
		
		// Loop through all actions and save their bindings
		for(auto a = action_names.begin(); a != action_names.end(); a++) {
			std::string act_name = a->first;
			
			printf("'Setting' key code of action %s to %d\n", act_name.c_str(), (int) KeyButtons[act_name]->get_key());
			
			// Select the ActionBindings object which has the action we're referring to
			ActionBindings *engine_mapping = controls::get_control_mappings();
			controls::ActionBindings *module_mapping = module::get_module_mappings();
			ActionBindings *mapping;
			
			if(engine_mapping->has_action(act_name)){
				mapping = engine_mapping;
			} else {
				mapping = module_mapping;
			}
			
			ComboList keys_for_action = mapping->get_keys_for_action(act_name);
			
			int old_key = keys_for_action[keys_for_action.size()-1][0];
			printf("Last key for action is %s\n", SDL_GetKeyName(old_key));
			
			
			// Create a blank list into which we place the result from the button
			std::vector<int> result;
			int new_key = KeyButtons[act_name]->get_key();
			result.push_back(new_key);
			
			// Mark the action as being modified from the default value if the keycodes do not match
			// This is neccessary for the action to be saved to the preferences
			if(new_key != old_key){
				mapping->set_are_bindings_default(act_name, false);
			}
			
			// Remove the last key binding, and then add it again with the new data 
			keys_for_action[keys_for_action.size()-1] = result;
			printf("Keys for action are %s\n", 	SDL_GetKeyName(keys_for_action[keys_for_action.size()-1][0]));
			mapping->set_keys_for_action(act_name, keys_for_action);
			keys_for_action = mapping->get_keys_for_action(act_name);
			
			printf("Keys for action after setting are %s\n", 	SDL_GetKeyName(keys_for_action[keys_for_action.size()-1][0]));
		}
		d->close();
	}
}

void show_controls_dialog()
{
	using namespace gui;
	using namespace controls;
	const int vw = graphics::GameScreen::get().getVirtualWidth();
	const int vh = graphics::GameScreen::get().getVirtualHeight();
	
	//HACK: 10 and 4 are the default button padding. Padding should be taken from buttons in KeyButtons list
	int butt_padx = 10;
	int butt_pady = 4;

	int butt_width = 70;
	int butt_height = 60;

	int butt_width_wp = butt_width + butt_padx;
	int butt_height_wp = butt_height + butt_pady;
	
	int sep_y = 50;

	int height = vh- 20;
	/*if(vh > 480) {
		height -= 100;
	}*/
	Dialog d(200, (vh > 480) ? 60 : 10, vw - 400, height);
	d.setBackgroundFrame("empty_window");
	d.setDrawBackgroundFn(draw_last_scene);

	ActionBindings *engine_mapping = controls::get_control_mappings();
	// Store *all* action names, i.e. both engine mappings and module mappings
	action_names = engine_mapping->get_action_names();
	printf("Length of action names is %d\n", (int) action_names.size());
	
	for(auto p = action_names.begin(); p != action_names.end(); p++) {
		printf("Adding button for action %s\n", p->first.c_str());
		printf("Label for that action is %s\n", p->second.c_str());
		
		std::string act_name = p->first.c_str();
		
		ComboList events = engine_mapping->get_keys_for_action(act_name);
		if(events.size() == 0){
			continue;
		}
		KeyButtons[act_name] = KeyButtonPtr(new KeyButton(events[events.size()-1][0], BUTTON_SIZE_DOUBLE_RESOLUTION));
		KeyButtons[act_name]->setDim(butt_width, butt_height);
	}
	
	controls::ActionBindings *module_mapping = module::get_module_mappings();
	std::map<std::string, std::string> module_action_names = module_mapping->get_action_names();
	std::vector<std::string> list_of_module_action_names = {};
	
	for(auto p = module_action_names.begin(); p != module_action_names.end(); p++) {
		// Populate the action names with the module actions as well
		std::string act_name = p->first.c_str();
		
		action_names[p->first] = p->second;
		list_of_module_action_names.insert(list_of_module_action_names.begin(), p->first);
		
		ComboList events = module_mapping->get_keys_for_action(act_name);
		if(events.size() == 0){
			continue;
		}
		KeyButtons[act_name] = KeyButtonPtr(new KeyButton(events[events.size()-1][0], BUTTON_SIZE_DOUBLE_RESOLUTION));
		KeyButtons[act_name]->setDim(butt_width, butt_height);
	}
	
	std::vector<WidgetPtr> module_buttons;
	int row_width = 6;
	
	// Ceiling of integer division
	int x = module_action_names.size();
	int y = row_width;
	
	int number_of_rows = x / y + (x % y > 0);
	
	printf("Number of rows for module mappings is %d.\nNumber of module actions is %d\n", number_of_rows, (int)module_action_names.size());
	
	/*for(int n = 0; n < NUM_CONTROLS; ++n) {
		const CONTROL_ITEM item = static_cast<CONTROL_ITEM>(n);
		KeyButtons[item] = KeyButtonPtr(new KeyButton(get_keycode(item), BUTTON_SIZE_DOUBLE_RESOLUTION));
		KeyButtons[item]->setDim(butt_width, butt_height);
	}*/

	WidgetPtr t_dirs(new GraphicalFontLabel(_("Directions"), "door_label", 2));
	
	WidgetPtr b_up(KeyButtons["up"]);
	WidgetPtr b_down(KeyButtons["down"]);
	WidgetPtr b_left(KeyButtons["left"]);
	WidgetPtr b_right(KeyButtons["right"]);
	WidgetPtr b_confirm(KeyButtons["confirm"]);
	WidgetPtr b_cancel(KeyButtons["cancel"]);
	
	WidgetPtr t_confirm(new GraphicalFontLabel(_(action_names["confirm"]), "door_label", 2));
	WidgetPtr t_cancel(new GraphicalFontLabel(_(action_names["cancel"]), "door_label", 2));
	
	/* Disable module-specific buttons for now
	WidgetPtr t_jump(new GraphicalFontLabel(_("Jump"), "door_label", 2));
	WidgetPtr b_jump(KeyButtons[CONTROL_JUMP]);
	WidgetPtr t_tongue(new GraphicalFontLabel(_("Tongue"), "door_label", 2));
	WidgetPtr b_tongue(KeyButtons[CONTROL_TONGUE]);
	WidgetPtr t_item(new GraphicalFontLabel(_("Item"), "door_label", 2));
	WidgetPtr b_item(KeyButtons[CONTROL_ATTACK]);
	*/
	
	//WidgetPtr b_sprint(KeyButtons[CONTROL_SPRINT]);
	//WidgetPtr t_sprint(new GraphicalFontLabel(_("Sprint"), "door_label", 2));

	WidgetPtr back_button(new Button(WidgetPtr(new GraphicalFontLabel(_("Back"), "door_label", 2)), std::bind(end_dialog, &d), BUTTON_STYLE_DEFAULT, BUTTON_SIZE_DOUBLE_RESOLUTION));
	back_button->setDim(230, 60);

	
	int top_label_height = d.padding();
	int top_label_botm_edge = top_label_height+t_dirs->height();

	int button_grid_width = 3*butt_width_wp;
	int left_edge = d.width()/2 - button_grid_width/2;

	int reference_y = static_cast<int>(d.padding() + butt_height_wp);
	
	// 'Directions' label
	d.addWidget(t_dirs, static_cast<int>(left_edge), static_cast<int>(reference_y));
	reference_y += t_dirs->height();

	// Arrow keys
	d.addWidget(b_up, static_cast<int>(left_edge+butt_width_wp), static_cast<int>(reference_y));
	d.addWidget(b_left, static_cast<int>(left_edge), static_cast<int>(reference_y + butt_height_wp), Dialog::MOVE_DIRECTION::RIGHT);
	d.addWidget(b_down, Dialog::MOVE_DIRECTION::RIGHT);
	d.addWidget(b_right);
	reference_y += butt_height_wp*2 + sep_y;

	
	// Confirm/Cancel labels
	d.addWidget(t_confirm, left_edge, reference_y);
	d.addWidget(t_cancel, static_cast<int>(left_edge+butt_width_wp), reference_y);
	//d.addWidget(t_jump, left_edge, reference_y);
	//d.addWidget(t_tongue, static_cast<int>(left_edge+butt_width_wp), reference_y);
	//d.addWidget(t_item, static_cast<int>(left_edge+butt_width_wp*2), reference_y);
	reference_y += t_confirm->height();

	/*
	d.addWidget(b_jump, left_edge, reference_y, Dialog::MOVE_DIRECTION::RIGHT);
	d.addWidget(b_tongue, Dialog::MOVE_DIRECTION::RIGHT);
	d.addWidget(b_item);
	*/
	
	d.addWidget(b_confirm, left_edge, reference_y);
	d.addWidget(b_cancel, static_cast<int>(left_edge+butt_width_wp), reference_y);
	
	reference_y += b_cancel->height();
	
	for(int y = 0; y<number_of_rows;y++){
		for(int x = 0; x<row_width;x++){
			int index = y*row_width + x;
			if (index >= list_of_module_action_names.size()){
				break;
			}
			std::string act_name = list_of_module_action_names[index];
			
			WidgetPtr label(new GraphicalFontLabel(_(module_action_names[act_name].c_str()), "door_label", 2));
			WidgetPtr button = KeyButtons[act_name.c_str()];
			
			int label_x = left_edge+butt_width_wp*x;
			int label_y = reference_y;
			
			int button_y = reference_y + label->height();
				
			printf("Drawing button/label for x %d, y %d and index %d (with action name %s)\nAt position %d %d\n", x, y, index, act_name.c_str(), label_x, label_y);
			
			d.addWidget(label, label_x, label_y);
			d.addWidget(button, label_x, button_y );
		}
		reference_y += butt_height_wp + sep_y;
	}
	
	d.addWidget(back_button, d.width()/2 - back_button->width()/2, reference_y);

	d.showModal();
}
