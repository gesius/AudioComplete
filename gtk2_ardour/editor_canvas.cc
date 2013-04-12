/*
    Copyright (C) 2005 Paul Davis

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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <jack/types.h>

#include "gtkmm2ext/utils.h"

#include "ardour/profile.h"
#include "ardour/rc_configuration.h"
#include "ardour/smf_source.h"

#include "canvas/canvas.h"
#include "canvas/rectangle.h"
#include "canvas/pixbuf.h"
#include "canvas/text.h"

#include "ardour_ui.h"
#include "editor.h"
#include "global_signals.h"
#include "editing.h"
#include "rgb_macros.h"
#include "utils.h"
#include "audio_time_axis.h"
#include "editor_drag.h"
#include "region_view.h"
#include "editor_group_tabs.h"
#include "editor_summary.h"
#include "video_timeline.h"
#include "keyboard.h"
#include "editor_cursors.h"
#include "mouse_cursors.h"
#include "verbose_cursor.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Glib;
using namespace Gtkmm2ext;
using namespace Editing;

/* XXX this is a hack. it ought to be the maximum value of an framepos_t */

const double max_canvas_coordinate = (double) JACK_MAX_FRAMES;

void
Editor::initialize_canvas ()
{
	/* XXX */

	_track_canvas_hadj = new Adjustment (0, 0, 1e16);
	_track_canvas_vadj = new Adjustment (0, 0, 1e16);
	_track_canvas_viewport = new ArdourCanvas::GtkCanvasViewport (*_track_canvas_hadj, *_track_canvas_vadj);
	_track_canvas = _track_canvas_viewport->canvas ();

	_time_bars_canvas_hadj = new Adjustment (0, 0, 1e16);
	_time_bars_canvas_vadj = new Adjustment (0, 0, 1e16);
	_time_bars_canvas_viewport = new ArdourCanvas::GtkCanvasViewport (*_time_bars_canvas_hadj, *_time_bars_canvas_vadj);
	_time_bars_canvas = _time_bars_canvas_viewport->canvas ();
	
	_verbose_cursor = new VerboseCursor (this);

	/* on the bottom, an image */

	if (Profile->get_sae()) {
		Image img (::get_icon (X_("saelogo")));
		// logo_item = new ArdourCanvas::Pixbuf (_track_canvas->root(), 0.0, 0.0, img.get_pixbuf());
		// logo_item->property_height_in_pixels() = true;
		// logo_item->property_width_in_pixels() = true;
		// logo_item->property_height_set() = true;
		// logo_item->property_width_set() = true;
		logo_item->show ();
	}

	/* a group to hold time (measure) lines */
	time_line_group = new ArdourCanvas::Group (_track_canvas->root());

        transport_loop_range_rect = new ArdourCanvas::Rectangle (time_line_group, ArdourCanvas::Rect (0.0, 0.0, 0.0, ArdourCanvas::COORD_MAX));
	transport_loop_range_rect->set_outline_width (1);
	transport_loop_range_rect->hide();

	transport_punch_range_rect = new ArdourCanvas::Rectangle (time_line_group, ArdourCanvas::Rect (0.0, 0.0, 0.0, ArdourCanvas::COORD_MAX));
	transport_punch_range_rect->set_outline_width (0);
	transport_punch_range_rect->hide();


	_background_group = new ArdourCanvas::Group (_track_canvas->root());
	_master_group = new ArdourCanvas::Group (_track_canvas->root());

	_trackview_group = new ArdourCanvas::Group (_master_group);
	_region_motion_group = new ArdourCanvas::Group (_trackview_group);

	meter_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	meter_bar = new ArdourCanvas::Rectangle (meter_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	meter_bar->set_outline_width (1);
	meter_bar->set_outline_what (0x8);

	tempo_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	tempo_bar = new ArdourCanvas::Rectangle (tempo_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	tempo_bar->set_outline_width (1);
	tempo_bar->set_outline_what (0x8);

	range_marker_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	range_marker_bar = new ArdourCanvas::Rectangle (range_marker_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	range_marker_bar->set_outline_width (1);
	range_marker_bar->set_outline_what (0x8);

	transport_marker_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	transport_marker_bar = new ArdourCanvas::Rectangle (transport_marker_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	transport_marker_bar->set_outline_width (1);
	transport_marker_bar->set_outline_what (0x8);

	marker_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	marker_bar = new ArdourCanvas::Rectangle (marker_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	marker_bar->set_outline_width (1);
	marker_bar->set_outline_what (0x8);

	cd_marker_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	cd_marker_bar = new ArdourCanvas::Rectangle (cd_marker_bar_group, ArdourCanvas::Rect (0.0, 0.0, ArdourCanvas::COORD_MAX, timebar_height - 1));
	cd_marker_bar->set_outline_width (1);
 	cd_marker_bar->set_outline_what (0x8);

	_time_markers_group = new ArdourCanvas::Group (_time_bars_canvas->root());

	meter_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, timebar_height * 5.0));
	tempo_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, timebar_height * 4.0));
	range_marker_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, timebar_height * 3.0));
	transport_marker_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, timebar_height * 2.0));
	marker_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, timebar_height));
	cd_marker_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple (0.0, 0.0));
	videotl_group = new ArdourCanvas::Group (_time_markers_group, ArdourCanvas::Duple(0.0, 0.0));


	videotl_bar_group = new ArdourCanvas::Group (_time_bars_canvas->root ());
	videotl_bar = new ArdourCanvas::Rectangle (videotl_bar_group, ArdourCanvas::Rect (0.0, 0.0, 100,(timebar_height * videotl_bar_height)));
	ARDOUR_UI::instance()->video_timeline = new VideoTimeLine(this, videotl_bar_group, (timebar_height * videotl_bar_height));

	cd_marker_bar_drag_rect = new ArdourCanvas::Rectangle (cd_marker_group, ArdourCanvas::Rect (0.0, 0.0, 100, timebar_height));
	cd_marker_bar_drag_rect->set_outline (false);
	cd_marker_bar_drag_rect->hide ();

	range_bar_drag_rect = new ArdourCanvas::Rectangle (range_marker_group, ArdourCanvas::Rect (0.0, 0.0, 100, timebar_height));
	range_bar_drag_rect->set_outline (false);
	range_bar_drag_rect->hide ();

	transport_bar_drag_rect = new ArdourCanvas::Rectangle (transport_marker_group, ArdourCanvas::Rect (0.0, 0.0, 100, timebar_height));
	transport_bar_drag_rect->set_outline (false);
	transport_bar_drag_rect->hide ();

	transport_punchin_line = new ArdourCanvas::Line (_master_group);
	transport_punchin_line->set_x0 (0);
	transport_punchin_line->set_y0 (0);
	transport_punchin_line->set_x1 (0);
	transport_punchin_line->set_y1 (ArdourCanvas::COORD_MAX);
	transport_punchin_line->hide ();

	transport_punchout_line  = new ArdourCanvas::Line (_master_group);
	transport_punchout_line->set_x0 (0);
	transport_punchout_line->set_y0 (0);
	transport_punchout_line->set_x1 (0);
	transport_punchout_line->set_y1 (ArdourCanvas::COORD_MAX);
	transport_punchout_line->hide();

	// used to show zoom mode active zooming
	zoom_rect = new ArdourCanvas::Rectangle (_master_group, ArdourCanvas::Rect (0.0, 0.0, 0.0, 0.0));
	zoom_rect->set_outline_width (1);
	zoom_rect->hide();

	zoom_rect->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_zoom_rect_event), (ArdourCanvas::Item*) 0));

	// used as rubberband rect
	rubberband_rect = new ArdourCanvas::Rectangle (_trackview_group, ArdourCanvas::Rect (0.0, 0.0, 0.0, 0.0));

	rubberband_rect->set_outline_width (1);
	rubberband_rect->hide();

	tempo_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_tempo_bar_event), tempo_bar));
	meter_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_meter_bar_event), meter_bar));
	marker_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_marker_bar_event), marker_bar));
	cd_marker_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_cd_marker_bar_event), cd_marker_bar));
	videotl_bar_group->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_videotl_bar_event), videotl_bar));
	range_marker_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_range_marker_bar_event), range_marker_bar));
	transport_marker_bar->Event.connect (sigc::bind (sigc::mem_fun (*this, &Editor::canvas_transport_marker_bar_event), transport_marker_bar));

	playhead_cursor = new EditorCursor (*this, &Editor::canvas_playhead_cursor_event);

	if (logo_item) {
		logo_item->lower_to_bottom ();
	}
	/* need to handle 4 specific types of events as catch-alls */

	_track_canvas->signal_scroll_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_scroll_event));
	_track_canvas->signal_motion_notify_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_motion_notify_event));
	_track_canvas->signal_button_press_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_button_press_event));
	_track_canvas->signal_button_release_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_button_release_event));
	_track_canvas->signal_drag_motion().connect (sigc::mem_fun (*this, &Editor::track_canvas_drag_motion));
	_track_canvas->signal_key_press_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_key_press));
	_track_canvas->signal_key_release_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_key_release));

	_track_canvas->set_name ("EditorMainCanvas");
	_track_canvas->add_events (Gdk::POINTER_MOTION_HINT_MASK | Gdk::SCROLL_MASK | Gdk::KEY_PRESS_MASK | Gdk::KEY_RELEASE_MASK);
	_track_canvas->signal_leave_notify_event().connect (sigc::mem_fun(*this, &Editor::left_track_canvas), false);
	_track_canvas->signal_enter_notify_event().connect (sigc::mem_fun(*this, &Editor::entered_track_canvas), false);
	_track_canvas->set_flags (CAN_FOCUS);

	/* set up drag-n-drop */

	vector<TargetEntry> target_table;

	// Drag-N-Drop from the region list can generate this target
	target_table.push_back (TargetEntry ("regions"));

	target_table.push_back (TargetEntry ("text/plain"));
	target_table.push_back (TargetEntry ("text/uri-list"));
	target_table.push_back (TargetEntry ("application/x-rootwin-drop"));

	_track_canvas->drag_dest_set (target_table);
	_track_canvas->signal_drag_data_received().connect (sigc::mem_fun(*this, &Editor::track_canvas_drag_data_received));

	_track_canvas_viewport->signal_size_allocate().connect (sigc::mem_fun(*this, &Editor::track_canvas_viewport_allocate));

	ColorsChanged.connect (sigc::mem_fun (*this, &Editor::color_handler));
	color_handler();

}

