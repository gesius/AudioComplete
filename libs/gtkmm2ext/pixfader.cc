/*
    Copyright (C) 2006 Paul Davis 

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

    $Id: fastmeter.h 570 2006-06-07 21:21:21Z sampo $
*/


#include <iostream>

#include "pbd/stacktrace.h"

#include "gtkmm2ext/pixfader.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/rgb_macros.h"
#include "gtkmm2ext/utils.h"

using namespace Gtkmm2ext;
using namespace Gtk;
using namespace std;

#define CORNER_RADIUS 4
#define CORNER_SIZE   2
#define CORNER_OFFSET 1
#define FADER_RESERVE 5

std::list<PixFader::FaderImage*> PixFader::_patterns;

PixFader::PixFader (Gtk::Adjustment& adj, int orientation, int fader_length, int fader_girth)
	: adjustment (adj)
	, span (fader_length)
	, girth (fader_girth)
	, _orien (orientation)
	, pattern (0)
	, _hovering (false)
	, last_drawn (-1)
	, dragging (false)
{
	default_value = adjustment.get_value();
	update_unity_position ();

	add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK|Gdk::SCROLL_MASK|Gdk::ENTER_NOTIFY_MASK|Gdk::LEAVE_NOTIFY_MASK);

	adjustment.signal_value_changed().connect (mem_fun (*this, &PixFader::adjustment_changed));
	adjustment.signal_changed().connect (mem_fun (*this, &PixFader::adjustment_changed));

	if (_orien == VERT) {
		DrawingArea::set_size_request(girth, span);
	} else {
		DrawingArea::set_size_request(span, girth);
	}
}

PixFader::~PixFader ()
{
}

cairo_pattern_t*
PixFader::find_pattern (double afr, double afg, double afb, 
			double abr, double abg, double abb, 
			int w, int h)
{
	for (list<FaderImage*>::iterator f = _patterns.begin(); f != _patterns.end(); ++f) {
		if ((*f)->matches (afr, afg, afb, abr, abg, abb, w, h)) {
			return (*f)->pattern;
		}
	}
	return 0;
}

void
PixFader::create_patterns ()
{
	Gdk::Color c = get_style()->get_fg (get_state());
	float fr, fg, fb;
	float br, bg, bb;

	fr = c.get_red_p ();
	fg = c.get_green_p ();
	fb = c.get_blue_p ();

	c = get_style()->get_bg (get_state());

	br = c.get_red_p ();
	bg = c.get_green_p ();
	bb = c.get_blue_p ();

	if ( !_text.empty()) {
		_layout->get_pixel_size (_text_width, _text_height);
	} else {
		_text_width = 0;
		_text_height = 0;
	}

	c = get_style()->get_text (get_state());

	text_r = c.get_red_p ();
	text_g = c.get_green_p ();
	text_b = c.get_blue_p ();

	cairo_surface_t* surface;
	cairo_t* tc = 0;

	if (get_width() <= 1 || get_height() <= 1) {
		return;
	}

	if ((pattern = find_pattern (fr, fg, fb, br, bg, bb, get_width(), get_height())) != 0) {
		/* found it - use it */
		return;
	}

 	if (_orien == VERT) {
		
		surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, get_width(), get_height() * 2.0);
		tc = cairo_create (surface);

		/* paint background + border */

		cairo_pattern_t* shade_pattern = cairo_pattern_create_linear (0.0, 0.0, get_width(), 0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, br*0.8,bg*0.8,bb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, br*0.6,bg*0.6,bb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		cairo_rectangle (tc, 0, 0, get_width(), get_height() * 2.0);
		cairo_fill (tc);

		cairo_pattern_destroy (shade_pattern);
		
		/* paint lower shade */
		
		shade_pattern = cairo_pattern_create_linear (0.0, 0.0, get_width() - 2 - CORNER_OFFSET , 0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, fr*0.8,fg*0.8,fb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, fr*0.6,fg*0.6,fb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		Gtkmm2ext::rounded_top_half_rectangle (tc, CORNER_OFFSET, get_height() + CORNER_OFFSET,
				get_width() - CORNER_SIZE, get_height(), CORNER_RADIUS);
		cairo_fill (tc);

		cairo_pattern_destroy (shade_pattern);

		pattern = cairo_pattern_create_for_surface (surface);

	} else {

		surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, get_width() * 2.0, get_height());
		tc = cairo_create (surface);

		/* paint right shade (background section)*/

		cairo_pattern_t* shade_pattern = cairo_pattern_create_linear (0.0, 0.0, 0.0, get_height());
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, br*0.8,bg*0.8,bb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, br*0.6,bg*0.6,bb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		cairo_rectangle (tc, 0, 0, get_width() * 2.0, get_height());
		cairo_fill (tc);

		/* paint left shade (active section/foreground) */
		
		shade_pattern = cairo_pattern_create_linear (0.0, 0.0, 0.0, get_height());
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, fr*0.8,fg*0.8,fb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, fr*0.6,fg*0.6,fb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		Gtkmm2ext::rounded_right_half_rectangle (tc, CORNER_OFFSET, CORNER_OFFSET,
				get_width() - CORNER_OFFSET, get_height() - CORNER_SIZE, CORNER_RADIUS);
		cairo_fill (tc);
		cairo_pattern_destroy (shade_pattern);
		
		pattern = cairo_pattern_create_for_surface (surface);
	}

	/* cache it for others to use */

	_patterns.push_back (new FaderImage (pattern, fr, fg, fb, br, bg, bb, get_width(), get_height()));

	cairo_destroy (tc);
	cairo_surface_destroy (surface);
}

