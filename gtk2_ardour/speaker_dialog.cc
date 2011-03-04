/*
    Copyright (C) 2011 Paul Davis

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

#include "pbd/cartesian.h"

#include "gtkmm2ext/keyboard.h"

#include "speaker_dialog.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;

SpeakerDialog::SpeakerDialog ()
        : ArdourDialog (_("Speaker Configuration"))
        , aspect_frame ("", 0.5, 0.5, 1.0, false)
        , azimuth_adjustment (0, 0.0, 360.0, 10.0, 1.0)
        , azimuth_spinner (azimuth_adjustment)
        , add_speaker_button (_("Add Speaker"))
        , use_system_button (_("Use System"))
                              
{
        side_vbox.set_homogeneous (false);
        side_vbox.set_border_width (9);
        side_vbox.set_spacing (6);
        side_vbox.pack_start (azimuth_spinner, false, false);
        side_vbox.pack_start (add_speaker_button, false, false);
        side_vbox.pack_start (use_system_button, false, false);

        aspect_frame.set_size_request (200, 200);
        aspect_frame.set_shadow_type (SHADOW_NONE);
        aspect_frame.add (darea);

        hbox.set_spacing (6);
        hbox.set_border_width (6);
        hbox.pack_start (aspect_frame, true, true);
        hbox.pack_start (side_vbox, false, false);

        get_vbox()->pack_start (hbox);
        get_vbox()->show_all ();

        darea.add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK);

        darea.signal_size_allocate().connect (sigc::mem_fun (*this, &SpeakerDialog::darea_size_allocate));
        darea.signal_expose_event().connect (sigc::mem_fun (*this, &SpeakerDialog::darea_expose_event));
        darea.signal_button_press_event().connect (sigc::mem_fun (*this, &SpeakerDialog::darea_button_press_event));
        darea.signal_button_release_event().connect (sigc::mem_fun (*this, &SpeakerDialog::darea_button_release_event));
        darea.signal_motion_notify_event().connect (sigc::mem_fun (*this, &SpeakerDialog::darea_motion_notify_event));

	add_speaker_button.signal_clicked().connect (sigc::mem_fun (*this, &SpeakerDialog::add_speaker));

        drag_index = -1;
}

void
SpeakerDialog::set_speakers (boost::shared_ptr<Speakers> s) 
{
        speakers = *s;
}

Speakers
SpeakerDialog::get_speakers () const
{
        return speakers;
}

bool
SpeakerDialog::darea_expose_event (GdkEventExpose* event)
{
	gint x, y;
	cairo_t* cr;

	cr = gdk_cairo_create (darea.get_window()->gobj());

	cairo_set_line_width (cr, 1.0);

	cairo_rectangle (cr, event->area.x, event->area.y, event->area.width, event->area.height);
        cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 1.0);
	cairo_fill_preserve (cr);
	cairo_clip (cr);

	if (height > 100) {
		cairo_translate (cr, 10.0, 10.0);
	}

	/* horizontal line of "crosshairs" */

	cairo_set_source_rgb (cr, 0.0, 0.1, 0.7);
	cairo_move_to (cr, 0.5, height/2.0+0.5);
	cairo_line_to (cr, width+0.5, height/2+0.5);
	cairo_stroke (cr);

	/* vertical line of "crosshairs" */
	
	cairo_move_to (cr, width/2+0.5, 0.5);
	cairo_line_to (cr, width/2+0.5, height+0.5);
	cairo_stroke (cr);

	/* the circle on which signals live */

	cairo_arc (cr, width/2, height/2, height/2, 0, 2.0 * M_PI);
	cairo_stroke (cr);

        float arc_radius;
        
        cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        
        if (height < 100) {
                cairo_set_font_size (cr, 10);
                arc_radius = 2.0;
        } else {
                cairo_set_font_size (cr, 16);
                arc_radius = 4.0;
        }

        uint32_t n = 0;
        for (vector<Speaker>::iterator i = speakers.speakers().begin(); i != speakers.speakers().end(); ++i) {
                
                Speaker& s (*i);
                CartesianVector c (s.coords());
                
                cart_to_gtk (c);
                
                x = (gint) floor (c.x);
                y = (gint) floor (c.y);
                
                /* XXX need to shift circles so that they are centered on the circle */
                
                cairo_arc (cr, x, y, arc_radius, 0, 2.0 * M_PI);
                cairo_set_source_rgb (cr, 0.8, 0.2, 0.1);
                cairo_close_path (cr);
                cairo_fill (cr);
                
                cairo_move_to (cr, x + 6, y + 6);
                
                char buf[256];
                snprintf (buf, sizeof (buf), "%d:%d", n+1, (int) lrint (s.angles().azi));
                cairo_show_text (cr, buf);
                ++n;
        }

        cairo_destroy (cr);

	return true;
        
}

