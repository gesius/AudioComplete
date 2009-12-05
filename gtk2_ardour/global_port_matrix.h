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

#include <gtkmm/button.h>
#include "port_matrix.h"
#include "port_group.h"
#include "ardour_dialog.h"
#include "i18n.h"

class GlobalPortMatrix : public PortMatrix
{
public:
	GlobalPortMatrix (Gtk::Window*, ARDOUR::Session*, ARDOUR::DataType);

	void setup_ports (int);

	void set_state (ARDOUR::BundleChannel c[2], bool);
	PortMatrixNode::State get_state (ARDOUR::BundleChannel c[2]) const;

	std::string disassociation_verb () const {
		return _("Disconnect");
	}

	std::string channel_noun () const {
		return _("port");
	}

	bool list_is_global (int) const {
		return true;
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
	GlobalPortMatrixWindow (ARDOUR::Session *, ARDOUR::DataType);

private:
	void on_show ();

	GlobalPortMatrix _port_matrix;
	Gtk::Button _rescan_button;
	Gtk::CheckButton _show_ports_button;
};


#endif
