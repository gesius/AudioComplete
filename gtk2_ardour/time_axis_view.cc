/*
    Copyright (C) 2000 Paul Davis

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

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <list>

#include <libgnomecanvasmm.h>
#include <libgnomecanvasmm/canvas.h>
#include <libgnomecanvasmm/item.h>

#include "pbd/error.h"
#include "pbd/convert.h"

#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/selector.h>

#include "ardour/session.h"
#include "ardour/utils.h"
#include "ardour/ladspa_plugin.h"
#include "ardour/processor.h"
#include "ardour/location.h"

#include "ardour_ui.h"
#include "gui_thread.h"
#include "public_editor.h"
#include "time_axis_view.h"
#include "region_view.h"
#include "ghostregion.h"
#include "simplerect.h"
#include "simpleline.h"
#include "selection.h"
#include "keyboard.h"
#include "rgb_macros.h"
#include "utils.h"
#include "streamview.h"
#include "editor_drag.h"

#include "i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gdk;
using namespace ARDOUR;
using namespace PBD;
using namespace Editing;
using namespace ArdourCanvas;
using Gtkmm2ext::Keyboard;

const double trim_handle_size = 6.0; /* pixels */
uint32_t TimeAxisView::extra_height;
uint32_t TimeAxisView::smaller_height;
int const TimeAxisView::_max_order = 512;
PBD::Signal1<void,TimeAxisView*> TimeAxisView::CatchDeletion;

TimeAxisView::TimeAxisView (ARDOUR::Session* sess, PublicEditor& ed, TimeAxisView* rent, Canvas& /*canvas*/)
	: AxisView (sess),
	  controls_table (2, 8),
	  _y_position (0),
	  _editor (ed),
	  _order (0)
{
	if (extra_height == 0) {
		compute_heights ();
	}
	
	_canvas_background = new Group (*ed.get_background_group (), 0.0, 0.0);
	_canvas_display = new Group (*ed.get_trackview_group (), 0.0, 0.0);
	_canvas_display->hide(); // reveal as needed 

	selection_group = new Group (*_canvas_display);
	selection_group->set_data (X_("timeselection"), (void *) 1);
	selection_group->hide();

	_ghost_group = new Group (*_canvas_display);
	_ghost_group->lower_to_bottom();
	_ghost_group->show();

	control_parent = 0;
	display_menu = 0;
	_hidden = false;
	in_destructor = false;
	height = 0;
	_effective_height = 0;
	parent = rent;
	_has_state = false;
	last_name_entry_key_press_event = 0;
	name_packing = NamePackingBits (0);
	_resize_drag_start = -1;

	/*
	  Create the standard LHS Controls
	  We create the top-level container and name add the name label here,
	  subclasses can add to the layout as required
	*/

	name_entry.set_name ("EditorTrackNameDisplay");
	name_entry.signal_button_release_event().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_button_release));
	name_entry.signal_button_press_event().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_button_press));
	name_entry.signal_key_release_event().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_key_release));
	name_entry.signal_activate().connect (sigc::mem_fun(*this, &TimeAxisView::name_entry_activated));
	name_entry.signal_focus_in_event().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_focus_in));
	name_entry.signal_focus_out_event().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_focus_out));
	Gtkmm2ext::set_size_request_to_display_given_text (name_entry, N_("gTortnam"), 10, 10); // just represents a short name

	name_label.set_name ("TrackLabel");
	name_label.set_alignment (0.0, 0.5);

	/* typically, either name_label OR name_entry are visible,
	   but not both. its up to derived classes to show/hide them as they
	   wish.
	*/

	name_hbox.show ();

	controls_table.set_size_request (200);
	controls_table.set_border_width (2);
	controls_table.set_row_spacings (0);
	controls_table.set_col_spacings (0);
	controls_table.set_homogeneous (true);

	controls_table.attach (name_hbox, 0, 5, 0, 1,  Gtk::FILL|Gtk::EXPAND,  Gtk::FILL|Gtk::EXPAND, 3, 0);
	controls_table.show_all ();
	controls_table.set_no_show_all ();

	resizer.set_size_request (10, 6);
	resizer.set_name ("ResizeHandle");
	resizer.signal_expose_event().connect (sigc::mem_fun (*this, &TimeAxisView::resizer_expose));
	resizer.signal_button_press_event().connect (sigc::mem_fun (*this, &TimeAxisView::resizer_button_press));
	resizer.signal_button_release_event().connect (sigc::mem_fun (*this, &TimeAxisView::resizer_button_release));
	resizer.signal_motion_notify_event().connect (sigc::mem_fun (*this, &TimeAxisView::resizer_motion));

	resizer.set_events (Gdk::BUTTON_PRESS_MASK|
			Gdk::BUTTON_RELEASE_MASK|
			Gdk::POINTER_MOTION_MASK|
			Gdk::SCROLL_MASK);

	resizer_box.pack_start (resizer, false, false);
	resizer.show ();
	resizer_box.show();

	HSeparator* separator = manage (new HSeparator());

	controls_vbox.pack_start (controls_table, false, false);
	controls_vbox.pack_end (resizer_box, false, false);
	controls_vbox.show ();

	//controls_ebox.set_name ("TimeAxisViewControlsBaseUnselected");
	controls_ebox.add (controls_vbox);
	controls_ebox.add_events (BUTTON_PRESS_MASK|BUTTON_RELEASE_MASK|SCROLL_MASK);
	controls_ebox.set_flags (CAN_FOCUS);

	controls_ebox.signal_button_release_event().connect (sigc::mem_fun (*this, &TimeAxisView::controls_ebox_button_release));
	controls_ebox.signal_scroll_event().connect (sigc::mem_fun (*this, &TimeAxisView::controls_ebox_scroll), true);

	controls_hbox.pack_start (controls_ebox, false, false);
	controls_hbox.show ();

	time_axis_vbox.pack_start (controls_hbox, true, true);
	time_axis_vbox.pack_end (*separator, false, false);
	time_axis_vbox.show();

	ColorsChanged.connect (sigc::mem_fun (*this, &TimeAxisView::color_handler));

	GhostRegion::CatchDeletion.connect (*this, invalidator (*this), ui_bind (&TimeAxisView::erase_ghost, this, _1), gui_context());
}

