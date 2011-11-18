/*
    Copyright (C) 1999 Paul Davis

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

#include <cstdio> // for sprintf
#include <cmath>

#include "pbd/convert.h"
#include "pbd/enumwriter.h"

#include <gtkmm/style.h>

#include "gtkmm2ext/cairocell.h"
#include "gtkmm2ext/utils.h"
#include "gtkmm2ext/rgb_macros.h"

#include "ardour/ardour.h"
#include "ardour/session.h"
#include "ardour/tempo.h"
#include "ardour/profile.h"
#include "ardour/slave.h"
#include <sigc++/bind.h>

#include "ardour_ui.h"
#include "audio_clock.h"
#include "global_signals.h"
#include "utils.h"
#include "keyboard.h"
#include "gui_thread.h"
#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace std;

using Gtkmm2ext::Keyboard;

sigc::signal<void> AudioClock::ModeChanged;
vector<AudioClock*> AudioClock::clocks;
const double AudioClock::info_font_scale_factor = 0.6;
const double AudioClock::separator_height = 2.0;
const double AudioClock::x_leading_padding = 6.0;

AudioClock::AudioClock (const string& clock_name, bool transient, const string& widget_name,
			bool allow_edit, bool follows_playhead, bool duration, bool with_info)
	: _name (clock_name)
	, is_transient (transient)
	, is_duration (duration)
	, editable (allow_edit)
	, _follows_playhead (follows_playhead)
	, _off (false)
	, ops_menu (0)
	, editing_attr (0)
	, foreground_attr (0)
	, mode_based_info_ratio (1.0)
	, editing (false)
	, bbt_reference_time (-1)
	, last_when(0)
	, last_pdelta (0)
	, last_sdelta (0)
	, dragging (false)
	, drag_field (Field (0))

{
	set_flags (CAN_FOCUS);

	_layout = Pango::Layout::create (get_pango_context());
	_layout->set_attributes (normal_attributes);

	if (with_info) {
		_left_layout = Pango::Layout::create (get_pango_context());
		_right_layout = Pango::Layout::create (get_pango_context());
	}

	set_widget_name (widget_name);

	_mode = BBT; /* lie to force mode switch */
	set_mode (Timecode);
	set (last_when, true);

	if (!is_transient) {
		clocks.push_back (this);
	}

	ColorsChanged.connect (sigc::mem_fun (*this, &AudioClock::set_colors));
}

AudioClock::~AudioClock ()
{
	delete foreground_attr;
	delete editing_attr;
}

void
AudioClock::set_widget_name (const string& str)
{
	if (str.empty()) {
		set_name ("clock");
	} else {
		set_name (str + " clock");
	}

	if (is_realized()) {
		set_colors ();
	}
}


void
AudioClock::on_realize ()
{
	CairoWidget::on_realize ();
	set_font ();
	set_colors ();
}

void
AudioClock::set_font ()
{
	Glib::RefPtr<Gtk::Style> style = get_style ();
	Pango::FontDescription font; 
	Pango::AttrFontDesc* font_attr;

	if (!is_realized()) {
		font = get_font_for_style (get_name());
	} else {
		font = style->get_font();
	}

	font_attr = new Pango::AttrFontDesc (Pango::Attribute::create_attr_font_desc (font));

	normal_attributes.change (*font_attr);
	editing_attributes.change (*font_attr);

	/* now a smaller version of the same font */

	delete font_attr;
	font.set_size ((int) lrint (font.get_size() * info_font_scale_factor));
	font.set_weight (Pango::WEIGHT_NORMAL);
	font_attr = new Pango::AttrFontDesc (Pango::Attribute::create_attr_font_desc (font));
 
	info_attributes.change (*font_attr);
	
	delete font_attr;
}

void
AudioClock::set_active_state (Gtkmm2ext::ActiveState s)
{
	CairoWidget::set_active_state (s);
	set_colors ();
}

void
AudioClock::set_colors ()
{
	int r, g, b, a;

	uint32_t bg_color;
	uint32_t text_color;
	uint32_t editing_color;

	if (active_state()) {
		bg_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1 active: background", get_name()));
		text_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1 active: text", get_name()));
		editing_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1 active: edited text", get_name()));
	} else {
		bg_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1: background", get_name()));
		text_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1: text", get_name()));
		editing_color = ARDOUR_UI::config()->color_by_name (string_compose ("%1: edited text", get_name()));
	}

	/* store for bg in render() */

	UINT_TO_RGBA (bg_color, &r, &g, &b, &a);
	r = lrint ((r/256.0) * 65535.0);
	g = lrint ((g/256.0) * 65535.0);
	b = lrint ((b/256.0) * 65535.0);
	bg_r = r/256.0;
	bg_g = g/256.0;
	bg_b = b/256.0;
	bg_a = a/256.0;

	UINT_TO_RGBA (text_color, &r, &g, &b, &a);
	r = lrint ((r/256.0) * 65535.0);
	g = lrint ((g/256.0) * 65535.0);
	b = lrint ((b/256.0) * 65535.0);
	foreground_attr = new Pango::AttrColor (Pango::Attribute::create_attr_foreground (r, g, b));

	UINT_TO_RGBA (editing_color, &r, &g, &b, &a);
	r = lrint ((r/256.0) * 65535.0);
	g = lrint ((g/256.0) * 65535.0);
	b = lrint ((b/256.0) * 65535.0);
	editing_attr = new Pango::AttrColor (Pango::Attribute::create_attr_foreground (r, g, b));
	
	normal_attributes.change (*foreground_attr);
	info_attributes.change (*foreground_attr);
	editing_attributes.change (*foreground_attr);
	editing_attributes.change (*editing_attr);

	if (!editing) {
		_layout->set_attributes (normal_attributes);
	} else {
		_layout->set_attributes (editing_attributes);
	}

	if (_left_layout) {
		_left_layout->set_attributes (info_attributes);
		_right_layout->set_attributes (info_attributes);
	}

	queue_draw ();
}

