/*
    Copyright (C) 2000 Paul Davis

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

#include <cmath>
#include <cstdlib>

#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/stop_signal.h>

#include "ardour/utils.h"
#include "ardour/configuration.h"
#include "ardour/session.h"
#include "pbd/memento_command.h"

#include "ardour_ui.h"
#include "prompter.h"
#include "location_ui.h"
#include "keyboard.h"
#include "utils.h"
#include "gui_thread.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Gtkmm2ext;

LocationEditRow::LocationEditRow(Session * sess, Location * loc, int32_t num)
	: SessionHandlePtr (0), /* explicitly set below */
	  location(0), 
	  item_table (1, 6, false),
	  start_clock (X_("locationstart"), true, X_("LocationEditRowClock"), true, false),
	  end_clock (X_("locationend"), true, X_("LocationEditRowClock"), true, false),
	  length_clock (X_("locationlength"), true, X_("LocationEditRowClock"), true, false, true),
	  cd_check_button (_("CD")),
	  hide_check_button (_("Hide")),
	  scms_check_button (_("SCMS")),
	  preemph_check_button (_("Pre-Emphasis"))

{
	i_am_the_modifier = 0;

	start_go_button.set_image (*manage (new Image (Stock::JUMP_TO, Gtk::ICON_SIZE_SMALL_TOOLBAR)));
	end_go_button.set_image (*manage (new Image (Stock::JUMP_TO, Gtk::ICON_SIZE_SMALL_TOOLBAR)));
	remove_button.set_image (*manage (new Image (Stock::REMOVE, Gtk::ICON_SIZE_SMALL_TOOLBAR)));

	number_label.set_name ("LocationEditNumberLabel");
	name_label.set_name ("LocationEditNameLabel");
	name_entry.set_name ("LocationEditNameEntry");
	start_go_button.set_name ("LocationEditGoButton");
	end_go_button.set_name ("LocationEditGoButton");
	cd_check_button.set_name ("LocationEditCdButton");
	hide_check_button.set_name ("LocationEditHideButton");
	remove_button.set_name ("LocationEditRemoveButton");
	isrc_label.set_name ("LocationEditNumberLabel");
	isrc_entry.set_name ("LocationEditNameEntry");
	scms_check_button.set_name ("LocationEditCdButton");
	preemph_check_button.set_name ("LocationEditCdButton");
	performer_label.set_name ("LocationEditNumberLabel");
	performer_entry.set_name ("LocationEditNameEntry");
	composer_label.set_name ("LocationEditNumberLabel");
	composer_entry.set_name ("LocationEditNameEntry");

	isrc_label.set_text ("ISRC: ");
	isrc_label.set_size_request (30, -1);
	performer_label.set_text ("Performer: ");
	performer_label.set_size_request (60, -1);
	composer_label.set_text ("Composer: ");
	composer_label.set_size_request (60, -1);

	isrc_entry.set_size_request (112, -1);
	isrc_entry.set_max_length(12);
	isrc_entry.set_editable (true);

	performer_entry.set_size_request (100, -1);
	performer_entry.set_editable (true);

	composer_entry.set_size_request (100, -1);
	composer_entry.set_editable (true);

	name_label.set_alignment (0, 0.5);

	cd_track_details_hbox.pack_start (isrc_label, false, false);
	cd_track_details_hbox.pack_start (isrc_entry, false, false);
	cd_track_details_hbox.pack_start (scms_check_button, false, false);
	cd_track_details_hbox.pack_start (preemph_check_button, false, false);
	cd_track_details_hbox.pack_start (performer_label, false, false);
	cd_track_details_hbox.pack_start (performer_entry, true, true);
	cd_track_details_hbox.pack_start (composer_label, false, false);
	cd_track_details_hbox.pack_start (composer_entry, true, true);

	isrc_entry.signal_changed().connect (sigc::mem_fun(*this, &LocationEditRow::isrc_entry_changed));
	performer_entry.signal_changed().connect (sigc::mem_fun(*this, &LocationEditRow::performer_entry_changed));
	composer_entry.signal_changed().connect (sigc::mem_fun(*this, &LocationEditRow::composer_entry_changed));
	scms_check_button.signal_toggled().connect(sigc::mem_fun(*this, &LocationEditRow::scms_toggled));
	preemph_check_button.signal_toggled().connect(sigc::mem_fun(*this, &LocationEditRow::preemph_toggled));

	set_session (sess);

	// start_hbox.pack_start (start_go_button, false, false);
	start_hbox.pack_start (start_clock, false, false);

	/* this is always in this location, no matter what the location is */

	item_table.attach (start_hbox, 1, 2, 0, 1, FILL, FILL, 4, 0);

	start_go_button.signal_clicked().connect(sigc::bind (sigc::mem_fun (*this, &LocationEditRow::go_button_pressed), LocStart));
 	start_clock.ValueChanged.connect (sigc::bind (sigc::mem_fun (*this, &LocationEditRow::clock_changed), LocStart));
 	start_clock.ChangeAborted.connect (sigc::bind (sigc::mem_fun (*this, &LocationEditRow::change_aborted), LocStart));

	// end_hbox.pack_start (end_go_button, false, false);
	end_hbox.pack_start (end_clock, false, false);

	end_go_button.signal_clicked().connect(sigc::bind (sigc::mem_fun (*this, &LocationEditRow::go_button_pressed), LocEnd));
	end_clock.ValueChanged.connect (sigc::bind (sigc::mem_fun (*this, &LocationEditRow::clock_changed), LocEnd));
 	end_clock.ChangeAborted.connect (sigc::bind (sigc::mem_fun (*this, &LocationEditRow::change_aborted), LocEnd));

	length_clock.ValueChanged.connect (sigc::bind ( sigc::mem_fun(*this, &LocationEditRow::clock_changed), LocLength));
 	length_clock.ChangeAborted.connect (sigc::bind (sigc::mem_fun (*this, &LocationEditRow::change_aborted), LocLength));

	cd_check_button.signal_toggled().connect(sigc::mem_fun(*this, &LocationEditRow::cd_toggled));
	hide_check_button.signal_toggled().connect(sigc::mem_fun(*this, &LocationEditRow::hide_toggled));

	remove_button.signal_clicked().connect(sigc::mem_fun(*this, &LocationEditRow::remove_button_pressed));

	pack_start(item_table, true, true);

	set_location (loc);
	set_number (num);
}

