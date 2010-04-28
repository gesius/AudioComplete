/*
    Copyright (C) 2000-2005 Paul Davis

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

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

#include "pbd/basename.h"
#include "pbd/enumwriter.h"

#include "ardour/audioregion.h"
#include "ardour/audiofilesource.h"
#include "ardour/region_factory.h"
#include "ardour/session.h"
#include "ardour/silentfilesource.h"
#include "ardour/profile.h"

#include <gtkmm2ext/stop_signal.h>

#include "editor.h"
#include "editing.h"
#include "keyboard.h"
#include "ardour_ui.h"
#include "gui_thread.h"
#include "actions.h"
#include "region_view.h"
#include "utils.h"
#include "editor_regions.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Glib;
using namespace Editing;
using Gtkmm2ext::Keyboard;

EditorRegions::EditorRegions (Editor* e)
	: EditorComponent (e)
	, _menu (0)
	, _show_automatic_regions (true)
	, _sort_type ((Editing::RegionListSortType) 0)
 	, _no_redisplay (false) 
	, ignore_region_list_selection_change (false)
	, ignore_selected_region_change (false)
{
	_display.set_size_request (100, -1);
	_display.set_name ("RegionListDisplay");
	/* Try to prevent single mouse presses from initiating edits.
	   This relies on a hack in gtktreeview.c:gtk_treeview_button_press()
	*/
	_display.set_data ("mouse-edits-require-mod1", (gpointer) 0x1);

	_model = TreeStore::create (_columns);
	_model->set_sort_func (0, sigc::mem_fun (*this, &EditorRegions::sorter));
	_model->set_sort_column (0, SORT_ASCENDING);

	_display.set_model (_model);
	_display.append_column (_("Regions"), _columns.name);
	_display.append_column (_("Start"), _columns.start);
	_display.append_column (_("End"), _columns.end);
	_display.append_column (_("Length"), _columns.length);
	_display.append_column (_("Sync"), _columns.sync);
	_display.append_column (_("Fade In"), _columns.fadein);
	_display.append_column (_("Fade Out"), _columns.fadeout);
	_display.append_column (_("L"), _columns.locked);
	_display.append_column (_("G"), _columns.glued);
	_display.append_column (_("M"), _columns.muted);
	_display.append_column (_("O"), _columns.opaque);
	_display.append_column (_("Used"), _columns.used);
	_display.append_column (_("Path"), _columns.path);
	_display.set_headers_visible (true);
	//_display.set_grid_lines (TREE_VIEW_GRID_LINES_BOTH);

	CellRendererText* region_name_cell = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
	region_name_cell->property_editable() = true;
	region_name_cell->signal_edited().connect (sigc::mem_fun (*this, &EditorRegions::name_edit));

	_display.get_selection()->set_select_function (sigc::mem_fun (*this, &EditorRegions::selection_filter));

	TreeViewColumn* tv_col = _display.get_column(0);
	CellRendererText* renderer = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
	tv_col->add_attribute(renderer->property_text(), _columns.name);
	tv_col->add_attribute(renderer->property_foreground_gdk(), _columns.color_);

	_display.get_selection()->set_mode (SELECTION_MULTIPLE);
	_display.add_object_drag (_columns.region.index(), "regions");

	/* setup DnD handling */

	list<TargetEntry> region_list_target_table;

	region_list_target_table.push_back (TargetEntry ("text/plain"));
	region_list_target_table.push_back (TargetEntry ("text/uri-list"));
	region_list_target_table.push_back (TargetEntry ("application/x-rootwin-drop"));

	_display.add_drop_targets (region_list_target_table);
	_display.signal_drag_data_received().connect (sigc::mem_fun(*this, &EditorRegions::drag_data_received));

	_scroller.add (_display);
	_scroller.set_policy (POLICY_AUTOMATIC, POLICY_AUTOMATIC);

	_display.signal_key_press_event().connect (sigc::mem_fun(*this, &EditorRegions::key_press));
	_display.signal_button_press_event().connect (sigc::mem_fun(*this, &EditorRegions::button_press), false);
	_change_connection = _display.get_selection()->signal_changed().connect (sigc::mem_fun(*this, &EditorRegions::selection_changed));
	// _display.signal_popup_menu().connect (sigc::bind (sigc::mem_fun (*this, &Editor::show__display_context_menu), 1, 0));

	//ARDOUR_UI::instance()->secondary_clock.mode_changed.connect (sigc::mem_fun(*this, &Editor::redisplay_regions));
	ARDOUR_UI::instance()->secondary_clock.mode_changed.connect (sigc::mem_fun(*this, &EditorRegions::update_all_rows));
	ARDOUR::Region::RegionPropertyChanged.connect (region_property_connection, MISSING_INVALIDATOR, ui_bind (&EditorRegions::region_changed, this, _1, _2), gui_context());
	ARDOUR::RegionFactory::CheckNewRegion.connect (check_new_region_connection, MISSING_INVALIDATOR, ui_bind (&EditorRegions::add_region, this, _1), gui_context());
}

