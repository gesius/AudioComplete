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

#include <cstdio>
#include <cmath>

#include <sigc++/bind.h>
#include <gtkmm/stock.h>
#include <gtkmm/separator.h>
#include <gtkmm/table.h>
#include <gtkmm2ext/window_title.h>

#include "pbd/error.h"
#include "pbd/convert.h"
#include "gtkmm2ext/utils.h"
#include "ardour/profile.h"
#include "ardour/template_utils.h"
#include "ardour/route_group.h"
#include "ardour/session.h"

#include "utils.h"
#include "add_route_dialog.h"
#include "route_group_dialog.h"
#include "i18n.h"

using namespace Gtk;
using namespace Gtkmm2ext;
using namespace sigc;
using namespace std;
using namespace PBD;
using namespace ARDOUR;

static const char* track_mode_names[] = {
	N_("Normal"),
	N_("Non Layered"),
	N_("Tape"),
	0
};

AddRouteDialog::AddRouteDialog (Session & s)
	: ArdourDialog (X_("add route dialog"))
	, _session (s)
	, routes_adjustment (1, 1, 128, 1, 4)
	, routes_spinner (routes_adjustment)
	, track_mode_label (_("Track mode:"))
{
	if (track_mode_strings.empty()) {
		track_mode_strings = I18N (track_mode_names);

		if (ARDOUR::Profile->get_sae()) {
			/* remove all but the first track mode (Normal) */

			while (track_mode_strings.size() > 1) {
				track_mode_strings.pop_back();
			}
		}
	}
	
	set_name ("AddRouteDialog");
	set_position (Gtk::WIN_POS_MOUSE);
	set_modal (true);
	set_skip_taskbar_hint (true);
	set_resizable (false);

	WindowTitle title(Glib::get_application_name());
	title += _("Add Route");
	set_title(title.get_string());

	name_template_entry.set_name (X_("AddRouteDialogNameTemplateEntry"));
	routes_spinner.set_name (X_("AddRouteDialogSpinner"));
	channel_combo.set_name (X_("ChannelCountSelector"));
	track_mode_combo.set_name (X_("ChannelCountSelector"));

	refill_channel_setups ();
	refill_route_groups ();
	set_popdown_strings (track_mode_combo, track_mode_strings, true);

	channel_combo.set_active_text (channel_combo_strings.front());
	track_mode_combo.set_active_text (track_mode_strings.front());

	track_bus_combo.append_text (_("tracks"));
	track_bus_combo.append_text (_("busses"));
	track_bus_combo.set_active (0);

	VBox* vbox = manage (new VBox);
	Gtk::Label* l;

	get_vbox()->set_spacing (4);

	vbox->set_spacing (18);
	vbox->set_border_width (5);

	HBox *type_hbox = manage (new HBox);
	type_hbox->set_spacing (6);
	
	/* track/bus choice */

	type_hbox->pack_start (*manage (new Label (_("Add:"))));
	type_hbox->pack_start (routes_spinner);
	type_hbox->pack_start (track_bus_combo);
	
	vbox->pack_start (*type_hbox, false, true);

	VBox* options_box = manage (new VBox);
	Table *table2 = manage (new Table (3, 3, false));

	options_box->set_spacing (6);
	table2->set_row_spacings (6);
	table2->set_col_spacing	(1, 6);

	l = manage (new Label (_("<b>Options</b>"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_use_markup ();
	options_box->pack_start (*l, false, true);

	l = manage (new Label ("", Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_padding (8, 0);
	table2->attach (*l, 0, 1, 0, 3, Gtk::FILL, Gtk::FILL, 0, 0);

	/* Route configuration */

	l = manage (new Label (_("Configuration:"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	table2->attach (*l, 1, 2, 0, 1, Gtk::FILL, Gtk::EXPAND, 0, 0);
	table2->attach (channel_combo, 2, 3, 0, 1, Gtk::FILL, Gtk::EXPAND & Gtk::FILL, 0, 0);

	if (!ARDOUR::Profile->get_sae ()) {

		/* Track mode */

		track_mode_label.set_alignment (Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER);
		table2->attach (track_mode_label, 1, 2, 1, 2, Gtk::FILL, Gtk::EXPAND, 0, 0);
		table2->attach (track_mode_combo, 2, 3, 1, 2, Gtk::FILL, Gtk::EXPAND & Gtk::FILL, 0, 0);

	}

	/* Group choise */

	l = manage (new Label (_("Group:"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	table2->attach (*l, 1, 2, 2, 3, Gtk::FILL, Gtk::EXPAND, 0, 0);
	table2->attach (route_group_combo, 2, 3, 2, 3, Gtk::FILL, Gtk::EXPAND & Gtk::FILL, 0, 0);
	
	options_box->pack_start (*table2, false, true);
	vbox->pack_start (*options_box, false, true);

	get_vbox()->pack_start (*vbox, false, false);

	track_bus_combo.signal_changed().connect (mem_fun (*this, &AddRouteDialog::track_type_chosen));
	channel_combo.set_row_separator_func (mem_fun (*this, &AddRouteDialog::channel_separator));
	route_group_combo.set_row_separator_func (mem_fun (*this, &AddRouteDialog::route_separator));
	route_group_combo.signal_changed ().connect (mem_fun (*this, &AddRouteDialog::group_changed));

	show_all_children ();

	/* track template info will be managed whenever
	   this dialog is shown, via ::on_show()
	*/

	add_button (Stock::CANCEL, RESPONSE_CANCEL);
	add_button (Stock::ADD, RESPONSE_ACCEPT);

	track_type_chosen ();
}

AddRouteDialog::~AddRouteDialog ()
{
}

void
AddRouteDialog::track_type_chosen ()
{
	track_mode_label.set_sensitive (track ());
	track_mode_combo.set_sensitive (track ());
}

bool
AddRouteDialog::track ()
{
	return track_bus_combo.get_active_row_number () == 0;
}

ARDOUR::DataType
AddRouteDialog::type ()
{
	// FIXME: ew
	
	const string str = channel_combo.get_active_text();
	if (str == _("MIDI")) {
		return ARDOUR::DataType::MIDI;
	} else {
		return ARDOUR::DataType::AUDIO;
	}
}

string
AddRouteDialog::name_template ()
{
	return name_template_entry.get_text ();
}

int
AddRouteDialog::count ()
{
	return (int) floor (routes_adjustment.get_value ());
}

ARDOUR::TrackMode
AddRouteDialog::mode ()
{
	if (ARDOUR::Profile->get_sae()) {
		return ARDOUR::Normal;
	}

	Glib::ustring str = track_mode_combo.get_active_text();
	if (str == _("Normal")) {
		return ARDOUR::Normal;
	} else if (str == _("Non Layered")){
		return ARDOUR::NonLayered;
	} else if (str == _("Tape")) {
		return ARDOUR::Destructive;
	} else {
		fatal << string_compose (X_("programming error: unknown track mode in add route dialog combo = %1"), str)
		      << endmsg;
		/*NOTREACHED*/
	}
	/* keep gcc happy */
	return ARDOUR::Normal;
}

int
AddRouteDialog::channels ()
{
	string str = channel_combo.get_active_text();
	
	for (ChannelSetups::iterator i = channel_setups.begin(); i != channel_setups.end(); ++i) {
		if (str == (*i).name) {
			return (*i).channels;
		}
	}

	return 0;
}

string
AddRouteDialog::track_template ()
{
	string str = channel_combo.get_active_text();
	
	for (ChannelSetups::iterator i = channel_setups.begin(); i != channel_setups.end(); ++i) {
		if (str == (*i).name) {
			return (*i).template_path;
		}
	}

	return string();
}

void
AddRouteDialog::on_show ()
{
	refill_channel_setups ();
	refill_route_groups ();
	
	Dialog::on_show ();
}

void
AddRouteDialog::refill_channel_setups ()
{
	ChannelSetup chn;
	
	route_templates.clear ();
	channel_combo_strings.clear ();
	channel_setups.clear ();

	chn.name = X_("MIDI");
	chn.channels = 0;
	channel_setups.push_back (chn);

	chn.name = "separator";
	channel_setups.push_back (chn);

	chn.name = _("Mono");
	chn.channels = 1;
	channel_setups.push_back (chn);

	chn.name = _("Stereo");
	chn.channels = 2;
	channel_setups.push_back (chn);

	ARDOUR::find_route_templates (route_templates);

	if (!ARDOUR::Profile->get_sae()) {
		if (!route_templates.empty()) {
			vector<string> v;
			for (vector<TemplateInfo>::iterator x = route_templates.begin(); x != route_templates.end(); ++x) {
				chn.name = x->name;
				chn.channels = 0;
				chn.template_path = x->path;
				channel_setups.push_back (chn);
			}
		} 

		chn.name = _("3 Channel");
		chn.channels = 3;
		channel_setups.push_back (chn);

		chn.name = _("4 Channel");
		chn.channels = 4;
		channel_setups.push_back (chn);

		chn.name = _("5 Channel");
		chn.channels = 5;
		channel_setups.push_back (chn);

		chn.name = _("6 Channel");
		chn.channels = 6;
		channel_setups.push_back (chn);

		chn.name = _("8 Channel");
		chn.channels = 8;
		channel_setups.push_back (chn);

		chn.name = _("12 Channel");
		chn.channels = 12;
		channel_setups.push_back (chn);

		chn.name = X_("Custom");
		chn.channels = 0;
		channel_setups.push_back (chn);
	}

	for (ChannelSetups::iterator i = channel_setups.begin(); i != channel_setups.end(); ++i) {
		channel_combo_strings.push_back ((*i).name);
	}

	set_popdown_strings (channel_combo, channel_combo_strings, true);
	channel_combo.set_active_text (channel_combo_strings.front());
}

void
AddRouteDialog::add_route_group (RouteGroup* g)
{
	route_group_combo.insert_text (3, g->name ());
}

RouteGroup*
AddRouteDialog::route_group ()
{
	if (route_group_combo.get_active_row_number () == 2) {
		return 0;
	}

	return _session.route_group_by_name (route_group_combo.get_active_text());
}

void
AddRouteDialog::refill_route_groups ()
{
	route_group_combo.clear ();
	route_group_combo.append_text (_("New group..."));

	route_group_combo.append_text ("separator");

	route_group_combo.append_text (_("No group"));

	_session.foreach_route_group (mem_fun (*this, &AddRouteDialog::add_route_group));
	
	route_group_combo.set_active (2);
}

void
AddRouteDialog::group_changed ()
{
	if (route_group_combo.get_active_text () == _("New group...")) {
		RouteGroup* g = new RouteGroup (_session, "", RouteGroup::Active);
	
		RouteGroupDialog d (g, Gtk::Stock::NEW);
		int const r = d.do_run ();

		if (r == Gtk::RESPONSE_OK) {
			_session.add_route_group (g);
			add_route_group (g);
			route_group_combo.set_active (3);
		} else {
			delete g;

			route_group_combo.set_active (0);
		}
	}
}

bool
AddRouteDialog::channel_separator (const Glib::RefPtr<Gtk::TreeModel> &m, const Gtk::TreeModel::iterator &i)
{
	channel_combo.set_active (i);

	return channel_combo.get_active_text () == "separator";
}

bool
AddRouteDialog::route_separator (const Glib::RefPtr<Gtk::TreeModel> &m, const Gtk::TreeModel::iterator &i)
{
	route_group_combo.set_active (i);

	return route_group_combo.get_active_text () == "separator";
}