LocationEditRow::~LocationEditRow()
{
	if (location) {
		connections.drop_connections ();
	}
}

void
LocationEditRow::set_session (Session *sess)
{
	SessionHandlePtr::set_session (sess);

	if (!_session) { 
		return;
	}

	start_clock.set_session (_session);
	end_clock.set_session (_session);
	length_clock.set_session (_session);

}

void
LocationEditRow::set_number (int num)
{
	number = num;

	if (number >= 0 ) {
		number_label.set_text (string_compose ("%1", number));
	}
}

void
LocationEditRow::set_location (Location *loc)
{
	if (location) {
		connections.drop_connections ();
	}

	location = loc;

	if (!location) return;

	if (!hide_check_button.get_parent()) {
		item_table.attach (hide_check_button, 5, 6, 0, 1, FILL, Gtk::FILL, 4, 0);
	}
	hide_check_button.set_active (location->is_hidden());

	if (location->is_auto_loop() || location-> is_auto_punch()) {
		// use label instead of entry

		name_label.set_text (location->name());
		name_label.set_size_request (80, -1);

		if (!name_label.get_parent()) {
			item_table.attach (name_label, 0, 1, 0, 1, FILL, FILL, 4, 0);
		}

		name_label.show();

	} else {

		name_entry.set_text (location->name());
		name_entry.set_size_request (100, -1);
		name_entry.set_editable (true);
		name_entry.signal_changed().connect (sigc::mem_fun(*this, &LocationEditRow::name_entry_changed));

		if (!name_entry.get_parent()) {
			item_table.attach (name_entry, 0, 1, 0, 1, FILL | EXPAND, FILL, 4, 0);
		}
		name_entry.show();

		if (!cd_check_button.get_parent()) {
			item_table.attach (cd_check_button, 4, 5, 0, 1, FILL, FILL, 4, 0);
		}
		if (!remove_button.get_parent()) {
			item_table.attach (remove_button, 6, 7, 0, 1, FILL, FILL, 4, 0);
		}

		if (location->is_session_range()) {
			remove_button.set_sensitive (false);
		}

		cd_check_button.set_active (location->is_cd_marker());
		cd_check_button.show();

		if (location->start() == _session->current_start_frame()) {
			cd_check_button.set_sensitive (false);
		} else {
			cd_check_button.set_sensitive (true);
		}

		hide_check_button.show();
	}

	start_clock.set (location->start(), true);


	if (!location->is_mark()) {
		if (!end_hbox.get_parent()) {
			item_table.attach (end_hbox, 2, 3, 0, 1, FILL, FILL, 4, 0);
		}
		if (!length_clock.get_parent()) {
			item_table.attach (length_clock, 3, 4, 0, 1, FILL, FILL, 4, 0);
		}

		end_clock.set (location->end(), true);
		length_clock.set (location->length(), true);

		end_go_button.show();
		end_clock.show();
		length_clock.show();

		ARDOUR_UI::instance()->set_tip (end_go_button, _("Jump to the end of this range"));
		ARDOUR_UI::instance()->set_tip (start_go_button, _("Jump to the start of this range"));
		ARDOUR_UI::instance()->set_tip (remove_button, _("Forget this range"));
		ARDOUR_UI::instance()->set_tip (start_clock, _("Start time"));
		ARDOUR_UI::instance()->set_tip (end_clock, _("End time"));
		ARDOUR_UI::instance()->set_tip (length_clock, _("Length"));

	} else {

		ARDOUR_UI::instance()->set_tip (start_go_button, _("Jump to this marker"));
		ARDOUR_UI::instance()->set_tip (remove_button, _("Forget this marker"));
		ARDOUR_UI::instance()->set_tip (start_clock, _("Position"));

		end_go_button.hide();
		end_clock.hide();
		length_clock.hide();
	}

	start_clock.set_sensitive (!location->locked());
	end_clock.set_sensitive (!location->locked());
	length_clock.set_sensitive (!location->locked());

	location->start_changed.connect (connections, invalidator (*this), ui_bind (&LocationEditRow::start_changed, this, _1), gui_context());
	location->end_changed.connect (connections, invalidator (*this), ui_bind (&LocationEditRow::end_changed, this, _1), gui_context());
	location->name_changed.connect (connections, invalidator (*this), ui_bind (&LocationEditRow::name_changed, this, _1), gui_context());
	location->changed.connect (connections, invalidator (*this), ui_bind (&LocationEditRow::location_changed, this, _1), gui_context());
	location->FlagsChanged.connect (connections, invalidator (*this), ui_bind (&LocationEditRow::flags_changed, this, _1, _2), gui_context());
}