void
Editor::track_canvas_viewport_allocate (Gtk::Allocation alloc)
{
	_canvas_viewport_allocation = alloc;
	track_canvas_viewport_size_allocated ();
}

bool
Editor::track_canvas_viewport_size_allocated ()
{
	bool height_changed = _visible_canvas_height != _canvas_viewport_allocation.get_height();

	_visible_canvas_width  = _canvas_viewport_allocation.get_width ();
	_visible_canvas_height = _canvas_viewport_allocation.get_height ();

	cerr << "VISIBLE CANVAS now: " << _visible_canvas_width << " x " << _visible_canvas_height << endl;

	if (_session) {
		TrackViewList::iterator i;

		for (i = track_views.begin(); i != track_views.end(); ++i) {
			(*i)->clip_to_viewport ();
		}
	}

	if (height_changed) {

		for (LocationMarkerMap::iterator i = location_markers.begin(); i != location_markers.end(); ++i) {
			i->second->canvas_height_set (_visible_canvas_height);
		}

		vertical_adjustment.set_page_size (_visible_canvas_height);
		if ((vertical_adjustment.get_value() + _visible_canvas_height) >= vertical_adjustment.get_upper()) {
			/*
			   We're increasing the size of the canvas while the bottom is visible.
			   We scroll down to keep in step with the controls layout.
			*/
			vertical_adjustment.set_value (_full_canvas_height - _visible_canvas_height);
		}
	}

	update_fixed_rulers();
	redisplay_tempo (false);
	_summary->set_overlays_dirty ();

	return false;
}