TimeAxisView::~TimeAxisView()
{
	in_destructor = true;

	for (list<GhostRegion*>::iterator i = ghosts.begin(); i != ghosts.end(); ++i) {
		delete *i;
	}

	for (list<SelectionRect*>::iterator i = free_selection_rects.begin(); i != free_selection_rects.end(); ++i) {
		delete (*i)->rect;
		delete (*i)->start_trim;
		delete (*i)->end_trim;

	}

	for (list<SelectionRect*>::iterator i = used_selection_rects.begin(); i != used_selection_rects.end(); ++i) {
		delete (*i)->rect;
		delete (*i)->start_trim;
		delete (*i)->end_trim;
	}

	delete selection_group;
	selection_group = 0;

	delete _canvas_background;
	_canvas_background = 0;

	delete _canvas_display;
	_canvas_display = 0;

	delete display_menu;
	display_menu = 0;
}

/** Display this TimeAxisView as the nth component of the parent box, at y.
*
* @param y y position.
* @param nth index for this TimeAxisView, increased if this view has children.
* @param parent parent component.
* @return height of this TimeAxisView.
*/
guint32
TimeAxisView::show_at (double y, int& nth, VBox *parent)
{
	if (control_parent) {
		control_parent->reorder_child (time_axis_vbox, nth);
	} else {
		control_parent = parent;
		parent->pack_start (time_axis_vbox, false, false);
		parent->reorder_child (time_axis_vbox, nth);
	}

	_order = nth;

	if (_y_position != y) {
		_canvas_display->property_y () = y;
		_canvas_background->property_y () = y;
		/* silly canvas */
		_canvas_display->move (0.0, 0.0);
		_canvas_background->move (0.0, 0.0);
		_y_position = y;

	}

	_canvas_background->raise_to_top ();
	_canvas_display->raise_to_top ();

	if (_marked_for_display) {
		time_axis_vbox.show ();
		controls_ebox.show ();
		_canvas_background->show ();
	}

	_hidden = false;

	_effective_height = current_height ();

	/* now show children */

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		if (canvas_item_visible ((*i)->_canvas_display)) {
			++nth;
			_effective_height += (*i)->show_at (y + _effective_height, nth, parent);
		} 
	}

	return _effective_height;
}

void
TimeAxisView::clip_to_viewport ()
{
	if (_marked_for_display) {
		if (_y_position + _effective_height < _editor.get_trackview_group_vertical_offset () || _y_position > _editor.get_trackview_group_vertical_offset () + _canvas_display->get_canvas()->get_height()) {
			_canvas_background->hide ();
			_canvas_display->hide ();
			return;
		}
		_canvas_background->show ();
		_canvas_display->show ();
	}
	return;
}