void
LocationEditRow::name_entry_changed ()
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::name_entry_changed)
	if (i_am_the_modifier || !location) return;

	location->set_name (name_entry.get_text());
}


void
LocationEditRow::isrc_entry_changed ()
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::isrc_entry_changed)

	if (i_am_the_modifier || !location) return;

	if (isrc_entry.get_text() != "" ) {

	  location->cd_info["isrc"] = isrc_entry.get_text();

	} else {
	  location->cd_info.erase("isrc");
	}
}

void
LocationEditRow::performer_entry_changed ()
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::performer_entry_changed)

	if (i_am_the_modifier || !location) return;

	if (performer_entry.get_text() != "") {
	  location->cd_info["performer"] = performer_entry.get_text();
	} else {
	  location->cd_info.erase("performer");
	}
}

void
LocationEditRow::composer_entry_changed ()
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::composer_entry_changed)

	if (i_am_the_modifier || !location) return;

	if (composer_entry.get_text() != "") {
	location->cd_info["composer"] = composer_entry.get_text();
	} else {
	  location->cd_info.erase("composer");
	}
}


void
LocationEditRow::go_button_pressed (LocationPart part)
{
	if (!location) return;

	switch (part) {
	case LocStart:
		ARDOUR_UI::instance()->do_transport_locate (location->start());
		break;
	case LocEnd:
		ARDOUR_UI::instance()->do_transport_locate (location->end());
		break;
	default:
		break;
	}
}