void
EditorRegions::set_session (ARDOUR::Session* s)
{
	EditorComponent::set_session (s);
	redisplay ();
}

void
EditorRegions::add_regions (vector<boost::shared_ptr<Region> >& regions)
{
	for (vector<boost::shared_ptr<Region> >::iterator x = regions.begin(); x != regions.end(); ++x) {
                add_region (*x);
	}
}

void
EditorRegions::add_region (boost::shared_ptr<Region> region)
{
	if (!region || !_session) {
		return;
	}

	string str;
	TreeModel::Row row;
	Gdk::Color c;
	bool missing_source = boost::dynamic_pointer_cast<SilentFileSource>(region->source());

	if (!_show_automatic_regions && region->automatic()) {
		return;
	}

	if (region->hidden()) {
		TreeModel::iterator iter = _model->get_iter ("0");
		TreeModel::Row parent;
		TreeModel::Row child;

		if (!iter) {
			parent = *(_model->append());
			parent[_columns.name] = _("Hidden");
			boost::shared_ptr<Region> proxy = parent[_columns.region];
			proxy.reset ();
		} else {
			if ((*iter)[_columns.name] != _("Hidden")) {
				parent = *(_model->insert(iter));
				parent[_columns.name] = _("Hidden");
				boost::shared_ptr<Region> proxy = parent[_columns.region];
				proxy.reset ();
			} else {
				parent = *iter;
			}
		}

		row = *(_model->append (parent.children()));

	} else if (region->whole_file()) {

		TreeModel::iterator i;
		TreeModel::Children rows = _model->children();

		for (i = rows.begin(); i != rows.end(); ++i) {
			boost::shared_ptr<Region> rr = (*i)[_columns.region];

			if (rr && region->region_list_equivalent (rr)) {
				return;
			}
		}

		row = *(_model->append());

		if (missing_source) {
			c.set_rgb(65535,0,0);     // FIXME: error color from style

		} else if (region->automatic()){
			c.set_rgb(0,65535,0);     // FIXME: error color from style

		} else {
			set_color(c, rgba_from_style ("RegionListWholeFile", 0xff, 0, 0, 0, "fg", Gtk::STATE_NORMAL, false ));

		}

		row[_columns.color_] = c;

		if (region->source()->name()[0] == '/') { // external file

			if (region->whole_file()) {

				boost::shared_ptr<AudioFileSource> afs = boost::dynamic_pointer_cast<AudioFileSource>(region->source());
				str = ".../";

				if (afs) {
					str = region_name_from_path (afs->path(), region->n_channels() > 1);
				} else {
					str += region->source()->name();
				}

			} else {
				str = region->name();
			}

		} else {
			str = region->name();
		}

		if (region->n_channels() > 1) {
			std::stringstream foo;
			foo << region->n_channels ();
			str += " [";
			str += foo.str();
			str += "]";
		}

		row[_columns.name] = str;
		row[_columns.region] = region;

		if (missing_source) {
			row[_columns.path] = _("(MISSING) ") + region->source()->name();

		} else {
			row[_columns.path] = region->source()->name();

		}

		if (region->automatic()) {
			return;
		}

	} else {

		/* find parent node, add as new child */

		TreeModel::iterator i;
		TreeModel::Children rows = _model->children();
		bool found_parent = false;

		for (i = rows.begin(); i != rows.end(); ++i) {
			boost::shared_ptr<Region> rr = (*i)[_columns.region];
			boost::shared_ptr<AudioRegion> r = boost::dynamic_pointer_cast<AudioRegion>(rr);

			if (r && r->whole_file()) {

				if (region->source_equivalent (r)) {
					row = *(_model->append ((*i).children()));
					found_parent = true;
					break;
				}
			}

			TreeModel::iterator ii;
			TreeModel::Children subrows = (*i).children();

			for (ii = subrows.begin(); ii != subrows.end(); ++ii) {
				boost::shared_ptr<Region> rrr = (*ii)[_columns.region];

				if (region->region_list_equivalent (rrr)) {
					return;

				}
			}
		}

		if (!found_parent) {
			row = *(_model->append());
		}
	}

	row[_columns.region] = region;

	populate_row(region, (*row));
}

