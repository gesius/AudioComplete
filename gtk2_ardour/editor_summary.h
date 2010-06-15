
/*
    Copyright (C) 2009 Paul Davis

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

#ifndef __gtk_ardour_editor_summary_h__
#define __gtk_ardour_editor_summary_h__

#include "cairo_widget.h"
#include "editor_component.h"

namespace ARDOUR {
	class Session;
}

class Editor;

/** Class to provide a visual summary of the contents of an editor window; represents
 *  the whole session as a set of lines, one per region view.
 */
class EditorSummary : public CairoWidget, public EditorComponent
{
public:
	EditorSummary (Editor *);

	void set_session (ARDOUR::Session *);
	void set_overlays_dirty ();

private:
	bool on_expose_event (GdkEventExpose *);
	void on_size_request (Gtk::Requisition *);
	bool on_button_press_event (GdkEventButton *);
	bool on_button_release_event (GdkEventButton *);
	bool on_motion_notify_event (GdkEventMotion *);
	bool on_scroll_event (GdkEventScroll *);

	void centre_on_click (GdkEventButton *);
	void render (cairo_t *);
	void render_region (RegionView*, cairo_t*, double) const;
	void get_editor (std::pair<double, double> *, std::pair<double, double> *) const;
	void set_editor (std::pair<double, double> const &, double);
	void playhead_position_changed (nframes64_t);
	double summary_y_to_editor (double) const;
	double editor_y_to_summary (double) const;

	nframes_t _start; ///< start frame of the overview
	nframes_t _end; ///< end frame of the overview

	/** fraction of the session length by which the overview size should extend past the start and end markers */
	double _overhang_fraction;

	double _x_scale; ///< pixels per frame for the x axis of the pixmap
	double _track_height;
	double _last_playhead;

	std::pair<double, double> _start_editor_x;
	std::pair<double, double> _start_editor_y;
	double _start_mouse_x;
	double _start_mouse_y;
	enum {
		IN_VIEWBOX,
		BELOW_OR_ABOVE_VIEWBOX,
		TO_LEFT_OR_RIGHT_OF_VIEWBOX
	} _start_position;

	bool _move_dragging;
	double _x_offset;
	double _y_offset;
	bool _moved;

	bool _zoom_dragging;
	bool _zoom_left;

	PBD::ScopedConnectionList position_connection;
	PBD::ScopedConnectionList region_property_connection;
};

#endif
