/*
    Copyright (C) 2002-2006 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/stop_signal.h>
#include <gtkmm2ext/choice.h>
#include <gtkmm2ext/doi.h>
#include <gtkmm2ext/bindable_button.h>
#include <gtkmm2ext/barcontroller.h>
#include <gtkmm2ext/gtk_ui.h>

#include "ardour/route_group.h"
#include "ardour/dB.h"
#include "pbd/memento_command.h"
#include "pbd/stacktrace.h"
#include "pbd/controllable.h"
#include "pbd/enumwriter.h"

#include "ardour_ui.h"
#include "editor.h"
#include "route_ui.h"
#include "led.h"
#include "keyboard.h"
#include "utils.h"
#include "prompter.h"
#include "gui_thread.h"
#include "ardour_dialog.h"
#include "latency_gui.h"
#include "mixer_strip.h"
#include "automation_time_axis.h"
#include "route_time_axis.h"

#include "ardour/route.h"
#include "ardour/event_type_map.h"
#include "ardour/session.h"
#include "ardour/audioengine.h"
#include "ardour/audio_track.h"
#include "ardour/midi_track.h"
#include "ardour/template_utils.h"
#include "ardour/filename_extensions.h"
#include "ardour/directory_names.h"
#include "ardour/profile.h"

#include "i18n.h"
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace ARDOUR;
using namespace PBD;

RouteUI::RouteUI (ARDOUR::Session* sess)
	: AxisView(sess)
{
	init ();
}

RouteUI::RouteUI (boost::shared_ptr<ARDOUR::Route> rt, ARDOUR::Session* sess)
	: AxisView(sess)
{
	init ();
	set_route (rt);
}

RouteUI::~RouteUI()
{
	_route.reset (); /* drop reference to route, so that it can be cleaned up */
	route_connections.drop_connections ();

	delete solo_menu;
	delete mute_menu;
	delete sends_menu;
}

void
RouteUI::init ()
{
	self_destruct = true;
	xml_node = 0;
	mute_menu = 0;
	solo_menu = 0;
	sends_menu = 0;
	pre_fader_mute_check = 0;
	post_fader_mute_check = 0;
	listen_mute_check = 0;
	main_mute_check = 0;
        solo_safe_check = 0;
        solo_isolated_check = 0;
        solo_isolated_led = 0;
        solo_safe_led = 0;
	ignore_toggle = false;
	_solo_release = 0;
	_mute_release = 0;
	route_active_menu_item = 0;
	denormal_menu_item = 0;
	multiple_mute_change = false;
	multiple_solo_change = false;

	invert_button = manage (new BindableToggleButton ());
	// mute_button->set_self_managed (true);
	invert_button->set_name ("InvertButton");
	invert_button->add (invert_button_label);
	invert_button_label.show ();
	UI::instance()->set_tip (invert_button, _("Invert (Phase reverse) this track"), "");

	mute_button = manage (new BindableToggleButton ());
	// mute_button->set_self_managed (true);
	mute_button->set_name ("MuteButton");
	mute_button->add (mute_button_label);
	mute_button_label.show ();
	UI::instance()->set_tip (mute_button, _("Mute this track"), "");

	solo_button = manage (new BindableToggleButton ());
	// solo_button->set_self_managed (true);
	solo_button->set_name ("SoloButton");
	solo_button->add (solo_button_label);
	solo_button_label.show ();
	UI::instance()->set_tip (solo_button, _("Mute other (non-soloed) tracks"), "");
	solo_button->set_no_show_all (true);

	rec_enable_button = manage (new BindableToggleButton ());
	rec_enable_button->set_name ("RecordEnableButton");
	// rec_enable_button->set_self_managed (true);
	rec_enable_button->add (rec_enable_button_label);
	rec_enable_button_label.show ();
	UI::instance()->set_tip (rec_enable_button, _("Enable recording on this track"), "");

	show_sends_button = manage (new BindableToggleButton (""));
	show_sends_button->set_name ("SendAlert");
	// show_sends_button->set_self_managed (true);
	UI::instance()->set_tip (show_sends_button, _("make mixer strips show sends to this bus"), "");

	_session->SoloChanged.connect (_session_connections, invalidator (*this), boost::bind (&RouteUI::solo_changed_so_update_mute, this), gui_context());
	_session->TransportStateChange.connect (_session_connections, invalidator (*this), boost::bind (&RouteUI::check_rec_enable_sensitivity, this), gui_context());
	_session->RecordStateChanged.connect (_session_connections, invalidator (*this), boost::bind (&RouteUI::session_rec_enable_changed, this), gui_context());

	Config->ParameterChanged.connect (*this, invalidator (*this), ui_bind (&RouteUI::parameter_changed, this, _1), gui_context());

	rec_enable_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::rec_enable_press), false);
	rec_enable_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::rec_enable_release), false);

	show_sends_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::show_sends_press), false);
	show_sends_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::show_sends_release));

	solo_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::solo_press), false);
	solo_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::solo_release), false);
	mute_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::mute_press), false);
	mute_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::mute_release), false);
	invert_button->signal_toggled().connect (sigc::mem_fun(*this, &RouteUI::invert_toggled), false);

}