void
AudioClock::render (cairo_t* cr)
{
	/* main layout: rounded rect, plus the text */
	
	if (_need_bg) {
		cairo_set_source_rgba (cr, bg_r, bg_g, bg_b, bg_a);
		Gtkmm2ext::rounded_rectangle (cr, 0, 0, get_width(), upper_height, 9);
		cairo_fill (cr);
	}

	cairo_move_to (cr, x_leading_padding, (upper_height - layout_height) / 2.0);
	pango_cairo_show_layout (cr, _layout->gobj());

	if (_left_layout) {

		double h = get_height() - upper_height - separator_height;

		if (_need_bg) {
			cairo_set_source_rgba (cr, bg_r, bg_g, bg_b, bg_a);
		}

		if (mode_based_info_ratio != 1.0) {

			double left_rect_width = round (((get_width() - separator_height) * mode_based_info_ratio) + 0.5);

			if (_need_bg) {
				Gtkmm2ext::rounded_rectangle (cr, 0, upper_height + separator_height, left_rect_width, h, 9);
				cairo_fill (cr);
			}

			cairo_move_to (cr, x_leading_padding, upper_height + separator_height + ((h - info_height)/2.0));
			pango_cairo_show_layout (cr, _left_layout->gobj());
			
			if (_need_bg) {
				Gtkmm2ext::rounded_rectangle (cr, left_rect_width + separator_height, upper_height + separator_height, 
							      get_width() - separator_height - left_rect_width, h, 9);
				cairo_fill (cr);	
			}

			cairo_move_to (cr, x_leading_padding + left_rect_width + separator_height, upper_height + separator_height + ((h - info_height)/2.0));
			pango_cairo_show_layout (cr, _right_layout->gobj());

		} else {
			/* no info to display, or just one */

			if (_need_bg) {
				Gtkmm2ext::rounded_rectangle (cr, 0, upper_height + separator_height, get_width(), h, 9);
				cairo_fill (cr);
			}
		}
	}

	if (editing) {
		const double cursor_width = 4.0; /* need em width here, not 16 */

		if (!insert_map.empty()) {
			Pango::Rectangle cursor = _layout->get_cursor_strong_pos (insert_map[input_string.length()]);
			
			cairo_set_source_rgba (cr, 0.9, 0.1, 0.1, 0.8);
			cairo_rectangle (cr, 
					 x_leading_padding + cursor.get_x()/PANGO_SCALE, 
					 (upper_height - layout_height)/2.0, 
					 cursor_width, cursor.get_height()/PANGO_SCALE);
			cairo_fill (cr);	
		} else {
			if (input_string.empty()) {
				cairo_set_source_rgba (cr, 0.9, 0.1, 0.1, 0.8);
				cairo_rectangle (cr, 
						 (get_width()/2.0) - cursor_width,
						 (upper_height - layout_height)/2.0, 
						 cursor_width, upper_height);
				cairo_fill (cr);
			}
		}
	}
}

void
AudioClock::on_size_allocate (Gtk::Allocation& alloc)
{
	CairoWidget::on_size_allocate (alloc);
	
	if (_left_layout) {
		upper_height = (get_height()/2.0) - 1.0;
	} else {
		upper_height = get_height();
	}
}

void
AudioClock::on_size_request (Gtk::Requisition* req)
{
	Glib::RefPtr<Pango::Layout> tmp;
	Glib::RefPtr<Gtk::Style> style = get_style ();
	Pango::FontDescription font; 

	tmp = Pango::Layout::create (get_pango_context());

	if (!is_realized()) {
		font = get_font_for_style (get_name());
	} else {
		font = style->get_font();
	}

	tmp->set_font_description (font);

	/* this string is the longest thing we will ever display,
	   and also includes the BBT "|" that may descends below
	   the baseline a bit, and a comma for the minsecs mode
	   where we printf a fractional value (XXX or should)
	*/

	tmp->set_text (" 88|88:88:88,888"); 

	tmp->get_pixel_size (req->width, req->height);

	layout_height = req->height;
	layout_width = req->width;

	/* now tackle height, for which we need to know the height of the lower
	 * layout
	 */

	if (_left_layout) {

		int w;

		font.set_size ((int) lrint (font.get_size() * info_font_scale_factor));
		font.set_weight (Pango::WEIGHT_NORMAL);
		tmp->set_font_description (font);

		/* we only care about height, so put as much stuff in here
		   as possible that might change the height.
		*/
		tmp->set_text ("qyhH|"); /* one ascender, one descender */
		
		tmp->get_pixel_size (w, info_height);
		
		/* silly extra padding that seems necessary to correct the info
		 * that pango just gave us. I have no idea why.
		 */

		info_height += 4;

		req->height += info_height;
		req->height += separator_height;
	}
}

void
AudioClock::show_edit_status (int length)
{
	editing_attr->set_start_index (edit_string.length() - length);
	editing_attr->set_end_index (edit_string.length());
	
	editing_attributes.change (*foreground_attr);
	editing_attributes.change (*editing_attr);

	_layout->set_attributes (editing_attributes);
}

void
AudioClock::start_edit ()
{
	pre_edit_string = _layout->get_text ();
	if (!insert_map.empty()) {
		edit_string = pre_edit_string;
	} else {
		edit_string.clear ();
		_layout->set_text ("");
	}
	input_string.clear ();
	editing = true;

	queue_draw ();

	Keyboard::magic_widget_grab_focus ();
	grab_focus ();
}

