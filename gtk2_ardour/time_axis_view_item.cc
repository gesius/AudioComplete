/*
    Copyright (C) 2003 Paul Davis

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

#include "pbd/error.h"
#include "pbd/stacktrace.h"

#include "ardour/types.h"
#include "ardour/ardour.h"

#include <gtkmm2ext/utils.h>

#include "ardour_ui.h"
/*
 * ardour_ui.h was moved up in the include list
 * due to a conflicting definition of 'Rect' between
 * Apple's MacTypes.h file and GTK
 */

#include "public_editor.h"
#include "time_axis_view_item.h"
#include "time_axis_view.h"
#include "simplerect.h"
#include "utils.h"
#include "canvas_impl.h"
#include "rgb_macros.h"

#include "i18n.h"

using namespace std;
using namespace Editing;
using namespace Glib;
using namespace PBD;
using namespace ARDOUR;

Pango::FontDescription* TimeAxisViewItem::NAME_FONT = 0;
const double TimeAxisViewItem::NAME_X_OFFSET = 15.0;
const double TimeAxisViewItem::GRAB_HANDLE_LENGTH = 6;

int    TimeAxisViewItem::NAME_HEIGHT;
double TimeAxisViewItem::NAME_Y_OFFSET;
double TimeAxisViewItem::NAME_HIGHLIGHT_SIZE;
double TimeAxisViewItem::NAME_HIGHLIGHT_THRESH;

void
TimeAxisViewItem::set_constant_heights ()
{
        NAME_FONT = get_font_for_style (X_("TimeAxisViewItemName"));
        
        Gtk::Window win;
        Gtk::Label foo;
        win.add (foo);
        
        Glib::RefPtr<Pango::Layout> layout = foo.create_pango_layout (X_("Hg")); /* ascender + descender */
        int width = 0;
        int height = 0;
        
        layout->set_font_description (*NAME_FONT);
        Gtkmm2ext::get_ink_pixel_size (layout, width, height);
        
        NAME_HEIGHT = height;
        NAME_Y_OFFSET = height + 3;
        NAME_HIGHLIGHT_SIZE = height + 2;
        NAME_HIGHLIGHT_THRESH = NAME_HIGHLIGHT_SIZE * 3;
}

/**
 * Construct a new TimeAxisViewItem.
 *
 * @param it_name the unique name of this item
 * @param parant the parent canvas group
 * @param tv the TimeAxisView we are going to be added to
 * @param spu samples per unit
 * @param base_color
 * @param start the start point of this item
 * @param duration the duration of this item
 */
TimeAxisViewItem::TimeAxisViewItem(const string & it_name, ArdourCanvas::Group& parent, TimeAxisView& tv, double spu, Gdk::Color const & base_color,
				   nframes64_t start, nframes64_t duration, bool recording,
				   Visibility vis)
	: trackview (tv)
	, _height (1.0)
	, _recregion (recording)
{
	group = new ArdourCanvas::Group (parent);

	init (it_name, spu, base_color, start, duration, vis, true, true);
}

TimeAxisViewItem::TimeAxisViewItem (const TimeAxisViewItem& other)
	: sigc::trackable(other)
	, PBD::ScopedConnectionList()
	, trackview (other.trackview)
	, _recregion (other._recregion)
{

	Gdk::Color c;
	int r,g,b,a;

	UINT_TO_RGBA (other.fill_color, &r, &g, &b, &a);
	c.set_rgb_p (r/255.0, g/255.0, b/255.0);

	/* share the other's parent, but still create a new group */

	Gnome::Canvas::Group* parent = other.group->property_parent();

	group = new ArdourCanvas::Group (*parent);

	_selected = other._selected;

	init (
		other.item_name, other.samples_per_unit, c, other.frame_position,
		other.item_duration, other.visibility, other.wide_enough_for_name, other.high_enough_for_name
		);
}