void
RouteUI::reset ()
{
	route_connections.drop_connections ();

	delete solo_menu;
	solo_menu = 0;

	delete mute_menu;
	mute_menu = 0;

	if (xml_node) {
		/* do not delete the node - its owned by the route */
		xml_node = 0;
	}

	route_active_menu_item = 0;
	denormal_menu_item = 0;
}

void
RouteUI::self_delete ()
{
	/* This may be called from a non-GUI thread. Keep it safe */

	delete_when_idle (this);
}

void
RouteUI::set_route (boost::shared_ptr<Route> rp)
{
	reset ();

	_route = rp;

	if (set_color_from_route()) {
		set_color (unique_random_color());
	}

	if (self_destruct) {
		rp->DropReferences.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::self_delete, this), gui_context());
	}
	
	mute_button->set_controllable (_route->mute_control());
	solo_button->set_controllable (_route->solo_control());

	_route->active_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::route_active_changed, this), gui_context());
	_route->mute_changed.connect (route_connections, invalidator (*this), ui_bind (&RouteUI::mute_changed, this, _1), gui_context());

	_route->solo_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::update_solo_display, this), gui_context());
	_route->solo_safe_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::update_solo_display, this), gui_context());
	_route->listen_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::update_solo_display, this), gui_context());
	_route->solo_isolated_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::update_solo_display, this), gui_context());

        _route->phase_invert_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::polarity_changed, this), gui_context());
	_route->PropertyChanged.connect (route_connections, invalidator (*this), ui_bind (&RouteUI::property_changed, this, _1), gui_context());

	if (_session->writable() && is_track()) {
		boost::shared_ptr<Track> t = boost::dynamic_pointer_cast<Track>(_route);

		t->RecordEnableChanged.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::route_rec_enable_changed, this), gui_context());

		rec_enable_button->show();
 		rec_enable_button->set_controllable (t->rec_enable_control());

		update_rec_display ();
	}

	mute_button->unset_flags (Gtk::CAN_FOCUS);
	solo_button->unset_flags (Gtk::CAN_FOCUS);

	mute_button->show();
        invert_button->show ();

	if (_route->is_monitor()) {
		solo_button->hide ();
	} else {
		solo_button->show();
	}

	map_frozen ();
}

void
RouteUI::invert_toggled ()
{
        _route->set_phase_invert (invert_button->get_active());
}

void
RouteUI::polarity_changed ()
{
        if (!_route) {
                return;
        }
        
	if (_route->phase_invert()) {
                invert_button->set_active (true);
	} else {
                invert_button->set_active (false);
	}
}

bool
RouteUI::mute_press (GdkEventButton* ev)
{
	if (ev->type == GDK_2BUTTON_PRESS || ev->type == GDK_3BUTTON_PRESS ) {
		return true;
	}

	multiple_mute_change = false;

	if (!ignore_toggle) {

		if (Keyboard::is_context_menu_event (ev)) {

			if (mute_menu == 0){
				build_mute_menu();
			}

			mute_menu->popup(0,ev->time);

		} else {

			if (Keyboard::is_button2_event (ev)) {
				// Primary-button2 click is the midi binding click
				// button2-click is "momentary"


				if (mute_button->on_button_press_event (ev)) {
                                        return true;
                                }

				_mute_release = new SoloMuteRelease (_route->muted ());
			}

			if (ev->button == 1 || Keyboard::is_button2_event (ev)) {

				if (Keyboard::modifier_state_equals (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::TertiaryModifier))) {

					if (_mute_release) {
						_mute_release->routes = _session->get_routes ();
					}

					_session->set_mute (_session->get_routes(), !_route->muted());

				} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {

					/* Primary-button1 applies change to the mix group even if it is not active
					   NOTE: Primary-button2 is MIDI learn.
					*/

					if (ev->button == 1 && _route->route_group()) {
						if (_mute_release) {
							_mute_release->routes = _session->get_routes ();
						}
								
						_session->set_mute (_session->get_routes(), !_route->muted(), Session::rt_cleanup, true);
					}

				} else {

					/* plain click applies change to this route */
					
					boost::shared_ptr<RouteList> rl (new RouteList);
					rl->push_back (_route);

					if (_mute_release) {
						_mute_release->routes = rl;
					}

					_session->set_mute (rl, !_route->muted());

				}
			}
		}

	}

	return true;
}

bool
RouteUI::mute_release (GdkEventButton*)
{
	if (!ignore_toggle) {
		if (_mute_release){
			_session->set_mute (_mute_release->routes, _mute_release->active, Session::rt_cleanup, true);
			delete _mute_release;
			_mute_release = 0;
		}
	}

	return true;
}

