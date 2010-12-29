/*
    Copyright (C) 2000-2006 Paul Davis

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

#include "plugin_ui.h"

class VSTPluginUI : public PlugUIBase, public Gtk::VBox
{
  public:
	VSTPluginUI (boost::shared_ptr<ARDOUR::PluginInsert>, boost::shared_ptr<ARDOUR::VSTPlugin>);
	~VSTPluginUI ();

	gint get_preferred_height ();
	gint get_preferred_width ();
	bool start_updating(GdkEventAny*) {return false;}
	bool stop_updating(GdkEventAny*) {return false;}

	int package (Gtk::Window&);

	void forward_key_event (GdkEventKey *);
	bool non_gtk_gui () const { return true; }

  private:
	boost::shared_ptr<ARDOUR::VSTPlugin> vst;
	Gtk::Socket socket;
	Gtk::HBox   preset_box;
	Gtk::VBox   vpacker;

	bool configure_handler (GdkEventConfigure*, Gtk::Socket*);
	void save_plugin_setting ();
	void preset_selected ();
};