void
LocationEditRow::clock_changed (LocationPart part)
{
	if (i_am_the_modifier || !location) return;

	switch (part) {
	case LocStart:
		location->set_start (start_clock.current_time());
		break;
	case LocEnd:
		location->set_end (end_clock.current_time());
		break;
	case LocLength:
		location->set_end (location->start() + length_clock.current_duration());
	default:
		break;
	}

}

void
LocationEditRow::change_aborted (LocationPart /*part*/)
{
	if (i_am_the_modifier || !location) return;

	set_location(location);
}

void
LocationEditRow::cd_toggled ()
{
	if (i_am_the_modifier || !location) {
		return;
	}

	//if (cd_check_button.get_active() == location->is_cd_marker()) {
	//	return;
	//}

	if (cd_check_button.get_active()) {
		if (location->start() <= _session->current_start_frame()) {
			error << _("You cannot put a CD marker at the start of the session") << endmsg;
			cd_check_button.set_active (false);
			return;
		}
	}

	location->set_cd (cd_check_button.get_active(), this);

	if (location->is_cd_marker() && !(location->is_mark())) {

		if (location->cd_info.find("isrc") != location->cd_info.end()) {
			isrc_entry.set_text(location->cd_info["isrc"]);
		}
		if (location->cd_info.find("performer") != location->cd_info.end()) {
			performer_entry.set_text(location->cd_info["performer"]);
		}
		if (location->cd_info.find("composer") != location->cd_info.end()) {
			composer_entry.set_text(location->cd_info["composer"]);
		}
		if (location->cd_info.find("scms") != location->cd_info.end()) {
			scms_check_button.set_active(true);
		}
		if (location->cd_info.find("preemph") != location->cd_info.end()) {
			preemph_check_button.set_active(true);
		}

		if (!cd_track_details_hbox.get_parent()) {
			item_table.attach (cd_track_details_hbox, 0, 7, 1, 2, FILL | EXPAND, FILL, 4, 0);
		}
		// item_table.resize(2, 7);
		cd_track_details_hbox.show_all();

	} else if (cd_track_details_hbox.get_parent()){

		item_table.remove (cd_track_details_hbox);
		//	  item_table.resize(1, 7);
		redraw_ranges(); /* 	EMIT_SIGNAL */
	}
}

void
LocationEditRow::hide_toggled ()
{
	if (i_am_the_modifier || !location) return;

	location->set_hidden (hide_check_button.get_active(), this);
}

void
LocationEditRow::remove_button_pressed ()
{
	if (!location) return;

	remove_requested(location); /* 	EMIT_SIGNAL */
}



void
LocationEditRow::scms_toggled ()
{
	if (i_am_the_modifier || !location) return;

	if (scms_check_button.get_active()) {
	  location->cd_info["scms"] = "on";
	} else {
	  location->cd_info.erase("scms");
	}

}

void
LocationEditRow::preemph_toggled ()
{
	if (i_am_the_modifier || !location) return;

	if (preemph_check_button.get_active()) {
	  location->cd_info["preemph"] = "on";
	} else {
	  location->cd_info.erase("preemph");
	}
}

void
LocationEditRow::end_changed (ARDOUR::Location *loc)
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::end_changed, loc)

	if (!location) return;

	// update end and length
	i_am_the_modifier++;

	end_clock.set (location->end());
	length_clock.set (location->length());

	i_am_the_modifier--;
}

void
LocationEditRow::start_changed (ARDOUR::Location *loc)
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::start_changed, loc)

	if (!location) return;

	// update end and length
	i_am_the_modifier++;

	start_clock.set (location->start());

	if (location->start() == _session->current_start_frame()) {
		cd_check_button.set_sensitive (false);
	} else {
		cd_check_button.set_sensitive (true);
	}

	i_am_the_modifier--;
}

