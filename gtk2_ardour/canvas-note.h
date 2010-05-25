/*
    Copyright (C) 2007 Paul Davis
    Author: Dave Robillard
    Author: Hans Baier

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

#ifndef __gtk_ardour_canvas_note_h__
#define __gtk_ardour_canvas_note_h__

#include <iostream>
#include "simplerect.h"
#include "canvas-note-event.h"
#include "midi_util.h"

namespace Gnome {
namespace Canvas {

class CanvasNote : public SimpleRect, public CanvasNoteEvent {
public:
	typedef Evoral::Note<Evoral::MusicalTime> NoteType;

	double x1() { return property_x1(); }
	double y1() { return property_y1(); }
	double x2() { return property_x2(); }
	double y2() { return property_y2(); }

	void set_outline_color(uint32_t c) { property_outline_color_rgba() = c; hide(); show(); }
	void set_fill_color(uint32_t c)    { property_fill_color_rgba()    = c; hide(); show(); }

	void show() { SimpleRect::show(); }
	void hide() { SimpleRect::hide(); }

	bool on_event(GdkEvent* ev);
	void move_event(double dx, double dy);

	CanvasNote (MidiRegionView&                   region,
		    Group&                            group,
		    const boost::shared_ptr<NoteType> note = boost::shared_ptr<NoteType>())
		: SimpleRect(group), CanvasNoteEvent(region, this, note)
	{
	}
};

class NoEventCanvasNote : public CanvasNote
{
public:
	NoEventCanvasNote (
		MidiRegionView& region,
		Group& group,
		const boost::shared_ptr<NoteType> note = boost::shared_ptr<NoteType>()
		)
		: CanvasNote (region, group, note) {}

	double point_vfunc(double, double, int, int, GnomeCanvasItem**) {
		/* return a huge value to tell the canvas that we're never the item for an event */
		return 9999999999999.0;
	}
};

} // namespace Gnome
} // namespace Canvas

#endif /* __gtk_ardour_canvas_note_h__ */
