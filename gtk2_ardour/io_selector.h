/*
    Copyright (C) 2002-2007 Paul Davis 

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

#ifndef __ardour_ui_io_selector_h__
#define __ardour_ui_io_selector_h__

#include "ardour_dialog.h"
#include "port_matrix.h"

namespace ARDOUR {
	class PortInsert;
}

class IOSelector : public PortMatrix
{
  public:
	IOSelector (ARDOUR::Session&, boost::shared_ptr<ARDOUR::IO>, bool);

	void set_state (ARDOUR::BundleChannel c[2], bool);
	State get_state (ARDOUR::BundleChannel c[2]) const;

	void add_channel (boost::shared_ptr<ARDOUR::Bundle>);
	bool can_remove_channels (int d) const {
		return d == _ours;
	}
	void remove_channel (ARDOUR::BundleChannel);
	bool can_rename_channels (int d) const {
		return false;
	}
	
	uint32_t n_io_ports () const;
	uint32_t maximum_io_ports () const;
	uint32_t minimum_io_ports () const;
	boost::shared_ptr<ARDOUR::IO> const io () { return _io; }
	void setup ();

	bool find_inputs_for_io_outputs () const {
		return _find_inputs_for_io_outputs;
	}

  private:
	
	int _other;
	int _ours;
	
	boost::shared_ptr<ARDOUR::IO> _io;
	boost::shared_ptr<PortGroup> _port_group;
	bool _find_inputs_for_io_outputs;
	
	void ports_changed ();
};

class IOSelectorWindow : public ArdourDialog
{
  public:
	IOSelectorWindow (ARDOUR::Session&, boost::shared_ptr<ARDOUR::IO>, bool for_input, bool can_cancel = false);

	IOSelector& selector() { return _selector; }

  protected:
	void on_map ();
	
  private:
	IOSelector _selector;

	/* overall operation buttons */

	Gtk::Button add_button;
	Gtk::Button disconnect_button;
	Gtk::Button ok_button;
	Gtk::Button cancel_button;
	Gtk::Button rescan_button;

	void cancel ();
	void accept ();

	void ports_changed ();
	void io_name_changed (void *src);
};


class PortInsertUI : public Gtk::VBox
{
  public: 
	PortInsertUI (ARDOUR::Session&, boost::shared_ptr<ARDOUR::PortInsert>);
	
	void redisplay ();
	void finished (IOSelector::Result);

  private:
	IOSelector input_selector;
	IOSelector output_selector;
};

class PortInsertWindow : public ArdourDialog
{
  public: 
	PortInsertWindow (ARDOUR::Session&, boost::shared_ptr<ARDOUR::PortInsert>, bool can_cancel = false);
	
  protected:
	void on_map ();
	
  private:
	PortInsertUI _portinsertui;
	Gtk::VBox vbox;
	
	Gtk::Button ok_button;
	Gtk::Button cancel_button;
	Gtk::Button rescan_button;
	Gtk::Frame button_frame;
	
	void cancel ();
	void accept ();

	void plugin_going_away ();
	sigc::connection going_away_connection;
};


#endif