bool
PixFader::on_expose_event (GdkEventExpose* ev)
{
	Cairo::RefPtr<Cairo::Context> context = get_window()->create_cairo_context();
	cairo_t* cr = context->cobj();

	if (!pattern) {
		create_patterns();
	}

	if (!pattern) {

		/* this isn't supposed to be happen, but some wackiness whereby
		   the pixfader ends up with a 1xN or Nx1 size allocation
		   leads to it. the basic wackiness needs fixing but we
		   shouldn't crash. just fill in the expose area with 
		   our bg color.
		*/

		Gdk::Color c = get_style()->get_bg (get_state());
		float br, bg, bb;

		br = c.get_red_p ();
		bg = c.get_green_p ();
		bb = c.get_blue_p ();
		cairo_set_source_rgb (cr, br, bg, bb);
		cairo_rectangle (cr, ev->area.x, ev->area.y, ev->area.width, ev->area.height);
		cairo_fill (cr);
		return true;
	}
		   
	cairo_rectangle (cr, ev->area.x, ev->area.y, ev->area.width, ev->area.height);
	cairo_clip (cr);

	int ds = display_span ();
	float w = get_width();
	float h = get_height();

	Gdk::Color c = get_style()->get_bg (Gtk::STATE_PRELIGHT);
	cairo_set_source_rgb (cr, c.get_red_p(), c.get_green_p(), c.get_blue_p());
	cairo_rectangle (cr, 0, 0, w, h);
	cairo_fill(cr);

	cairo_set_line_width (cr, 1);
	cairo_set_source_rgba (cr, 0, 0, 0, 1.0);

	cairo_matrix_t matrix;
	Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, CORNER_OFFSET, w-CORNER_SIZE, h-CORNER_SIZE, CORNER_RADIUS);
	cairo_stroke_preserve(cr);

	if (_orien == VERT) {

		if (ds > h - FADER_RESERVE - CORNER_OFFSET) {
			ds = h - FADER_RESERVE - CORNER_OFFSET;
		}

		cairo_set_source (cr, pattern);
		cairo_matrix_init_translate (&matrix, 0, (h - ds));
		cairo_pattern_set_matrix (pattern, &matrix);
		cairo_fill (cr);

	} else {

		if (ds < FADER_RESERVE) {
			ds = FADER_RESERVE;
		}

		/*
		  if ds == w, the pattern does not need to be translated
		  if ds == 0 (or FADER_RESERVE), the pattern needs to be moved
   		      w to the left, which is -w in pattern space, and w in
		      user space
		  if ds == 10, then the pattern needs to be moved w - 10
		      to the left, which is -(w-10) in pattern space, which 
		      is (w - 10) in user space

		  thus: translation = (w - ds)
		 */

		cairo_set_source (cr, pattern);
		cairo_matrix_init_translate (&matrix, w - ds, 0);
		cairo_pattern_set_matrix (pattern, &matrix);
		cairo_fill (cr);
	}
		
	/* draw the unity-position line if it's not at either end*/
	if (unity_loc > 0) {
		context->set_line_width (1);
		context->set_line_cap (Cairo::LINE_CAP_ROUND);
		Gdk::Color c = get_style()->get_fg (Gtk::STATE_ACTIVE);
		context->set_source_rgba (c.get_red_p()*1.5, c.get_green_p()*1.5, c.get_blue_p()*1.5, 0.85);
		if ( _orien == VERT) {
			if (unity_loc < h ) {
				context->move_to (1.5, unity_loc + CORNER_OFFSET + .5);
				context->line_to (girth - 1.5, unity_loc + CORNER_OFFSET + .5);
				context->stroke ();
			}
		} else {
			if ( unity_loc < w ){
				context->move_to (unity_loc - CORNER_OFFSET + .5, 1.5);
				context->line_to (unity_loc - CORNER_OFFSET + .5, girth - 1.5);
				context->stroke ();
			}
		}
	}

	if ( !_text.empty() ) {

		/* center text */
		cairo_new_path (cr);
		cairo_move_to (cr, (get_width() - _text_width)/2.0, get_height()/2.0 - _text_height/2.0);
		cairo_set_source_rgba (cr, text_r, text_g, text_b, 0.9);
		pango_cairo_show_layout (cr, _layout->gobj());
	} 
	
	if (!get_sensitive()) {
		Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, CORNER_OFFSET, w-CORNER_SIZE, h-CORNER_SIZE, CORNER_RADIUS);
		cairo_set_source_rgba (cr, 0.505, 0.517, 0.525, 0.4);
		cairo_fill (cr);
	} else if (_hovering) {
		Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, CORNER_OFFSET, w-CORNER_SIZE, h-CORNER_SIZE, CORNER_RADIUS);
		cairo_set_source_rgba (cr, 0.905, 0.917, 0.925, 0.1);
		cairo_fill (cr);
	}

	last_drawn = ds;

	return true;
}