bool
TimeAxisView::controls_ebox_scroll (GdkEventScroll* ev)
{
	if (Keyboard::some_magic_widget_has_focus()) {
		return false;
	}

	switch (ev->direction) {
	case GDK_SCROLL_UP:
		if (Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)) {
			step_height (true);
			return true;
		} else if (Keyboard::no_modifiers_active (ev->state)) {
			_editor.scroll_tracks_up_line();
			return true;
		}
		break;

	case GDK_SCROLL_DOWN:
		if (Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)) {
			step_height (false);
			return true;
		} else if (Keyboard::no_modifiers_active (ev->state)) {
			_editor.scroll_tracks_down_line();
			return true;
		}
		break;

	default:
		/* no handling for left/right, yet */
		break;
	}

	return false;
}

bool
TimeAxisView::controls_ebox_button_release (GdkEventButton* ev)
{
	switch (ev->button) {
	case 1:
		selection_click (ev);
		break;

	case 3:
		popup_display_menu (ev->time);
		break;
	}

	return true;
}

void
TimeAxisView::selection_click (GdkEventButton* ev)
{
	Selection::Operation op = ArdourKeyboard::selection_type (ev->state);
	_editor.set_selected_track (*this, op, false);
}

void
TimeAxisView::hide ()
{
	if (_hidden) {
		return;
	}

	_canvas_display->hide ();
	_canvas_background->hide ();

	if (control_parent) {
		control_parent->remove (time_axis_vbox);
		control_parent = 0;
	}

	_y_position = -1;
	_hidden = true;

	/* now hide children */

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->hide ();
	}

	/* if its hidden, it cannot be selected */

	_editor.get_selection().remove (this);

	Hiding ();
}

void
TimeAxisView::step_height (bool bigger)
{
	static const uint32_t step = 25;

	if (bigger) {
		if (height == preset_height(HeightSmall)) {
			set_height (preset_height(HeightSmaller));
		} 
		else if (height == preset_height(HeightSmaller)) {
			set_height (preset_height(HeightNormal));
		} 
		else {
			set_height (height + step);
		}

	} else {
		if ( height == preset_height(HeightSmall)){
			return;
		}
		
		if (height <= preset_height (HeightSmaller) && height > preset_height (HeightSmall)) {
			set_height (preset_height(HeightSmall));
		}
		else if ( height <= preset_height (HeightNormal) && height > preset_height (HeightSmaller)){
			set_height (preset_height(HeightSmaller));
		}
		else {
			set_height (height - step);
		} 
	}
}

void
TimeAxisView::set_heights (uint32_t h)
{
	TrackSelection& ts (_editor.get_selection().tracks);

	for (TrackSelection::iterator i = ts.begin(); i != ts.end(); ++i) {
		(*i)->set_height (h);
	}
}

void
TimeAxisView::set_height (Height h)
{
	set_height (preset_height (h));
}

void
TimeAxisView::set_height (uint32_t h)
{
	if (h < preset_height (HeightSmall)) {
		h = preset_height (HeightSmall);
	}
	
	time_axis_vbox.property_height_request () = h;
	height = h;

	for (list<GhostRegion*>::iterator i = ghosts.begin(); i != ghosts.end(); ++i) {
		(*i)->set_height ();
	}

	if (canvas_item_visible (selection_group)) {
		/* resize the selection rect */
		show_selection (_editor.get_selection().time);
	}
}

bool
TimeAxisView::name_entry_key_release (GdkEventKey* ev)
{
	TrackViewList::iterator i;

	switch (ev->keyval) {
	case GDK_Escape:
		name_entry.select_region (0,0);
		controls_ebox.grab_focus ();
		name_entry_changed ();
		return true;

	/* Shift+Tab Keys Pressed. Note that for Shift+Tab, GDK actually
	 * generates a different ev->keyval, rather than setting
	 * ev->state.
	 */
	case GDK_ISO_Left_Tab:
	case GDK_Tab:
	{
		name_entry_changed ();
		TrackViewList const & allviews = _editor.get_track_views ();
		TrackViewList::const_iterator i = find (allviews.begin(), allviews.end(), this);
		if (ev->keyval == GDK_Tab) {
			if (i != allviews.end()) {
				do {
					if (++i == allviews.end()) {
						return true;
					}
				} while((*i)->hidden());
			}
		} else {
			if (i != allviews.begin()) {
				do {
					if (i == allviews.begin()) {
						return true;
					}
					--i;
				} while ((*i)->hidden());
			}
		}
		
		
		/* resize to show editable name display */
		
		if ((*i)->current_height() <= preset_height (HeightSmaller)) {
			(*i)->set_height (HeightSmaller);
		}
		
		(*i)->name_entry.grab_focus();
	}
	return true;
	
	case GDK_Up:
	case GDK_Down:
		name_entry_changed ();
		return true;

	default:
		break;
	}

#ifdef TIMEOUT_NAME_EDIT
	/* adapt the timeout to reflect the user's typing speed */

	guint32 name_entry_timeout;

	if (last_name_entry_key_press_event) {
		/* timeout is 1/2 second or 5 times their current inter-char typing speed */
		name_entry_timeout = std::max (500U, (5 * (ev->time - last_name_entry_key_press_event)));
	} else {
		/* start with a 1 second timeout */
		name_entry_timeout = 1000;
	}

	last_name_entry_key_press_event = ev->time;

	/* wait 1 seconds and if no more keys are pressed, act as if they pressed enter */

	name_entry_key_timeout.disconnect();
	name_entry_key_timeout = Glib::signal_timeout().connect (sigc::mem_fun (*this, &TimeAxisView::name_entry_key_timed_out), name_entry_timeout);
#endif

	return false;
}

