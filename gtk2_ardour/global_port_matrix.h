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

#ifndef __ardour_gtk_global_port_matrix_h__
#define __ardour_gtk_global_port_matrix_h__

#include "port_matrix.h"
#include "port_group.h"
#include "ardour_dialog.h"

class GlobalPortMatrix : public PortMatrix
{
public:
	GlobalPortMatrix (ARDOUR::Session&, ARDOUR::DataType);

	void setup ();
	
	void set_state (ARDOUR::BundleChannel c[2], bool);
	State get_state (ARDOUR::BundleChannel c[2]) const;

	void add_channel (boost::shared_ptr<ARDOUR::Bundle>) {}
	bool can_remove_channels (int d) const {
		return false;
	}
	void remove_channel (ARDOUR::BundleChannel) {}
	bool can_rename_channels (int d) const {
		return false;
	}

private:
	/* see PortMatrix: signal flow from 0 to 1 (out to in) */
	enum {
		OUT = 0,
		IN = 1,
	};
};


class GlobalPortMatrixWindow : public Gtk::Window
{
public:
	GlobalPortMatrixWindow (ARDOUR::Session&, ARDOUR::DataType);

private:
	GlobalPortMatrix _port_matrix;

};


#endif