void
Editor::reset_controls_layout_width ()
{
	GtkRequisition req;
	gint w;

	edit_controls_vbox.size_request (req);
	w = req.width;

        if (_group_tabs->is_mapped()) {
		_group_tabs->size_request (req);
                w += req.width;
        }

        /* the controls layout has no horizontal scrolling, its visible
           width is always equal to the total width of its contents.
        */

        controls_layout.property_width() = w;
        controls_layout.property_width_request() = w;
}

void
Editor::reset_controls_layout_height (int32_t h)
{
        /* set the height of the scrollable area (i.e. the sum of all contained widgets)
         */

        controls_layout.property_height() = h;

        /* size request is set elsewhere, see ::track_canvas_allocate() */
}

bool
Editor::track_canvas_map_handler (GdkEventAny* /*ev*/)
{
	if (current_canvas_cursor) {
		set_canvas_cursor (current_canvas_cursor);
	}
	return false;
}

/** This is called when something is dropped onto the track canvas */
void
Editor::track_canvas_drag_data_received (const RefPtr<Gdk::DragContext>& context,
					 int x, int y,
					 const SelectionData& data,
					 guint info, guint time)
{
	if (data.get_target() == "regions") {
		drop_regions (context, x, y, data, info, time);
	} else {
		drop_paths (context, x, y, data, info, time);
	}
}

