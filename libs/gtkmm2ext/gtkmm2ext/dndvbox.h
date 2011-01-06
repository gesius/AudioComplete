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

#include <gtkmm/box.h>

namespace Gtkmm2ext {

/** Parent class for children of a DnDVBox */	
class DnDVBoxChild
{
public:
	virtual ~DnDVBoxChild () {}
	
	/** @return The widget that is to be put into the DnDVBox */
	virtual Gtk::Widget& widget () = 0;
	
	/** @return An EventBox containing the widget that should be used for selection, dragging etc. */
	virtual Gtk::EventBox& action_widget () = 0;

	/** @return Text to use in the icon that is dragged */
	virtual std::string drag_text () const = 0;
};

/** A VBox whose contents can be dragged and dropped */
template <class T>
class DnDVBox : public Gtk::EventBox
{
public:
	DnDVBox () : _active (0), _drag_icon (0), _expecting_unwanted_button_event (false), _drag_placeholder (0)
	{
		_targets.push_back (Gtk::TargetEntry ("processor"));

		add (_internal_vbox);
		add_events (
			Gdk::KEY_PRESS_MASK | Gdk::KEY_RELEASE_MASK |
			Gdk::ENTER_NOTIFY_MASK | Gdk::LEAVE_NOTIFY_MASK |
			Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK
			);

		signal_button_press_event().connect (bind (mem_fun (*this, &DnDVBox::button_press), (T *) 0));
		signal_button_release_event().connect (bind (mem_fun (*this, &DnDVBox::button_release), (T *) 0));
		signal_drag_motion().connect (mem_fun (*this, &DnDVBox::drag_motion));
		signal_drag_leave().connect (mem_fun (*this, &DnDVBox::drag_leave));
		
		drag_dest_set (_targets);
		signal_drag_data_received().connect (mem_fun (*this, &DnDVBox::drag_data_received));
	}
	
	virtual ~DnDVBox ()
	{
		clear ();
		
		delete _drag_icon;
	}

	/** Add a child at the end of the widget.  The DnDVBox will take responsibility for deleting the child */
	void add_child (T* child)
	{
		child->action_widget().drag_source_set (_targets);
		child->action_widget().signal_drag_begin().connect (sigc::bind (mem_fun (*this, &DnDVBox::drag_begin), child));
		child->action_widget().signal_drag_data_get().connect (sigc::bind (mem_fun (*this, &DnDVBox::drag_data_get), child));
		child->action_widget().signal_drag_end().connect (sigc::bind (mem_fun (*this, &DnDVBox::drag_end), child));
		child->action_widget().signal_button_press_event().connect (sigc::bind (mem_fun (*this, &DnDVBox::button_press), child));
		child->action_widget().signal_button_release_event().connect (sigc::bind (mem_fun (*this, &DnDVBox::button_release), child));
		
		_internal_vbox.pack_start (child->widget(), false, false);
		
		_children.push_back (child);
		child->widget().show_all ();
	}

	/** @return Children, sorted into the order that they are currently being displayed in the widget */
	std::list<T*> children ()
	{
		std::list<T*> sorted_children;

		std::list<Gtk::Widget*> widget_children = _internal_vbox.get_children ();
		for (std::list<Gtk::Widget*>::iterator i = widget_children.begin(); i != widget_children.end(); ++i) {
			T* c = child_from_widget (*i);

			if (c) {
				sorted_children.push_back (c);
			}
		}

		return sorted_children;
	}

	/** @return Selected children */
	std::list<T*> selection () const {
		return _selection;
	}

	/** Set the `active' child; this is simply a child which is set to have the Gtk
	 *  STATE_ACTIVE for whatever purposes the client may have.
	 *  @param c Child, or 0 for none.
	 */
	void set_active (T* c) {
		T* old_active = _active;
		_active = c;
		if (old_active) {
			setup_child_state (old_active);
		}
		if (_active) {
			setup_child_state (_active);
		}
	}

	/** @param Child
	 *  @return true if the child is selected, otherwise false.
	 */
	bool selected (T* child) const {
		return (find (_selection.begin(), _selection.end(), child) != _selection.end());
	}