bool
RouteUI::solo_press(GdkEventButton* ev)
{
	/* ignore double/triple clicks */

	if (ev->type == GDK_2BUTTON_PRESS || ev->type == GDK_3BUTTON_PRESS ) {
		return true;
	}
	
	multiple_solo_change = false;

	if (!ignore_toggle) {
		
		if (Keyboard::is_context_menu_event (ev)) {
			
                        if (!solo_isolated_led) {

                                if (solo_menu == 0) {
                                        build_solo_menu ();
                                }
                                
                                solo_menu->popup (1, ev->time);
                        }
			
		} else {
			
			if (Keyboard::is_button2_event (ev)) {
				
				// Primary-button2 click is the midi binding click
				// button2-click is "momentary"
				
                                if (solo_button->on_button_press_event (ev)) {
                                        return true;
                                }

				_solo_release = new SoloMuteRelease (_route->self_soloed());
			}
			
			if (ev->button == 1 || Keyboard::is_button2_event (ev)) {
				
				if (Keyboard::modifier_state_equals (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::TertiaryModifier))) {
					
					/* Primary-Tertiary-click applies change to all routes */

					if (_solo_release) {
						_solo_release->routes = _session->get_routes ();
					}
					
					if (Config->get_solo_control_is_listen_control()) {
						_session->set_listen (_session->get_routes(), !_route->listening(),  Session::rt_cleanup, true);
					} else {
						_session->set_solo (_session->get_routes(), !_route->self_soloed(),  Session::rt_cleanup, true);
					}
					
				} else if (Keyboard::modifier_state_contains (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::SecondaryModifier))) {
					
					// Primary-Secondary-click: exclusively solo this track

					if (_solo_release) {
						_solo_release->exclusive = true;

						boost::shared_ptr<RouteList> routes = _session->get_routes();

						for (RouteList::iterator i = routes->begin(); i != routes->end(); ++i) {
							if ((*i)->soloed ()) {
								_solo_release->routes_on->push_back (*i);
							} else {
								_solo_release->routes_off->push_back (*i);
							}
						}
					}
					
					if (Config->get_solo_control_is_listen_control()) {
						/* ??? we need a just_one_listen() method */
					} else {
						_session->set_just_one_solo (_route, true);
					}

				} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)) {
					
					// shift-click: toggle solo isolated status
					
					_route->set_solo_isolated (!_route->solo_isolated(), this);
					delete _solo_release;
					_solo_release = 0;
					
				} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
					
					/* Primary-button1: solo mix group.
					   NOTE: Primary-button2 is MIDI learn.
					*/
					
					if (ev->button == 1 && _route->route_group()) {

						if (_solo_release) {
							_solo_release->routes = _route->route_group()->route_list();
						}
					
						if (Config->get_solo_control_is_listen_control()) {
							_session->set_listen (_route->route_group()->route_list(), !_route->listening(),  Session::rt_cleanup, true);
						} else {
							_session->set_solo (_route->route_group()->route_list(), !_route->self_soloed(),  Session::rt_cleanup, true);
						}
					}
					
				} else {
					
					/* click: solo this route */
					
					boost::shared_ptr<RouteList> rl (new RouteList);
					rl->push_back (route());

					if (_solo_release) {
						_solo_release->routes = rl;
					}

					if (Config->get_solo_control_is_listen_control()) {
						_session->set_listen (rl, !_route->listening());
					} else {
						_session->set_solo (rl, !_route->self_soloed());
					}
				}
			}
		}
	}

	return true;
}

bool
RouteUI::solo_release (GdkEventButton*)
{
	if (!ignore_toggle) {
		
		if (_solo_release) {

			if (_solo_release->exclusive) {

			} else {
                                if (Config->get_solo_control_is_listen_control()) {
                                        _session->set_listen (_solo_release->routes, _solo_release->active, Session::rt_cleanup, true);
                                } else {
                                        _session->set_solo (_solo_release->routes, _solo_release->active, Session::rt_cleanup, true);
                                }
			}

			delete _solo_release;
			_solo_release = 0;
		}
	}

	return true;
}

bool
RouteUI::rec_enable_press(GdkEventButton* ev)
{
	if (ev->type == GDK_2BUTTON_PRESS || ev->type == GDK_3BUTTON_PRESS ) {
		return true;
	}

	if (!_session->engine().connected()) {
	        MessageDialog msg (_("Not connected to JACK - cannot engage record"));
		msg.run ();
		return true;
	}

	if (!ignore_toggle && is_track() && rec_enable_button) {

		if (Keyboard::is_button2_event (ev)) {

			// do nothing on midi sigc::bind event
			return rec_enable_button->on_button_press_event (ev);

		} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::TertiaryModifier))) {

			_session->set_record_enable (_session->get_routes(), !rec_enable_button->get_active());

		} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {

			/* Primary-button1 applies change to the route group (even if it is not active)
			   NOTE: Primary-button2 is MIDI learn.
			*/
			if (ev->button == 1 && _route->route_group()) {
				_session->set_record_enable (_route->route_group()->route_list(), !rec_enable_button->get_active(), Session::rt_cleanup, true);
			}

		} else if (Keyboard::is_context_menu_event (ev)) {

			/* do this on release */

		} else {

			boost::shared_ptr<RouteList> rl (new RouteList);
			rl->push_back (route());
			_session->set_record_enable (rl, !rec_enable_button->get_active());
		}
	}

	return true;
}