void
TimeAxisViewItem::init (
	const string& it_name, double spu, Gdk::Color const & base_color, nframes64_t start, nframes64_t duration, Visibility vis, bool wide, bool high)
{
	item_name = it_name;
	samples_per_unit = spu;
	should_show_selection = true;
	frame_position = start;
	item_duration = duration;
	name_connected = false;
	fill_opacity = 60;
	position_locked = false;
	max_item_duration = ARDOUR::max_frames;
	min_item_duration = 0;
	show_vestigial = true;
	visibility = vis;
	_sensitive = true;
	name_pixbuf_width = 0;
	last_item_width = 0;
	wide_enough_for_name = wide;
	high_enough_for_name = high;

	if (duration == 0) {
		warning << "Time Axis Item Duration == 0" << endl;
	}

	vestigial_frame = new ArdourCanvas::SimpleRect (*group, 0.0, 1.0, 2.0, trackview.current_height());
	vestigial_frame->hide ();
	vestigial_frame->property_outline_what() = 0xF;
	vestigial_frame->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_VestigialFrame.get();
	vestigial_frame->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_VestigialFrame.get();

	if (visibility & ShowFrame) {
		frame = new ArdourCanvas::SimpleRect (*group, 0.0, 1.0, trackview.editor().frame_to_pixel(duration), trackview.current_height());
		
		frame->property_outline_pixels() = 1;
		frame->property_outline_what() = 0xF;
		
		if(_recregion){
			frame->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_RecordingRect.get();
		}
		else {
			frame->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_TimeAxisFrame.get();
		}
		
		frame->property_outline_what() = 0x1|0x2|0x4|0x8;

	} else {
		frame = 0;
	}

	if (visibility & ShowNameHighlight) {
		
		if (visibility & FullWidthNameHighlight) {
			name_highlight = new ArdourCanvas::SimpleRect (*group, 0.0, trackview.editor().frame_to_pixel(item_duration), trackview.current_height() - TimeAxisViewItem::NAME_HIGHLIGHT_SIZE, trackview.current_height() - 1);
		} else {
			name_highlight = new ArdourCanvas::SimpleRect (*group, 1.0, trackview.editor().frame_to_pixel(item_duration) - 1, trackview.current_height() - TimeAxisViewItem::NAME_HIGHLIGHT_SIZE, trackview.current_height() - 1);
		}
		
		name_highlight->set_data ("timeaxisviewitem", this);

	} else {
		name_highlight = 0;
	}

	if (visibility & ShowNameText) {
		name_pixbuf = new ArdourCanvas::Pixbuf(*group);
		name_pixbuf->property_x() = NAME_X_OFFSET;
		name_pixbuf->property_y() = trackview.current_height() + 1 - NAME_Y_OFFSET;

	} else {
		name_pixbuf = 0;
	}

	/* create our grab handles used for trimming/duration etc */
	if (!_recregion) {
		frame_handle_start = new ArdourCanvas::SimpleRect (*group, 0.0, TimeAxisViewItem::GRAB_HANDLE_LENGTH, 5.0, trackview.current_height());
		frame_handle_start->property_outline_what() = 0x0;
		frame_handle_end = new ArdourCanvas::SimpleRect (*group, 0.0, TimeAxisViewItem::GRAB_HANDLE_LENGTH, 5.0, trackview.current_height());
		frame_handle_end->property_outline_what() = 0x0;
	} else {
		frame_handle_start = frame_handle_end = 0;
	}

	set_color (base_color);

	set_duration (item_duration, this);
	set_position (start, this);
}

TimeAxisViewItem::~TimeAxisViewItem()
{
	delete group;
}


/**
 * Set the position of this item on the timeline.
 *
 * @param pos the new position
 * @param src the identity of the object that initiated the change
 * @return true on success
 */

bool
TimeAxisViewItem::set_position(nframes64_t pos, void* src, double* delta)
{
	if (position_locked) {
		return false;
	}

	frame_position = pos;

	/*  This sucks. The GnomeCanvas version I am using
	    doesn't correctly implement gnome_canvas_group_set_arg(),
	    so that simply setting the "x" arg of the group
	    fails to move the group. Instead, we have to
	    use gnome_canvas_item_move(), which does the right
	    thing. I see that in GNOME CVS, the current (Sept 2001)
	    version of GNOME Canvas rectifies this issue cleanly.
	*/

	double old_unit_pos;
	double new_unit_pos = pos / samples_per_unit;

	old_unit_pos = group->property_x();

	if (new_unit_pos != old_unit_pos) {
		group->move (new_unit_pos - old_unit_pos, 0.0);
	}

	if (delta) {
		(*delta) = new_unit_pos - old_unit_pos;
	}

	PositionChanged (frame_position, src); /* EMIT_SIGNAL */

	return true;
}