void
AudioClock::end_edit (bool modify)
{
	if (modify) {

		bool ok = true;
		
		switch (_mode) {
		case Timecode:
			ok = timecode_validate_edit (edit_string);
			break;
			
		case BBT:
			ok = bbt_validate_edit (edit_string);
			break;
			
		case MinSec:
			break;
			
		case Frames:
			break;
		}
		
		if (!ok) {
			edit_string = pre_edit_string;
			input_string.clear ();
			_layout->set_text (edit_string);
			show_edit_status (0);
			/* edit attributes remain in use */
		} else {

			editing = false;
			framepos_t pos;

			switch (_mode) {
			case Timecode:
				pos = frames_from_timecode_string (edit_string);
				break;
				
			case BBT:
				if (is_duration) {
					pos = frame_duration_from_bbt_string (0, edit_string);
				} else {
					pos = frames_from_bbt_string (0, edit_string);
				}
				break;
				
			case MinSec:
				pos = frames_from_minsec_string (edit_string);
				break;
				
			case Frames:
				pos = frames_from_audioframes_string (edit_string);
				break;
			}

			set (pos, true);
			_layout->set_attributes (normal_attributes);
			ValueChanged(); /* EMIT_SIGNAL */
		}

	} else {

		editing = false;
		_layout->set_attributes (normal_attributes);
		_layout->set_text (pre_edit_string);
	}

	queue_draw ();

	if (!editing) {
		drop_focus ();
	}
}

void
AudioClock::drop_focus ()
{
	/* move focus back to the default widget in the top level window */
	
	Keyboard::magic_widget_drop_focus ();
	Widget* top = get_toplevel();
	if (top->is_toplevel ()) {
		Window* win = dynamic_cast<Window*> (top);
		win->grab_focus ();
	}
}

framecnt_t 
AudioClock::parse_as_frames_distance (const std::string& str)
{
	framecnt_t f;

	if (sscanf (str.c_str(), "%" PRId64, &f) == 1) {
		return f;
	}

	return 0;
}

framecnt_t 
AudioClock::parse_as_minsec_distance (const std::string& str)
{
	framecnt_t sr = _session->frame_rate();
	int msecs;
	int secs;
	int mins;
	int hrs;

	switch (str.length()) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
	case 4:
		sscanf (str.c_str(), "%" PRId32, &msecs);
		return msecs * (sr / 1000);
		
	case 5:
		sscanf (str.c_str(), "%1" PRId32 "%" PRId32, &secs, &msecs);
		return (secs * sr) + (msecs * (sr/1000));

	case 6:
		sscanf (str.c_str(), "%2" PRId32 "%" PRId32, &secs, &msecs);
		return (secs * sr) + (msecs * (sr/1000));

	case 7:
		sscanf (str.c_str(), "%1" PRId32 "%2" PRId32 "%" PRId32, &mins, &secs, &msecs);
		return (mins * 60 * sr) + (secs * sr) + (msecs * (sr/1000));

	case 8:
		sscanf (str.c_str(), "%2" PRId32 "%2" PRId32 "%" PRId32, &mins, &secs, &msecs);
		return (mins * 60 * sr) + (secs * sr) + (msecs * (sr/1000));

	case 9:
		sscanf (str.c_str(), "%1" PRId32 "%2" PRId32 "%2" PRId32 "%" PRId32, &hrs, &mins, &secs, &msecs);
		return (hrs * 3600 * sr) + (mins * 60 * sr) + (secs * sr) + (msecs * (sr/1000));

	case 10:
		sscanf (str.c_str(), "%1" PRId32 "%2" PRId32 "%2" PRId32 "%" PRId32, &hrs, &mins, &secs, &msecs);
		return (hrs * 3600 * sr) + (mins * 60 * sr) + (secs * sr) + (msecs * (sr/1000));
	
	default:
		break;
	}

	return 0;
}

framecnt_t 
AudioClock::parse_as_timecode_distance (const std::string& str)
{
	double fps = _session->timecode_frames_per_second();
	framecnt_t sr = _session->frame_rate();
	int frames;
	int secs;
	int mins;
	int hrs;
	
	switch (str.length()) {
	case 0:
		return 0;
	case 1:
	case 2:
		sscanf (str.c_str(), "%" PRId32, &frames);
		return lrint ((frames/(float)fps) * sr);

	case 3:
		sscanf (str.c_str(), "%1" PRId32 "%" PRId32, &secs, &frames);
		return (secs * sr) + lrint ((frames/(float)fps) * sr);

	case 4:
		sscanf (str.c_str(), "%2" PRId32 "%" PRId32, &secs, &frames);
		return (secs * sr) + lrint ((frames/(float)fps) * sr);
		
	case 5:
		sscanf (str.c_str(), "%1" PRId32 "%2" PRId32 "%" PRId32, &mins, &secs, &frames);
		return (mins * 60 * sr) + (secs * sr) + lrint ((frames/(float)fps) * sr);

	case 6:
		sscanf (str.c_str(), "%2" PRId32 "%2" PRId32 "%" PRId32, &mins, &secs, &frames);
		return (mins * 60 * sr) + (secs * sr) + lrint ((frames/(float)fps) * sr);

	case 7:
		sscanf (str.c_str(), "%1" PRId32 "%2" PRId32 "%2" PRId32 "%" PRId32, &hrs, &mins, &secs, &frames);
		return (hrs * 3600 * sr) + (mins * 60 * sr) + (secs * sr) + lrint ((frames/(float)fps) * sr);

	case 8:
		sscanf (str.c_str(), "%2" PRId32 "%2" PRId32 "%2" PRId32 "%" PRId32, &hrs, &mins, &secs, &frames);
		return (hrs * 3600 * sr) + (mins * 60 * sr) + (secs * sr) + lrint ((frames/(float)fps) * sr);
	
	default:
		break;
	}

	return 0;
}

framecnt_t 
AudioClock::parse_as_bbt_distance (const std::string& str)
{
	return 0;
}

framecnt_t 
AudioClock::parse_as_distance (const std::string& instr)
{
	string str = instr;

	/* the input string is in reverse order */
	
	std::reverse (str.begin(), str.end());

	switch (_mode) {
	case Timecode:
		return parse_as_timecode_distance (str);
		break;
	case Frames:
		return parse_as_frames_distance (str);
		break;
	case BBT:
		return parse_as_bbt_distance (str);
		break;
	case MinSec:
		return parse_as_minsec_distance (str);
		break;
	}
	return 0;
}