void
EditorRegions::region_changed (boost::shared_ptr<Region> r, const PropertyChange& what_changed)
{
	if (what_changed.contains (ARDOUR::Properties::name) ||
            what_changed.contains (ARDOUR::Properties::start) ||
            what_changed.contains (ARDOUR::Properties::position) ||
            what_changed.contains (ARDOUR::Properties::length)) {

		/* find the region in our model and update its row */
		TreeModel::Children rows = _model->children ();
		TreeModel::iterator i = rows.begin ();
		while (i != rows.end ()) {
			TreeModel::Children children = (*i)->children ();
			TreeModel::iterator j = children.begin ();
			while (j != children.end()) {
				boost::shared_ptr<Region> c = (*j)[_columns.region];
				if (c == r) {
					break;
				}
				++j;
			}

			if (j != children.end()) {
                                populate_row (r, *j);
			}

			++i;
		}

	}

	if (what_changed.contains (ARDOUR::Properties::hidden)) {
		redisplay ();
	}
}

void
EditorRegions::selection_changed ()
{
	if (ignore_region_list_selection_change) {
		return;
	}

	if (_display.get_selection()->count_selected_rows() > 0) {

		TreeIter iter;
		TreeView::Selection::ListHandle_Path rows = _display.get_selection()->get_selected_rows ();

		_editor->get_selection().clear_regions ();

		for (TreeView::Selection::ListHandle_Path::iterator i = rows.begin(); i != rows.end(); ++i) {

			if (iter = _model->get_iter (*i)) { 
				boost::shared_ptr<Region> region = (*iter)[_columns.region];

                                // they could have clicked on a row that is just a placeholder, like "Hidden"

				if (region) {

					if (region->automatic()) {

						_display.get_selection()->unselect(*i);

					} else {
						_change_connection.block (true);
						cerr << "\tpush to region selection\n";
						_editor->set_selected_regionview_from_region_list (region, Selection::Add);

						_change_connection.block (false);
					}
				}
			}
		}
	} else {
		_editor->get_selection().clear_regions ();
	}
}

void
EditorRegions::set_selected (RegionSelection& regions)
{
	for (RegionSelection::iterator iter = regions.begin(); iter != regions.end(); ++iter) {

		TreeModel::iterator i;
		TreeModel::Children rows = _model->children();
		boost::shared_ptr<Region> r ((*iter)->region());

		for (i = rows.begin(); i != rows.end(); ++i) {

			boost::shared_ptr<Region> compared_region = (*i)[_columns.region];

			if (r == compared_region) {
				cerr << "\tpush into region list\n";
				_display.get_selection()->select(*i);
				break;
			}

			if (!(*i).children().empty()) {
				cerr << "\tlook for " << r->name() << " among children of " << (compared_region ? compared_region->name() : string ("NO REGION")) << endl;
				if (set_selected_in_subrow(r, (*i), 2)) {
					break;
				}
			}
		}
	}
}

bool
EditorRegions::set_selected_in_subrow (boost::shared_ptr<Region> region, TreeModel::Row const &parent_row, int level)
{
	TreeModel::iterator i;
	TreeModel::Children subrows = (*parent_row).children();

	for (i = subrows.begin(); i != subrows.end(); ++i) {

		boost::shared_ptr<Region> compared_region = (*i)[_columns.region];

		if (region == compared_region) {
			_display.get_selection()->select(*i);
			return true;
		}

		if (!(*i).children().empty()) {
			if (set_selected_in_subrow (region, (*i), level + 1)) {
				return true;
			}
		}
	}
	return false;
}

void
EditorRegions::insert_into_tmp_regionlist(boost::shared_ptr<Region> region)
{
	/* keep all whole files at the beginning */

	if (region->whole_file()) {
		tmp_region_list.push_front (region);
	} else {
		tmp_region_list.push_back (region);
	}
}