void
PixFader::on_size_request (GtkRequisition* req)
{
	if (_orien == VERT) {
		req->width = (girth ? girth : -1);
		req->height = (span ? span : -1);
	} else {
		req->height = (girth ? girth : -1);
		req->width = (span ? span : -1);
	}
}

void
PixFader::on_size_allocate (Gtk::Allocation& alloc)
{
	DrawingArea::on_size_allocate(alloc);

	if (_orien == VERT) {
		girth = alloc.get_width ();
		span = alloc.get_height ();
	} else {
		girth = alloc.get_height ();
		span = alloc.get_width ();
	}

	if (is_realized()) {
		/* recreate patterns in case we've changed size */
		create_patterns ();
	}

	update_unity_position ();
}

bool
PixFader::on_button_press_event (GdkEventButton* ev)
{
	if (ev->type != GDK_BUTTON_PRESS) {
		return true;
	}

	if (ev->button != 1 && ev->button != 2) {
		return false;
	}

	add_modal_grab ();
	grab_loc = (_orien == VERT) ? ev->y : ev->x;
	grab_start = (_orien == VERT) ? ev->y : ev->x;
	grab_window = ev->window;
	dragging = true;
	gdk_pointer_grab(ev->window,false,
			GdkEventMask( Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_PRESS_MASK |Gdk::BUTTON_RELEASE_MASK),
			NULL,NULL,ev->time);

	if (ev->button == 2) {
		set_adjustment_from_event (ev);
	}
	
	return true;
}

bool
PixFader::on_button_release_event (GdkEventButton* ev)
{
	double const ev_pos = (_orien == VERT) ? ev->y : ev->x;
	
	switch (ev->button) {
	case 1:
		if (dragging) {
			remove_modal_grab();
			dragging = false;
			gdk_pointer_ungrab (GDK_CURRENT_TIME);

			if (!_hovering) {
				Keyboard::magic_widget_drop_focus();
				queue_draw ();
			}

			if (ev_pos == grab_start) {

				/* no motion - just a click */

				if (ev->state & Keyboard::TertiaryModifier) {
					adjustment.set_value (default_value);
				} else if (ev->state & Keyboard::GainFineScaleModifier) {
					adjustment.set_value (adjustment.get_lower());
				} else if ((_orien == VERT && ev_pos < display_span()) || (_orien == HORIZ && ev_pos > display_span())) {
					/* above the current display height, remember X Window coords */
					adjustment.set_value (adjustment.get_value() + adjustment.get_step_increment());
				} else {
					adjustment.set_value (adjustment.get_value() - adjustment.get_step_increment());
				}
			}
			return true;
		} 
		break;
		
	case 2:
		if (dragging) {
			remove_modal_grab();
			dragging = false;
			set_adjustment_from_event (ev);
			gdk_pointer_ungrab (GDK_CURRENT_TIME);
			return true;
		}
		break;

	default:
		break;
	}

	return false;
}