/** @return position of this item on the timeline */
nframes64_t
TimeAxisViewItem::get_position() const
{
	return frame_position;
}

/**
 * Set the duration of this item.
 *
 * @param dur the new duration of this item
 * @param src the identity of the object that initiated the change
 * @return true on success
 */

bool
TimeAxisViewItem::set_duration (nframes64_t dur, void* src)
{
	if ((dur > max_item_duration) || (dur < min_item_duration)) {
		warning << string_compose (_("new duration %1 frames is out of bounds for %2"), get_item_name(), dur)
			<< endmsg;
		return false;
	}

	if (dur == 0) {
		group->hide();
	}

	item_duration = dur;

	reset_width_dependent_items (trackview.editor().frame_to_pixel (dur));

	DurationChanged (dur, src); /* EMIT_SIGNAL */
	return true;
}

/** @return duration of this item */
nframes64_t
TimeAxisViewItem::get_duration() const
{
	return item_duration;
}

/**
 * Set the maximum duration that this item can have.
 *
 * @param dur the new maximum duration
 * @param src the identity of the object that initiated the change
 */
void
TimeAxisViewItem::set_max_duration(nframes64_t dur, void* src)
{
	max_item_duration = dur;
	MaxDurationChanged(max_item_duration, src); /* EMIT_SIGNAL */
}

/** @return the maximum duration that this item may have */
nframes64_t
TimeAxisViewItem::get_max_duration() const
{
	return max_item_duration;
}

/**
 * Set the minimum duration that this item may have.
 *
 * @param the minimum duration that this item may be set to
 * @param src the identity of the object that initiated the change
 */
void
TimeAxisViewItem::set_min_duration(nframes64_t dur, void* src)
{
	min_item_duration = dur;
	MinDurationChanged(max_item_duration, src); /* EMIT_SIGNAL */
}

/** @return the minimum duration that this item mey have */
nframes64_t
TimeAxisViewItem::get_min_duration() const
{
	return min_item_duration;
}

/**
 * Set whether this item is locked to its current position.
 * Locked items cannot be moved until the item is unlocked again.
 *
 * @param yn true to lock this item to its current position
 * @param src the identity of the object that initiated the change
 */
void
TimeAxisViewItem::set_position_locked(bool yn, void* src)
{
	position_locked = yn;
	set_trim_handle_colors();
	PositionLockChanged (position_locked, src); /* EMIT_SIGNAL */
}

/** @return true if this item is locked to its current position */
bool
TimeAxisViewItem::get_position_locked() const
{
	return position_locked;
}

/**
 * Set whether the maximum duration constraint is active.
 *
 * @param active set true to enforce the max duration constraint
 * @param src the identity of the object that initiated the change
 */
void
TimeAxisViewItem::set_max_duration_active (bool active, void* /*src*/)
{
	max_duration_active = active;
}

/** @return true if the maximum duration constraint is active */
bool
TimeAxisViewItem::get_max_duration_active() const
{
	return max_duration_active;
}

/**
 * Set whether the minimum duration constraint is active.
 *
 * @param active set true to enforce the min duration constraint
 * @param src the identity of the object that initiated the change
 */

void
TimeAxisViewItem::set_min_duration_active (bool active, void* /*src*/)
{
	min_duration_active = active;
}

/** @return true if the maximum duration constraint is active */
bool
TimeAxisViewItem::get_min_duration_active() const
{
	return min_duration_active;
}

/**
 * Set the name of this item.
 *
 * @param new_name the new name of this item
 * @param src the identity of the object that initiated the change
 */

void
TimeAxisViewItem::set_item_name(std::string new_name, void* src)
{
	if (new_name != item_name) {
		std::string temp_name = item_name;
		item_name = new_name;
		NameChanged (item_name, temp_name, src); /* EMIT_SIGNAL */
	}
}