	/** Clear all children from the widget */
	void clear ()
	{
		_selection.clear ();

		for (typename std::list<T*>::iterator i = _children.begin(); i != _children.end(); ++i) {
			_internal_vbox.remove ((*i)->widget());
			delete *i;
		}

		_children.clear ();
		_active = 0;
	}

	void select_all ()
	{
		clear_selection ();
		for (typename std::list<T*>::iterator i = _children.begin(); i != _children.end(); ++i) {
			add_to_selection (*i);
		}

		SelectionChanged (); /* EMIT SIGNAL */
	}

	void select_none ()
	{
		clear_selection ();

		SelectionChanged (); /* EMIT SIGNAL */
	}

	/** Look at a y coordinate and find the children below y, and the ones either side.
	 *  @param y y position.
	 *  @param before Filled in with the child before, or 0.
	 *  @param at Filled in with the child under y, or 0.
	 *  @param after Filled in with the child after, or 0.
	 *  @return Fractional position in terms of child height, or -1 if not over a child.
	 */
	double get_children_around_position (int y, T** before, T** at, T** after) const
	{
		if (_children.empty()) {
			*before = *at = *after = 0;
			return -1;
		}

		*before = 0;

		typename std::list<T*>::const_iterator j = _children.begin ();

		/* index of current child */
		int i = 0;
		/* top of current child */
		double top = 0;
		/* bottom of current child */
		double bottom = (*j)->widget().get_allocation().get_height ();

		while (y >= bottom && j != _children.end()) {

			top = bottom;
			
			*before = *j;
			++i;
			++j;

			if (j != _children.end()) {
				bottom += (*j)->widget().get_allocation().get_height ();
			}
		}

		if (j == _children.end()) {
			*at = 0;
			*after = 0;
			return -1;
		}

		*at = *j;

		++j;
		*after = j != _children.end() ? *j : 0;

		return i + ((y - top) / (*at)->widget().get_allocation().get_height());
	}

	/** @param y y coordinate.
	 *  @return Pair consisting of the child under y (or 0) and the (fractional) index of the child under y (or -1)
	 */
	std::pair<T*, double> get_child_at_position (int y) const
	{
		T* before;
		T* after;

		std::pair<T*, double> r;
		
		r.second = get_children_around_position (y, &before, &r.first, &after);

		return r;
	}

	void set_spacing (int s) {
		_internal_vbox.set_spacing (s);
	}
 
	/** Children have been reordered by a drag */
	sigc::signal<void> Reordered;

	/** A button has been pressed over the widget */
	sigc::signal<bool, GdkEventButton*, T*> ButtonPress;

	/** A button has been release over the widget */
	sigc::signal<bool, GdkEventButton*, T*> ButtonRelease;

	/** A child has been dropped onto this DnDVBox from another one;
	 *  Parameters are the source DnDVBox, our child which the other one was dropped on (or 0) and the DragContext.
	 */
	sigc::signal<void, DnDVBox*, T*, Glib::RefPtr<Gdk::DragContext> const & > DropFromAnotherBox;
	sigc::signal<void> SelectionChanged;

private:
	void drag_begin (Glib::RefPtr<Gdk::DragContext> const & context, T* child)
	{
		_drag_child = child;
		
		/* make up an icon for the drag */
		_drag_icon = new Gtk::Window (Gtk::WINDOW_POPUP);
		
		Gtk::Allocation a = child->widget().get_allocation ();
		_drag_icon->set_size_request (a.get_width(), a.get_height());
		
		_drag_icon->signal_expose_event().connect (sigc::mem_fun (*this, &DnDVBox::icon_expose));
		_drag_icon->set_name (get_name ());

		/* make the icon transparent if possible */
		Glib::RefPtr<Gdk::Screen const> s = _drag_icon->get_screen ();
		Glib::RefPtr<Gdk::Colormap const> c = s->get_rgba_colormap ();
		if (c) {
			_drag_icon->set_colormap (c);
			_have_alpha = true;
		} else {
			_have_alpha = false;
		}

		int w, h;
		_drag_icon->get_size (w, h);
		_drag_icon->drag_set_as_icon (context, w / 2, h / 2);
		
		_drag_source = this;
	}