void
AudioClock::end_edit_relative (bool add)
{
	framecnt_t frames = parse_as_distance (input_string);

	editing = false;

	editing = false;
	_layout->set_attributes (normal_attributes);

	if (frames != 0) {
		if (add) {
			set (current_time() + frames, true);
		} else {
			framepos_t c = current_time();

			if (c > frames) {
				set (c - frames, true);
			} else {
				set (0, true);
			}
		}
		ValueChanged (); /* EMIT SIGNAL */
	}

	input_string.clear ();
	queue_draw ();
	drop_focus ();
}

void
AudioClock::session_configuration_changed (std::string p)
{
	if (p == "sync-source" || p == "external-sync") {
		set (current_time(), true);
		return;
	}

	if (p != "timecode-offset" && p != "timecode-offset-negative") {
		return;
	}

	framecnt_t current;

	switch (_mode) {
	case Timecode:
		if (is_duration) {
			current = current_duration ();
		} else {
			current = current_time ();
		}
		set (current, true);
		break;
	default:
		break;
	}
}

void
AudioClock::set (framepos_t when, bool force, framecnt_t offset)
{
 	if ((!force && !is_visible()) || _session == 0) {
		return;
	}

	if (is_duration) {
		when = when - offset;
	} 

	if (when == last_when && !force) {
		return;
	}

	if (!editing) {

		switch (_mode) {
		case Timecode:
			set_timecode (when, force);
			break;
			
		case BBT:
			set_bbt (when, force);
			break;
			
		case MinSec:
			set_minsec (when, force);
			break;
			
		case Frames:
			set_frames (when, force);
			break;
		}
	}

	queue_draw ();
	last_when = when;
}

void
AudioClock::set_frames (framepos_t when, bool /*force*/)
{
	char buf[32];
	bool negative = false;

	if (_off) {
		_layout->set_text ("\u2012\u2012\u2012\u2012\u2012\u2012\u2012\u2012\u2012\u2012");

		if (_left_layout) {
			_left_layout->set_text ("");
			_right_layout->set_text ("");
		}
		
		return;
	}
	
	if (when < 0) {
		when = -when;
		negative = true;
	}

	if (negative) {
		snprintf (buf, sizeof (buf), "-%10" PRId64, when);
	} else {
		snprintf (buf, sizeof (buf), " %10" PRId64, when);
	}

	_layout->set_text (buf);

	if (_left_layout) {
		framecnt_t rate = _session->frame_rate();

		if (fmod (rate, 100.0) == 0.0) {
			sprintf (buf, "SR %.1fkHz", rate/1000.0);
		} else {
			sprintf (buf, "SR %" PRId64, rate);
		}

		_left_layout->set_text (buf);

		float vid_pullup = _session->config.get_video_pullup();

		if (vid_pullup == 0.0) {
			_right_layout->set_text (_("pullup: \u2012"));
		} else {
			sprintf (buf, _("pullup %-6.4f"), vid_pullup);
			_right_layout->set_text (buf);
		}
	}
}