bool
TimeAxisView::name_entry_focus_in (GdkEventFocus*)
{
	name_entry.select_region (0, -1);
	name_entry.set_name ("EditorActiveTrackNameDisplay");
	return false;
}

bool
TimeAxisView::name_entry_focus_out (GdkEventFocus*)
{
	/* clean up */

	last_name_entry_key_press_event = 0;
	name_entry_key_timeout.disconnect ();
	name_entry.set_name ("EditorTrackNameDisplay");
	name_entry.select_region (0,0);

	/* do the real stuff */

	name_entry_changed ();

	return false;
}

bool
TimeAxisView::name_entry_key_timed_out ()
{
	name_entry_activated();
	return false;
}

void
TimeAxisView::name_entry_activated ()
{
	controls_ebox.grab_focus();
}

void
TimeAxisView::name_entry_changed ()
{
}

bool
TimeAxisView::name_entry_button_press (GdkEventButton *ev)
{
	if (ev->button == 3) {
		return true;
	}
	return false;
}

bool
TimeAxisView::name_entry_button_release (GdkEventButton *ev)
{
	if (ev->button == 3) {
		popup_display_menu (ev->time);
		return true;
	}
	return false;
}

void
TimeAxisView::conditionally_add_to_selection ()
{
	Selection& s (_editor.get_selection ());

	if (!s.selected (this)) {
		_editor.set_selected_track (*this, Selection::Set);
	}
}

void
TimeAxisView::popup_display_menu (guint32 when)
{
	if (display_menu == 0) {
		build_display_menu ();
	}

	conditionally_add_to_selection ();
	display_menu->popup (1, when);
}

void
TimeAxisView::set_selected (bool yn)
{
	if (yn == _selected) {
		return;
	}

	Selectable::set_selected (yn);

	if (_selected) {
		controls_ebox.set_name (controls_base_selected_name);
		time_axis_vbox.set_name (controls_base_selected_name);
		controls_vbox.set_name (controls_base_selected_name);
	} else {
		controls_ebox.set_name (controls_base_unselected_name);
		time_axis_vbox.set_name (controls_base_unselected_name);
		controls_vbox.set_name (controls_base_unselected_name);
		hide_selection ();

		/* children will be set for the yn=true case. but when deselecting
		   the editor only has a list of top-level trackviews, so we
		   have to do this here.
		*/

		for (Children::iterator i = children.begin(); i != children.end(); ++i) {
			(*i)->set_selected (false);
		}
	}

	resizer.queue_draw ();
}

void
TimeAxisView::build_display_menu ()
{
	using namespace Menu_Helpers;

	delete display_menu;

	display_menu = new Menu;
	display_menu->set_name ("ArdourContextMenu");

	// Just let implementing classes define what goes into the manu
}

void
TimeAxisView::set_samples_per_unit (double spu)
{
	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->set_samples_per_unit (spu);
	}

	AnalysisFeatureList::const_iterator i;
	list<ArdourCanvas::SimpleLine*>::iterator l;
}

void
TimeAxisView::show_timestretch (nframes_t start, nframes_t end)
{
	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->show_timestretch (start, end);
	}
}

void
TimeAxisView::hide_timestretch ()
{
	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->hide_timestretch ();
	}
}

