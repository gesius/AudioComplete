/*
    Copyright (C) 2010 Paul Davis

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

#ifndef __gtk2_ardour_ardour_button_h__
#define __gtk2_ardour_ardour_button_h__

#include <list>
#include <stdint.h>

#include <gtkmm/action.h>

#include "pbd/signals.h"
#include "gtkmm2ext/binding_proxy.h"

#include "cairo_widget.h"

class ArdourButton : public CairoWidget
{
  public:
	enum Element {
		Edge = 0x1,
		Body = 0x2,
		Text = 0x4,
		Indicator = 0x8,
	};

	static Element default_elements;
	static Element led_default_elements;
	static Element just_led_default_elements;

	ArdourButton (Element e = default_elements);
	ArdourButton (const std::string&, Element e = default_elements);
	virtual ~ArdourButton ();

	enum Tweaks {
		ShowClick = 0x1,
		NoModel = 0x4,
	};

	Tweaks tweaks() const { return _tweaks; }
	void set_tweaks (Tweaks);

	void set_active_state (Gtkmm2ext::ActiveState);
	void set_visual_state (Gtkmm2ext::VisualState);

	/* this is an API simplification for buttons
	   that only use the Active and Normal active states.
	*/
	void set_active (bool);
	bool get_active () { return active_state() != Gtkmm2ext::ActiveState (0); }

	void set_elements (Element);
	Element elements() const { return _elements; }

	void set_corner_radius (float);
	void set_diameter (float);

	void set_text (const std::string&);
	void set_markup (const std::string&);

	void set_led_left (bool yn);
	void set_distinct_led_click (bool yn);

	sigc::signal<void> signal_led_clicked;

	boost::shared_ptr<PBD::Controllable> get_controllable() { return binding_proxy.get_controllable(); }
 	void set_controllable (boost::shared_ptr<PBD::Controllable> c);
        void watch ();

	void set_related_action (Glib::RefPtr<Gtk::Action>);

	bool on_button_press_event (GdkEventButton*);
	bool on_button_release_event (GdkEventButton*);

	void set_image (const Glib::RefPtr<Gdk::Pixbuf>&);

  protected:
	void render (cairo_t *);
	void on_size_request (Gtk::Requisition* req);
	void on_size_allocate (Gtk::Allocation&);
	void on_style_changed (const Glib::RefPtr<Gtk::Style>&);
	bool on_enter_notify_event (GdkEventCrossing*);
	bool on_leave_notify_event (GdkEventCrossing*);

        void controllable_changed ();
        PBD::ScopedConnection watch_connection;

  private:
	Glib::RefPtr<Pango::Layout> _layout;
	Glib::RefPtr<Gdk::Pixbuf>   _pixbuf;
	std::string                 _text;
	Element                     _elements;
	Tweaks                      _tweaks;
	BindingProxy binding_proxy;
	bool    _act_on_release;

	int   _text_width;
	int   _text_height;
	float _diameter;
	float _corner_radius;

	cairo_pattern_t* edge_pattern;
	cairo_pattern_t* fill_pattern;
	cairo_pattern_t* led_inset_pattern;
	cairo_pattern_t* reflection_pattern;

	double text_r;
	double text_g;
	double text_b;
	double text_a;

	double led_r;
	double led_g;
	double led_b;
	double led_a;

	bool _led_left;
	bool _fixed_diameter;
	bool _distinct_led_click;
	cairo_rectangle_t* _led_rect;
	bool _hovering;

	void setup_led_rect ();
	void set_colors ();
	void color_handler ();

	Glib::RefPtr<Gtk::Action> _action;
	void action_activated ();
	void action_toggled ();

	void action_sensitivity_changed ();
	void action_visibility_changed ();
	void action_tooltip_changed ();
};

#endif /* __gtk2_ardour_ardour_button_h__ */
