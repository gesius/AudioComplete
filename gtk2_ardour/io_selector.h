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
	IOSelector (Gtk::Window*, ARDOUR::Session *, boost::shared_ptr<ARDOUR::IO>);

	void set_state (ARDOUR::BundleChannel c[2], bool);
	PortMatrixNode::State get_state (ARDOUR::BundleChannel c[2]) const;

	std::string disassociation_verb () const;
	std::string channel_noun () const;

	uint32_t n_io_ports () const;
	boost::shared_ptr<ARDOUR::IO> const io () { return _io; }
	void setup_ports (int);
	bool list_is_global (int) const;

	bool find_inputs_for_io_outputs () const {
		return _find_inputs_for_io_outputs;
	}

	int ours () const {
		return _ours;
	}

	int other () const {
		return _other;
	}

  private:

	int _other;
	int _ours;
	boost::shared_ptr<ARDOUR::IO> _io;
	boost::shared_ptr<PortGroup> _port_group;
	bool _find_inputs_for_io_outputs;
};

class IOSelectorWindow : public Gtk::Window
{
  public:
	IOSelectorWindow (ARDOUR::Session *, boost::shared_ptr<ARDOUR::IO>, bool can_cancel = false);

	IOSelector& selector() { return _selector; }

  protected:
	void on_map ();
	void on_show ();

  private:
	IOSelector _selector;

	void io_name_changed (void *src);
	bool wm_delete (GdkEventAny*);
};


class PortInsertUI : public Gtk::HBox
{
  public:
	PortInsertUI (Gtk::Window*, ARDOUR::Session *, boost::shared_ptr<ARDOUR::PortInsert>);

	void redisplay ();
	void finished (IOSelector::Result);

  private:
        boost::shared_ptr<ARDOUR::PortInsert> _pi;
        
        Gtk::ToggleButton latency_button;
	IOSelector input_selector;
	IOSelector output_selector;
        Gtk::Label latency_display;
        Gtk::Frame latency_frame;
        Gtk::HBox  latency_hbox;
        sigc::connection latency_timeout;

        bool check_latency_measurement ();
        void latency_button_toggled ();
};

class PortInsertWindow : public ArdourDialog
{
  public:
	PortInsertWindow (ARDOUR::Session *, boost::shared_ptr<ARDOUR::PortInsert>, bool can_cancel = false);

  protected:
	void on_map ();

  private:
	PortInsertUI _portinsertui;
	Gtk::VBox vbox;

	Gtk::Button ok_button;
	Gtk::Button cancel_button;
	Gtk::Frame button_frame;

	void cancel ();
	void accept ();

	void plugin_going_away ();
	PBD::ScopedConnection going_away_connection;

	bool wm_delete (GdkEventAny*);
};


#endif