bool
RouteUI::rec_enable_release (GdkEventButton*)
{
	return true;
}

void
RouteUI::build_sends_menu ()
{
	using namespace Menu_Helpers;

	sends_menu = new Menu;
	sends_menu->set_name ("ArdourContextMenu");
	MenuList& items = sends_menu->items();

	items.push_back (MenuElem(_("Assign all tracks (prefader)"), sigc::bind (sigc::mem_fun (*this, &RouteUI::create_sends), PreFader)));
	items.push_back (MenuElem(_("Assign all tracks (postfader)"), sigc::bind (sigc::mem_fun (*this, &RouteUI::create_sends), PostFader)));
	items.push_back (MenuElem(_("Assign selected tracks (prefader)"), sigc::bind (sigc::mem_fun (*this, &RouteUI::create_selected_sends), PreFader)));
	items.push_back (MenuElem(_("Assign selected tracks (postfader)"), sigc::bind (sigc::mem_fun (*this, &RouteUI::create_selected_sends), PostFader)));
	items.push_back (MenuElem(_("Copy track gains to sends"), sigc::mem_fun (*this, &RouteUI::set_sends_gain_from_track)));
	items.push_back (MenuElem(_("Set sends gain to -inf"), sigc::mem_fun (*this, &RouteUI::set_sends_gain_to_zero)));
	items.push_back (MenuElem(_("Set sends gain to 0dB"), sigc::mem_fun (*this, &RouteUI::set_sends_gain_to_unity)));

}

void
RouteUI::create_sends (Placement p)
{
	_session->globally_add_internal_sends (_route, p);
}

void
RouteUI::create_selected_sends (Placement p)
{
	boost::shared_ptr<RouteList> rlist (new RouteList);
	TrackSelection& selected_tracks (ARDOUR_UI::instance()->the_editor().get_selection().tracks);

	for (TrackSelection::iterator i = selected_tracks.begin(); i != selected_tracks.end(); ++i) {
		RouteTimeAxisView* rtv;
		RouteUI* rui;
		if ((rtv = dynamic_cast<RouteTimeAxisView*>(*i)) != 0) {
			if ((rui = dynamic_cast<RouteUI*>(rtv)) != 0) {
				if (boost::dynamic_pointer_cast<AudioTrack>(rui->route())) {
					rlist->push_back (rui->route());
				}
			}
		}
	}
	
	_session->add_internal_sends (_route, p, rlist);
}

void
RouteUI::set_sends_gain_from_track ()
{
	_session->globally_set_send_gains_from_track (_route);
}

void
RouteUI::set_sends_gain_to_zero ()
{
	_session->globally_set_send_gains_to_zero (_route);
}

void
RouteUI::set_sends_gain_to_unity ()
{
	_session->globally_set_send_gains_to_unity (_route);
}