void
EditorRegions::redisplay ()
{
	if (_no_redisplay || !_session) {
		return;
	}

	bool tree_expanded = false;

	if (_toggle_full_action && _toggle_full_action->get_active()) {   //If the list was expanded prior to rebuilding,
		tree_expanded = true;																//expand it again afterwards
	}

	_display.set_model (Glib::RefPtr<Gtk::TreeStore>(0));
	_model->clear ();

	/* now add everything we have, via a temporary list used to help with
		sorting.
	*/

	tmp_region_list.clear();

        const RegionFactory::RegionMap& regions (RegionFactory::regions());
        for (RegionFactory::RegionMap::const_iterator i = regions.begin(); i != regions.end(); ++i) {
                insert_into_tmp_regionlist (i->second);
        }

	for (list<boost::shared_ptr<Region> >::iterator r = tmp_region_list.begin(); r != tmp_region_list.end(); ++r) {
		add_region (*r);
	}
	tmp_region_list.clear();

	_display.set_model (_model);

	if (tree_expanded) {
		_display.expand_all();
	}
}

void
EditorRegions::update_row (boost::shared_ptr<Region> region)
{
	if (!region || !_session) {
		return;
	}

	TreeModel::iterator i;
	TreeModel::Children rows = _model->children();
	
	return;

	for (i = rows.begin(); i != rows.end(); ++i) {

//		cerr << "Level 1: Compare " << region->name() << " with parent " << (*i)[_columns.name] << "\n";

		boost::shared_ptr<Region> compared_region = (*i)[_columns.region];

		if (region == compared_region) {
//			cerr << "Matched\n";
			populate_row(region, (*i));
			return;
		}

		if (!(*i).children().empty()) {
			if (update_subrows(region, (*i), 2)) {
				return;
			}
		}
	}

//	cerr << "Returning - No match\n";
}

bool
EditorRegions::update_subrows (boost::shared_ptr<Region> region, TreeModel::Row const &parent_row, int level)
{
	TreeModel::iterator i;
	TreeModel::Children subrows = (*parent_row).children();

	for (i = subrows.begin(); i != subrows.end(); ++i) {

//		cerr << "Level " << level << ": Compare " << region->name() << " with child " << (*i)[_columns.name] << "\n";

		boost::shared_ptr<Region> compared_region = (*i)[_columns.region];

		if (region == compared_region) {
			populate_row(region, (*i));
//			cerr << "Matched\n";
			return true;
		}

		if (!(*i).children().empty()) {
			if (update_subrows (region, (*i), level + 1)) {
				return true;
			}
		}
	}

	return false;
}

void
EditorRegions::update_all_rows ()
{
	if (!_session) {
		return;
	}
	
	TreeModel::iterator i;
	TreeModel::Children rows = _model->children();

	for (i = rows.begin(); i != rows.end(); ++i) {

		boost::shared_ptr<Region> region = (*i)[_columns.region];

		if (!region->automatic()) {
			cerr << "level 1 : Updating " << region->name() << "\n";
			populate_row(region, (*i));
		}

		if (!(*i).children().empty()) {
			update_all_subrows ((*i), 2);
		}
	}
}

void
EditorRegions::update_all_subrows (TreeModel::Row const &parent_row, int level)
{
	TreeModel::iterator i;
	TreeModel::Children subrows = (*parent_row).children();

	for (i = subrows.begin(); i != subrows.end(); ++i) {

		boost::shared_ptr<Region> region = (*i)[_columns.region];

		if (!region->automatic()) {
			cerr << "level " << level << " : Updating " << region->name() << "\n";
			populate_row(region, (*i));
		}

		if (!(*i).children().empty()) {
			update_all_subrows ((*i), level + 1);
		}
	}
}

