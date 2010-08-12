/*
    Copyright (C) 2000-2010 Paul Davis

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
#include <gtkmm/comboboxtext.h>
#include <gtkmm/stock.h>
#include <gtkmm/alignment.h>
#include "insert_time_dialog.h"
#include "audio_clock.h"
#include "i18n.h"

using namespace Gtk;
using namespace Editing;

InsertTimeDialog::InsertTimeDialog (PublicEditor& e)
	: ArdourDialog (_("Insert Time"))
	, _editor (e)
	, _clock ("insertTimeClock", true, X_("InsertTimeClock"), true, false, true, true)
{
	set_session (_editor.session ());
	
	nframes64_t const pos = _editor.get_preferred_edit_position ();

	get_vbox()->set_border_width (12);
	get_vbox()->set_spacing (4);

	Table* table = manage (new Table (2, 2));
	table->set_spacings (4);

	Label* time_label = manage (new Label (_("Time to insert:")));
	time_label->set_alignment (1, 0.5);
	table->attach (*time_label, 0, 1, 0, 1, FILL | EXPAND);
	_clock.set (0);
	_clock.set_session (_session);
	_clock.set_bbt_reference (pos);
	table->attach (_clock, 1, 2, 0, 1);

	Label* intersected_label = manage (new Label (_("Intersected regions should:")));
	intersected_label->set_alignment (1, 0.5);
	table->attach (*intersected_label, 0, 1, 1, 2, FILL | EXPAND);
	_intersected_combo.append_text (_("stay in position"));
	_intersected_combo.append_text (_("move"));
	_intersected_combo.append_text (_("be split"));
	_intersected_combo.set_active (0);
	table->attach (_intersected_combo, 1, 2, 1, 2);

	get_vbox()->pack_start (*table);

	_move_glued.set_label (_("Move glued regions"));
	get_vbox()->pack_start (_move_glued);
	_move_markers.set_label (_("Move markers"));
	get_vbox()->pack_start (_move_markers);
	_move_markers.signal_toggled().connect (sigc::mem_fun (*this, &InsertTimeDialog::move_markers_toggled));
	_move_glued_markers.set_label (_("Move glued markers"));
	Alignment* indent = manage (new Alignment);
	indent->set_padding (0, 0, 12, 0);
	indent->add (_move_glued_markers);
	get_vbox()->pack_start (*indent);
	_move_locked_markers.set_label (_("Move locked markers"));
	indent = manage (new Alignment);
	indent->set_padding (0, 0, 12, 0);
	indent->add (_move_locked_markers);
	get_vbox()->pack_start (*indent);
	_move_tempos.set_label (_("Move tempo and meter changes"));
	get_vbox()->pack_start (_move_tempos);

	add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	add_button (_("Insert time"), Gtk::RESPONSE_OK);
	show_all ();

	move_markers_toggled ();
}

InsertTimeOption
InsertTimeDialog::intersected_region_action ()
{
	/* only setting this to keep GCC quiet */
	InsertTimeOption opt = LeaveIntersected;

	switch (_intersected_combo.get_active_row_number ()) {
	case 0:
		opt = LeaveIntersected;
		break;
	case 1:
		opt = MoveIntersected;
		break;
	case 2:
		opt = SplitIntersected;
		break;
	}

	return opt;
}

bool
InsertTimeDialog::move_glued () const
{
	return _move_glued.get_active ();
}

bool
InsertTimeDialog::move_tempos () const
{
	return _move_tempos.get_active ();
}

bool
InsertTimeDialog::move_markers () const
{
	return _move_markers.get_active ();
}

bool
InsertTimeDialog::move_glued_markers () const
{
	return _move_glued_markers.get_active ();
}

bool
InsertTimeDialog::move_locked_markers () const
{
	return _move_locked_markers.get_active ();
}

nframes64_t
InsertTimeDialog::distance () const
{
	return _clock.current_duration (_editor.get_preferred_edit_position ());
}

void
InsertTimeDialog::move_markers_toggled ()
{
	_move_glued_markers.set_sensitive (_move_markers.get_active ());
	_move_locked_markers.set_sensitive (_move_markers.get_active ());
}