bool
RouteUI::show_sends_press(GdkEventButton* ev)
{
	if (ev->type == GDK_2BUTTON_PRESS || ev->type == GDK_3BUTTON_PRESS ) {
		return true;
	}

	if (!ignore_toggle && !is_track() && show_sends_button) {

		if (Keyboard::is_button2_event (ev) && Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {

			// do nothing on midi sigc::bind event
			return false;

		} else if (Keyboard::is_context_menu_event (ev)) {

			if (sends_menu == 0) {
				build_sends_menu ();
			}

			sends_menu->popup (0, ev->time);

		} else {

			/* change button state */

			show_sends_button->set_active (!show_sends_button->get_active());

			/* start blinking */

			if (show_sends_button->get_active()) {
				/* show sends to this bus */
				MixerStrip::SwitchIO (_route);
				send_blink_connection = ARDOUR_UI::instance()->Blink.connect (sigc::mem_fun(*this, &RouteUI::send_blink));
			} else {
				/* everybody back to normal */
				send_blink_connection.disconnect ();
				MixerStrip::SwitchIO (boost::shared_ptr<Route>());
			}

		}
	}

	return true;
}

bool
RouteUI::show_sends_release (GdkEventButton*)
{
	return true;
}

void
RouteUI::send_blink (bool onoff)
{
	if (!show_sends_button) {
		return;
	}

	if (onoff) {
		show_sends_button->set_state (STATE_ACTIVE);
	} else {
		show_sends_button->set_state (STATE_NORMAL);
	}
}

int
RouteUI::solo_visual_state (boost::shared_ptr<Route> r)
{
	if (r->is_master() || r->is_monitor()) {
		return 0;
	}
	
	if (Config->get_solo_control_is_listen_control()) {

		if (r->listening()) {
			return 1;
		} else {
			return 0;
		}

	} 
	
	if (r->soloed()) {
		return 1;
	} else {
		return 0;
	}
}

int
RouteUI::solo_visual_state_with_isolate (boost::shared_ptr<Route> r)
{
	if (r->is_master() || r->is_monitor()) {
		return 0;
	}
	
	if (Config->get_solo_control_is_listen_control()) {

		if (r->listening()) {
			return 1;
		} else {
                        return 0;
		}

	} 
	
	if (r->solo_isolated()) {
		return 2;
	} else if (r->soloed()) {
                if (!r->self_soloed()) {
                        return 3;
                } else {
                        return 1;
                }
	} else {
		return 0;
	}
}

int
RouteUI::solo_isolate_visual_state (boost::shared_ptr<Route> r)
{
	if (r->is_master() || r->is_monitor()) {
		return 0;
	}
	
	if (r->solo_isolated()) {
		return 1;
	} else {
		return 0;
	}
}

int
RouteUI::solo_safe_visual_state (boost::shared_ptr<Route> r)
{
	if (r->is_master() || r->is_monitor()) {
		return 0;
	}
	
	if (r->solo_safe()) {
		return 1;
	} else {
		return 0;
	}
}

void
RouteUI::update_solo_display ()
{
	bool x;

	if (Config->get_solo_control_is_listen_control()) {

		if (solo_button->get_active() != (x = _route->listening())) {
			ignore_toggle = true;
			solo_button->set_active(x);
			ignore_toggle = false;
		}

	} else {

		if (solo_button->get_active() != (x = _route->soloed())) {
			ignore_toggle = true;
			solo_button->set_active (x);
			ignore_toggle = false;
		}

	}

	bool yn = _route->solo_safe ();

	if (solo_safe_check && solo_safe_check->get_active() != yn) {
		solo_safe_check->set_active (yn);
	}

	yn = _route->solo_isolated ();

	if (solo_isolated_check && solo_isolated_check->get_active() != yn) {
		solo_isolated_check->set_active (yn);
	}

        set_button_names ();

        if (solo_isolated_led) {
                solo_isolated_led->set_visual_state (_route->solo_isolated() ? 1 : 0);
        }

        if (solo_safe_led) {
                solo_safe_led->set_visual_state (_route->solo_safe() ? 1 : 0);
        }

	solo_button->set_visual_state (solo_visual_state (_route));
}

void
RouteUI::solo_changed_so_update_mute ()
{
	update_mute_display ();
}

void
RouteUI::mute_changed(void* /*src*/)
{
	update_mute_display ();
}

int
RouteUI::mute_visual_state (Session* s, boost::shared_ptr<Route> r)
{
	if (r->is_master() || r->is_monitor()) {
		return 0;
	}

	if (Config->get_show_solo_mutes()) {

		if (r->muted ()) {
			/* full mute */
			return 2;
		} else if (s->soloing() && !r->soloed() && !r->solo_isolated()) {
			return 1;
		} else {
			/* no mute at all */
			return 0;
		}

	} else {

		if (r->muted()) {
			/* full mute */
			return 2;
		} else {
			/* no mute at all */
			return 0;
		}
	}

	return 0;
}

void
RouteUI::update_mute_display ()
{
        if (!_route) {
                return;
        }

	bool model = _route->muted();
	bool view = mute_button->get_active();

	/* first make sure the button's "depressed" visual
	   is correct.
	*/

	if (model != view) {
		ignore_toggle = true;
		mute_button->set_active (model);
		ignore_toggle = false;
	}

	mute_button->set_visual_state (mute_visual_state (_session, _route));
}

void
RouteUI::route_rec_enable_changed ()
{
	Gtkmm2ext::UI::instance()->call_slot (invalidator (*this), boost::bind (&RouteUI::update_rec_display, this));
}

void
RouteUI::session_rec_enable_changed ()
{
        update_rec_display ();
}

void
RouteUI::update_rec_display ()
{
	if (!rec_enable_button || !_route) {
		return;
	}
			
	bool model = _route->record_enabled();
	bool view = rec_enable_button->get_active();

	/* first make sure the button's "depressed" visual
	   is correct.
	*/

	if (model != view) {
		ignore_toggle = true;
		rec_enable_button->set_active (model);
		ignore_toggle = false;
	}

	/* now make sure its color state is correct */

	if (model) {

		switch (_session->record_status ()) {
		case Session::Recording:
			rec_enable_button->set_visual_state (1);
			break;

		case Session::Disabled:
		case Session::Enabled:
			rec_enable_button->set_visual_state (2);
			break;

		}

	} else {
		rec_enable_button->set_visual_state (0);
	}

	check_rec_enable_sensitivity ();
}

void
RouteUI::build_solo_menu (void)
{
	using namespace Menu_Helpers;

	solo_menu = new Menu;
	solo_menu->set_name ("ArdourContextMenu");
	MenuList& items = solo_menu->items();
	CheckMenuItem* check;

	check = new CheckMenuItem(_("Solo Isolate"));
	check->set_active (_route->solo_isolated());
	check->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_solo_isolated), check));
	items.push_back (CheckMenuElem(*check));
        solo_isolated_check = dynamic_cast<CheckMenuItem*>(&items.back());
	check->show_all();

	check = new CheckMenuItem(_("Solo Safe"));
	check->set_active (_route->solo_safe());
	check->signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_solo_safe), check));
	items.push_back (CheckMenuElem(*check));
        solo_safe_check = dynamic_cast<CheckMenuItem*>(&items.back());
	check->show_all();

	//items.push_back (SeparatorElem());
	// items.push_back (MenuElem (_("MIDI Bind"), sigc::mem_fun (*mute_button, &BindableToggleButton::midi_learn)));

}