void
EditorRegions::populate_row (boost::shared_ptr<Region> region, TreeModel::Row const &row)
{
	char start_str[16];
	char end_str[16];
	char length_str[16];
	char sync_str[16];
	char fadein_str[16];
	char fadeout_str[16];
	char used_str[8];
	int used;
	BBT_Time bbt;
	Timecode::Time timecode;

	bool missing_source = boost::dynamic_pointer_cast<SilentFileSource>(region->source());

	boost::shared_ptr<AudioRegion> audioRegion = boost::dynamic_pointer_cast<AudioRegion>(region);

	bool fades_in_seconds = false;

	start_str[0] = '\0';
	end_str[0] = '\0';
	length_str[0] = '\0';
	sync_str[0] = '\0';
	fadein_str[0] = '\0';
	fadeout_str[0] = '\0';
	used_str[0] = '\0';

	used = _editor->get_regionview_count_from_region_list (region);
	sprintf (used_str, "%4d" , used);

	switch (ARDOUR_UI::instance()->secondary_clock.mode ()) {
	case AudioClock::Timecode:
	case AudioClock::Off:												/* If the secondary clock is off, default to Timecode */
		_session->timecode_time (region->position(), timecode);
		sprintf (start_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
		_session->timecode_time (region->position() + region->length() - 1, timecode);
		sprintf (end_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
		_session->timecode_time (region->length(), timecode);
		sprintf (length_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
		_session->timecode_time (region->sync_position() + region->position(), timecode);
		sprintf (sync_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);

		if (audioRegion && !fades_in_seconds) {
			_session->timecode_time (audioRegion->fade_in()->back()->when, timecode);
			sprintf (fadein_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
			_session->timecode_time (audioRegion->fade_out()->back()->when, timecode);
			sprintf (fadeout_str, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
		}

		break;

	case AudioClock::BBT:
		_session->tempo_map().bbt_time (region->position(), bbt);
		sprintf (start_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
		_session->tempo_map().bbt_time (region->position() + region->length() - 1, bbt);
		sprintf (end_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
		_session->tempo_map().bbt_time (region->length(), bbt);
		sprintf (length_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
		_session->tempo_map().bbt_time (region->sync_position() + region->position(), bbt);
		sprintf (sync_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);

		if (audioRegion && !fades_in_seconds) {
			_session->tempo_map().bbt_time (audioRegion->fade_in()->back()->when, bbt);
			sprintf (fadein_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
			_session->tempo_map().bbt_time (audioRegion->fade_out()->back()->when, bbt);
			sprintf (fadeout_str, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
		}
		break;

	case AudioClock::MinSec:
		nframes_t left;
		int hrs;
		int mins;
		float secs;

		left = region->position();
		hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
		left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		secs = left / (float) _session->frame_rate();
		sprintf (start_str, "%02d:%02d:%06.3f", hrs, mins, secs);

		left = region->position() + region->length() - 1;
		hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
		left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		secs = left / (float) _session->frame_rate();
		sprintf (end_str, "%02d:%02d:%06.3f", hrs, mins, secs);

		left = region->length();
		hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
		left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		secs = left / (float) _session->frame_rate();
		sprintf (length_str, "%02d:%02d:%06.3f", hrs, mins, secs);

		left = region->sync_position() + region->position();
		hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
		left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		secs = left / (float) _session->frame_rate();
		sprintf (sync_str, "%02d:%02d:%06.3f", hrs, mins, secs);

		if (audioRegion && !fades_in_seconds) {
			left = audioRegion->fade_in()->back()->when;
			hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
			left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
			mins = (int) floor (left / (_session->frame_rate() * 60.0f));
			left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
			secs = left / (float) _session->frame_rate();
			sprintf (fadein_str, "%02d:%02d:%06.3f", hrs, mins, secs);

			left = audioRegion->fade_out()->back()->when;
			hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
			left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
			mins = (int) floor (left / (_session->frame_rate() * 60.0f));
			left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
			secs = left / (float) _session->frame_rate();
			sprintf (fadeout_str, "%02d:%02d:%06.3f", hrs, mins, secs);
		}

		break;

	case AudioClock::Frames:
		snprintf (start_str, sizeof (start_str), "%" PRId64, region->position());
		snprintf (end_str, sizeof (end_str), "%" PRId64, (region->last_frame()));
		snprintf (length_str, sizeof (length_str), "%" PRId64, region->length());
		snprintf (sync_str, sizeof (sync_str), "%" PRId64, region->sync_position() + region->position());

		if (audioRegion && !fades_in_seconds) {
			snprintf (fadein_str, sizeof (fadein_str), "%u", uint (audioRegion->fade_in()->back()->when));
			snprintf (fadeout_str, sizeof (fadeout_str), "%u", uint (audioRegion->fade_out()->back()->when));
		}

		break;

	default:
		break;
	}

	if (audioRegion && fades_in_seconds) {

		nframes_t left;
		int mins;
		int millisecs;

		left = audioRegion->fade_in()->back()->when;
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		millisecs = (int) floor ((left * 1000.0f) / _session->frame_rate());

		if (audioRegion->fade_in()->back()->when >= _session->frame_rate()) {
			sprintf (fadein_str, "%01dM %01dmS", mins, millisecs);
		} else {
			sprintf (fadein_str, "%01dmS", millisecs);
		}

		left = audioRegion->fade_out()->back()->when;
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		millisecs = (int) floor ((left * 1000.0f) / _session->frame_rate());

		if (audioRegion->fade_out()->back()->when >= _session->frame_rate()) {
			sprintf (fadeout_str, "%01dM %01dmS", mins, millisecs);
		} else {
			sprintf (fadeout_str, "%01dmS", millisecs);
		}
	}

	if (used > 1) {
		row[_columns.start] = _("Multiple");
		row[_columns.end] = _("Multiple");
		row[_columns.sync] = _("Multiple");
		row[_columns.fadein] = _("Multiple");
		row[_columns.fadeout] = _("Multiple");
		row[_columns.locked] = false;
		row[_columns.glued] = false;
		row[_columns.muted] = false;
		row[_columns.opaque] = false;
	} else {
		row[_columns.start] = start_str;
		row[_columns.end] = end_str;

		if (region->sync_position() == 0) {
			row[_columns.sync] = _("Start");
		} else if (region->sync_position() == region->length() - 1) {
			row[_columns.sync] = _("End");
		} else {
			row[_columns.sync] = sync_str;
		}

		if (audioRegion) {
			if (audioRegion->fade_in_active()) {
				row[_columns.fadein] = string_compose("%1%2%3", " ", fadein_str, " ");
			} else {
				row[_columns.fadein] = string_compose("%1%2%3", "(", fadein_str, ")");
			}
		} else {
			row[_columns.fadein] = "";
		}

		if (audioRegion) {
			if (audioRegion->fade_out_active()) {
				row[_columns.fadeout] = string_compose("%1%2%3", " ", fadeout_str, " ");
			} else {
				row[_columns.fadeout] = string_compose("%1%2%3", "(", fadeout_str, ")");
			}
		} else {
			row[_columns.fadeout] = "";
		}

		row[_columns.locked] = region->locked();

		if (region->positional_lock_style() == Region::MusicTime) {
			row[_columns.glued] = true;
		} else {
			row[_columns.glued] = false;
		}

		row[_columns.muted] = region->muted();
		row[_columns.opaque] = region->opaque();
	}

	row[_columns.length] = length_str;
	row[_columns.used] = used_str;

	if (missing_source) {
		row[_columns.path] = _("MISSING ") + region->source()->name();
	} else {
		row[_columns.path] = region->source()->name();
	}

	if (region->n_channels() > 1) {
		row[_columns.name] = string_compose("%1  [%2]", region->name(), region->n_channels());
	} else {
		row[_columns.name] = region->name();
	}
}

void
EditorRegions::build_menu ()
{
	_menu = dynamic_cast<Menu*>(ActionManager::get_widget ("/RegionListMenu"));

	/* now grab specific menu items that we need */

	Glib::RefPtr<Action> act;

	_hide_action = ActionManager::get_action (X_("RegionList"), X_("rlHide"));
	_show_action = ActionManager::get_action (X_("RegionList"), X_("rlShow"));

	act = ActionManager::get_action (X_("RegionList"), X_("rlShowAll"));
	if (act) {
		_toggle_full_action = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
	}

	act = ActionManager::get_action (X_("RegionList"), X_("rlShowAuto"));
	if (act) {
		_toggle_show_auto_regions_action = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
	}
}

void
EditorRegions::toggle_show_auto_regions ()
{
	_show_automatic_regions = _toggle_show_auto_regions_action->get_active();
	redisplay ();
}

void
EditorRegions::toggle_full ()
{
	if (_toggle_full_action->get_active()) {
		_display.expand_all ();
	} else {
		_display.collapse_all ();
	}
}

void
EditorRegions::show_context_menu (int button, int time)
{
	if (_menu == 0) {
		build_menu ();
	}

	if (_display.get_selection()->count_selected_rows() > 0) {
		ActionManager::set_sensitive (ActionManager::region_list_selection_sensitive_actions, true);
	} else {
		ActionManager::set_sensitive (ActionManager::region_list_selection_sensitive_actions, false);
	}

	/* Enable the "Show" option if any selected regions are hidden, and vice versa for "Hide" */

	bool have_shown = false;
	bool have_hidden = false;
	
	TreeView::Selection::ListHandle_Path rows = _display.get_selection()->get_selected_rows ();
	for (TreeView::Selection::ListHandle_Path::iterator i = rows.begin(); i != rows.end(); ++i) {
		TreeIter t = _model->get_iter (*i);
		boost::shared_ptr<Region> r = (*t)[_columns.region];
		if (r) {
			if (r->hidden ()) {
				have_hidden = true;
			} else {
				have_shown = true;
			}
		}
	}

	_hide_action->set_sensitive (have_shown);
	_show_action->set_sensitive (have_hidden);

	_menu->popup (button, time);
}

bool
EditorRegions::key_press (GdkEventKey* /*ev*/)
{
	return false;
}


bool
EditorRegions::button_press (GdkEventButton *ev)
{
	boost::shared_ptr<Region> region;
	TreeIter iter;
	TreeModel::Path path;
	TreeViewColumn* column;
	int cellx;
	int celly;

	if (_display.get_path_at_pos ((int)ev->x, (int)ev->y, path, column, cellx, celly)) {
		if ((iter = _model->get_iter (path))) {
			region = (*iter)[_columns.region];
		}
	}

	if (Keyboard::is_context_menu_event (ev)) {
		show_context_menu (ev->button, ev->time);
		return true;
	}

	if (region != 0 && Keyboard::is_button2_event (ev)) {
		// start/stop audition
		if (!Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
			_editor->consider_auditioning (region);
		}
		return true;
	}

	return false;
}

int
EditorRegions::sorter (TreeModel::iterator a, TreeModel::iterator b)
{
	int cmp = 0;

	boost::shared_ptr<Region> r1 = (*a)[_columns.region];
	boost::shared_ptr<Region> r2 = (*b)[_columns.region];

	/* handle rows without regions, like "Hidden" */

	if (r1 == 0) {
		return -1;
	}

	if (r2 == 0) {
		return 1;
	}

	boost::shared_ptr<AudioRegion> region1 = boost::dynamic_pointer_cast<AudioRegion> (r1);
	boost::shared_ptr<AudioRegion> region2 = boost::dynamic_pointer_cast<AudioRegion> (r2);

	if (region1 == 0 || region2 == 0) {
		Glib::ustring s1;
		Glib::ustring s2;
		switch (_sort_type) {
		case ByName:
			s1 = (*a)[_columns.name];
			s2 = (*b)[_columns.name];
			return (s1.compare (s2));
		default:
			return 0;
		}
	}

	switch (_sort_type) {
	case ByName:
		cmp = strcasecmp (region1->name().c_str(), region2->name().c_str());
		break;

	case ByLength:
		cmp = region1->length() - region2->length();
		break;

	case ByPosition:
		cmp = region1->position() - region2->position();
		break;

	case ByTimestamp:
		cmp = region1->source()->timestamp() - region2->source()->timestamp();
		break;

	case ByStartInFile:
		cmp = region1->start() - region2->start();
		break;

	case ByEndInFile:
		// cerr << "Compare " << (region1->start() + region1->length()) << " and " << (region2->start() + region2->length()) << endl;
		cmp = (region1->start() + region1->length()) - (region2->start() + region2->length());
		break;

	case BySourceFileName:
		cmp = strcasecmp (region1->source()->name().c_str(), region2->source()->name().c_str());
		break;

	case BySourceFileLength:
		cmp = region1->source_length(0) - region2->source_length(0);
		break;

	case BySourceFileCreationDate:
		cmp = region1->source()->timestamp() - region2->source()->timestamp();
		break;

	case BySourceFileFS:
		if (region1->source()->name() == region2->source()->name()) {
			cmp = strcasecmp (region1->name().c_str(),  region2->name().c_str());
		} else {
			cmp = strcasecmp (region1->source()->name().c_str(),  region2->source()->name().c_str());
		}
		break;
	}

	// cerr << "Comparison on " << enum_2_string (_sort_type) << " gives " << cmp << endl;

	if (cmp < 0) {
		return -1;
	} else if (cmp > 0) {
		return 1;
	} else {
		return 0;
	}
}

void
EditorRegions::reset_sort_type (RegionListSortType type, bool force)
{
	if (type != _sort_type || force) {
		_sort_type = type;
		_model->set_sort_func (0, (sigc::mem_fun (*this, &EditorRegions::sorter)));
	}
}

void
EditorRegions::reset_sort_direction (bool up)
{
	_model->set_sort_column (0, up ? SORT_ASCENDING : SORT_DESCENDING);
}

void
EditorRegions::selection_mapover (sigc::slot<void,boost::shared_ptr<Region> > sl)
{
	Glib::RefPtr<TreeSelection> selection = _display.get_selection();
	TreeView::Selection::ListHandle_Path rows = selection->get_selected_rows ();
	TreeView::Selection::ListHandle_Path::iterator i = rows.begin();

	if (selection->count_selected_rows() == 0 || _session == 0) {
		return;
	}

	for (; i != rows.end(); ++i) {
		TreeIter iter;

		if ((iter = _model->get_iter (*i))) {

			/* some rows don't have a region associated with them, but can still be
			   selected (XXX maybe prevent them from being selected)
			*/

			boost::shared_ptr<Region> r = (*iter)[_columns.region];

			if (r) {
				sl (r);
			}
		}
	}
}


void
EditorRegions::drag_data_received (const RefPtr<Gdk::DragContext>& context,
				   int x, int y,
				   const SelectionData& data,
				   guint info, guint time)
{
	vector<ustring> paths;

	if (data.get_target() == "GTK_TREE_MODEL_ROW") {
		_display.on_drag_data_received (context, x, y, data, info, time);
		return;
	}

	if (_editor->convert_drop_to_paths (paths, context, x, y, data, info, time) == 0) {
		nframes64_t pos = 0;
		if (Profile->get_sae() || Config->get_only_copy_imported_files()) {
			_editor->do_import (paths, Editing::ImportDistinctFiles, Editing::ImportAsRegion, SrcBest, pos);
		} else {
			_editor->do_embed (paths, Editing::ImportDistinctFiles, ImportAsRegion, pos);
		}
		context->drag_finish (true, false, time);
	}
}

bool
EditorRegions::selection_filter (const RefPtr<TreeModel>& model, const TreeModel::Path& path, bool /*yn*/)
{
	/* not possible to select rows that do not represent regions, like "Hidden" */

	TreeModel::iterator iter = model->get_iter (path);

	if (iter) {
		boost::shared_ptr<Region> r =(*iter)[_columns.region];
		if (!r) {
			return false;
		}
	}

	return true;
}

void
EditorRegions::name_edit (const Glib::ustring& path, const Glib::ustring& new_text)
{
	boost::shared_ptr<Region> region;
	TreeIter iter;

	if ((iter = _model->get_iter (path))) {
		region = (*iter)[_columns.region];
		(*iter)[_columns.name] = new_text;
	}

	/* now mapover everything */

	if (region) {
		vector<RegionView*> equivalents;
		_editor->get_regions_corresponding_to (region, equivalents);

		for (vector<RegionView*>::iterator i = equivalents.begin(); i != equivalents.end(); ++i) {
			if (new_text != (*i)->region()->name()) {
				(*i)->region()->set_name (new_text);
			}
		}
	}

}

boost::shared_ptr<Region>
EditorRegions::get_dragged_region ()
{
	list<boost::shared_ptr<Region> > regions;
	TreeView* source;
	_display.get_object_drag_data (regions, &source);
	assert (regions.size() == 1);
	return regions.front ();
}

void
EditorRegions::clear ()
{
	_display.set_model (Glib::RefPtr<Gtk::TreeStore> (0));
	_model->clear ();
	_display.set_model (_model);
}

boost::shared_ptr<Region>
EditorRegions::get_single_selection ()
{
	Glib::RefPtr<TreeSelection> selected = _display.get_selection();

	if (selected->count_selected_rows() != 1) {
		return boost::shared_ptr<Region> ();
	}

	TreeView::Selection::ListHandle_Path rows = selected->get_selected_rows ();

	/* only one row selected, so rows.begin() is it */

	TreeIter iter = _model->get_iter (*rows.begin());

	if (!iter) {
		return boost::shared_ptr<Region> ();
	}

	return (*iter)[_columns.region];
}