void
AudioClock::set_minsec (framepos_t when, bool force)
{
	char buf[32];
	framecnt_t left;
	int hrs;
	int mins;
	int secs;
	int millisecs;
	bool negative = false;

	if (_off) {
		_layout->set_text ("\u2012\u2012:\u2012\u2012:\u2012\u2012.\u2012\u2012\u2012");

		if (_left_layout) {
			_left_layout->set_text ("");
			_right_layout->set_text ("");
		}
		
		return;
	}	

	if (when < 0) {
		when = -when;
		negative = true;
	}

	left = when;
	hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
	left -= (framecnt_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
	mins = (int) floor (left / (_session->frame_rate() * 60.0f));
	left -= (framecnt_t) floor (mins * _session->frame_rate() * 60.0f);
	secs = (int) floor (left / (float) _session->frame_rate());
	left -= (framecnt_t) floor (secs * _session->frame_rate());
	millisecs = floor (left * 1000.0 / (float) _session->frame_rate());
	
	if (negative) {
		snprintf (buf, sizeof (buf), "-%02" PRId32 ":%02" PRId32 ":%02" PRId32 ".%03" PRId32, hrs, mins, secs, millisecs);
	} else {
		snprintf (buf, sizeof (buf), " %02" PRId32 ":%02" PRId32 ":%02" PRId32 ".%03" PRId32, hrs, mins, secs, millisecs);
	}

	_layout->set_text (buf);
}

void
AudioClock::set_timecode (framepos_t when, bool force)
{
	char buf[32];
	Timecode::Time TC;
	bool negative = false;

	if (_off) {
		_layout->set_text ("\u2012\u2012:\u2012\u2012:\u2012\u2012:\u2012\u2012");
		if (_left_layout) {
			_left_layout->set_text ("");
			_right_layout->set_text ("");
		}
		
		return;
	}

	if (when < 0) {
		when = -when;
		negative = true;
	}

	if (is_duration) {
		_session->timecode_duration (when, TC);
	} else {
		_session->timecode_time (when, TC);
	}
	
	if (TC.negative || negative) {
		snprintf (buf, sizeof (buf), "-%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32, TC.hours, TC.minutes, TC.seconds, TC.frames);
	} else {
		snprintf (buf, sizeof (buf), " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32, TC.hours, TC.minutes, TC.seconds, TC.frames);
	}

	_layout->set_text (buf);

	if (_left_layout) {

		if (_session->config.get_external_sync()) {
			switch (_session->config.get_sync_source()) {
			case JACK:
				_left_layout->set_text ("JACK");
				break;
			case MTC:
				_left_layout->set_text ("MTC");
				break;
			case MIDIClock:
				_left_layout->set_text ("M-Clock");
				break;
			}
		} else {
			_left_layout->set_text ("INT");
		}

		double timecode_frames = _session->timecode_frames_per_second();
	
		if (fmod(timecode_frames, 1.0) == 0.0) {
			sprintf (buf, "FPS %u %s", int (timecode_frames), (_session->timecode_drop_frames() ? "D" : ""));
		} else {
			sprintf (buf, "%.2f %s", timecode_frames, (_session->timecode_drop_frames() ? "D" : ""));
		}

		_right_layout->set_text (buf);
	}
}

void
AudioClock::set_bbt (framepos_t when, bool force)
{
	char buf[16];
	Timecode::BBT_Time BBT;
	bool negative = false;

	if (_off) {
		_layout->set_text ("\u2012\u2012\u2012|\u2012\u2012|\u2012\u2012\u2012\u2012");
		if (_left_layout) {
			_left_layout->set_text ("");
			_right_layout->set_text ("");
		}
		return;
	}

	if (when < 0) {
		when = -when;
		negative = true;
	}

	/* handle a common case */
	if (is_duration) {
		if (when == 0) {
			BBT.bars = 0;
			BBT.beats = 0;
			BBT.ticks = 0;
		} else {
			_session->tempo_map().bbt_time (when, BBT);
			BBT.bars--;
			BBT.beats--;
		}
	} else {
		_session->tempo_map().bbt_time (when, BBT);
	}

	if (negative) {
		snprintf (buf, sizeof (buf), "-%03" PRIu32 "|%02" PRIu32 "|%04" PRIu32, BBT.bars, BBT.beats, BBT.ticks);
	} else {
		snprintf (buf, sizeof (buf), " %03" PRIu32 "|%02" PRIu32 "|%04" PRIu32, BBT.bars, BBT.beats, BBT.ticks);
	}

	_layout->set_text (buf);
		 
	if (_right_layout) {
		framepos_t pos;

		if (bbt_reference_time < 0) {
			pos = when;
		} else {
			pos = bbt_reference_time;
		}

		TempoMetric m (_session->tempo_map().metric_at (pos));

		sprintf (buf, "%-5.2f", m.tempo().beats_per_minute());
		_left_layout->set_text (buf);

		sprintf (buf, "%g|%g", m.meter().beats_per_bar(), m.meter().note_divisor());
		_right_layout->set_text (buf);
	}
}

void
AudioClock::set_session (Session *s)
{
	SessionHandlePtr::set_session (s);

	if (_session) {

		_session->config.ParameterChanged.connect (_session_connections, invalidator (*this), boost::bind (&AudioClock::session_configuration_changed, this, _1), gui_context());

		const XMLProperty* prop;
		XMLNode* node = _session->extra_xml (X_("ClockModes"));
		AudioClock::Mode amode;

		if (node) {
			for (XMLNodeList::const_iterator i = node->children().begin(); i != node->children().end(); ++i) {
				if ((prop = (*i)->property (X_("name"))) && prop->value() == _name) {

					if ((prop = (*i)->property (X_("mode"))) != 0) {
						amode = AudioClock::Mode (string_2_enum (prop->value(), amode));
						set_mode (amode);
					}
					if ((prop = (*i)->property (X_("on"))) != 0) {
						set_off (!string_is_affirmative (prop->value()));
					}
					break;
				}
			}
		}

		set (last_when, true);
	}
}

bool
AudioClock::on_key_press_event (GdkEventKey* ev)
{
	if (!editing) {
		return false;
	}
	
	/* return true for keys that we MIGHT use 
	   at release
	*/
	switch (ev->keyval) {
	case GDK_0:
	case GDK_KP_0:
	case GDK_1:
	case GDK_KP_1:
	case GDK_2:
	case GDK_KP_2:
	case GDK_3:
	case GDK_KP_3:
	case GDK_4:
	case GDK_KP_4:
	case GDK_5:
	case GDK_KP_5:
	case GDK_6:
	case GDK_KP_6:
	case GDK_7:
	case GDK_KP_7:
	case GDK_8:
	case GDK_KP_8:
	case GDK_9:
	case GDK_KP_9:
	case GDK_period:
	case GDK_comma:
	case GDK_KP_Decimal:
	case GDK_Tab:
	case GDK_Return:
	case GDK_KP_Enter:
	case GDK_Escape:
		return true;
	default:
		return false;
	}
}

bool
AudioClock::on_key_release_event (GdkEventKey *ev)
{
	if (!editing) {
		return false;
	}

	string new_text;
	char new_char = 0;

	switch (ev->keyval) {
	case GDK_0:
	case GDK_KP_0:
		new_char = '0';
		break;
	case GDK_1:
	case GDK_KP_1:
		new_char = '1';
		break;
	case GDK_2:
	case GDK_KP_2:
		new_char = '2';
		break;
	case GDK_3:
	case GDK_KP_3:
		new_char = '3';
		break;
	case GDK_4:
	case GDK_KP_4:
		new_char = '4';
		break;
	case GDK_5:
	case GDK_KP_5:
		new_char = '5';
		break;
	case GDK_6:
	case GDK_KP_6:
		new_char = '6';
		break;
	case GDK_7:
	case GDK_KP_7:
		new_char = '7';
		break;
	case GDK_8:
	case GDK_KP_8:
		new_char = '8';
		break;
	case GDK_9:
	case GDK_KP_9:
		new_char = '9';
		break;

	case GDK_minus:
	case GDK_KP_Subtract:
		end_edit_relative (false);
		return true;
		break;

	case GDK_plus:
	case GDK_KP_Add:
		end_edit_relative (true);
		return true;
		break;

	case GDK_Tab:
	case GDK_Return:
	case GDK_KP_Enter:
		end_edit (true);
		return true;
		break;

	case GDK_Escape:
		end_edit (false);
		ChangeAborted();  /*  EMIT SIGNAL  */
		return true;

	default:
		return false;
	}

	if (!insert_map.empty() && (input_string.length() >= insert_map.size())) {
		/* eat the key event, but do no nothing with it */
		return true;
	}

	input_string.insert (input_string.begin(), new_char);
	
	string::reverse_iterator ri;
	vector<int> insert_at;
	int highlight_length;
	char buf[32];
	framepos_t pos;

	/* merge with pre-edit-string into edit string */
	
	switch (_mode) {
	case Frames:
		/* get this one in the right order, and to the right width */
		edit_string.push_back (new_char);
		sscanf (edit_string.c_str(), "%" PRId64, &pos);
		snprintf (buf, sizeof (buf), " %10" PRId64, pos);
		edit_string = buf;
		highlight_length = edit_string.length();
		break;
		
	default:
		edit_string = pre_edit_string;
		
		/* backup through the original string, till we have
		 * enough digits locations to put all the digits from
		 * the input string.
		 */
		
		for (ri = edit_string.rbegin(); ri != edit_string.rend(); ++ri) {
			if (isdigit (*ri)) {
				insert_at.push_back (edit_string.length() - (ri - edit_string.rbegin()) - 1);
				if (insert_at.size() == input_string.length()) {
					break;
				}
			}
		}
		
		if (insert_at.size() != input_string.length()) {
			error << "something went wrong " << endmsg;
		} else {
			for (int i = input_string.length() - 1; i >= 0; --i) {
				edit_string[insert_at[i]] = input_string[i];
			}
			
			highlight_length = edit_string.length() - insert_at.back();
		}
		
		break;
	}
	
	if (edit_string != _layout->get_text()) {
		show_edit_status (highlight_length);
		_layout->set_text (edit_string);
		queue_draw ();
	} 

	return true;
}

AudioClock::Field
AudioClock::index_to_field (int index) const
{
	switch (_mode) {
	case Timecode:
		if (index < 4) {
			return Timecode_Hours;
		} else if (index < 7) {
			return Timecode_Minutes;
		} else if (index < 10) {
			return Timecode_Seconds;
		} else if (index < 13) {
			return Timecode_Frames;
		}
		break;
	case BBT:
		if (index < 5) {
			return Bars;
		} else if (index < 7) {
			return Beats;
		} else if (index < 12) {
			return Ticks;
		}
		break;
	case MinSec:
		if (index < 3) {
			return Timecode_Hours;
		} else if (index < 6) {
			return MS_Minutes;
		} else if (index < 9) {
			return MS_Seconds;
		} else if (index < 12) {
			return MS_Milliseconds;
		}
		break;
	case Frames:
		return AudioFrames;
		break;
	}

	return Field (0);
}

bool
AudioClock::on_button_press_event (GdkEventButton *ev)
{
	switch (ev->button) {
	case 1:
		if (editable && !_off) {
			dragging = true;
			/* make absolutely sure that the pointer is grabbed */
			gdk_pointer_grab(ev->window,false ,
					 GdkEventMask( Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_PRESS_MASK |Gdk::BUTTON_RELEASE_MASK),
					 NULL,NULL,ev->time);
			drag_accum = 0;
			drag_start_y = ev->y;
			drag_y = ev->y;
			
			int index;
			int trailing;
			int y;
			int x;

			/* the text has been centered vertically, so adjust
			 * x and y. 
			 */

			y = ev->y - ((upper_height - layout_height)/2);
			x = ev->x - x_leading_padding;
			
			if (_layout->xy_to_index (x * PANGO_SCALE, y * PANGO_SCALE, index, trailing)) {			
				drag_field = index_to_field (index);
			} else {
				drag_field = Field (0);
			}
		}
		break;
		
	default:
		return false;
		break;
	}

	return true;
}

bool
AudioClock::on_button_release_event (GdkEventButton *ev)
{
	if (editable && !_off) {
		if (dragging) {
			gdk_pointer_ungrab (GDK_CURRENT_TIME);
			dragging = false;
			if (ev->y > drag_start_y+1 || ev->y < drag_start_y-1 || Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)){
				// we actually dragged so return without
				// setting editing focus, or we shift clicked
				return true;
			} else {
				if (ev->button == 1) {
					start_edit ();
				}
			}

		}
	}

	if (Keyboard::is_context_menu_event (ev)) {
		if (ops_menu == 0) {
			build_ops_menu ();
		}
		ops_menu->popup (1, ev->time);
		return true;
	}

	return false;
}

bool
AudioClock::on_focus_out_event (GdkEventFocus* ev)
{
	bool ret = CairoWidget::on_focus_out_event (ev);

	if (editing) {
		end_edit (false);
	}

	return ret;
}

bool
AudioClock::on_scroll_event (GdkEventScroll *ev)
{
	int index;
	int trailing;

	if (editing || _session == 0 || !editable || _off) {
		return false;
	}

	int y;
	int x;
	
	/* the text has been centered vertically, so adjust
	 * x and y. 
	 */

	y = ev->y - ((upper_height - layout_height)/2);
	x = ev->x - x_leading_padding;

	if (!_layout->xy_to_index (x * PANGO_SCALE, y * PANGO_SCALE, index, trailing)) {
		/* not in the main layout */
		return false;
	}
	
	Field f = index_to_field (index);
	framepos_t frames = 0;

	switch (ev->direction) {

	case GDK_SCROLL_UP:
		frames = get_frame_step (f);
		if (frames != 0) {
			if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
				frames *= 10;
			}
			set (current_time() + frames, true);
			ValueChanged (); /* EMIT_SIGNAL */
		}
		break;
		
	case GDK_SCROLL_DOWN:
		frames = get_frame_step (f);
		if (frames != 0) {
			if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
				frames *= 10;
			}
			
			if ((double)current_time() - (double)frames < 0.0) {
				set (0, true);
			} else {
				set (current_time() - frames, true);
			}
			
			ValueChanged (); /* EMIT_SIGNAL */
		}
		break;
		
	default:
		return false;
		break;
	}
	
	return true;
}