void
SpeakerDialog::cart_to_gtk (CartesianVector& c) const
{
	/* "c" uses a coordinate space that is:
            
	   center = 0.0
	   dimension = 2.0 * 2.0
	   so max values along each axis are -1..+1

	   GTK uses a coordinate space that is:

	   top left = 0.0
	   dimension = width * height
	   so max values along each axis are 0,width and
	   0,height
	*/

	c.x = (width / 2) * (c.x + 1);
	c.y = (height / 2) * (1 - c.y);

	/* XXX z-axis not handled - 2D for now */
}

void
SpeakerDialog::gtk_to_cart (CartesianVector& c) const
{
	c.x = (c.x / (width / 2.0)) - 1.0;
	c.y = -((c.y / (height / 2.0)) - 1.0);

	/* XXX z-axis not handled - 2D for now */
}

void
SpeakerDialog::clamp_to_circle (double& x, double& y)
{
	double azi, ele;
	double z = 0.0;
        double l;

	PBD::cartesian_to_spherical (x, y, z, azi, ele, l);
	PBD::spherical_to_cartesian (azi, ele, 1.0, x, y, z);
}

void
SpeakerDialog::darea_size_allocate (Gtk::Allocation& alloc)
{
  	width = alloc.get_width();
  	height = alloc.get_height();

	if (height > 100) {
		width -= 20;
		height -= 20;
	}
}

bool
SpeakerDialog::darea_button_press_event (GdkEventButton *ev)
{
	GdkModifierType state;

	if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
		return false;
	}

        drag_index = -1;

	switch (ev->button) {
	case 1:
	case 2:
		drag_index = find_closest_object (ev->x, ev->y);
		drag_x = (int) floor (ev->x);
		drag_y = (int) floor (ev->y);
		state = (GdkModifierType) ev->state;

		return handle_motion (drag_x, drag_y, state);
		break;

	default:
		break;
	}

	return false;
}

bool
SpeakerDialog::darea_button_release_event (GdkEventButton *ev)
{
	gint x, y;
	GdkModifierType state;
	bool ret = false;

	switch (ev->button) {
	case 1:
		x = (int) floor (ev->x);
		y = (int) floor (ev->y);
		state = (GdkModifierType) ev->state;

		if (Keyboard::modifier_state_contains (state, Keyboard::TertiaryModifier)) {
                        
			for (vector<Speaker>::iterator i = speakers.speakers().begin(); i != speakers.speakers().end(); ++i) {
				/* XXX DO SOMETHING TO SET SPEAKER BACK TO "normal" */
			}

			queue_draw ();
			ret = true;

		} else {
			ret = handle_motion (x, y, state);
		}

		break;

	case 2:
		x = (int) floor (ev->x);
		y = (int) floor (ev->y);
		state = (GdkModifierType) ev->state;

                ret = handle_motion (x, y, state);
		break;

	case 3:
		break;

	}
        
        drag_index = -1;

	return ret;
}

int
SpeakerDialog::find_closest_object (gdouble x, gdouble y) 
{
	float distance;
	float best_distance = FLT_MAX;
	int n = 0;
        int which = -1;

	for (vector<Speaker>::iterator i = speakers.speakers().begin(); i != speakers.speakers().end(); ++i, ++n) {

		Speaker& candidate (*i);
		CartesianVector c;
        
		candidate.angles().cartesian (c);
		cart_to_gtk (c);

		distance = sqrt ((c.x - x) * (c.x - x) +
		                 (c.y - y) * (c.y - y));


		if (distance < best_distance) {
			best_distance = distance;
			which = n;
		}
	}

	if (best_distance > 20) { // arbitrary 
                return -1;
	}

        return which;
}

bool
SpeakerDialog::darea_motion_notify_event (GdkEventMotion *ev)
{
	gint x, y;
	GdkModifierType state;

	if (ev->is_hint) {
		gdk_window_get_pointer (ev->window, &x, &y, &state);
	} else {
		x = (int) floor (ev->x);
		y = (int) floor (ev->y);
		state = (GdkModifierType) ev->state;
	}

	return handle_motion (x, y, state);
}

bool
SpeakerDialog::handle_motion (gint evx, gint evy, GdkModifierType state)
{
	if (drag_index < 0) {
		return false;
	}

	if ((state & (GDK_BUTTON1_MASK|GDK_BUTTON2_MASK)) == 0) {
		return false;
	}


	if (state & GDK_BUTTON1_MASK && !(state & GDK_BUTTON2_MASK)) {
		CartesianVector c;
		bool need_move = false;
                Speaker& moving (speakers.speakers()[drag_index]);

		moving.angles().cartesian (c);
		cart_to_gtk (c);

		if ((evx != c.x) || (evy != c.y)) {
			need_move = true;
		}

		if (need_move) {
			CartesianVector cp (evx, evy, 0.0);

			/* canonicalize position */

			gtk_to_cart (cp);

			/* position actual signal on circle */

			clamp_to_circle (cp.x, cp.y);
                        
			/* generate an angular representation and set drag target (GUI) position */

                        AngularVector a;

			cp.angular (a);

                        moving.move (a);

			queue_draw ();
		}
	} 

	return true;
}

void
SpeakerDialog::add_speaker ()
{
	speakers.add_speaker (PBD::AngularVector (0, 0, 0));
	queue_draw ();
}