bool
Editor::idle_drop_paths (vector<string> paths, framepos_t frame, double ypos, bool copy)
{
	drop_paths_part_two (paths, frame, ypos, copy);
	return false;
}

void
Editor::drop_paths_part_two (const vector<string>& paths, framepos_t frame, double ypos, bool copy)
{
	RouteTimeAxisView* tv;
	
	/* MIDI files must always be imported, because we consider them
	 * writable. So split paths into two vectors, and follow the import
	 * path on the MIDI part.
	 */

	vector<string> midi_paths;
	vector<string> audio_paths;

	for (vector<string>::const_iterator i = paths.begin(); i != paths.end(); ++i) {
		if (SMFSource::safe_midi_file_extension (*i)) {
			midi_paths.push_back (*i);
		} else {
			audio_paths.push_back (*i);
		}
	}


	std::pair<TimeAxisView*, int> const tvp = trackview_by_y_position (ypos);
	if (tvp.first == 0) {

		/* drop onto canvas background: create new tracks */

		frame = 0;

		do_import (midi_paths, Editing::ImportDistinctFiles, ImportAsTrack, SrcBest, frame);
		
		if (Profile->get_sae() || Config->get_only_copy_imported_files() || copy) {
			do_import (audio_paths, Editing::ImportDistinctFiles, Editing::ImportAsTrack, SrcBest, frame);
		} else {
			do_embed (audio_paths, Editing::ImportDistinctFiles, ImportAsTrack, frame);
		}

	} else if ((tv = dynamic_cast<RouteTimeAxisView*> (tvp.first)) != 0) {

		/* check that its a track, not a bus */

		if (tv->track()) {
			/* select the track, then embed/import */
			selection->set (tv);

			do_import (midi_paths, Editing::ImportSerializeFiles, ImportToTrack, SrcBest, frame);

			if (Profile->get_sae() || Config->get_only_copy_imported_files() || copy) {
				do_import (audio_paths, Editing::ImportSerializeFiles, Editing::ImportToTrack, SrcBest, frame);
			} else {
				do_embed (audio_paths, Editing::ImportSerializeFiles, ImportToTrack, frame);
			}
		}
	}
}

void
Editor::drop_paths (const RefPtr<Gdk::DragContext>& context,
		    int x, int y,
		    const SelectionData& data,
		    guint info, guint time)
{
	vector<string> paths;
	GdkEvent ev;
	framepos_t frame;
	double cy;

	if (convert_drop_to_paths (paths, context, x, y, data, info, time) == 0) {

		/* D-n-D coordinates are window-relative, so convert to "world" coordinates
		 */

		ev.type = GDK_BUTTON_RELEASE;
		ev.button.x = x;
		ev.button.y = y;

		frame = window_event_frame (&ev, 0, &cy);

		snap_to (frame);

		bool copy = ((context->get_actions() & (Gdk::ACTION_COPY | Gdk::ACTION_LINK | Gdk::ACTION_MOVE)) == Gdk::ACTION_COPY);
#ifdef GTKOSX
		/* We are not allowed to call recursive main event loops from within
		   the main event loop with GTK/Quartz. Since import/embed wants
		   to push up a progress dialog, defer all this till we go idle.
		*/
		Glib::signal_idle().connect (sigc::bind (sigc::mem_fun (*this, &Editor::idle_drop_paths), paths, frame, cy, copy));
#else
		drop_paths_part_two (paths, frame, cy, copy);
#endif
	}

	context->drag_finish (true, false, time);
}

