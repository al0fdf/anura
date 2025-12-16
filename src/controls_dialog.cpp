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
#include <SDL2/SDL_keycode.h>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace
{
	std::map<std::string, gui::KeyButtonPtr> KeyButtons;
	std::map<std::string, std::string> action_names;
	
	void end_dialog(gui::Dialog* d)
	{
		using namespace controls;
		
		// Loop through all actions and save their bindings
		for(auto a = controls::menu_positions.begin(); a != controls::menu_positions.end(); a++) {
			std::string act_name = a->first;
			printf("Action name is %s\n", act_name.c_str());
			printf("Key buttons has length %d\n", (int)KeyButtons.size());
			// Select the ActionBindings object which has the action we're referring to
			ActionBindings *engine_mapping = controls::get_control_mappings();
			controls::ActionBindings *module_mapping = module::get_module_mappings();
			ActionBindings *mapping;
			
			if(engine_mapping->has_action(act_name)){
				mapping = engine_mapping;
			} else {
				mapping = module_mapping;
			}
			
			if(KeyButtons.find(act_name) == KeyButtons.end()){
				printf("Remapping button for action '%s' not found. Skipping\n", act_name.c_str());
				continue;
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
				printf("Marking action %s as dirty.\n", act_name.c_str());
				mapping->set_are_bindings_default(act_name, false);
			}
			
			// Remove the last key binding, and then add it again with the new data 
			keys_for_action[keys_for_action.size()-1] = result;
			printf("Keys for action are %s\n", 	SDL_GetKeyName(keys_for_action[keys_for_action.size()-1][0]));
			printf("Size is %d\n", (int)keys_for_action.size());
			mapping->set_keys_for_action(act_name, keys_for_action);
			keys_for_action = mapping->get_keys_for_action(act_name);
			printf("Size is %d\n", (int)keys_for_action.size());
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
	
	int sh = graphics::GameScreen::get().getHeight();
	int sw = graphics::GameScreen::get().getWidth();
	
	int back_button_height = 60;
	if(sh < 800){
		back_button_height = 40;
	}
	
	//HACK: 10 and 4 are the default button padding. Padding should be taken from buttons in KeyButtons list
	int butt_padx = 10;
	int butt_pady = 4;

	int butt_width = 70;
	int butt_height = 60;

	int butt_width_wp = butt_width + butt_padx;
	int butt_height_wp = butt_height + butt_pady*3; //butt_pady*3*3 to add some extra padding for the label, and to make the grid look not as crammed

	// Allow 3 buttons to fit onto dialog
	// HACK: Make that larger, because the height is not large enough if we use 3. Don't know why (al0f)
	int height = (butt_height_wp*5) + (back_button_height*2);
	
	// Allow 7 buttons horizontal to fit onto dialog
	// HACK: Make that 8, because the width is not large enough if we use 7. Don't know why (al0f)
	int width = butt_width_wp*8;
	
	// Center the dialog on screen
	int dialog_x = (sw - width)/2;
	int dialog_y = (sh - height)/2;
	
	Dialog d(dialog_x, dialog_y, width, height);
	d.setBackgroundFrame("empty_window");
	d.setDrawBackgroundFn(draw_last_scene);

	
	WidgetPtr back_button(new Button(WidgetPtr(new GraphicalFontLabel(_("Back"), "door_label", 2)), std::bind(end_dialog, &d), BUTTON_STYLE_DEFAULT, BUTTON_SIZE_DOUBLE_RESOLUTION));
	back_button->setDim(230, back_button_height);
	d.addWidget(back_button, d.width()/2 - back_button->width()/2, back_button_height);
	
	
	int left_edge = d.width()/16;

	int reference_y = static_cast<int>(back_button->y() + back_button_height*2);
	
	ActionBindings *engine_mapping = controls::get_control_mappings();
	ActionBindings *module_mapping = module::get_module_mappings();
	
	std::map<std::string, std::string> engine_action_names = engine_mapping->get_action_names();
	action_names = module_mapping->get_action_names();
	
	for(auto p = engine_action_names.begin(); p != engine_action_names.end(); p++){
		action_names[p->first] = p->second;
	}
	
	std::map<std::string, std::vector<float>> menu_positions = controls::menu_positions;
	
	for(auto p = controls::menu_positions.begin(); p!= controls::menu_positions.end(); p++){
		std::string act_name = p->first;
		printf("Spawning button for action %s\n", act_name.c_str());
		ComboList events = engine_mapping->get_keys_for_action(act_name);
		ComboList events2 = module_mapping->get_keys_for_action(act_name);
		if(events.size() == 0 && events2.size() == 0){
			KeyButtons[act_name] = KeyButtonPtr(new KeyButton(SDLK_UNKNOWN, BUTTON_SIZE_DOUBLE_RESOLUTION));
		} else {
			if(engine_mapping->has_action(act_name)){
				KeyButtons[act_name] = KeyButtonPtr(new KeyButton(events[events.size()-1][0], BUTTON_SIZE_DOUBLE_RESOLUTION));
			} else {
				KeyButtons[act_name] = KeyButtonPtr(new KeyButton(events2[events2.size()-1][0], BUTTON_SIZE_DOUBLE_RESOLUTION));
			}
		}
		
		KeyButtons[act_name]->setDim(butt_width, butt_height);
		
		WidgetPtr label(new GraphicalFontLabel(_(action_names[act_name].c_str()), "door_label", 2));
		WidgetPtr button = KeyButtons[act_name.c_str()];
		
		float grid_x = p->second[0];
		float grid_y = p->second[1];
		
		int label_x = left_edge + butt_width_wp*grid_x;
		int label_y = reference_y + (butt_height_wp+label->height())*grid_y;
		
		d.addWidget(label, label_x, label_y);
		
		int button_x = label_x;
		//FIXME: Why doesn't the label return its actual height. I shouldn't need to multiply this by 2
		int button_y = label_y + label->height();
		
		d.addWidget(button, button_x, button_y);
	}
	

	d.showModal();
}