bool
AudioClock::on_motion_notify_event (GdkEventMotion *ev)
{
	if (editing || _session == 0 || !dragging) {
		return false;
	}

	float pixel_frame_scale_factor = 0.2f;

	if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier))  {
		pixel_frame_scale_factor = 0.1f;
	}


	if (Keyboard::modifier_state_contains (ev->state,
					       Keyboard::PrimaryModifier|Keyboard::SecondaryModifier)) {

		pixel_frame_scale_factor = 0.025f;
	}

	double y_delta = ev->y - drag_y;

	drag_accum +=  y_delta*pixel_frame_scale_factor;

	drag_y = ev->y;

	if (trunc (drag_accum) != 0) {

		framepos_t frames;
		framepos_t pos;
		int dir;
		dir = (drag_accum < 0 ? 1:-1);
		pos = current_time();
		frames = get_frame_step (drag_field,pos,dir);

		if (frames  != 0 &&  frames * drag_accum < current_time()) {
			set ((framepos_t) floor (pos - drag_accum * frames), false); // minus because up is negative in GTK
		} else {
			set (0 , false);
 		}

	       	drag_accum= 0;
		ValueChanged();	 /* EMIT_SIGNAL */
	}

	return true;
}

framepos_t
AudioClock::get_frame_step (Field field, framepos_t pos, int dir)
{
	framecnt_t f = 0;
	Timecode::BBT_Time BBT;
	switch (field) {
	case Timecode_Hours:
		f = (framecnt_t) floor (3600.0 * _session->frame_rate());
		break;
	case Timecode_Minutes:
		f = (framecnt_t) floor (60.0 * _session->frame_rate());
		break;
	case Timecode_Seconds:
		f = _session->frame_rate();
		break;
	case Timecode_Frames:
		f = (framecnt_t) floor (_session->frame_rate() / _session->timecode_frames_per_second());
		break;

	case AudioFrames:
		f = 1;
		break;

	case MS_Hours:
		f = (framecnt_t) floor (3600.0 * _session->frame_rate());
		break;
	case MS_Minutes:
		f = (framecnt_t) floor (60.0 * _session->frame_rate());
		break;
	case MS_Seconds:
		f = (framecnt_t) _session->frame_rate();
		break;
	case MS_Milliseconds:
		f = (framecnt_t) floor (_session->frame_rate() / 1000.0);
		break;

	case Bars:
		BBT.bars = 1;
		BBT.beats = 0;
		BBT.ticks = 0;
		f = _session->tempo_map().bbt_duration_at (pos,BBT,dir);
		break;
	case Beats:
		BBT.bars = 0;
		BBT.beats = 1;
		BBT.ticks = 0;
		f = _session->tempo_map().bbt_duration_at(pos,BBT,dir);
		break;
	case Ticks:
		BBT.bars = 0;
		BBT.beats = 0;
		BBT.ticks = 1;
		f = _session->tempo_map().bbt_duration_at(pos,BBT,dir);
		break;
	default:
		error << string_compose (_("programming error: %1"), "attempt to get frames from non-text field!") << endmsg;
		f = 0;
		break;
	}

	return f;
}