/** If the editor window is arranged such that the edge of the trackview is right up
 *  against the edge of the screen, autoscroll will not work very well.  In this situation,
 *  we start autoscrolling some distance in from the right-hand-side of the screen edge;
 *  this is the distance at which that happens.
 */
int
Editor::autoscroll_fudge_threshold () const
{
	return current_page_samples() / 6;
}

/** @param allow_horiz true to allow horizontal autoscroll, otherwise false.
 *  @param allow_vert true to allow vertical autoscroll, otherwise false.
 *  @param moving_left true if we are moving left, so we only want to autoscroll on the left of the canvas,
 *  otherwise false, so we only want to autoscroll on the right of the canvas.
 *  @param moving_up true if we are moving up, so we only want to autoscroll at the top of the canvas,
 *  otherwise false, so we only want to autoscroll at the bottom of the canvas.
 */
void
Editor::maybe_autoscroll (bool allow_horiz, bool allow_vert, bool moving_left, bool moving_up)
{
	if (!Config->get_autoscroll_editor ()) {
		return;
	}
	
	bool startit = false;

	/* Work out the distance between the right hand edge of the trackview and the edge of
	   the monitor that it is on.
	*/

	Glib::RefPtr<Gdk::Window> gdk_window = get_window ();
	Gdk::Rectangle window_rect;
	gdk_window->get_frame_extents (window_rect);
	
	Glib::RefPtr<Gdk::Screen> screen = get_screen ();
	Gdk::Rectangle root_rect;
	screen->get_root_window()->get_frame_extents (root_rect);

	Gtk::Allocation editor_list = _the_notebook.get_allocation ();

	framecnt_t distance = pixel_to_sample (root_rect.get_x() + root_rect.get_width() - window_rect.get_x() - window_rect.get_width());
	if (_the_notebook.is_visible ()) {
		distance += pixel_to_sample (editor_list.get_width());
	}

	/* Note whether we're fudging the autoscroll (see autoscroll_fudge_threshold) */
	_autoscroll_fudging = (distance < autoscroll_fudge_threshold ());

	double const ty = _drags->current_pointer_y() - get_trackview_group_vertical_offset ();

	autoscroll_y = 0;
	autoscroll_x = 0;
	if (ty < 0 && moving_up && allow_vert) {
		autoscroll_y = -1;
		startit = true;
	} else if (ty > _visible_canvas_height && !moving_up && allow_vert) {
		autoscroll_y = 1;
		startit = true;
	}

	framepos_t rightmost_frame = leftmost_frame + current_page_samples();
	if (_autoscroll_fudging) {
		rightmost_frame -= autoscroll_fudge_threshold ();
	}

	if (_drags->current_pointer_frame() > rightmost_frame && allow_horiz) {
		if (rightmost_frame < max_framepos && !moving_left) {
			autoscroll_x = 1;
			startit = true;
		}
	} else if (_drags->current_pointer_frame() < leftmost_frame && allow_horiz) {
		if (leftmost_frame > 0 && moving_left) {
			autoscroll_x = -1;
			startit = true;
		}
	}

	if (autoscroll_active && ((autoscroll_x != last_autoscroll_x) || (autoscroll_y != last_autoscroll_y) || (autoscroll_x == 0 && autoscroll_y == 0))) {
		stop_canvas_autoscroll ();
	}

	if (startit && autoscroll_timeout_tag < 0) {
		start_canvas_autoscroll (autoscroll_x, autoscroll_y);
	}

	last_autoscroll_x = autoscroll_x;
	last_autoscroll_y = autoscroll_y;
}

gint
Editor::_autoscroll_canvas (void *arg)
{
        return ((Editor *) arg)->autoscroll_canvas ();
}

