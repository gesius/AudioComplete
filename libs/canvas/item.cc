/*
    Copyright (C) 2011-2013 Paul Davis
    Author: Carl Hetherington <cth@carlh.net>

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

#include "pbd/compose.h"
#include "pbd/stacktrace.h"
#include "pbd/convert.h"

#include "ardour/utils.h"

#include "canvas/group.h"
#include "canvas/item.h"
#include "canvas/canvas.h"
#include "canvas/debug.h"

using namespace std;
using namespace PBD;
using namespace ArdourCanvas;

Item::Item (Canvas* canvas)
	: _canvas (canvas)
	, _parent (0)
{
	init ();
}

Item::Item (Group* parent)
	: _canvas (parent->canvas ())
	, _parent (parent)
{
	init ();
}

Item::Item (Group* parent, Duple position)
	: _canvas (parent->canvas())
	, _parent (parent)
	, _position (position)
{
	init ();
}

void
Item::init ()
{
	_visible = true;
	_bounding_box_dirty = true;
	_ignore_events = false;
	
	if (_parent) {
		_parent->add (this);
	}

	DEBUG_TRACE (DEBUG::CanvasItems, string_compose ("new canvas item %1\n", this));
}	

Item::~Item ()
{
	if (_parent) {
		_parent->remove (this);
	}

	if (_canvas) {
		_canvas->item_going_away (this, _bounding_box);
	}
}

ArdourCanvas::Rect
Item::item_to_parent (ArdourCanvas::Rect const & r) const
{
	return r.translate (_position);
}

ArdourCanvas::Rect
Item::item_to_canvas (ArdourCanvas::Rect const & r) const
{
	Item const * i = this;
	Duple offset;

	while (i) {
		offset = offset.translate (i->position());
		i = i->parent();
	}

	return r.translate (offset);
}

ArdourCanvas::Duple
Item::item_to_canvas (ArdourCanvas::Duple const & d) const
{
	Item const * i = this;
	Duple offset;

	while (i) {
		offset = offset.translate (i->position());
		i = i->parent();
	}

	return d.translate (offset);
}

ArdourCanvas::Duple
Item::canvas_to_item (ArdourCanvas::Duple const & d) const
{
	Item const * i = this;
	Duple offset;

	while (i) {
		offset = offset.translate (-(i->position()));
		i = i->parent();
	}

	return d.translate (offset);
}

ArdourCanvas::Rect
Item::canvas_to_item (ArdourCanvas::Rect const & d) const
{
	Item const * i = this;
	Duple offset;

	while (i) {
		offset = offset.translate (-(i->position()));
		i = i->parent();
	}

	return d.translate (offset);
}

void
Item::item_to_canvas (Coord& x, Coord& y) const
{
	Duple d = item_to_canvas (Duple (x, y));
		
	x = d.x;
	y = d.y;
}

void
Item::canvas_to_item (Coord& x, Coord& y) const
{
	Duple d = canvas_to_item (Duple (x, y));

	x = d.x;
	y = d.y;
}

Duple
Item::item_to_window (ArdourCanvas::Duple const & d) const
{
	return _canvas->canvas_to_window (item_to_canvas (d));
}

Duple
Item::window_to_item (ArdourCanvas::Duple const & d) const
{
	return _canvas->window_to_canvas (canvas_to_item (d));
}

ArdourCanvas::Rect
Item::item_to_window (ArdourCanvas::Rect const & r) const
{
	return _canvas->canvas_to_window (item_to_canvas (r));
}

/** Set the position of this item in the parent's coordinates */
void
Item::set_position (Duple p)
{
	if (p == _position) {
		return;
	}

	boost::optional<ArdourCanvas::Rect> bbox = bounding_box ();
	boost::optional<ArdourCanvas::Rect> pre_change_parent_bounding_box;

	if (bbox) {
		/* see the comment in Canvas::item_moved() to understand
		 * why we use the parent's bounding box here.
		 */
		pre_change_parent_bounding_box = item_to_parent (bbox.get());
	}
	
	_position = p;

	_canvas->item_moved (this, pre_change_parent_bounding_box);

	if (_parent) {
		_parent->child_changed ();
	}
}

void
Item::set_x_position (Coord x)
{
	set_position (Duple (x, _position.y));
}

void
Item::set_y_position (Coord y)
{
	set_position (Duple (_position.x, y));
}

void
Item::raise_to_top ()
{
	assert (_parent);
	_parent->raise_child_to_top (this);
}

void
Item::raise (int levels)
{
	assert (_parent);
	_parent->raise_child (this, levels);
}

void
Item::lower_to_bottom ()
{
	assert (_parent);
	_parent->lower_child_to_bottom (this);
}

void
Item::hide ()
{
	if (_visible) {
		_visible = false;
		_canvas->item_shown_or_hidden (this);
	}
}

void
Item::show ()
{
	if (!_visible) {
		_visible = true;
		_canvas->item_shown_or_hidden (this);
	}
}

Duple
Item::item_to_parent (Duple const & d) const
{
	return d.translate (_position);
}

Duple
Item::parent_to_item (Duple const & d) const
{
	return d.translate (- _position);
}

ArdourCanvas::Rect
Item::parent_to_item (ArdourCanvas::Rect const & d) const
{
	return d.translate (- _position);
}

