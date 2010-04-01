/*
    Copyright (C) 2003 Paul Barton-Davis
 
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

    $Id$
*/

#include <cmath>
#include <iostream>

#include "pbd/xml++.h"

#include "gtkmm2ext/tearoff.h"
#include "gtkmm2ext/utils.h"

#include "i18n.h"

using namespace Gtkmm2ext;
using namespace Gtk;
using namespace Gdk;
using namespace Glib;
using namespace std;

TearOff::TearOff (Widget& c, bool allow_resize)
	: contents (c)
        , own_window (Gtk::WINDOW_TOPLEVEL)
        , tearoff_arrow (ARROW_DOWN, SHADOW_OUT)
        , close_arrow (ARROW_UP, SHADOW_OUT)
        , dragging (false)
        , _visible (true)
        , _torn (false)
        , _can_be_torn_off (true)
         
{
        own_window_width = 0;
        own_window_height = 0;
        own_window_xpos = 0;
        own_window_ypos = 0;

	tearoff_event_box.add (tearoff_arrow);
	tearoff_event_box.set_events (BUTTON_PRESS_MASK|BUTTON_RELEASE_MASK);
	tearoff_event_box.signal_button_release_event().connect (mem_fun (*this, &TearOff::tearoff_click));

	close_event_box.add (close_arrow);
	close_event_box.set_events (BUTTON_PRESS_MASK|BUTTON_RELEASE_MASK);
	close_event_box.signal_button_release_event().connect (mem_fun (*this, &TearOff::close_click));
	

	VBox* box1;
	box1 = manage (new VBox);
	box1->pack_start (close_event_box, false, false, 2);
	
	window_box.pack_end (*box1, false, false, 2);

	own_window.add_events (KEY_PRESS_MASK|KEY_RELEASE_MASK|BUTTON_PRESS_MASK|BUTTON_RELEASE_MASK|POINTER_MOTION_MASK|POINTER_MOTION_HINT_MASK);
	own_window.set_resizable (allow_resize);
	own_window.set_type_hint (WINDOW_TYPE_HINT_TOOLBAR);

	own_window.add (window_box);
	
	own_window.signal_button_press_event().connect (mem_fun (*this, &TearOff::window_button_press));
	own_window.signal_button_release_event().connect (mem_fun (*this, &TearOff::window_button_release));
	own_window.signal_motion_notify_event().connect (mem_fun (*this, &TearOff::window_motion));
	own_window.signal_delete_event().connect (mem_fun (*this, &TearOff::window_delete_event));
        own_window.signal_realize().connect (sigc::mem_fun (*this, &TearOff::own_window_realized));
        own_window.signal_configure_event().connect (sigc::mem_fun (*this, &TearOff::own_window_configured), false);

	tearoff_arrow.set_name ("TearOffArrow");
	close_arrow.set_name ("TearOffArrow");

	VBox* box2;
	box2 = manage (new VBox);
	box2->pack_start (tearoff_event_box, false, false, 2);

	pack_start (contents);
	pack_start (*box2, false, false, 2);
}

TearOff::~TearOff ()
{
}

void
TearOff::set_can_be_torn_off (bool yn)
{
	if (yn != _can_be_torn_off) {
		if (yn) {
			tearoff_arrow.set_no_show_all (false);
			tearoff_arrow.show ();
		} else {
			tearoff_arrow.set_no_show_all (true);
			tearoff_arrow.hide ();
		}
		_can_be_torn_off = yn;
	}
}

void
TearOff::set_visible (bool yn)
{
	/* don't change visibility if torn off */

	if (_torn) {
		return;
	}

	if (_visible != yn) {
		_visible = yn;
		if (yn) {
			show_all();
			Visible ();
		} else {
			hide ();
			Hidden ();
		}
	}
}

gint
TearOff::tearoff_click (GdkEventButton* /*ev*/)
{
        tear_it_off ();
	return true;
}

void
TearOff::tear_it_off ()
{
	if (!_can_be_torn_off) {
                return;
        }
                
        if (torn_off()) {
                return;
        }

        remove (contents);
        window_box.pack_start (contents);
        own_window.set_name (get_name());
        close_event_box.set_name (get_name());
        own_window.show_all ();
        own_window.present ();
        hide ();

        _torn = true;

        Detach ();
}        