/** @return the name of this item */
std::string
TimeAxisViewItem::get_item_name() const
{
	return item_name;
}

/**
 * Set selection status.
 *
 * @param yn true if this item is currently selected
 */
void
TimeAxisViewItem::set_selected(bool yn)
{
	if (_selected != yn) {
		Selectable::set_selected (yn);
		set_frame_color ();
	}
}

/**
 * Set whether an item should show its selection status.
 *
 * @param yn true if this item should show its selected status
 */

void
TimeAxisViewItem::set_should_show_selection (bool yn)
{
	if (should_show_selection != yn) {
		should_show_selection = yn;
		set_frame_color ();
	}
}

/** @return the TimeAxisView that this item is on */
TimeAxisView&
TimeAxisViewItem::get_time_axis_view()
{
	return trackview;
}

/**
 * Set the displayed item text.
 * This item is the visual text name displayed on the canvas item, this can be different to the name of the item.
 *
 * @param new_name the new name text to display
 */

void
TimeAxisViewItem::set_name_text(const ustring& new_name)
{
	if (!name_pixbuf) {
		return;
	}

	last_item_width = trackview.editor().frame_to_pixel(item_duration);
	name_pixbuf_width = pixel_width (new_name, *NAME_FONT) + 2;
	name_pixbuf->property_pixbuf() = pixbuf_from_ustring(new_name, NAME_FONT, name_pixbuf_width, NAME_HEIGHT, Gdk::Color ("#000000"));
}


/**
 * Set the height of this item.
 *
 * @param h new height
 */
void
TimeAxisViewItem::set_height (double height)
{
        _height = height;

	if (name_highlight) {
		if (height < NAME_HIGHLIGHT_THRESH) {
			name_highlight->hide ();
			high_enough_for_name = false;

		} else {
			name_highlight->show();
			high_enough_for_name = true;
		}

		if (height > NAME_HIGHLIGHT_SIZE) {
			name_highlight->property_y1() = (double) height - 1 - NAME_HIGHLIGHT_SIZE;
			name_highlight->property_y2() = (double) height - 2;
		}
		else {
			/* it gets hidden now anyway */
			name_highlight->property_y1() = (double) 1.0;
			name_highlight->property_y2() = (double) height;
		}
	}

	if (visibility & ShowNameText) {
		name_pixbuf->property_y() =  height + 1 - NAME_Y_OFFSET;
	}

	if (frame) {
		frame->property_y2() = height - 1;
		if (frame_handle_start) {
			frame_handle_start->property_y2() = height - 1;
			frame_handle_end->property_y2() = height - 1;
		}
	}

	vestigial_frame->property_y2() = height - 1;

	update_name_pixbuf_visibility ();
}

void
TimeAxisViewItem::set_color (Gdk::Color const & base_color)
{
	compute_colors (base_color);
	set_colors ();
}

ArdourCanvas::Item*
TimeAxisViewItem::get_canvas_frame()
{
	return frame;
}

ArdourCanvas::Group*
TimeAxisViewItem::get_canvas_group()
{
	return group;
}

ArdourCanvas::Item*
TimeAxisViewItem::get_name_highlight()
{
	return name_highlight;
}

ArdourCanvas::Pixbuf*
TimeAxisViewItem::get_name_pixbuf()
{
	return name_pixbuf;
}

/**
 * Calculate some contrasting color for displaying various parts of this item, based upon the base color.
 *
 * @param color the base color of the item
 */