void
Item::unparent ()
{
	_parent = 0;
}

void
Item::reparent (Group* new_parent)
{
	assert (_canvas == _parent->canvas());

	if (_parent) {
		_parent->remove (this);
	}

	assert (new_parent);

	_parent = new_parent;
	_canvas = _parent->canvas ();
	_parent->add (this);
}

bool
Item::common_ancestor_within (uint32_t limit, const Item& other) const
{
	uint32_t d1 = depth();
	uint32_t d2 = other.depth();
	const Item* i1 = this;
	const Item* i2 = &other;
	
	/* move towards root until we are at the same level
	   for both items
	*/

	while (d1 != d2) {
		if (d1 > d2) {
			i1 = i1->parent();
			d1--;
			limit--;
		} else {
			i2 = i2->parent();
			d2--;
			limit--;
		}
		if (limit == 0) {
			return false;
		}
	}

	/* now see if there is a common parent */

	while (i1 != i2) {
		if (i1) {
			i1 = i1->parent();
		}
		if (i2) {
			i2 = i2->parent ();
		}

		limit--;
		if (limit == 0) {
			return false;
		}
	}
	
	return true;
}

const Item*
Item::closest_ancestor_with (const Item& other) const
{
	uint32_t d1 = depth();
	uint32_t d2 = other.depth();
	const Item* i1 = this;
	const Item* i2 = &other;

	/* move towards root until we are at the same level
	   for both items
	*/

	while (d1 != d2) {
		if (d1 > d2) {
			i1 = i1->parent();
			d1--;
		} else {
			i2 = i2->parent();
			d2--;
		}
	}

	/* now see if there is a common parent */

	while (i1 != i2) {
		if (i1) {
			i1 = i1->parent();
		}
		if (i2) {
			i2 = i2->parent ();
		}
	}
	
	return i1;
}

bool
Item::is_descendant_of (const Item& candidate) const
{
	Item const * i = _parent;

	while (i) {
		if (i == &candidate) {
			return true;
		}
		i = i->parent();
	}

	return false;
}

void
Item::grab_focus ()
{
	/* XXX */
}

/** @return Bounding box in this item's coordinates */
boost::optional<ArdourCanvas::Rect>
Item::bounding_box () const
{
	if (_bounding_box_dirty) {
		compute_bounding_box ();
	}

	assert (!_bounding_box_dirty);
	return _bounding_box;
}

Coord
Item::height () const 
{
	boost::optional<ArdourCanvas::Rect> bb  = bounding_box();

	if (bb) {
		return bb->height ();
	}
	return 0;
}

Coord
Item::width () const 
{
	boost::optional<ArdourCanvas::Rect> bb = bounding_box().get();

	if (bb) {
		return bb->width ();
	}

	return 0;
}

void
Item::redraw () const
{
	if (_visible && _bounding_box && _canvas) {
		_canvas->request_redraw (item_to_canvas (_bounding_box.get()));
	}
}	

void
Item::begin_change ()
{
	_pre_change_bounding_box = bounding_box ();
}

void
Item::end_change ()
{
	_canvas->item_changed (this, _pre_change_bounding_box);
	
	if (_parent) {
		_parent->child_changed ();
	}
}

void
Item::begin_visual_change ()
{
}

void
Item::end_visual_change ()
{
	_canvas->item_visual_property_changed (this);
}

void
Item::move (Duple movement)
{
	set_position (position() + movement);
}

void
Item::grab ()
{
	assert (_canvas);
	_canvas->grab (this);
}

void
Item::ungrab ()
{
	assert (_canvas);
	_canvas->ungrab ();
}

void
Item::set_data (string const & key, void* data)
{
	_data[key] = data;
}

void *
Item::get_data (string const & key) const
{
	map<string, void*>::const_iterator i = _data.find (key);
	if (i == _data.end ()) {
		return 0;
	}
	
	return i->second;
}

void
Item::set_ignore_events (bool ignore)
{
	_ignore_events = ignore;
}

void
Item::dump (ostream& o) const
{
	boost::optional<ArdourCanvas::Rect> bb = bounding_box();

	o << _canvas->indent() << whatami() << ' ' << this << " Visible ? " << _visible;
	o << " @ " << position();
	
#ifdef CANVAS_DEBUG
	if (!name.empty()) {
		o << ' ' << name;
	}
#endif

	if (bb) {
		o << endl << _canvas->indent() << "\tbbox: " << bb.get();
		o << endl << _canvas->indent() << "\tCANVAS bbox: " << item_to_canvas (bb.get());
	} else {
		o << " bbox unset";
	}

	o << endl;
}

std::string
Item::whatami () const 
{
	std::string type = demangle (typeid (*this).name());
	return type.substr (type.find_last_of (':') + 1);
}

uint32_t
Item::depth () const
{
	Item* i = _parent;
	int d = 0;
	while (i) {
		++d;
		i = i->parent();
	}
	return d;
}

bool
Item::covers (Duple const & point) const
{
	Duple p = canvas_to_item (point);

	if (_bounding_box_dirty) {
		compute_bounding_box ();
	}

	boost::optional<Rect> r = bounding_box();

	if (!r) {
		return false;
	}

	return r.get().contains (p);
}

ostream&
ArdourCanvas::operator<< (ostream& o, const Item& i)
{
	i.dump (o);
	return o;
}