void
LocationEditRow::name_changed (ARDOUR::Location *loc)
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::name_changed, loc)

	if (!location) return;

	// update end and length
	i_am_the_modifier++;

	name_entry.set_text(location->name());
	name_label.set_text(location->name());

	i_am_the_modifier--;

}

void
LocationEditRow::location_changed (ARDOUR::Location *loc)
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::location_changed, loc)

	if (!location) return;

	i_am_the_modifier++;

	start_clock.set (location->start());
	end_clock.set (location->end());
	length_clock.set (location->length());

	start_clock.set_sensitive (!location->locked());
	end_clock.set_sensitive (!location->locked());
	length_clock.set_sensitive (!location->locked());

	i_am_the_modifier--;

}

void
LocationEditRow::flags_changed (ARDOUR::Location *loc, void *src)
{
	ENSURE_GUI_THREAD (*this, &LocationEditRow::flags_changed, loc, src)

	if (!location) return;

	i_am_the_modifier++;

	cd_check_button.set_active (location->is_cd_marker());
	hide_check_button.set_active (location->is_hidden());

	i_am_the_modifier--;
}

void
LocationEditRow::focus_name() {
	name_entry.grab_focus();
}


LocationUI::LocationUI ()
	: add_location_button (_("New Marker"))
	, add_range_button (_("New Range"))
{
	i_am_the_modifier = 0;

	location_vpacker.set_spacing (5);
	
	add_location_button.set_image (*Gtk::manage (new Gtk::Image (Gtk::Stock::ADD, Gtk::ICON_SIZE_BUTTON)));
	add_range_button.set_image (*Gtk::manage (new Gtk::Image (Gtk::Stock::ADD, Gtk::ICON_SIZE_BUTTON)));

	loop_punch_box.pack_start (loop_edit_row, false, false);
	loop_punch_box.pack_start (punch_edit_row, false, false);
	
	loop_punch_scroller.add (loop_punch_box);
	loop_punch_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_NEVER);
	loop_punch_scroller.set_shadow_type (Gtk::SHADOW_NONE);
	
	location_vpacker.pack_start (loop_punch_scroller, false, false);

	location_rows.set_name("LocationLocRows");
	location_rows_scroller.add (location_rows);
	location_rows_scroller.set_name ("LocationLocRowsScroller");
	location_rows_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
	location_rows_scroller.set_size_request (-1, 130);

	newest_location = 0;

	loc_frame_box.set_spacing (5);
	loc_frame_box.set_border_width (5);
	loc_frame_box.set_name("LocationFrameBox");

	loc_frame_box.pack_start (location_rows_scroller, true, true);

	add_location_button.set_name ("LocationAddLocationButton");
	
	HBox* add_button_box = manage (new HBox);

	// loc_frame_box.pack_start (add_location_button, false, false);
	add_button_box->pack_start (add_location_button, true, true);

	loc_frame.set_name ("LocationLocEditorFrame");
	loc_frame.set_label (_("Markers (including CD index)"));
	loc_frame.add (loc_frame_box);
	loc_range_panes.pack1(loc_frame, true, false);


	range_rows.set_name("LocationRangeRows");
	range_rows_scroller.add (range_rows);
	range_rows_scroller.set_name ("LocationRangeRowsScroller");
	range_rows_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
	range_rows_scroller.set_size_request (-1, 130);

	range_frame_box.set_spacing (5);
	range_frame_box.set_name("LocationFrameBox");
	range_frame_box.set_border_width (5);
	range_frame_box.pack_start (range_rows_scroller, true, true);

	add_range_button.set_name ("LocationAddRangeButton");
	//range_frame_box.pack_start (add_range_button, false, false);

	add_button_box->pack_start (add_range_button, true, true);

	range_frame.set_name ("LocationRangeEditorFrame");
	range_frame.set_label (_("Ranges (including CD track ranges)"));
	range_frame.add (range_frame_box);
	loc_range_panes.pack2(range_frame, true, false);
	location_vpacker.pack_start (loc_range_panes, true, true);
	location_vpacker.pack_start (*add_button_box, false, false);

	pack_start (location_vpacker, true, true);

	add_location_button.signal_clicked().connect (sigc::mem_fun(*this, &LocationUI::add_new_location));
	add_range_button.signal_clicked().connect (sigc::mem_fun(*this, &LocationUI::add_new_range));
	
	show_all ();
}

