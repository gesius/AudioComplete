/*
    Copyright (C) 2002-2009 Paul Davis 

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

#ifndef __port_matrix_row_labels_h__
#define __port_matrix_row_labels_h__

#include <boost/shared_ptr.hpp>
#include <gdkmm/color.h>
#include "port_matrix_component.h"

class PortMatrix;
class PortMatrixBody;
class PortMatrixNode;
class PortMatrixBundleChannel;

namespace ARDOUR {
	class Bundle;
}

namespace Gtk {
	class Menu;
}

class PortMatrixRowLabels : public PortMatrixComponent
{
public:
	enum Location {
		LEFT,
		RIGHT
	};
	
	PortMatrixRowLabels (PortMatrix *, PortMatrixBody *, Location);
	~PortMatrixRowLabels ();

	void button_press (double, double, int, uint32_t);
  
	double component_to_parent_x (double x) const;
	double parent_to_component_x (double x) const;
	double component_to_parent_y (double y) const;
	double parent_to_component_y (double y) const;
	void mouseover_changed (PortMatrixNode const &);
	void draw_extra (cairo_t* cr);

private:
	void render (cairo_t *);
	void compute_dimensions ();
	void remove_channel_proxy (boost::weak_ptr<ARDOUR::Bundle>, uint32_t);
	void rename_channel_proxy (boost::weak_ptr<ARDOUR::Bundle>, uint32_t);
	void render_port_name (cairo_t *, Gdk::Color, double, double, PortMatrixBundleChannel const &);
	double channel_y (PortMatrixBundleChannel const &) const;
	void queue_draw_for (PortMatrixNode const &);
	double port_name_x () const;

	PortMatrix* _port_matrix;
	double _longest_port_name;
	double _longest_bundle_name;
	double _highest_group_name;
	Gtk::Menu* _menu;
	Location _location;
};

#endif