bool
Editor::autoscroll_canvas ()
{
	framepos_t new_frame;
	framepos_t limit = max_framepos - current_page_samples();
	GdkEventMotion ev;
	double new_pixel;
	double target_pixel;
	
	if (autoscroll_x_distance != 0) {

		if (autoscroll_x > 0) {
			autoscroll_x_distance = (_drags->current_pointer_frame() - (leftmost_frame + current_page_samples())) / 3;
			if (_autoscroll_fudging) {
				autoscroll_x_distance += autoscroll_fudge_threshold () / 3;
			}
		} else if (autoscroll_x < 0) {
			autoscroll_x_distance = (leftmost_frame - _drags->current_pointer_frame()) / 3;

		}
	}

	if (autoscroll_y_distance != 0) {
		if (autoscroll_y > 0) {
			autoscroll_y_distance = (_drags->current_pointer_y() - (get_trackview_group_vertical_offset() + _visible_canvas_height)) / 3;
		} else if (autoscroll_y < 0) {

			autoscroll_y_distance = (vertical_adjustment.get_value () - _drags->current_pointer_y()) / 3;
		}
	}

	if (autoscroll_x < 0) {
		if (leftmost_frame < autoscroll_x_distance) {
			new_frame = 0;
		} else {
			new_frame = leftmost_frame - autoscroll_x_distance;
		}
 	} else if (autoscroll_x > 0) {
		if (leftmost_frame > limit - autoscroll_x_distance) {
			new_frame = limit;
		} else {
			new_frame = leftmost_frame + autoscroll_x_distance;
		}
	} else {
		new_frame = leftmost_frame;
	}

	double vertical_pos = vertical_adjustment.get_value();

	if (autoscroll_y < 0) {

		if (vertical_pos < autoscroll_y_distance) {
			new_pixel = 0;
		} else {
			new_pixel = vertical_pos - autoscroll_y_distance;
		}

		target_pixel = _drags->current_pointer_y() - autoscroll_y_distance;
		target_pixel = max (target_pixel, 0.0);

 	} else if (autoscroll_y > 0) {

		double const top_of_bottom_of_canvas = _full_canvas_height - _visible_canvas_height;

		if (vertical_pos > _full_canvas_height - autoscroll_y_distance) {
			new_pixel = _full_canvas_height;
		} else {
			new_pixel = vertical_pos + autoscroll_y_distance;
		}

		new_pixel = min (top_of_bottom_of_canvas, new_pixel);

		target_pixel = _drags->current_pointer_y() + autoscroll_y_distance;

		/* don't move to the full canvas height because the item will be invisible
		   (its top edge will line up with the bottom of the visible canvas.
		*/

		target_pixel = min (target_pixel, _full_canvas_height - 10);

	} else {
	  	target_pixel = _drags->current_pointer_y();
		new_pixel = vertical_pos;
	}

	if ((new_frame == 0 || new_frame == limit) && (new_pixel == 0 || new_pixel == DBL_MAX)) {
		/* we are done */
		return false;
	}

	if (new_frame != leftmost_frame) {
		reset_x_origin (new_frame);
	}

	vertical_adjustment.set_value (new_pixel);

	/* fake an event. */

	Glib::RefPtr<Gdk::Window> canvas_window = const_cast<Editor*>(this)->_track_canvas->get_window();
	gint x, y;
	Gdk::ModifierType mask;
	canvas_window->get_pointer (x, y, mask);
	ev.type = GDK_MOTION_NOTIFY;
	ev.state = Gdk::BUTTON1_MASK;
	ev.x = x;
	ev.y = y;

	motion_handler (0, (GdkEvent*) &ev, true);

	autoscroll_cnt++;

	if (autoscroll_cnt == 1) {

		/* connect the timeout so that we get called repeatedly */

		autoscroll_timeout_tag = g_idle_add ( _autoscroll_canvas, this);
		return false;

	}

	return true;
}

void
Editor::start_canvas_autoscroll (int dx, int dy)
{
	if (!_session || autoscroll_active) {
		return;
	}

	stop_canvas_autoscroll ();

	autoscroll_active = true;
	autoscroll_x = dx;
	autoscroll_y = dy;
	autoscroll_x_distance = (framepos_t) floor (current_page_samples()/50.0);
	autoscroll_y_distance = fabs (dy * 5); /* pixels */
	autoscroll_cnt = 0;

	/* do it right now, which will start the repeated callbacks */

	autoscroll_canvas ();
}