void
TimeAxisViewItem::compute_colors (Gdk::Color const & base_color)
{
	unsigned char radius;
	char minor_shift;

	unsigned char r,g,b;

	/* FILL: this is simple */
	r = base_color.get_red()/256;
	g = base_color.get_green()/256;
	b = base_color.get_blue()/256;
	fill_color = RGBA_TO_UINT(r,g,b,160);

	/*  for minor colors:
		if the overall saturation is strong, make the minor colors light.
		if its weak, make them dark.

   		we do this by moving an equal distance to the other side of the
		central circle in the color wheel from where we started.
	*/

	radius = (unsigned char) rint (floor (sqrt (static_cast<double>(r*r + g*g + b+b))/3.0f));
	minor_shift = 125 - radius;

	/* LABEL: rotate around color wheel by 120 degrees anti-clockwise */

	r = base_color.get_red()/256;
	g = base_color.get_green()/256;
	b = base_color.get_blue()/256;

	if (r > b)
	{
		if (r > g)
		{
			/* red sector => green */
			swap (r,g);
		}
		else
		{
			/* green sector => blue */
			swap (g,b);
		}
	}
	else
	{
		if (b > g)
		{
			/* blue sector => red */
			swap (b,r);
		}
		else
		{
			/* green sector => blue */
			swap (g,b);
		}
	}

	r += minor_shift;
	b += minor_shift;
	g += minor_shift;

	label_color = RGBA_TO_UINT(r,g,b,255);
	r = (base_color.get_red()/256)   + 127;
	g = (base_color.get_green()/256) + 127;
	b = (base_color.get_blue()/256)  + 127;

	label_color = RGBA_TO_UINT(r,g,b,255);

	/* XXX can we do better than this ? */
	/* We're trying;) */
	/* NUKECOLORS */

	//frame_color_r = 192;
	//frame_color_g = 192;
	//frame_color_b = 194;

	//selected_frame_color_r = 182;
	//selected_frame_color_g = 145;
	//selected_frame_color_b = 168;

	//handle_color_r = 25;
	//handle_color_g = 0;
	//handle_color_b = 255;
	//lock_handle_color_r = 235;
	//lock_handle_color_g = 16;
	//lock_handle_color_b = 16;
}

/**
 * Convenience method to set the various canvas item colors
 */
void
TimeAxisViewItem::set_colors()
{
	set_frame_color();

	if (name_highlight) {
		name_highlight->property_fill_color_rgba() = fill_color;
		name_highlight->property_outline_color_rgba() = fill_color;
	}
	set_trim_handle_colors();
}

/**
 * Sets the frame color depending on whether this item is selected
 */
void
TimeAxisViewItem::set_frame_color()
{
	if (frame) {
		uint32_t r,g,b,a;

		if (_selected && should_show_selection) {
			UINT_TO_RGBA(ARDOUR_UI::config()->canvasvar_SelectedFrameBase.get(), &r, &g, &b, &a);
			frame->property_fill_color_rgba() = RGBA_TO_UINT(r, g, b, a);
		} else {
			if (_recregion) {
				UINT_TO_RGBA(ARDOUR_UI::config()->canvasvar_RecordingRect.get(), &r, &g, &b, &a);
				frame->property_fill_color_rgba() = RGBA_TO_UINT(r, g, b, a);
			} else {
				UINT_TO_RGBA(ARDOUR_UI::config()->canvasvar_FrameBase.get(), &r, &g, &b, &a);
				frame->property_fill_color_rgba() = RGBA_TO_UINT(r, g, b, fill_opacity ? fill_opacity : a);
			}
		}
	}
}

/**
 * Set the colors of the start and end trim handle depending on object state
 */
void
TimeAxisViewItem::set_trim_handle_colors()
{
	if (frame_handle_start) {
		if (position_locked) {
			frame_handle_start->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_TrimHandleLocked.get();
			frame_handle_end->property_fill_color_rgba() =  ARDOUR_UI::config()->canvasvar_TrimHandleLocked.get();
		} else {
			frame_handle_start->property_fill_color_rgba() = RGBA_TO_UINT(1, 1, 1, 0); //ARDOUR_UI::config()->canvasvar_TrimHandle.get();
			frame_handle_end->property_fill_color_rgba() = RGBA_TO_UINT(1, 1, 1, 0); //ARDOUR_UI::config()->canvasvar_TrimHandle.get();
		}
	}
}

/** @return the samples per unit of this item */
double
TimeAxisViewItem::get_samples_per_unit()
{
	return samples_per_unit;
}

/**
 * Set the samples per unit of this item.
 * This item is used to determine the relative visual size and position of this item
 * based upon its duration and start value.
 *
 * @param spu the new samples per unit value
 */