framepos_t
AudioClock::current_time (framepos_t pos) const
{
	return last_when;
}

framepos_t
AudioClock::current_duration (framepos_t pos) const
{
	framepos_t ret = 0;

	switch (_mode) {
	case Timecode:
		ret = last_when;
		break;
	case BBT:
		ret = frame_duration_from_bbt_string (pos, _layout->get_text());
		break;

	case MinSec:
		ret = last_when;
		break;

	case Frames:
		ret = last_when;
		break;
	}

	return ret;
}

bool
AudioClock::bbt_validate_edit (const string& str)
{
	AnyTime any;

	sscanf (str.c_str(), "%" PRIu32 "|%" PRIu32 "|%" PRIu32, &any.bbt.bars, &any.bbt.beats, &any.bbt.ticks);
	
	if (!is_duration && any.bbt.bars == 0) {
		return false;
	}

	if (!is_duration && any.bbt.beats == 0) {
		return false;
	}

	return true;
}

bool
AudioClock::timecode_validate_edit (const string& str)
{
	Timecode::Time TC;

	if (sscanf (_layout->get_text().c_str(), "%" PRId32 ":%" PRId32 ":%" PRId32 ":%" PRId32, 
		    &TC.hours, &TC.minutes, &TC.seconds, &TC.frames) != 4) {
		return false;
	}

	if (TC.minutes > 59 || TC.seconds > 59) {
		return false;
	}

	if (TC.frames > (long)rint(_session->timecode_frames_per_second()) - 1) {
		return false;
	}

	if (_session->timecode_drop_frames()) {
		if (TC.minutes % 10 && TC.seconds == 0 && TC.frames < 2) {
			return false;
		}
	}

	return true;
}

framepos_t
AudioClock::frames_from_timecode_string (const string& str) const
{
	if (_session == 0) {
		return 0;
	}

	Timecode::Time TC;
	framepos_t sample;

	if (sscanf (str.c_str(), "%d:%d:%d:%d", &TC.hours, &TC.minutes, &TC.seconds, &TC.frames) != 4) {
		error << string_compose (_("programming error: %1 %2"), "badly formatted timecode clock string", str) << endmsg;
		return 0;
	}

	TC.rate = _session->timecode_frames_per_second();
	TC.drop= _session->timecode_drop_frames();

	_session->timecode_to_sample (TC, sample, false /* use_offset */, false /* use_subframes */ );
	
	// timecode_tester ();

	return sample;
}