LocationUI::~LocationUI()
{
}

gint 
LocationUI::do_location_remove (ARDOUR::Location *loc)
{
	/* this is handled internally by Locations, but there's
	   no point saving state etc. when we know the marker
	   cannot be removed.
	*/

	if (loc->is_session_range()) {
		return FALSE;
	}

	_session->begin_reversible_command (_("remove marker"));
	XMLNode &before = _session->locations()->get_state();
	_session->locations()->remove (loc);
	XMLNode &after = _session->locations()->get_state();
	_session->add_command(new MementoCommand<Locations>(*(_session->locations()), &before, &after));
	_session->commit_reversible_command ();

	return FALSE;
}

void 
LocationUI::location_remove_requested (ARDOUR::Location *loc)
{
	// must do this to prevent problems when destroying
	// the effective sender of this event

	Glib::signal_idle().connect (sigc::bind (sigc::mem_fun(*this, &LocationUI::do_location_remove), loc));
}


void 
LocationUI::location_redraw_ranges ()
{
	range_rows.hide();
	range_rows.show();
}

void
LocationUI::location_added (Location* location)
{
	ENSURE_GUI_THREAD (*this, &LocationUI::location_added, location)

	if (location->is_auto_punch()) {
		punch_edit_row.set_location(location);
	}
	else if (location->is_auto_loop()) {
		loop_edit_row.set_location(location);
	}
	else {
		refresh_location_list ();
	}
}

void
LocationUI::location_removed (Location* location)
{
	ENSURE_GUI_THREAD (*this, &LocationUI::location_removed, location)

	if (location->is_auto_punch()) {
		punch_edit_row.set_location(0);
	}
	else if (location->is_auto_loop()) {
		loop_edit_row.set_location(0);
	}
	else {
		refresh_location_list ();
	}
}

struct LocationSortByStart {
    bool operator() (Location *a, Location *b) {
	    return a->start() < b->start();
    }
};

void
LocationUI::map_locations (Locations::LocationList& locations)
{
	Locations::LocationList::iterator i;
	Location* location;
	gint n;
	int mark_n = 0;
	Locations::LocationList temp = locations;
	LocationSortByStart cmp;

	temp.sort (cmp);
	locations = temp;

	Box_Helpers::BoxList & loc_children = location_rows.children();
	Box_Helpers::BoxList & range_children = range_rows.children();
	LocationEditRow * erow;

	for (n = 0, i = locations.begin(); i != locations.end(); ++n, ++i) {

		location = *i;

		if (location->is_mark()) {
			mark_n++;
			erow = manage (new LocationEditRow(_session, location, mark_n));
			erow->remove_requested.connect (sigc::mem_fun(*this, &LocationUI::location_remove_requested));
 			erow->redraw_ranges.connect (sigc::mem_fun(*this, &LocationUI::location_redraw_ranges));
			loc_children.push_back(Box_Helpers::Element(*erow, PACK_SHRINK, 1, PACK_START));
			if (location == newest_location) {
				newest_location = 0;
				erow->focus_name();
			}
		}
		else if (location->is_auto_punch()) {
			punch_edit_row.set_session (_session);
			punch_edit_row.set_location (location);
			punch_edit_row.show_all();
		}
		else if (location->is_auto_loop()) {
			loop_edit_row.set_session (_session);
			loop_edit_row.set_location (location);
			loop_edit_row.show_all();
		}
		else {
			erow = manage (new LocationEditRow(_session, location));
			erow->remove_requested.connect (sigc::mem_fun(*this, &LocationUI::location_remove_requested));
			range_children.push_back(Box_Helpers::Element(*erow,  PACK_SHRINK, 1, PACK_START));
		}
	}

	range_rows.show_all();
	location_rows.show_all();
}