void
RouteUI::build_mute_menu(void)
{
	using namespace Menu_Helpers;

	mute_menu = new Menu;
	mute_menu->set_name ("ArdourContextMenu");

	MenuList& items = mute_menu->items();

	pre_fader_mute_check = manage (new CheckMenuItem(_("Pre Fader")));
	init_mute_menu(MuteMaster::PreFader, pre_fader_mute_check);
	pre_fader_mute_check->signal_toggled().connect(sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_mute_menu), MuteMaster::PreFader, pre_fader_mute_check));
	items.push_back (CheckMenuElem(*pre_fader_mute_check));
	pre_fader_mute_check->show_all();

	post_fader_mute_check = manage (new CheckMenuItem(_("Post Fader")));
	init_mute_menu(MuteMaster::PostFader, post_fader_mute_check);
	post_fader_mute_check->signal_toggled().connect(sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_mute_menu), MuteMaster::PostFader, post_fader_mute_check));
	items.push_back (CheckMenuElem(*post_fader_mute_check));
	post_fader_mute_check->show_all();

	listen_mute_check = manage (new CheckMenuItem(_("Control Outs")));
	init_mute_menu(MuteMaster::Listen, listen_mute_check);
	listen_mute_check->signal_toggled().connect(sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_mute_menu), MuteMaster::Listen, listen_mute_check));
	items.push_back (CheckMenuElem(*listen_mute_check));
	listen_mute_check->show_all();

	main_mute_check = manage (new CheckMenuItem(_("Main Outs")));
	init_mute_menu(MuteMaster::Main, main_mute_check);
	main_mute_check->signal_toggled().connect(sigc::bind (sigc::mem_fun (*this, &RouteUI::toggle_mute_menu), MuteMaster::Main, main_mute_check));
	items.push_back (CheckMenuElem(*main_mute_check));
	main_mute_check->show_all();

	//items.push_back (SeparatorElem());
	// items.push_back (MenuElem (_("MIDI Bind"), sigc::mem_fun (*mute_button, &BindableToggleButton::midi_learn)));

	_route->mute_points_changed.connect (route_connections, invalidator (*this), boost::bind (&RouteUI::muting_change, this), gui_context());
}

void
RouteUI::init_mute_menu(MuteMaster::MutePoint mp, CheckMenuItem* check)
{
	check->set_active (_route->mute_points() & mp);
}

void
RouteUI::toggle_mute_menu(MuteMaster::MutePoint mp, Gtk::CheckMenuItem* check)
{
	if (check->get_active()) {
		_route->set_mute_points (MuteMaster::MutePoint (_route->mute_points() | mp));
	} else {
		_route->set_mute_points (MuteMaster::MutePoint (_route->mute_points() & ~mp));
	}
}

void
RouteUI::muting_change ()
{
	ENSURE_GUI_THREAD (*this, &RouteUI::muting_change)

	bool yn;
	MuteMaster::MutePoint current = _route->mute_points ();

	yn = (current & MuteMaster::PreFader);

	if (pre_fader_mute_check->get_active() != yn) {
		pre_fader_mute_check->set_active (yn);
	}

	yn = (current & MuteMaster::PostFader);

	if (post_fader_mute_check->get_active() != yn) {
		post_fader_mute_check->set_active (yn);
	}

	yn = (current & MuteMaster::Listen);

	if (listen_mute_check->get_active() != yn) {
		listen_mute_check->set_active (yn);
	}

	yn = (current & MuteMaster::Main);

	if (main_mute_check->get_active() != yn) {
		main_mute_check->set_active (yn);
	}
}

bool
RouteUI::solo_isolate_button_release (GdkEventButton* ev)
{
        bool view = (solo_isolated_led->visual_state() != 0);
        bool model = _route->solo_isolated();

        /* called BEFORE the view has changed */

        if (model == view) {
                _route->set_solo_isolated (!view, this);
        }

        return true;
}

bool
RouteUI::solo_safe_button_release (GdkEventButton* ev)
{
        _route->set_solo_safe (!(solo_safe_led->visual_state() > 0), this);
        return true;
}

void
RouteUI::toggle_solo_isolated (Gtk::CheckMenuItem* check)
{
        bool view = check->get_active();
        bool model = _route->solo_isolated();

        /* called AFTER the view has changed */

        if (model != view) {
                _route->set_solo_isolated (view, this);
        }
}