void
Editor::stop_canvas_autoscroll ()
{
	if (autoscroll_timeout_tag >= 0) {
		g_source_remove (autoscroll_timeout_tag);
		autoscroll_timeout_tag = -1;
	}

	autoscroll_active = false;
}

bool
Editor::left_track_canvas (GdkEventCrossing */*ev*/)
{
	DropDownKeys ();
	within_track_canvas = false;
	//cerr << "left track canvas\n";
	set_entered_track (0);
	set_entered_regionview (0);
	reset_canvas_action_sensitivity (false);
	return false;
}

bool
Editor::entered_track_canvas (GdkEventCrossing */*ev*/)
{
	//cerr << "entered track canvas\n";
	within_track_canvas = true;
	reset_canvas_action_sensitivity (true);
	return FALSE;
}

void
Editor::ensure_time_axis_view_is_visible (const TimeAxisView& tav)
{
	double begin = tav.y_position();

	double v = vertical_adjustment.get_value ();

	if (begin < v || begin + tav.current_height() > v + _visible_canvas_height) {
		/* try to put the TimeAxisView roughly central */
		if (begin >= _visible_canvas_height/2.0) {
			begin -= _visible_canvas_height/2.0;
		}
		vertical_adjustment.set_value (begin);
	}
}

/** Called when the main vertical_adjustment has changed */
void
Editor::tie_vertical_scrolling ()
{

	_track_canvas_vadj->set_value (vertical_adjustment.get_value ());

	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		(*i)->clip_to_viewport ();
	}

	/* required to keep the controls_layout in lock step with the canvas group */
	
	update_canvas_now ();

	/* this will do an immediate redraw */

	controls_layout.get_vadjustment()->set_value (vertical_adjustment.get_value());

	if (pending_visual_change.idle_handler_id < 0) {
		_summary->set_overlays_dirty ();
	}
}

void
Editor::set_horizontal_position (double p)
{
	_track_canvas_hadj->set_value (p);
	_time_bars_canvas_hadj->set_value (p);

	leftmost_frame = (framepos_t) floor (p * frames_per_pixel);

	update_fixed_rulers ();
	redisplay_tempo (true);

	if (pending_visual_change.idle_handler_id < 0) {
		_summary->set_overlays_dirty ();
	}

	update_video_timeline();

	HorizontalPositionChanged (); /* EMIT SIGNAL */

#ifndef GTKOSX
	if (!autoscroll_active && !_stationary_playhead) {
		/* force rulers and canvas to move in lock step */
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
	}
#endif
}

// CAIROCANVAS
#if 0
void
Editor::scroll_canvas_vertically ()
{
	/* vertical scrolling only */

	double y_delta;

	y_delta = last_trackview_group_vertical_offset - get_trackview_group_vertical_offset ();
	_trackview_group->move (0, y_delta);
	_background_group->move (0, y_delta);

	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		(*i)->clip_to_viewport ();
	}
	last_trackview_group_vertical_offset = get_trackview_group_vertical_offset ();
	/* required to keep the controls_layout in lock step with the canvas group */
	update_canvas_now ();
}
#endif