void
TimeAxisViewItem::set_samples_per_unit (double spu)
{
	samples_per_unit = spu;
	set_position (this->get_position(), this);
	reset_width_dependent_items ((double)get_duration() / samples_per_unit);
}

void
TimeAxisViewItem::reset_width_dependent_items (double pixel_width)
{
	if (pixel_width < GRAB_HANDLE_LENGTH * 2) {

		if (frame_handle_start) {
			frame_handle_start->hide();
			frame_handle_end->hide();
		}

	}

	if (pixel_width < 2.0) {

		if (show_vestigial) {
			vestigial_frame->show();
		}

		if (name_highlight) {
			name_highlight->hide();
		}

		if (frame) {
			frame->hide();
		}

		if (frame_handle_start) {
			frame_handle_start->hide();
			frame_handle_end->hide();
		}

		wide_enough_for_name = false;

	} else {
		vestigial_frame->hide();

		if (name_highlight) {

			if (_height < NAME_HIGHLIGHT_THRESH) {
				name_highlight->hide();
				high_enough_for_name = false;
			} else {
				name_highlight->show();
				if (!get_item_name().empty()) {
					reset_name_width (pixel_width);
				}
				high_enough_for_name = true;
			}
                        
			if (visibility & FullWidthNameHighlight) {
				name_highlight->property_x2() = pixel_width;
			} else {
				name_highlight->property_x2() = pixel_width - 1.0;
			}

		}

		if (frame) {
			frame->show();
			frame->property_x2() = pixel_width;
		}

		if (frame_handle_start) {
			if (pixel_width < (2*TimeAxisViewItem::GRAB_HANDLE_LENGTH)) {
				frame_handle_start->hide();
				frame_handle_end->hide();
			}
			frame_handle_start->show();
			frame_handle_end->property_x1() = pixel_width - (TimeAxisViewItem::GRAB_HANDLE_LENGTH);
			frame_handle_end->show();
			frame_handle_end->property_x2() = pixel_width;
		}
	}

        update_name_pixbuf_visibility ();
}

void
TimeAxisViewItem::reset_name_width (double /*pixel_width*/)
{
	uint32_t it_width;
	int pb_width;
	bool pixbuf_holds_full_name;

	if (!name_pixbuf) {
		return;
	}

	it_width = trackview.editor().frame_to_pixel(item_duration);
	pb_width = name_pixbuf_width;

	pixbuf_holds_full_name = last_item_width > pb_width + NAME_X_OFFSET;
	last_item_width = it_width;

	if (pixbuf_holds_full_name && (it_width >= pb_width + NAME_X_OFFSET)) {
		/*
		  we've previously had the full name length showing 
		  and its still showing.
		*/
		return;
	}
	
	if (pb_width > it_width - NAME_X_OFFSET) {
		pb_width = it_width - NAME_X_OFFSET;
	}
	
	if (it_width <= NAME_X_OFFSET) {
		wide_enough_for_name = false;
	} else {
		wide_enough_for_name = true;
	}

	update_name_pixbuf_visibility ();
	if (pb_width > 0) {
		name_pixbuf->property_pixbuf() = pixbuf_from_ustring(item_name, NAME_FONT, pb_width, NAME_HEIGHT, Gdk::Color ("#000000"));
	}
}

/**
 * Callback used to remove this time axis item during the gtk idle loop.
 * This is used to avoid deleting the obejct while inside the remove_this_item
 * method.
 *
 * @param item the TimeAxisViewItem to remove.
 * @param src the identity of the object that initiated the change.
 */
gint
TimeAxisViewItem::idle_remove_this_item(TimeAxisViewItem* item, void* src)
{
	item->ItemRemoved (item->get_item_name(), src); /* EMIT_SIGNAL */
	delete item;
	item = 0;
	return false;
}

void
TimeAxisViewItem::set_y (double y)
{
	double const old = group->property_y ();
	if (y != old) {
		group->move (0, y - old);
	}
}

void
TimeAxisViewItem::update_name_pixbuf_visibility ()
{
	if (!name_pixbuf) {
		return;
	}
	
	if (wide_enough_for_name && high_enough_for_name) {
		name_pixbuf->show ();
	} else {
		name_pixbuf->hide ();
	}
}