	/* Draw the drag icon */
	bool icon_expose (GdkEventExpose* ev)
	{
		/* Just grab the child's widget and use that */

		int w, h;
		_drag_icon->get_size (w, h);

		cairo_t* cr = gdk_cairo_create (_drag_icon->get_window()->gobj ());

		Glib::RefPtr<Gdk::Pixmap> p = _drag_child->widget().get_snapshot();
		gdk_cairo_set_source_pixmap (cr, p->gobj(), 0, 0);
		cairo_rectangle (cr, 0, 0, w, h);
		cairo_fill (cr);
		cairo_destroy (cr);
		
		return false;
	}
	
	void drag_data_get (Glib::RefPtr<Gdk::DragContext> const &, Gtk::SelectionData & selection_data, guint, guint, T* child)
	{
		selection_data.set (selection_data.get_target(), 8, (const guchar *) &child, sizeof (&child));
	}
	
	void drag_data_received (
		Glib::RefPtr<Gdk::DragContext> const & context, int x, int y, Gtk::SelectionData const & selection_data, guint /*info*/, guint time
		)
	{
		/* work out where it was dropped */
		std::pair<T*, double> const drop = get_child_at_position (y);
		
		if (_drag_source == this) {

			/* dropped from ourselves onto ourselves */
			
			T* child = *((T **) selection_data.get_data());
			
			/* reorder child widgets accordingly */
			if (drop.first == 0) {
				_internal_vbox.reorder_child (child->widget(), -1);
			} else {
				_internal_vbox.reorder_child (child->widget(), int (drop.second));
			}
			
		} else {
			
			/* drag started in another DnDVBox; raise a signal to say what happened */
			
			std::list<T*> dropped = _drag_source->selection ();
			DropFromAnotherBox (_drag_source, drop.first, context);
		}
		
		context->drag_finish (false, false, time);
	}
	
	void drag_end (Glib::RefPtr<Gdk::DragContext> const &, T *)
	{
		delete _drag_icon;
		_drag_icon = 0;
		
		_drag_child = 0;
		remove_drag_placeholder ();

		Reordered (); /* EMIT SIGNAL */
	}

	void remove_drag_placeholder ()
	{
		if (_drag_placeholder) {
			_internal_vbox.remove (*_drag_placeholder);
			_drag_placeholder = 0;
		}
	}

	bool drag_motion (Glib::RefPtr<Gdk::DragContext> const &, int x, int y, guint)
	{
		if (_children.empty ()) {
			return false;
		}

		T* before;
		T* at;
		T* after;

		/* decide where we currently are */
		double const c = get_children_around_position (y, &before, &at, &after);

		/* whether we're in the top or bottom half of the child that we're over */
		bool top_half = (c - int (c)) < 0.5;

		if (top_half && (before == _drag_child || at == _drag_child)) {
			/* dropping here would have no effect, so remove the visual cue */
			remove_drag_placeholder ();
			return false;
		}

		if (!top_half && (at == _drag_child || after == _drag_child)) {
			/* dropping here would have no effect, so remove the visual cue */
			remove_drag_placeholder ();
			return false;
		}

		/* the index that the placeholder should be put at */
		int const n = int (c + 0.5);
		
		if (_drag_placeholder == 0) {
			_drag_placeholder = manage (new Gtk::Label (""));
			_internal_vbox.pack_start (*_drag_placeholder, false, false);
			_drag_placeholder->show ();
		}

		if (c < 0) {
			_internal_vbox.reorder_child (*_drag_placeholder, -1);
		} else {
			_internal_vbox.reorder_child (*_drag_placeholder, n);
		}

		return false;
	}

	void drag_leave (Glib::RefPtr<Gdk::DragContext> const &, guint)
	{
		remove_drag_placeholder ();
	}