gint
TearOff::close_click (GdkEventButton* /*ev*/)
{
        put_it_back ();
	return true;
}		

void
TearOff::put_it_back ()
{
        if (!torn_off()) {
                return;
        }

	window_box.remove (contents);
	pack_start (contents);
	reorder_child (contents, 0);
	own_window.hide ();
	show_all ();

        _torn = false;

	Attach ();
}

gint
TearOff::window_button_press (GdkEventButton* ev)
{
	if (dragging || ev->button != 1) {
		dragging = false;
		own_window.remove_modal_grab();
		return true;
	}

	dragging = true;
	drag_x = ev->x_root;
	drag_y = ev->y_root;

	own_window.add_modal_grab();

	return true;
}

gint
TearOff::window_button_release (GdkEventButton* /*ev*/)
{
	dragging = false;
	own_window.remove_modal_grab();
	return true;
}

gint
TearOff::window_delete_event (GdkEventAny* /*ev*/)
{
	return close_click(0);
}

gint
TearOff::window_motion (GdkEventMotion* ev)
{
	gint x;
	gint y;
	gint mx, my;
	double x_delta;
	double y_delta;
	RefPtr<Gdk::Window> win (own_window.get_window());
	
	own_window.get_pointer (mx, my);

	if (!dragging) {
		return true;
	}

	if (!(ev->state & GDK_BUTTON1_MASK)) {
		dragging = false;
		own_window.remove_modal_grab();
		return true;
	}

	x_delta = ev->x_root - drag_x;
	y_delta = ev->y_root - drag_y;

	win->get_root_origin (x, y);
	win->move ((gint) floor (x + x_delta), (gint) floor (y + y_delta));
	
	drag_x = ev->x_root;
	drag_y = ev->y_root;
	
	return true;
}

bool
TearOff::torn_off() const
{
	return _torn;
}

void
TearOff::add_state (XMLNode& node) const
{
        node.add_property ("tornoff", (_torn ? "yes" : "no"));

        if (own_window_width > 0) {
                char buf[32];

                snprintf (buf, sizeof (buf), "%d", own_window_width);
                node.add_property ("width", buf);
                snprintf (buf, sizeof (buf), "%d", own_window_height);
                node.add_property ("height", buf);
                snprintf (buf, sizeof (buf), "%d", own_window_xpos);
                node.add_property ("xpos", buf);
                snprintf (buf, sizeof (buf), "%d", own_window_ypos);
                node.add_property ("ypos", buf);
        }
}        

void
TearOff::set_state (const XMLNode& node)
{
        Glib::RefPtr<Gdk::Window> win;
        const XMLProperty* prop;

        if ((prop = node.property (X_("tornoff"))) == 0) {
                return;
        }

        if (prop->value() == "yes") {
                tear_it_off ();
        } else {
                put_it_back ();
        }

        if ((prop = node.property (X_("width"))) != 0) {
                sscanf (prop->value().c_str(), "%d", &own_window_width);
        }
        if ((prop = node.property (X_("height"))) != 0) {
                sscanf (prop->value().c_str(), "%d", &own_window_height);
        }
        if ((prop = node.property (X_("xpos"))) != 0) {
                sscanf (prop->value().c_str(), "%d", &own_window_xpos);
        }
        if ((prop = node.property (X_("ypos"))) != 0) {
                sscanf (prop->value().c_str(), "%d", &own_window_ypos);
        }

        if (own_window.is_realized()) {
                own_window.set_default_size (own_window_width, own_window_height);
                own_window.move (own_window_xpos, own_window_ypos);
        }
        /* otherwise do it once the window is realized, see below */
}        

void
TearOff::own_window_realized ()
{
	own_window.get_window()->set_decorations (WMDecoration (DECOR_BORDER|DECOR_RESIZEH));

        if (own_window_width > 0) {
                own_window.set_default_size (own_window_width, own_window_height);
                own_window.move (own_window_xpos, own_window_ypos);
        }
}

bool
TearOff::own_window_configured (GdkEventConfigure*)
{
        Glib::RefPtr<const Gdk::Window> win;

        win = own_window.get_window ();
        
        if (win) {
                win->get_size (own_window_width, own_window_height);
                win->get_position (own_window_xpos, own_window_ypos);
        }

        return false;
}

void
TearOff::hide_visible ()
{
        if (torn_off()) {
                own_window.hide ();
        }

        hide ();
}