framepos_t
AudioClock::frames_from_minsec_string (const string& str) const
{
	if (_session == 0) {
		return 0;
	}

	int hrs, mins, secs, millisecs;
	framecnt_t sr = _session->frame_rate();

	if (sscanf (str.c_str(), "%d:%d:%d.%d", &hrs, &mins, &secs, &millisecs) != 4) {
		error << string_compose (_("programming error: %1 %2"), "badly formatted minsec clock string", str) << endmsg;
		return 0;
	}

	return (framepos_t) floor ((hrs * 60.0f * 60.0f * sr) + (mins * 60.0f * sr) + (secs * sr) + (millisecs * sr / 1000.0));
}

framepos_t
AudioClock::frames_from_bbt_string (framepos_t pos, const string& str) const
{
	if (_session == 0) {
		error << "AudioClock::current_time() called with BBT mode but without session!" << endmsg;
		return 0;
	}

	AnyTime any;
	any.type = AnyTime::BBT;

	sscanf (str.c_str(), "%" PRId32 "|%" PRId32 "|%" PRId32, &any.bbt.bars, &any.bbt.beats, &any.bbt.ticks);
	
	if (is_duration) {
		any.bbt.bars++;
		any.bbt.beats++;
                return _session->any_duration_to_frames (pos, any);
	} else {
                return _session->convert_to_frames (any);
        }
}


framepos_t
AudioClock::frame_duration_from_bbt_string (framepos_t pos, const string& str) const
{
	if (_session == 0) {
		error << "AudioClock::current_time() called with BBT mode but without session!" << endmsg;
		return 0;
	}

	Timecode::BBT_Time bbt;

	sscanf (str.c_str(), "%" PRIu32 "|%" PRIu32 "|%" PRIu32, &bbt.bars, &bbt.beats, &bbt.ticks);

	return _session->tempo_map().bbt_duration_at(pos,bbt,1);
}

framepos_t
AudioClock::frames_from_audioframes_string (const string& str) const
{
	framepos_t f;
	sscanf (str.c_str(), "%" PRId64, &f);
	return f;
}

void
AudioClock::build_ops_menu ()
{
	using namespace Menu_Helpers;
	ops_menu = new Menu;
	MenuList& ops_items = ops_menu->items();
	ops_menu->set_name ("ArdourContextMenu");

	if (!Profile->get_sae()) {
		ops_items.push_back (MenuElem (_("Timecode"), sigc::bind (sigc::mem_fun(*this, &AudioClock::set_mode), Timecode)));
	}
	ops_items.push_back (MenuElem (_("Bars:Beats"), sigc::bind (sigc::mem_fun(*this, &AudioClock::set_mode), BBT)));
	ops_items.push_back (MenuElem (_("Minutes:Seconds"), sigc::bind (sigc::mem_fun(*this, &AudioClock::set_mode), MinSec)));
	ops_items.push_back (MenuElem (_("Samples"), sigc::bind (sigc::mem_fun(*this, &AudioClock::set_mode), Frames)));

	if (editable && !_off && !is_duration && !_follows_playhead) {
		ops_items.push_back (SeparatorElem());
		ops_items.push_back (MenuElem (_("Set From Playhead"), sigc::mem_fun(*this, &AudioClock::set_from_playhead)));
		ops_items.push_back (MenuElem (_("Locate to This Time"), sigc::mem_fun(*this, &AudioClock::locate)));
	}
}

void
AudioClock::set_from_playhead ()
{
	if (!_session) {
		return;
	}

	set (_session->transport_frame());
	ValueChanged ();
}

void
AudioClock::locate ()
{
	if (!_session || is_duration) {
		return;
	}

	_session->request_locate (current_time(), _session->transport_rolling ());
}

void
AudioClock::set_mode (Mode m)
{
	if (_mode == m) {
		return;
	}

	_mode = m;

	insert_map.clear();

	_layout->set_text ("");

	if (_left_layout) {
		_left_layout->set_text ("");
		_right_layout->set_text ("");
	}

	switch (_mode) {
	case Timecode:
		mode_based_info_ratio = 0.5;
		insert_map.push_back (11);
		insert_map.push_back (10);
		insert_map.push_back (8);
		insert_map.push_back (7);
		insert_map.push_back (5);
		insert_map.push_back (4);
		insert_map.push_back (2);
		insert_map.push_back (1);
		break;
		
	case BBT:
		mode_based_info_ratio = 0.5;
		insert_map.push_back (11);
		insert_map.push_back (10);
		insert_map.push_back (9);
		insert_map.push_back (8);
		insert_map.push_back (6);
		insert_map.push_back (5);	
		insert_map.push_back (3);	
		insert_map.push_back (2);	
		insert_map.push_back (1);	
		break;
		
	case MinSec:
		mode_based_info_ratio = 1.0;
		insert_map.push_back (12);
		insert_map.push_back (11);
		insert_map.push_back (10);
		insert_map.push_back (8);
		insert_map.push_back (7);
		insert_map.push_back (5);
		insert_map.push_back (4);
		insert_map.push_back (2);	
		insert_map.push_back (1);	
		break;
		
	case Frames:
		mode_based_info_ratio = 0.5;
		break;
	}

	set (last_when, true);

        if (!is_transient) {
                ModeChanged (); /* EMIT SIGNAL (the static one)*/
        }

        mode_changed (); /* EMIT SIGNAL (the member one) */
}

void
AudioClock::set_bbt_reference (framepos_t pos)
{
	bbt_reference_time = pos;
}

void
AudioClock::on_style_changed (const Glib::RefPtr<Gtk::Style>& old_style)
{
	CairoWidget::on_style_changed (old_style);
	set_font ();
	set_colors ();
}

void
AudioClock::set_editable (bool yn)
{
	editable = yn;
}

void
AudioClock::set_is_duration (bool yn)
{
	if (yn == is_duration) {
		return;
	}

	is_duration = yn;
	set (last_when, true);
}

void
AudioClock::set_off (bool yn) 
{
	if (_off == yn) {
		return;
	}

	_off = yn;

	/* force a redraw. last_when will be preserved, but the clock text will
	 * change 
	 */
	
	set (last_when, true);
}

void
AudioClock::focus ()
{
	start_edit ();
}