bool
PixFader::on_scroll_event (GdkEventScroll* ev)
{
	double scale;
	bool ret = false;

	if (ev->state & Keyboard::GainFineScaleModifier) {
		if (ev->state & Keyboard::GainExtraFineScaleModifier) {
			scale = 0.01;
		} else {
			scale = 0.05;
		}
	} else {
		scale = 0.25;
	}

	if (_orien == VERT) {

		/* should left/right scroll affect vertical faders ? */

		switch (ev->direction) {

		case GDK_SCROLL_UP:
			adjustment.set_value (adjustment.get_value() + (adjustment.get_page_increment() * scale));
			ret = true;
			break;
		case GDK_SCROLL_DOWN:
			adjustment.set_value (adjustment.get_value() - (adjustment.get_page_increment() * scale));
			ret = true;
			break;
		default:
			break;
		}
	} else {

		/* up/down scrolls should definitely affect horizontal faders
		   because they are so much easier to use
		*/

		switch (ev->direction) {

		case GDK_SCROLL_RIGHT:
		case GDK_SCROLL_UP:
			adjustment.set_value (adjustment.get_value() + (adjustment.get_page_increment() * scale));
			ret = true;
			break;
		case GDK_SCROLL_LEFT:
		case GDK_SCROLL_DOWN:
			adjustment.set_value (adjustment.get_value() - (adjustment.get_page_increment() * scale));
			ret = true;
			break;
		default:
			break;
		}
	}
	return ret;
}

bool
PixFader::on_motion_notify_event (GdkEventMotion* ev)
{
	if (dragging) {
		double scale = 1.0;
		double const ev_pos = (_orien == VERT) ? ev->y : ev->x;
		
		if (ev->window != grab_window) {
			grab_loc = ev_pos;
			grab_window = ev->window;
			return true;
		}
		
		if (ev->state & Keyboard::GainFineScaleModifier) {
			if (ev->state & Keyboard::GainExtraFineScaleModifier) {
				scale = 0.05;
			} else {
				scale = 0.1;
			}
		}

		double const delta = ev_pos - grab_loc;
		grab_loc = ev_pos;

		double fract = (delta / span);

		fract = min (1.0, fract);
		fract = max (-1.0, fract);

		// X Window is top->bottom for 0..Y
		
		if (_orien == VERT) {
			fract = -fract;
		}

		adjustment.set_value (adjustment.get_value() + scale * fract * (adjustment.get_upper() - adjustment.get_lower()));
	}

	return true;
}

void
PixFader::adjustment_changed ()
{
	if (display_span() != last_drawn) {
		queue_draw ();
	}
}

/** @return pixel offset of the current value from the right or bottom of the fader */
int
PixFader::display_span ()
{
	float fract = (adjustment.get_value () - adjustment.get_lower()) / ((adjustment.get_upper() - adjustment.get_lower()));
	int ds;
	if (_orien == VERT) {
		ds = (int)floor ( span * (1.0 - fract));
	} else {
		ds = (int)floor (span * fract);
	}
	
	return ds;
}

void
PixFader::update_unity_position ()
{
	if (_orien == VERT) {
		unity_loc = (int) rint (span * (1 - (default_value / (adjustment.get_upper() - adjustment.get_lower())))) - 1;
	} else {
		unity_loc = (int) rint (default_value * span / (adjustment.get_upper() - adjustment.get_lower()));
	}

	queue_draw ();
}

bool
PixFader::on_enter_notify_event (GdkEventCrossing*)
{
	_hovering = true;
	Keyboard::magic_widget_grab_focus ();
	queue_draw ();
	return false;
}

bool
PixFader::on_leave_notify_event (GdkEventCrossing*)
{
	if (!dragging) {
		_hovering = false;
		Keyboard::magic_widget_drop_focus();
		queue_draw ();
	}
	return false;
}

void
PixFader::set_adjustment_from_event (GdkEventButton* ev)
{
	double fract = (_orien == VERT) ? (1.0 - (ev->y / span)) : (ev->x / span);

	fract = min (1.0, fract);
	fract = max (0.0, fract);

	adjustment.set_value (fract * (adjustment.get_upper () - adjustment.get_lower ()));
}

void
PixFader::set_default_value (float d)
{
	default_value = d;
	update_unity_position ();
}

void
PixFader::set_text (const std::string& str)
{
	_text = str;

 	if (!_layout && !_text.empty()) {
		_layout = Pango::Layout::create (get_pango_context());
	} 

	if (_layout) {
		_layout->set_text (str);
		_layout->get_pixel_size (_text_width, _text_height);
	}

	queue_resize ();
}

void
PixFader::on_state_changed (Gtk::StateType old_state)
{
	Widget::on_state_changed (old_state);
	create_patterns ();
	queue_draw ();
}

void
PixFader::on_style_changed (const Glib::RefPtr<Gtk::Style>&)
{
	if (_layout) {
		std::string txt = _layout->get_text();
		_layout.clear (); // drop reference to existing layout
		set_text (txt);
	}

	/* remember that all patterns are cached and not owned by an individual
	   pixfader. we will lazily create a new pattern when needed.
	*/

	pattern = 0;
	queue_draw ();
}