void
Editor::color_handler()
{
	playhead_cursor->set_color (ARDOUR_UI::config()->canvasvar_PlayHead.get());
	_verbose_cursor->set_color (ARDOUR_UI::config()->canvasvar_VerboseCanvasCursor.get());

	meter_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_MeterBar.get());
	meter_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	tempo_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_TempoBar.get());
	tempo_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	marker_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_MarkerBar.get());
	marker_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	cd_marker_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_CDMarkerBar.get());
	cd_marker_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	videotl_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_VideoBar.get());
	videotl_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	range_marker_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_RangeMarkerBar.get());
	range_marker_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	transport_marker_bar->set_fill_color (ARDOUR_UI::config()->canvasvar_TransportMarkerBar.get());
	transport_marker_bar->set_outline_color (ARDOUR_UI::config()->canvasvar_MarkerBarSeparator.get());

	cd_marker_bar_drag_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_RangeDragBarRect.get());
	cd_marker_bar_drag_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_RangeDragBarRect.get());

	range_bar_drag_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_RangeDragBarRect.get());
	range_bar_drag_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_RangeDragBarRect.get());

	transport_bar_drag_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_TransportDragRect.get());
	transport_bar_drag_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_TransportDragRect.get());

	transport_loop_range_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_TransportLoopRect.get());
	transport_loop_range_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_TransportLoopRect.get());

	transport_punch_range_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_TransportPunchRect.get());
	transport_punch_range_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_TransportPunchRect.get());

	transport_punchin_line->set_outline_color (ARDOUR_UI::config()->canvasvar_PunchLine.get());
	transport_punchout_line->set_outline_color (ARDOUR_UI::config()->canvasvar_PunchLine.get());

	zoom_rect->set_fill_color (ARDOUR_UI::config()->canvasvar_ZoomRect.get());
	zoom_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_ZoomRect.get());

	rubberband_rect->set_outline_color (ARDOUR_UI::config()->canvasvar_RubberBandRect.get());
	rubberband_rect->set_fill_color ((guint32) ARDOUR_UI::config()->canvasvar_RubberBandRect.get());

	location_marker_color = ARDOUR_UI::config()->canvasvar_LocationMarker.get();
	location_range_color = ARDOUR_UI::config()->canvasvar_LocationRange.get();
	location_cd_marker_color = ARDOUR_UI::config()->canvasvar_LocationCDMarker.get();
	location_loop_color = ARDOUR_UI::config()->canvasvar_LocationLoop.get();
	location_punch_color = ARDOUR_UI::config()->canvasvar_LocationPunch.get();

	refresh_location_display ();
/*
	redisplay_tempo (true);

	if (_session)
	      _session->tempo_map().apply_with_metrics (*this, &Editor::draw_metric_marks); // redraw metric markers
*/
}

void
Editor::flush_canvas ()
{
	if (is_mapped()) {
		update_canvas_now ();
		// gdk_window_process_updates (GTK_LAYOUT(track_canvas->gobj())->bin_window, true);
	}
}

void
Editor::update_canvas_now ()
{
	/* GnomeCanvas has a bug whereby if its idle handler is not scheduled between
	   two calls to update_now, an assert will trip.  This wrapper works around
	   that problem by only calling update_now if the assert will not trip.

	   I think the GC bug is due to the fact that its code will reset need_update
	   and need_redraw to FALSE without checking to see if an idle handler is scheduled.
	   If one is scheduled, GC should probably remove it.
	*/

	/* XXX: CANVAS */
//	GnomeCanvas* c = track_canvas->gobj ();
//	if (c->need_update || c->need_redraw) {
//		track_canvas->update_now ();
//	}
}

double
Editor::horizontal_position () const
{
	return sample_to_pixel (leftmost_frame);
}

void
Editor::set_canvas_cursor (Gdk::Cursor* cursor, bool save)
{
	if (save) {
		current_canvas_cursor = cursor;
	}

	Glib::RefPtr<Gdk::Window> win = _track_canvas->get_window();

	if (win) {
	        _track_canvas->get_window()->set_cursor (*cursor);
	}
}

bool
Editor::track_canvas_key_press (GdkEventKey*)
{
	/* XXX: event does not report the modifier key pressed down, AFAICS, so use the Keyboard object instead */
	if (mouse_mode == Editing::MouseZoom && Keyboard::the_keyboard().key_is_down (GDK_Control_L)) {
		set_canvas_cursor (_cursors->zoom_out, true);
	}

	return false;
}

bool
Editor::track_canvas_key_release (GdkEventKey*)
{
	if (mouse_mode == Editing::MouseZoom && !Keyboard::the_keyboard().key_is_down (GDK_Control_L)) {
		set_canvas_cursor (_cursors->zoom_in, true);
	}

	return false;
}

double
Editor::clamp_verbose_cursor_x (double x)
{
	if (x < 0) {
		x = 0;
	} else {
		x = min (_visible_canvas_width - 200.0, x);
	}
	return x;
}

double
Editor::clamp_verbose_cursor_y (double y)
{
	y = max (0.0, y);
	y = min (_visible_canvas_height - 50, y);
	return y;
}