	bool button_press (GdkEventButton* ev, T* child)
	{
		if (_expecting_unwanted_button_event == true && child == 0) {
			_expecting_unwanted_button_event = false;
			return true;
		}

		if (child) {
			_expecting_unwanted_button_event = true;
		}
			
		if (ev->button == 1 || ev->button == 3) {

			if (!selected (child)) {

				if ((ev->state & Gdk::SHIFT_MASK) && !_selection.empty()) {

					/* Shift-click; select all between the clicked child and any existing selections */

					bool selecting = false;
					bool done = false;
					for (typename std::list<T*>::const_iterator i = _children.begin(); i != _children.end(); ++i) {

						bool const was_selected = selected (*i);

						if (selecting && !was_selected) {
							add_to_selection (*i);
						}
						
						if (!selecting && !done) {
							if (selected (*i)) {
								selecting = true;
							} else if (*i == child) {
								selecting = true;
								add_to_selection (child);
							}
						} else if (selecting) {
							if (was_selected || *i == child) {
								selecting = false;
								done = true;
							}
						}
					}

				} else {
						
					if ((ev->state & Gdk::CONTROL_MASK) == 0) {
						clear_selection ();
					}
					
					if (child) {
						add_to_selection (child);
					}

				}
				
				SelectionChanged (); /* EMIT SIGNAL */
				
			} else {
				/* XXX THIS NEEDS GENERALIZING FOR OS X */
				if (ev->button == 1 && (ev->state & Gdk::CONTROL_MASK)) {
					if (child && selected (child)) {
						remove_from_selection (child);
						SelectionChanged (); /* EMIT SIGNAL */
					}
				}
			}
		}


		return ButtonPress (ev, child); /* EMIT SIGNAL */
	}
	
	bool button_release (GdkEventButton* ev, T* child)
	{
		if (_expecting_unwanted_button_event == true && child == 0) {
			_expecting_unwanted_button_event = false;
			return true;
		}

		if (child) {
			_expecting_unwanted_button_event = true;
		}

		return ButtonRelease (ev, child); /* EMIT SIGNAL */
	}

	/** Setup a child's GTK state correctly */
	void setup_child_state (T* c)
	{
		assert (c);
		
		if (c == _active) {
			c->action_widget().set_state (Gtk::STATE_ACTIVE);
		} else if (selected (c)) {
			c->action_widget().set_state (Gtk::STATE_SELECTED);
		} else {
			c->action_widget().set_state (Gtk::STATE_NORMAL);
		}
	}

	void clear_selection ()
	{
		std::list<T*> old_selection = _selection;
		_selection.clear ();
		for (typename std::list<T*>::iterator i = old_selection.begin(); i != old_selection.end(); ++i) {
			setup_child_state (*i);
		}
	}
	
	void add_to_selection (T* child)
	{
		_selection.push_back (child);
		setup_child_state (child);
	}

	void remove_from_selection (T* child)
	{
		typename std::list<T*>::iterator x = find (_selection.begin(), _selection.end(), child);
		if (x != _selection.end()) {
			_selection.erase (x);
			setup_child_state (*x);
		}
	}
		
	T* child_from_widget (Gtk::Widget const * w) const
	{
		typename std::list<T*>::const_iterator i = _children.begin();
		while (i != _children.end() && &(*i)->widget() != w) {
			++i;
		}
		
		if (i == _children.end()) {
			return 0;
		}

		return *i;
	}
	
	Gtk::VBox _internal_vbox;
	std::list<Gtk::TargetEntry> _targets;
	std::list<T*> _children;
	std::list<T*> _selection;
	T* _active;
	Gtk::Window* _drag_icon;
	bool _expecting_unwanted_button_event;
	/** A blank label used as a placeholder to indicate where a dragged item would
	 *  go if it were dropped now.
	 */
	Gtk::Label* _drag_placeholder;
	/** Our child being dragged, or 0 */
	T* _drag_child;
	/** true if we can have an alpha-blended drag icon */
	bool _have_alpha;
	
	static DnDVBox* _drag_source;
	
};

template <class T>
DnDVBox<T>* DnDVBox<T>::_drag_source = 0;
	
}
