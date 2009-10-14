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

#include <gtkmm/table.h>
#include <gtkmm/stock.h>
#include <gtkmm2ext/window_title.h>
#include "ardour/route_group.h"
#include "route_group_dialog.h"
#include "i18n.h"
#include <iostream>

using namespace Gtk;
using namespace Gtkmm2ext;
using namespace ARDOUR;
using namespace std;

RouteGroupDialog::RouteGroupDialog (RouteGroup* g, StockID const & s)
	: ArdourDialog (_("route group dialog")),
	  _group (g),
	  _active (_("Active")),
	  _gain (_("Gain")),
	  _relative (_("Relative")),
	  _mute (_("Muting")),
	  _solo (_("Soloing")),
	  _rec_enable (_("Record enable")),
	  _select (_("Selection")),
	  _edit (_("Editing"))
{
	set_modal (true);
	set_skip_taskbar_hint (true);
	set_resizable (false);
	set_position (Gtk::WIN_POS_MOUSE);
	set_name (N_("RouteGroupDialog"));

	WindowTitle title (Glib::get_application_name());
	title += _("Route group");
	set_title(title.get_string());

	VBox* vbox = manage (new VBox);
	Gtk::Label* l;

	get_vbox()->set_spacing (4);

	vbox->set_spacing (18);
	vbox->set_border_width (5);

	HBox* hbox = manage (new HBox);
	hbox->set_spacing (6);
	l = manage (new Label (_("Name:"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false ));

	hbox->pack_start (*l, false, true);
	hbox->pack_start (_name, true, true);

	vbox->pack_start (*hbox, false, true);

	VBox* options_box = manage (new VBox);
	options_box->set_spacing (6);

	l = manage (new Label (_("<b>Sharing</b>"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false ));
	l->set_use_markup ();
	options_box->pack_start (*l, false, true);

	_name.set_text (_group->name ());
	_active.set_active (_group->is_active ());

	_gain.set_active (_group->property (RouteGroup::Gain));
	_relative.set_active (_group->is_relative());
	_mute.set_active (_group->property (RouteGroup::Mute));
	_solo.set_active (_group->property (RouteGroup::Solo));
	_rec_enable.set_active (_group->property (RouteGroup::RecEnable));
	_select.set_active (_group->property (RouteGroup::Select));
	_edit.set_active (_group->property (RouteGroup::Edit));

	gain_toggled ();

	Table* table = manage (new Table (8, 3, false));
	table->set_row_spacings	(6);

	l = manage (new Label ("", Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_padding (8, 0);
	table->attach (*l, 0, 1, 0, 8, Gtk::FILL, Gtk::FILL, 0, 0);

	table->attach (_active, 1, 3, 0, 1, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_gain, 1, 3, 1, 2, Gtk::FILL, Gtk::FILL, 0, 0);

	l = manage (new Label ("", Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_padding (0, 0);
	table->attach (*l, 1, 2, 2, 3, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_relative, 2, 3, 2, 3, Gtk::FILL, Gtk::FILL, 0, 0);

	table->attach (_mute, 1, 3, 3, 4, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_solo, 1, 3, 4, 5, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_rec_enable, 1, 3, 5, 6, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_select, 1, 3, 6, 7, Gtk::FILL, Gtk::FILL, 0, 0);
	table->attach (_edit, 1, 3, 7, 8, Gtk::FILL, Gtk::FILL, 0, 0);

	options_box->pack_start (*table, false, true);
	vbox->pack_start (*options_box, false, true);

	get_vbox()->pack_start (*vbox, false, false);

	_gain.signal_toggled().connect(mem_fun (*this, &RouteGroupDialog::gain_toggled));

	add_button (Stock::CANCEL, RESPONSE_CANCEL);
	add_button (s, RESPONSE_OK);

	show_all_children ();
}

int
RouteGroupDialog::do_run ()
{
	int const r = run ();

	if (r == Gtk::RESPONSE_OK) {
		_group->set_property (RouteGroup::Gain, _gain.get_active ());
		_group->set_property (RouteGroup::Mute, _mute.get_active ());
		_group->set_property (RouteGroup::Solo, _solo.get_active ());
		_group->set_property (RouteGroup::RecEnable, _rec_enable.get_active ());
		_group->set_property (RouteGroup::Select, _select.get_active ());
		_group->set_property (RouteGroup::Edit, _edit.get_active ());
		_group->set_name (_name.get_text ()); // This emits changed signal
		_group->set_active (_active.get_active (), this);
		_group->set_relative (_relative.get_active(), this);
	}

	return r;
}

void
RouteGroupDialog::gain_toggled ()
{
	_relative.set_sensitive (_gain.get_active ());
}