void
TimeAxisView::show_selection (TimeSelection& ts)
{
	double x1;
	double x2;
	double y2;
	SelectionRect *rect;

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->show_selection (ts);
	}

	if (canvas_item_visible (selection_group)) {
		while (!used_selection_rects.empty()) {
			free_selection_rects.push_front (used_selection_rects.front());
			used_selection_rects.pop_front();
			free_selection_rects.front()->rect->hide();
			free_selection_rects.front()->start_trim->hide();
			free_selection_rects.front()->end_trim->hide();
		}
		selection_group->hide();
	}

	selection_group->show();
	selection_group->raise_to_top();

	for (list<AudioRange>::iterator i = ts.begin(); i != ts.end(); ++i) {
		nframes_t start, end, cnt;

		start = (*i).start;
		end = (*i).end;
		cnt = end - start + 1;

		rect = get_selection_rect ((*i).id);

		x1 = _editor.frame_to_unit (start);
		x2 = _editor.frame_to_unit (start + cnt - 1);
		y2 = current_height();

		rect->rect->property_x1() = x1;
		rect->rect->property_y1() = 1.0;
		rect->rect->property_x2() = x2;
		rect->rect->property_y2() = y2;

		// trim boxes are at the top for selections

		if (x2 > x1) {
			rect->start_trim->property_x1() = x1;
			rect->start_trim->property_y1() = 1.0;
			rect->start_trim->property_x2() = x1 + trim_handle_size;
			rect->start_trim->property_y2() = y2;

			rect->end_trim->property_x1() = x2 - trim_handle_size;
			rect->end_trim->property_y1() = 1.0;
			rect->end_trim->property_x2() = x2;
			rect->end_trim->property_y2() = y2;

			rect->start_trim->show();
			rect->end_trim->show();
		} else {
			rect->start_trim->hide();
			rect->end_trim->hide();
		}

		rect->rect->show ();
		used_selection_rects.push_back (rect);
	}
}

void
TimeAxisView::reshow_selection (TimeSelection& ts)
{
	show_selection (ts);

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->show_selection (ts);
	}
}

void
TimeAxisView::hide_selection ()
{
	if (canvas_item_visible (selection_group)) {
		while (!used_selection_rects.empty()) {
			free_selection_rects.push_front (used_selection_rects.front());
			used_selection_rects.pop_front();
			free_selection_rects.front()->rect->hide();
			free_selection_rects.front()->start_trim->hide();
			free_selection_rects.front()->end_trim->hide();
		}
		selection_group->hide();
	}

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->hide_selection ();
	}
}

void
TimeAxisView::order_selection_trims (ArdourCanvas::Item *item, bool put_start_on_top)
{
	/* find the selection rect this is for. we have the item corresponding to one
	   of the trim handles.
	 */

	for (list<SelectionRect*>::iterator i = used_selection_rects.begin(); i != used_selection_rects.end(); ++i) {
		if ((*i)->start_trim == item || (*i)->end_trim == item) {

			/* make one trim handle be "above" the other so that if they overlap,
			   the top one is the one last used.
			*/

			(*i)->rect->raise_to_top ();
			(put_start_on_top ? (*i)->start_trim : (*i)->end_trim)->raise_to_top ();
			(put_start_on_top ? (*i)->end_trim : (*i)->start_trim)->raise_to_top ();

			break;
		}
	}
}

SelectionRect *
TimeAxisView::get_selection_rect (uint32_t id)
{
	SelectionRect *rect;

	/* check to see if we already have a visible rect for this particular selection ID */

	for (list<SelectionRect*>::iterator i = used_selection_rects.begin(); i != used_selection_rects.end(); ++i) {
		if ((*i)->id == id) {
			return (*i);
		}
	}

	/* ditto for the free rect list */

	for (list<SelectionRect*>::iterator i = free_selection_rects.begin(); i != free_selection_rects.end(); ++i) {
		if ((*i)->id == id) {
			SelectionRect* ret = (*i);
			free_selection_rects.erase (i);
			return ret;
		}
	}

	/* no existing matching rect, so go get a new one from the free list, or create one if there are none */

	if (free_selection_rects.empty()) {

		rect = new SelectionRect;

		rect->rect = new SimpleRect (*selection_group);
		rect->rect->property_outline_what() = 0x0;
		rect->rect->property_x1() = 0.0;
		rect->rect->property_y1() = 0.0;
		rect->rect->property_x2() = 0.0;
		rect->rect->property_y2() = 0.0;
		rect->rect->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_SelectionRect.get();

		rect->start_trim = new SimpleRect (*selection_group);
		rect->start_trim->property_outline_what() = 0x0;
		rect->start_trim->property_x1() = 0.0;
		rect->start_trim->property_x2() = 0.0;
		
		rect->end_trim = new SimpleRect (*selection_group);
		rect->end_trim->property_outline_what() = 0x0;
		rect->end_trim->property_x1() = 0.0;
		rect->end_trim->property_x2() = 0.0;

		free_selection_rects.push_front (rect);

		rect->rect->signal_event().connect (sigc::bind (sigc::mem_fun (_editor, &PublicEditor::canvas_selection_rect_event), rect->rect, rect));
		rect->start_trim->signal_event().connect (sigc::bind (sigc::mem_fun (_editor, &PublicEditor::canvas_selection_start_trim_event), rect->rect, rect));
		rect->end_trim->signal_event().connect (sigc::bind (sigc::mem_fun (_editor, &PublicEditor::canvas_selection_end_trim_event), rect->rect, rect));
	}

	rect = free_selection_rects.front();
	rect->id = id;
	free_selection_rects.pop_front();
	return rect;
}