void
RouteUI::toggle_solo_safe (Gtk::CheckMenuItem* check)
{
	_route->set_solo_safe (check->get_active(), this);
}

bool
RouteUI::choose_color()
{
	bool picked;
	Gdk::Color color;

	color = Gtkmm2ext::UI::instance()->get_color (_("Color Selection"), picked, &_color);

	if (picked) {
		set_color (color);
	}

	return picked;
}

void
RouteUI::set_color (const Gdk::Color & c)
{
	char buf[64];

	_color = c;

	ensure_xml_node ();
	snprintf (buf, sizeof (buf), "%d:%d:%d", c.get_red(), c.get_green(), c.get_blue());
	xml_node->add_property ("color", buf);

	_route->gui_changed ("color", (void *) 0); /* EMIT_SIGNAL */
}


void
RouteUI::ensure_xml_node ()
{
	if (xml_node == 0) {
		if ((xml_node = _route->extra_xml ("GUI")) == 0) {
			xml_node = new XMLNode ("GUI");
			_route->add_extra_xml (*xml_node);
		}
	}
}

XMLNode*
RouteUI::get_automation_child_xml_node (Evoral::Parameter param)
{
	ensure_xml_node ();

	XMLNodeList kids = xml_node->children();
	XMLNodeConstIterator iter;

	const string sym = ARDOUR::EventTypeMap::instance().to_symbol(param);

	for (iter = kids.begin(); iter != kids.end(); ++iter) {
		if ((*iter)->name() == AutomationTimeAxisView::state_node_name) {
			XMLProperty* type = (*iter)->property("automation-id");
			if (type && type->value() == sym)
				return *iter;
		}
	}

	// Didn't find it, make a new one
	XMLNode* child = new XMLNode (AutomationTimeAxisView::state_node_name);
	child->add_property("automation-id", sym);
	xml_node->add_child_nocopy (*child);

	return child;
}

int
RouteUI::set_color_from_route ()
{
	XMLProperty *prop;

	RouteUI::ensure_xml_node ();

	if ((prop = xml_node->property ("color")) != 0) {
		int r, g, b;
		sscanf (prop->value().c_str(), "%d:%d:%d", &r, &g, &b);
		_color.set_red(r);
		_color.set_green(g);
		_color.set_blue(b);
		return 0;
	}
	return 1;
}

void
RouteUI::remove_this_route ()
{
	vector<string> choices;
	string prompt;

	if (is_track()) {
		prompt  = string_compose (_("Do you really want to remove track \"%1\" ?\n\nYou may also lose the playlist used by this track.\n(this cannot be undone)"), _route->name());
	} else {
		prompt  = string_compose (_("Do you really want to remove bus \"%1\" ?\n(this cannot be undone)"), _route->name());
	}

	choices.push_back (_("No, do nothing."));
	choices.push_back (_("Yes, remove it."));

	string title;
	if (is_track()) {
		title = _("Remove track");
	} else {
		title = _("Remove bus");
	}

	Choice prompter (title, prompt, choices);

	if (prompter.run () == 1) {
		Glib::signal_idle().connect (sigc::bind (sigc::ptr_fun (&RouteUI::idle_remove_this_route), this));
	}
}

gint
RouteUI::idle_remove_this_route (RouteUI *rui)
{
	rui->_session->remove_route (rui->_route);
	return false;
}

void
RouteUI::route_rename ()
{
	ArdourPrompter name_prompter (true);
	string result;
	name_prompter.set_prompt (_("New Name: "));
	name_prompter.set_initial_text (_route->name());
	name_prompter.add_button (_("Rename"), Gtk::RESPONSE_ACCEPT);
	name_prompter.set_response_sensitive (Gtk::RESPONSE_ACCEPT, false);
	name_prompter.show_all ();

	switch (name_prompter.run ()) {

	case Gtk::RESPONSE_ACCEPT:
        name_prompter.get_result (result);
        if (result.length()) {
			_route->set_name (result);
		}
		break;
	}

	return;

}

void
RouteUI::property_changed (const PropertyChange& what_changed)
{
	if (what_changed.contains (ARDOUR::Properties::name)) {
		name_label.set_text (_route->name());
	}
}

void
RouteUI::toggle_route_active ()
{
	bool yn;

	if (route_active_menu_item) {
		if (route_active_menu_item->get_active() != (yn = _route->active())) {
			_route->set_active (!yn);
		}
	}
}

void
RouteUI::route_active_changed ()
{
	if (route_active_menu_item) {
		Gtkmm2ext::UI::instance()->call_slot (invalidator (*this), boost::bind (&CheckMenuItem::set_active, route_active_menu_item, _route->active()));
	}
}


void
RouteUI::toggle_denormal_protection ()
{
	if (denormal_menu_item) {

		bool x;

		ENSURE_GUI_THREAD (*this, &RouteUI::toggle_denormal_protection)

		if ((x = denormal_menu_item->get_active()) != _route->denormal_protection()) {
			_route->set_denormal_protection (x);
		}
	}
}