void
LocationUI::add_new_location()
{
	string markername;

	if (_session) {
		nframes_t where = _session->audible_frame();
		_session->locations()->next_available_name(markername,"mark");
		Location *location = new Location (where, where, markername, Location::IsMark);
		if (Config->get_name_new_markers()) {
			newest_location = location;
		}
		_session->begin_reversible_command (_("add marker"));
		XMLNode &before = _session->locations()->get_state();
		_session->locations()->add (location, true);
		XMLNode &after = _session->locations()->get_state();
		_session->add_command (new MementoCommand<Locations>(*(_session->locations()), &before, &after));
		_session->commit_reversible_command ();
	}

}

void
LocationUI::add_new_range()
{
	string rangename;

	if (_session) {
		nframes_t where = _session->audible_frame();
		_session->locations()->next_available_name(rangename,"unnamed");
		Location *location = new Location (where, where, rangename, Location::IsRangeMarker);
		_session->begin_reversible_command (_("add range marker"));
		XMLNode &before = _session->locations()->get_state();
		_session->locations()->add (location, true);
		XMLNode &after = _session->locations()->get_state();
		_session->add_command (new MementoCommand<Locations>(*(_session->locations()), &before, &after));
		_session->commit_reversible_command ();
	}
}

void
LocationUI::refresh_location_list ()
{
	ENSURE_GUI_THREAD (*this, &LocationUI::refresh_location_list)
	using namespace Box_Helpers;

	// this is just too expensive to do when window is not shown
	if (!is_visible()) return;

	BoxList & loc_children = location_rows.children();
	BoxList & range_children = range_rows.children();

	loc_children.clear();
	range_children.clear();

	if (_session) {
		_session->locations()->apply (*this, &LocationUI::map_locations);
	}

}

void
LocationUI::set_session(ARDOUR::Session* s)
{
	SessionHandlePtr::set_session (s);

	if (_session) {
		_session->locations()->changed.connect (_session_connections, invalidator (*this), boost::bind (&LocationUI::refresh_location_list, this), gui_context());
		_session->locations()->StateChanged.connect (_session_connections, invalidator (*this), boost::bind (&LocationUI::refresh_location_list, this), gui_context());
		_session->locations()->added.connect (_session_connections, invalidator (*this), ui_bind (&LocationUI::location_added, this, _1), gui_context());
		_session->locations()->removed.connect (_session_connections, invalidator (*this), ui_bind (&LocationUI::location_removed, this, _1), gui_context());
	}

	loop_edit_row.set_session (s);
	punch_edit_row.set_session (s);

	refresh_location_list ();
}

void
LocationUI::session_going_away()
{
	ENSURE_GUI_THREAD (*this, &LocationUI::session_going_away);

	using namespace Box_Helpers;
	BoxList & loc_children = location_rows.children();
	BoxList & range_children = range_rows.children();

	loc_children.clear();
	range_children.clear();

	loop_edit_row.set_session (0);
	loop_edit_row.set_location (0);

	punch_edit_row.set_session (0);
	punch_edit_row.set_location (0);

	SessionHandlePtr::session_going_away ();
}

/*------------------------*/

LocationUIWindow::LocationUIWindow ()
	: ArdourDialog ("locations dialog")
{
	set_title (_("Locations"));
	set_wmclass(X_("ardour_locations"), "Ardour");
	set_name ("LocationWindow");

	get_vbox()->pack_start (_ui);
}

LocationUIWindow::~LocationUIWindow()
{
}

void 
LocationUIWindow::on_show()
{
	_ui.refresh_location_list();
	ArdourDialog::on_show();
}

bool
LocationUIWindow::on_delete_event (GdkEventAny*)
{
	hide ();
	return true;
}

void
LocationUIWindow::set_session (Session *s)
{
	ArdourDialog::set_session (s);
	_ui.set_session (s);
}

void
LocationUIWindow::session_going_away ()
{
	ArdourDialog::session_going_away ();
	hide_all();
}