struct null_deleter { void operator()(void const *) const {} };

bool
TimeAxisView::is_child (TimeAxisView* tav)
{
	return find (children.begin(), children.end(), boost::shared_ptr<TimeAxisView>(tav, null_deleter())) != children.end();
}

void
TimeAxisView::add_child (boost::shared_ptr<TimeAxisView> child)
{
	children.push_back (child);
}

void
TimeAxisView::remove_child (boost::shared_ptr<TimeAxisView> child)
{
	Children::iterator i;

	if ((i = find (children.begin(), children.end(), child)) != children.end()) {
		children.erase (i);
	}
}

/** Get selectable things within a given range.
 *  @param start Start time in session frames.
 *  @param end End time in session frames.
 *  @param top Top y range, in trackview coordinates (ie 0 is the top of the track view)
 *  @param bot Bottom y range, in trackview coordinates (ie 0 is the top of the track view)
 *  @param result Filled in with selectable things.
 */
void
TimeAxisView::get_selectables (framepos_t /*start*/, framepos_t /*end*/, double /*top*/, double /*bot*/, list<Selectable*>& /*result*/)
{
	return;
}

void
TimeAxisView::get_inverted_selectables (Selection& /*sel*/, list<Selectable*>& /*result*/)
{
	return;
}

void
TimeAxisView::add_ghost (RegionView* rv)
{
	GhostRegion* gr = rv->add_ghost (*this);

	if(gr) {
		ghosts.push_back(gr);
	}
}

void
TimeAxisView::remove_ghost (RegionView* rv) 
{
	rv->remove_ghost_in (*this);
}

void
TimeAxisView::erase_ghost (GhostRegion* gr) 
{
	if (in_destructor) {
		return;
	}
	
	for (list<GhostRegion*>::iterator i = ghosts.begin(); i != ghosts.end(); ++i) {
		if ((*i) == gr) {
			ghosts.erase (i);
			break;
		}
	}
}

bool
TimeAxisView::touched (double top, double bot)
{
	/* remember: this is X Window - coordinate space starts in upper left and moves down.
	  y_position is the "origin" or "top" of the track.
	*/

	double mybot = _y_position + current_height();

	return ((_y_position <= bot && _y_position >= top) ||
		((mybot <= bot) && (top < mybot)) ||
		(mybot >= bot && _y_position < top));
}

void
TimeAxisView::set_parent (TimeAxisView& p)
{
	parent = &p;
}

bool
TimeAxisView::has_state () const
{
	return _has_state;
}

TimeAxisView*
TimeAxisView::get_parent_with_state ()
{
	if (parent == 0) {
		return 0;
	}

	if (parent->has_state()) {
		return parent;
	}

	return parent->get_parent_with_state ();
}


XMLNode&
TimeAxisView::get_state ()
{
	/* XXX: is this method used? */
	
	XMLNode* node = new XMLNode ("TAV-" + name());
	char buf[32];

	snprintf (buf, sizeof(buf), "%u", height);
	node->add_property ("height", buf);
	node->add_property ("marked-for-display", (_marked_for_display ? "1" : "0"));
	return *node;
}

int
TimeAxisView::set_state (const XMLNode& node, int /*version*/)
{
	const XMLProperty *prop;

	/* XXX: I think this might be vestigial */
	if ((prop = node.property ("marked-for-display")) != 0) {
		_marked_for_display = (prop->value() == "1");
	}

	if ((prop = node.property ("shown-editor")) != 0) {
		_marked_for_display = string_is_affirmative (prop->value ());
	}

	if ((prop = node.property ("track-height")) != 0) {

		if (prop->value() == "largest") {
			set_height (HeightLargest);
		} else if (prop->value() == "large") {
			set_height (HeightLarge);
		} else if (prop->value() == "larger") {
			set_height (HeightLarger);
		} else if (prop->value() == "normal") {
			set_height (HeightNormal);
		} else if (prop->value() == "smaller") {
			set_height (HeightSmaller);
		} else if (prop->value() == "small") {
			set_height (HeightSmall);
		} else {
			error << string_compose(_("unknown track height name \"%1\" in XML GUI information"), prop->value()) << endmsg;
			set_height (HeightNormal);
		}

	} else if ((prop = node.property ("height")) != 0) {

		set_height (atoi (prop->value()));

	} else {

		set_height (HeightNormal);
	}

	return 0;
}