void
RouteUI::denormal_protection_changed ()
{
	if (denormal_menu_item) {
		denormal_menu_item->set_active (_route->denormal_protection());
	}
}

void
RouteUI::disconnect_input ()
{
	_route->input()->disconnect (this);
}

void
RouteUI::disconnect_output ()
{
	_route->output()->disconnect (this);
}

bool
RouteUI::is_track () const
{
	return boost::dynamic_pointer_cast<Track>(_route) != 0;
}

boost::shared_ptr<Track>
RouteUI::track() const
{
	return boost::dynamic_pointer_cast<Track>(_route);
}

bool
RouteUI::is_audio_track () const
{
	return boost::dynamic_pointer_cast<AudioTrack>(_route) != 0;
}

boost::shared_ptr<AudioTrack>
RouteUI::audio_track() const
{
	return boost::dynamic_pointer_cast<AudioTrack>(_route);
}

bool
RouteUI::is_midi_track () const
{
	return boost::dynamic_pointer_cast<MidiTrack>(_route) != 0;
}

boost::shared_ptr<MidiTrack>
RouteUI::midi_track() const
{
	return boost::dynamic_pointer_cast<MidiTrack>(_route);
}

string
RouteUI::name() const
{
	return _route->name();
}

void
RouteUI::map_frozen ()
{
	ENSURE_GUI_THREAD (*this, &RouteUI::map_frozen)

 	AudioTrack* at = dynamic_cast<AudioTrack*>(_route.get());

	if (at) {
		switch (at->freeze_state()) {
		case AudioTrack::Frozen:
			rec_enable_button->set_sensitive (false);
			break;
		default:
			rec_enable_button->set_sensitive (true);
			break;
		}
	}
}

void
RouteUI::adjust_latency ()
{
	LatencyDialog dialog (_route->name() + _(" latency"), *(_route->output()), _session->frame_rate(), _session->engine().frames_per_cycle());
}

void
RouteUI::save_as_template ()
{
	sys::path path;
	Glib::ustring safe_name;
	string name;

	path = ARDOUR::user_route_template_directory ();

	if (g_mkdir_with_parents (path.to_string().c_str(), 0755)) {
		error << string_compose (_("Cannot create route template directory %1"), path.to_string()) << endmsg;
		return;
	}

	Prompter p (true); // modal

	p.set_prompt (_("Template name:"));
	switch (p.run()) {
	case RESPONSE_ACCEPT:
		break;
	default:
		return;
	}

	p.hide ();
	p.get_result (name, true);

	safe_name = legalize_for_path (name);
	safe_name += template_suffix;

	path /= safe_name;

	_route->save_as_template (path.to_string(), name);
}

void
RouteUI::check_rec_enable_sensitivity ()
{
	if (_session->transport_rolling() && rec_enable_button->get_active() && Config->get_disable_disarm_during_roll()) {
		rec_enable_button->set_sensitive (false);
	} else {
		rec_enable_button->set_sensitive (true);
	}
}

void
RouteUI::parameter_changed (string const & p)
{
	if (p == "disable-disarm-during-roll") {
		check_rec_enable_sensitivity ();
	} else if (p == "solo-control-is-listen-control") {
		set_button_names ();
	} else if (p == "listen-position") {
		set_button_names ();
	}
}

void
RouteUI::step_gain_up ()
{
	_route->set_gain (dB_to_coefficient (accurate_coefficient_to_dB (_route->gain_control()->get_value()) + 0.1), this);
}

void
RouteUI::page_gain_up ()
{
	_route->set_gain (dB_to_coefficient (accurate_coefficient_to_dB (_route->gain_control()->get_value()) + 0.5), this);
}

void
RouteUI::step_gain_down ()
{
	_route->set_gain (dB_to_coefficient (accurate_coefficient_to_dB (_route->gain_control()->get_value()) - 0.1), this);
}

void
RouteUI::page_gain_down ()
{
	_route->set_gain (dB_to_coefficient (accurate_coefficient_to_dB (_route->gain_control()->get_value()) - 0.5), this);
}

void
RouteUI::open_remote_control_id_dialog ()
{
	ArdourDialog dialog (_("Remote Control ID"));

	uint32_t const limit = _session->ntracks() + _session->nbusses () + 4;

	HBox* hbox = manage (new HBox);
	hbox->set_spacing (6);
	hbox->pack_start (*manage (new Label (_("Remote control ID:"))));
	SpinButton* spin = manage (new SpinButton);
	spin->set_digits (0);
	spin->set_increments (1, 10);
	spin->set_range (0, limit);
	spin->set_value (_route->remote_control_id());
	hbox->pack_start (*spin);
	dialog.get_vbox()->pack_start (*hbox);

	dialog.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	dialog.add_button (Stock::APPLY, RESPONSE_ACCEPT);

	dialog.show_all ();
	int const r = dialog.run ();

	if (r == RESPONSE_ACCEPT) {
		_route->set_remote_control_id (spin->get_value_as_int ());
	}
}