void
TimeAxisView::reset_height()
{
	set_height (height);

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		(*i)->set_height ((*i)->height);
	}
}

void
TimeAxisView::compute_heights ()
{
	Gtk::Window window (Gtk::WINDOW_TOPLEVEL);
	Gtk::Table two_row_table (2, 8);
	Gtk::Table one_row_table (1, 8);
	Button* buttons[5];
	const int border_width = 2;
	extra_height = (2 * border_width)
		//+ 2   // 2 pixels for the hseparator between TimeAxisView control areas
		+ 6; // resizer button (3 x 2 pixel elements + 2 x 2 pixel gaps)

	window.add (one_row_table);

	one_row_table.set_border_width (border_width);
	one_row_table.set_row_spacings (0);
	one_row_table.set_col_spacings (0);
	one_row_table.set_homogeneous (true);

	two_row_table.set_border_width (border_width);
	two_row_table.set_row_spacings (0);
	two_row_table.set_col_spacings (0);
	two_row_table.set_homogeneous (true);

	for (int i = 0; i < 5; ++i) {
		buttons[i] = manage (new Button (X_("f")));
		buttons[i]->set_name ("TrackMuteButton");
	}

	one_row_table.attach (*buttons[0], 6, 7, 0, 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

	one_row_table.show_all ();
	Gtk::Requisition req(one_row_table.size_request ());

	// height required to show 1 row of buttons

	smaller_height = req.height + extra_height;
}

void
TimeAxisView::show_name_label ()
{
	if (!(name_packing & NameLabelPacked)) {
		name_hbox.pack_start (name_label, true, true);
		name_packing = NamePackingBits (name_packing | NameLabelPacked);
		name_hbox.show ();
		name_label.show ();
	}
}

void
TimeAxisView::show_name_entry ()
{
	if (!(name_packing & NameEntryPacked)) {
		name_hbox.pack_start (name_entry, true, true);
		name_packing = NamePackingBits (name_packing | NameEntryPacked);
		name_hbox.show ();
		name_entry.show ();
	}
}

void
TimeAxisView::hide_name_label ()
{
	if (name_packing & NameLabelPacked) {
		name_hbox.remove (name_label);
		name_packing = NamePackingBits (name_packing & ~NameLabelPacked);
	}
}

void
TimeAxisView::hide_name_entry ()
{
	if (name_packing & NameEntryPacked) {
		name_hbox.remove (name_entry);
		name_packing = NamePackingBits (name_packing & ~NameEntryPacked);
	}
}

void
TimeAxisView::color_handler ()
{
	for (list<GhostRegion*>::iterator i=ghosts.begin(); i != ghosts.end(); i++ ) {
		(*i)->set_colors();
	}

	for (list<SelectionRect*>::iterator i = used_selection_rects.begin(); i != used_selection_rects.end(); ++i) {

		(*i)->rect->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_SelectionRect.get();
		(*i)->rect->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();

		(*i)->start_trim->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
		(*i)->start_trim->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();

		(*i)->end_trim->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
		(*i)->end_trim->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
	}

	for (list<SelectionRect*>::iterator i = free_selection_rects.begin(); i != free_selection_rects.end(); ++i) {

		(*i)->rect->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_SelectionRect.get();
		(*i)->rect->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();

		(*i)->start_trim->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
		(*i)->start_trim->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();

		(*i)->end_trim->property_fill_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
		(*i)->end_trim->property_outline_color_rgba() = ARDOUR_UI::config()->canvasvar_Selection.get();
	}
}

/** @return Pair: TimeAxisView, layer index.
 * TimeAxisView is non-0 if this object covers y, or one of its children does.
 * If the covering object is a child axis, then the child is returned.
 * TimeAxisView is 0 otherwise.
 * Layer index is the layer number if the TimeAxisView is valid and is in stacked
 * region display mode, otherwise 0.
 */
std::pair<TimeAxisView*, layer_t>
TimeAxisView::covers_y_position (double y)
{
	if (hidden()) {
		return std::make_pair ( (TimeAxisView *) 0, 0);
	}

	if (_y_position <= y && y < (_y_position + height)) {

		/* work out the layer index if appropriate */
		layer_t l = 0;
		if (layer_display () == Stacked && view ()) {
			/* compute layer */
			l = layer_t ((_y_position + height - y) / (view()->child_height ()));
			/* clamp to max layers to be on the safe side; sometimes the above calculation
			  returns a too-high value */
			if (l >= view()->layers ()) {
				l = view()->layers() - 1;
			}
		}

		return std::make_pair (this, l);
	}

	for (Children::const_iterator i = children.begin(); i != children.end(); ++i) {

		std::pair<TimeAxisView*, int> const r = (*i)->covers_y_position (y);
		if (r.first) {
			return r;
		}
	}

	return std::make_pair ( (TimeAxisView *) 0, 0);
}

bool
TimeAxisView::resizer_button_press (GdkEventButton* event)
{
	_resize_drag_start = event->y_root;
	return true;
}

bool
TimeAxisView::resizer_button_release (GdkEventButton*)
{
	_editor.stop_canvas_autoscroll ();
	_resize_drag_start = -1;
	return true;
}

void
TimeAxisView::idle_resize (uint32_t h)
{
	set_height (h);
}

bool
TimeAxisView::resizer_motion (GdkEventMotion* ev)
{
	if (_resize_drag_start >= 0) {
		/* (ab)use the DragManager to do autoscrolling; adjust the event coordinates
		   into the world coordinate space that DragManager::motion_handler is expecting,
		   and then fake a DragManager motion event so that when maybe_autoscroll
		   asks DragManager for the current pointer position it will get the correct
		   answers.
		*/
		int tx, ty;
		resizer.translate_coordinates (*control_parent, ev->x, ev->y, tx, ty);
		ev->y = ty - _editor.get_trackview_group_vertical_offset();
		_editor.drags()->motion_handler ((GdkEvent *) ev, false);
		_editor.maybe_autoscroll (false, true);

		/* now do the actual TAV resize */
                int32_t const delta = (int32_t) floor (ev->y_root - _resize_drag_start);
                _editor.add_to_idle_resize (this, delta);
                _resize_drag_start = ev->y_root;
        }

	return true;
}

bool
TimeAxisView::resizer_expose (GdkEventExpose* event)
{
	int w, h, x, y, d;
	Glib::RefPtr<Gdk::Window> win (resizer.get_window());
	Glib::RefPtr<Gdk::GC> dark (resizer.get_style()->get_fg_gc (STATE_NORMAL));
	Glib::RefPtr<Gdk::GC> light (resizer.get_style()->get_bg_gc (STATE_NORMAL));

	win->draw_rectangle (controls_ebox.get_style()->get_bg_gc(STATE_NORMAL),
			true,
			event->area.x,
			event->area.y,
			event->area.width,
			event->area.height);

	win->get_geometry (x, y, w, h, d);

	/* handle/line #1 */

	win->draw_line (dark, 0, 0, w - 2, 0);
	win->draw_point (dark, 0, 1);
	win->draw_line (light, 1, 1, w - 1, 1);
	win->draw_point (light, w - 1, 0);

	/* handle/line #2 */

	win->draw_line (dark, 0, 4, w - 2, 4);
	win->draw_point (dark, 0, 5);
	win->draw_line (light, 1, 5, w - 1, 5);
	win->draw_point (light, w - 1, 4);

	/* use vertical resize mouse cursor */
	win->set_cursor(Gdk::Cursor(Gdk::SB_V_DOUBLE_ARROW));

	return true;
}

bool
TimeAxisView::set_visibility (bool yn)
{
	if (yn != marked_for_display()) {
		if (yn) {
			set_marked_for_display (true);
			canvas_display()->show();
		} else {
			set_marked_for_display (false);
			hide ();
		}
		return true; // things changed
	}
	
	return false;
}

uint32_t
TimeAxisView::preset_height (Height h)
{
	switch (h) {
	case HeightLargest:
		return extra_height + 48 + 250;
	case HeightLarger:
		return extra_height + 48 + 150;
	case HeightLarge:
		return extra_height + 48 + 50;
	case HeightNormal:
		return extra_height + 48;
	case HeightSmall:
		return 27;
	case HeightSmaller:
		return smaller_height;
	}

	/* NOTREACHED */
	return 0;
}

/** @return Child time axis views that are not hidden */
TimeAxisView::Children
TimeAxisView::get_child_list ()
{
	Children c;

	for (Children::iterator i = children.begin(); i != children.end(); ++i) {
		if (!(*i)->hidden()) {
			c.push_back(*i);
		}
	}
	
	return c;
}


