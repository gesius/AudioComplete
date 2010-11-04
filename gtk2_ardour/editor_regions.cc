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
#include "ardour/session_playlists.h"
#include "ardour/silentfilesource.h"
#include "ardour/profile.h"

#include "gtkmm2ext/treeutils.h"

#include "editor.h"
#include "editing.h"
#include "keyboard.h"
#include "ardour_ui.h"
#include "gui_thread.h"
#include "actions.h"
#include "region_view.h"
#include "utils.h"
#include "editor_regions.h"
#include "editor_drag.h"

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
        , old_focus (0)
        , name_editable (0)
	, _menu (0)
	, _show_automatic_regions (true)
	, _sort_type ((Editing::RegionListSortType) 0)
 	, _no_redisplay (false) 
	, ignore_region_list_selection_change (false)
	, ignore_selected_region_change (false)
        , expanded (false)
{
	_display.set_size_request (100, -1);
	_display.set_name ("RegionListDisplay");
	_display.set_rules_hint (true);
	/* Try to prevent single mouse presses from initiating edits.
	   This relies on a hack in gtktreeview.c:gtk_treeview_button_press()
	*/
	_display.set_data ("mouse-edits-require-mod1", (gpointer) 0x1);

	_model = TreeStore::create (_columns);
	_model->set_sort_func (0, sigc::mem_fun (*this, &EditorRegions::sorter));
	_model->set_sort_column (0, SORT_ASCENDING);

	_display.set_model (_model);

	_display.append_column (_("Regions"), _columns.name);
	_display.append_column (_("Position"), _columns.position);
	_display.append_column (_("End"), _columns.end);
	_display.append_column (_("Length"), _columns.length);
	_display.append_column (_("Sync"), _columns.sync);
	_display.append_column (_("Fade In"), _columns.fadein);
	_display.append_column (_("Fade Out"), _columns.fadeout);
	_display.append_column (_("L"), _columns.locked);
	_display.append_column (_("G"), _columns.glued);
	_display.append_column (_("M"), _columns.muted);
	_display.append_column (_("O"), _columns.opaque);
	// _display.append_column (_("Used"), _columns.used);
	// _display.append_column (_("Path"), _columns.path);
	_display.set_headers_visible (true);
	//_display.set_grid_lines (TREE_VIEW_GRID_LINES_BOTH);

        /* show path as the row tooltip */
        _display.set_tooltip_column (15); /* path */

	CellRendererText* region_name_cell = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
	region_name_cell->property_editable() = true;
	region_name_cell->signal_edited().connect (sigc::mem_fun (*this, &EditorRegions::name_edit));
        region_name_cell->signal_editing_started().connect (sigc::mem_fun (*this, &EditorRegions::name_editing_started));

	_display.get_selection()->set_select_function (sigc::mem_fun (*this, &EditorRegions::selection_filter));

	TreeViewColumn* tv_col = _display.get_column(0);
	CellRendererText* renderer = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
	tv_col->add_attribute(renderer->property_text(), _columns.name);
	tv_col->add_attribute(renderer->property_foreground_gdk(), _columns.color_);

	CellRendererToggle* locked_cell = dynamic_cast<CellRendererToggle*> (_display.get_column_cell_renderer (7));
	locked_cell->property_activatable() = true;
	locked_cell->signal_toggled().connect (sigc::mem_fun (*this, &EditorRegions::locked_changed));
	TreeViewColumn* locked_col = _display.get_column (7);
	locked_col->add_attribute (locked_cell->property_visible(), _columns.property_toggles_visible);

	CellRendererToggle* glued_cell = dynamic_cast<CellRendererToggle*> (_display.get_column_cell_renderer (8));
	glued_cell->property_activatable() = true;
	glued_cell->signal_toggled().connect (sigc::mem_fun (*this, &EditorRegions::glued_changed));
	TreeViewColumn* glued_col = _display.get_column (8);
	glued_col->add_attribute (glued_cell->property_visible(), _columns.property_toggles_visible);

	CellRendererToggle* muted_cell = dynamic_cast<CellRendererToggle*> (_display.get_column_cell_renderer (9));
	muted_cell->property_activatable() = true;
	muted_cell->signal_toggled().connect (sigc::mem_fun (*this, &EditorRegions::muted_changed));
	TreeViewColumn* muted_col = _display.get_column (9);
	muted_col->add_attribute (muted_cell->property_visible(), _columns.property_toggles_visible);

	CellRendererToggle* opaque_cell = dynamic_cast<CellRendererToggle*> (_display.get_column_cell_renderer (10));
	opaque_cell->property_activatable() = true;
	opaque_cell->signal_toggled().connect (sigc::mem_fun (*this, &EditorRegions::opaque_changed));
	TreeViewColumn* opaque_col = _display.get_column (10);
	opaque_col->add_attribute (opaque_cell->property_visible(), _columns.property_toggles_visible);
	
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

	_display.signal_button_press_event().connect (sigc::mem_fun(*this, &EditorRegions::button_press), false);
	_change_connection = _display.get_selection()->signal_changed().connect (sigc::mem_fun(*this, &EditorRegions::selection_changed));

	_scroller.signal_key_press_event().connect (sigc::mem_fun(*this, &EditorRegions::key_press), false);
        _scroller.signal_focus_in_event().connect (sigc::mem_fun (*this, &EditorRegions::focus_in), false);
        _scroller.signal_focus_out_event().connect (sigc::mem_fun (*this, &EditorRegions::focus_out));

        _display.signal_enter_notify_event().connect (sigc::mem_fun (*this, &EditorRegions::enter_notify), false);
        _display.signal_leave_notify_event().connect (sigc::mem_fun (*this, &EditorRegions::leave_notify), false);

	// _display.signal_popup_menu().connect (sigc::bind (sigc::mem_fun (*this, &Editor::show__display_context_menu), 1, 0));

	//ARDOUR_UI::instance()->secondary_clock.mode_changed.connect (sigc::mem_fun(*this, &Editor::redisplay_regions));
	ARDOUR_UI::instance()->secondary_clock.mode_changed.connect (sigc::mem_fun(*this, &EditorRegions::update_all_rows));
	ARDOUR::Region::RegionPropertyChanged.connect (region_property_connection, MISSING_INVALIDATOR, ui_bind (&EditorRegions::region_changed, this, _1, _2), gui_context());
	ARDOUR::RegionFactory::CheckNewRegion.connect (check_new_region_connection, MISSING_INVALIDATOR, ui_bind (&EditorRegions::add_region, this, _1), gui_context());
}

bool
EditorRegions::focus_in (GdkEventFocus*)
{
        Window* win = dynamic_cast<Window*> (_scroller.get_toplevel ());

        if (win) {
                old_focus = win->get_focus ();
        } else {
                old_focus = 0;
        }

        name_editable = 0;

        /* try to do nothing on focus in (doesn't work, hence selection_count nonsense) */
        return true;
}

bool
EditorRegions::focus_out (GdkEventFocus*)
{
        if (old_focus) {
                old_focus->grab_focus ();
                old_focus = 0;
        }

        name_editable = 0;

        return false;
}

bool
EditorRegions::enter_notify (GdkEventCrossing* ev)
{
        /* arm counter so that ::selection_filter() will deny selecting anything for the 
           next two attempts to change selection status.
        */
        _scroller.grab_focus ();
        Keyboard::magic_widget_grab_focus ();
        return false;
}

bool
EditorRegions::leave_notify (GdkEventCrossing*)
{
        if (old_focus) {
                old_focus->grab_focus ();
                old_focus = 0;
        }

        name_editable = 0;
        Keyboard::magic_widget_drop_focus ();
        return false;
}

void
EditorRegions::set_session (ARDOUR::Session* s)
{
	SessionHandlePtr::set_session (s);
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
                        string s = (*iter)[_columns.name];
			if (s != _("Hidden")) {
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
		row[_columns.property_toggles_visible] = false;

		if (missing_source) {
                        row[_columns.path] = _("(MISSING) ") + region->source()->name();

		} else {
                        boost::shared_ptr<FileSource> fs = boost::dynamic_pointer_cast<FileSource>(region->source());
                        if (fs) {
                                row[_columns.path] = fs->path();
                        } else {
                                row[_columns.path] = region->source()->name();
                        }
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
			boost::shared_ptr<Region> r = (*i)[_columns.region];

			if (r && r->whole_file()) {

				if (region->source_equivalent (r)) {
					found_parent = true;
				}
			}
                        
			TreeModel::iterator ii;
			TreeModel::Children subrows = (*i).children();

			for (ii = subrows.begin(); ii != subrows.end(); ++ii) {
				boost::shared_ptr<Region> rr = (*ii)[_columns.region];

				if (region->region_list_equivalent (rr)) {
					return;
				}
			}

                        if (found_parent) {
                                row = *(_model->append ((*i).children()));
                                break;
                        }
		}

		if (!found_parent) {
			row = *(_model->append());
		}

		row[_columns.property_toggles_visible] = true;
	}

	row[_columns.region] = region;

	populate_row(region, (*row));
}

void
EditorRegions::region_changed (boost::shared_ptr<Region> r, const PropertyChange& what_changed)
{
        PropertyChange our_interests;

        our_interests.add (ARDOUR::Properties::name);
        our_interests.add (ARDOUR::Properties::position);
        our_interests.add (ARDOUR::Properties::length);
        our_interests.add (ARDOUR::Properties::start);
        our_interests.add (ARDOUR::Properties::locked);
        our_interests.add (ARDOUR::Properties::position_lock_style);
        our_interests.add (ARDOUR::Properties::muted);
        our_interests.add (ARDOUR::Properties::opaque);
        our_interests.add (ARDOUR::Properties::fade_in);
        our_interests.add (ARDOUR::Properties::fade_out);
	
	if (last_row != 0) {

		TreeModel::iterator j = _model->get_iter (last_row.get_path());
		boost::shared_ptr<Region> c = (*j)[_columns.region];

		if (c == r) {
			populate_row (r, (*j));
			
			if (what_changed.contains (ARDOUR::Properties::hidden)) {
				redisplay ();
			}
			
			return;
		}
	}


        if (what_changed.contains (our_interests)) {

		/* find the region in our model and update its row */
		TreeModel::Children rows = _model->children ();
		TreeModel::iterator i = rows.begin ();
		
		while (i != rows.end ()) {
			
			TreeModel::Children children = (*i)->children ();
			TreeModel::iterator j = children.begin ();
			
			while (j != children.end()) {
			  
				boost::shared_ptr<Region> c = (*j)[_columns.region];
			
				if (c == r) {
					last_row = TreeRowReference(_model, TreePath(j));
					break;
				}
				++j;
			}

			if (j != children.end()) {

                                boost::shared_ptr<AudioRegion> audioregion = boost::dynamic_pointer_cast<AudioRegion>(r);
                                uint32_t used = _editor->get_regionview_count_from_region_list (r);

                                if (what_changed.contains (ARDOUR::Properties::name)) {
                                        populate_row_name (r, *j);
                                }
                                if (what_changed.contains (ARDOUR::Properties::position)) {
                                        populate_row_position (r, *j, used);
                                        populate_row_end (r, *j, used);
                                }
                                if (what_changed.contains (ARDOUR::Properties::length)) {
                                        populate_row_end (r, *j, used);
                                        populate_row_length (r, *j);
                                }
                                if (what_changed.contains (ARDOUR::Properties::start)) {
                                        populate_row_length (r, *j);
                                }
                                if (what_changed.contains (ARDOUR::Properties::locked)) {
                                        populate_row_locked (r, *j, used);
                                }
                                if (what_changed.contains (ARDOUR::Properties::position_lock_style)) {
                                        populate_row_glued (r, *j, used);
                                }
                                if (what_changed.contains (ARDOUR::Properties::muted)) {
                                        populate_row_muted (r, *j, used);
                                }
                                if (what_changed.contains (ARDOUR::Properties::opaque)) {
                                        populate_row_opaque (r, *j, used);
                                }
                                if (what_changed.contains (ARDOUR::Properties::fade_in)) {
                                        populate_row_fade_in (r, *j, used, audioregion);
                                }
                                if (what_changed.contains (ARDOUR::Properties::fade_out)) {
                                        populate_row_fade_out (r, *j, used, audioregion);
                                }

                                break;
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

	_editor->_region_selection_change_updates_region_list = false;

	if (_display.get_selection()->count_selected_rows() > 0) {

		TreeIter iter;
		TreeView::Selection::ListHandle_Path rows = _display.get_selection()->get_selected_rows ();

		_editor->get_selection().clear_regions ();

		for (TreeView::Selection::ListHandle_Path::iterator i = rows.begin(); i != rows.end(); ++i) {

			if (iter = _model->get_iter (*i)) { 
				boost::shared_ptr<Region> region = (*iter)[_columns.region];

                                // they could have clicked on a row that is just a placeholder, like "Hidden"
                                // although that is not allowed by our selection filter. check it anyway
                                // since we need a region ptr.

				if (region) {
                                        
					if (region->automatic()) {

						_display.get_selection()->unselect(*i);

					} else {
						_change_connection.block (true);
						_editor->set_selected_regionview_from_region_list (region, Selection::Add);

						_change_connection.block (false);
					}
				}
			}
		}
	} else {
		_editor->get_selection().clear_regions ();
	}

	_editor->_region_selection_change_updates_region_list = true;
}

void
EditorRegions::set_selected (RegionSelection& regions)
{
	TreeModel::Children rows = _model->children();

	for (RegionSelection::iterator iter = regions.begin(); iter != regions.end(); ++iter) {

		TreeModel::iterator i;
		
		boost::shared_ptr<Region> r ((*iter)->region());

		for (i = rows.begin(); i != rows.end(); ++i) {

			boost::shared_ptr<Region> compared_region = (*i)[_columns.region];

			if (r == compared_region) {
				_display.get_selection()->select(*i);
				break;
			}

			if (!(*i).children().empty()) {
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

	/* If the list was expanded prior to rebuilding, expand it again afterwards */
	if (toggle_full_action()->get_active()) {
		tree_expanded = true;
	}

	_display.set_model (Glib::RefPtr<Gtk::TreeStore>(0));
	_model->clear ();

	/* now add everything we have, via a temporary list used to help with sorting */

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
			populate_row(region, (*i));
		}

		if (!(*i).children().empty()) {
			update_all_subrows ((*i), level + 1);
		}
	}
}

void
EditorRegions::format_position (framepos_t pos, char* buf, size_t bufsize)
{
	BBT_Time bbt;
	Timecode::Time timecode;

	switch (ARDOUR_UI::instance()->secondary_clock.mode ()) {
	case AudioClock::BBT:
		_session->tempo_map().bbt_time (pos, bbt);
		snprintf (buf, bufsize, "%03d|%02d|%04d" , bbt.bars, bbt.beats, bbt.ticks);
		break;

	case AudioClock::MinSec:
		framepos_t left;
		int hrs;
		int mins;
		float secs;

		left = pos;
		hrs = (int) floor (left / (_session->frame_rate() * 60.0f * 60.0f));
		left -= (nframes_t) floor (hrs * _session->frame_rate() * 60.0f * 60.0f);
		mins = (int) floor (left / (_session->frame_rate() * 60.0f));
		left -= (nframes_t) floor (mins * _session->frame_rate() * 60.0f);
		secs = left / (float) _session->frame_rate();
		snprintf (buf, bufsize, "%02d:%02d:%06.3f", hrs, mins, secs);
		break;

	case AudioClock::Frames:
		snprintf (buf, bufsize, "%" PRId64, pos);
		break;

	case AudioClock::Timecode:
	case AudioClock::Off: /* If the secondary clock is off, default to Timecode */
	default:
		_session->timecode_time (pos, timecode);
		snprintf (buf, bufsize, "%02d:%02d:%02d:%02d", timecode.hours, timecode.minutes, timecode.seconds, timecode.frames);
		break;
	}
}

void
EditorRegions::populate_row (boost::shared_ptr<Region> region, TreeModel::Row const &row)
{
	boost::shared_ptr<AudioRegion> audioregion = boost::dynamic_pointer_cast<AudioRegion>(region);
        uint32_t used = _session->playlists->region_use_count (region);

        populate_row_position (region, row, used);
        populate_row_end (region, row, used);
        populate_row_sync (region, row, used);
        populate_row_fade_in (region, row, used, audioregion);
        populate_row_fade_out (region, row, used, audioregion);
        populate_row_locked (region, row, used);
        populate_row_glued (region, row, used);
        populate_row_muted (region, row, used);
        populate_row_opaque (region, row, used);
        populate_row_length (region, row);
        populate_row_source (region, row);
        populate_row_name (region, row);
        populate_row_used (region, row, used);
}

#if 0
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
#endif

void
EditorRegions::populate_row_used (boost::shared_ptr<Region> region, TreeModel::Row const& row, uint32_t used)
{
        char buf[8];
	snprintf (buf, sizeof (buf), "%4d" , used);
	row[_columns.used] = buf;
}

void
EditorRegions::populate_row_length (boost::shared_ptr<Region> region, TreeModel::Row const &row)
{
        char buf[16];
        format_position (region->length(), buf, sizeof (buf));
        row[_columns.length] = buf;
}

void
EditorRegions::populate_row_end (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file()) {
                row[_columns.end] = "";
        } else if (used > 1) {
                row[_columns.end] = _("Mult.");
        } else {
                char buf[16];
                format_position (region->last_frame(), buf, sizeof (buf));
                row[_columns.end] = buf;
        }
}

void
EditorRegions::populate_row_position (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file()) {
                row[_columns.position] = "";
        } else if (used > 1) {
                row[_columns.position] = _("Mult.");
        } else {
                char buf[16];
                format_position (region->position(), buf, sizeof (buf));
                row[_columns.position] = buf;
        }
}

void
EditorRegions::populate_row_sync (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file()) {
                row[_columns.sync] = "";
        } else if (used > 1) {
                row[_columns.sync] = _("Mult."); /* translators: a short phrase for "multiple" as in "many" */
        } else {
		if (region->sync_position() == region->position()) {
			row[_columns.sync] = _("Start");
		} else if (region->sync_position() == (region->last_frame())) {
			row[_columns.sync] = _("End");
		} else {
                        char buf[16];
                        format_position (region->sync_position(), buf, sizeof (buf));
			row[_columns.sync] = buf;
		}
        }
}

void
EditorRegions::populate_row_fade_in (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used, boost::shared_ptr<AudioRegion> audioregion)
{
        if (!audioregion || region->whole_file()) {
			row[_columns.fadein] = "";
        } else {
                if (used > 1) {
                        row[_columns.fadein] = _("Multiple");
                } else {

                        char buf[16];
                        format_position (audioregion->fade_in()->back()->when, buf, sizeof (buf));
                        row[_columns.fadein] = buf;
                        
			if (audioregion->fade_in_active()) {
				row[_columns.fadein] = string_compose("%1%2%3", " ", buf, " ");
			} else {
				row[_columns.fadein] = string_compose("%1%2%3", "(", buf, ")");
			}
                }
        }
}

void
EditorRegions::populate_row_fade_out (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used, boost::shared_ptr<AudioRegion> audioregion)
{
        if (!audioregion || region->whole_file()) {
                row[_columns.fadeout] = "";
        } else {
                if (used > 1) {
                        row[_columns.fadeout] = _("Multiple");
                } else {
                        char buf[16];
                        format_position (audioregion->fade_out()->back()->when, buf, sizeof (buf));
                        
                        if (audioregion->fade_out_active()) {
                                row[_columns.fadeout] = string_compose("%1%2%3", " ", buf, " ");
                        } else {
                                row[_columns.fadeout] = string_compose("%1%2%3", "(", buf, ")");
                        }
                } 
        }
}
        
void
EditorRegions::populate_row_locked (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file()) {
                row[_columns.locked] = false;
        } else if (used > 1) {
                row[_columns.locked] = false;
        } else {
		row[_columns.locked] = region->locked();
        }
}

void
EditorRegions::populate_row_glued (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file() || used > 1) {
                row[_columns.glued] = false;
        } else {
		if (region->position_lock_style() == MusicTime) {
			row[_columns.glued] = true;
		} else {
			row[_columns.glued] = false;
		}
        }
}

void
EditorRegions::populate_row_muted (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file() || used > 1) {
                row[_columns.muted] = false;
        } else {
		row[_columns.muted] = region->muted();
        }
}

void
EditorRegions::populate_row_opaque (boost::shared_ptr<Region> region, TreeModel::Row const &row, uint32_t used)
{
        if (region->whole_file() || used > 1) {
                row[_columns.opaque] = false;
        } else {
		row[_columns.opaque] = region->opaque();
        }
}

void
EditorRegions::populate_row_name (boost::shared_ptr<Region> region, TreeModel::Row const &row)
{
	if (region->n_channels() > 1) {
		row[_columns.name] = string_compose("%1  [%2]", region->name(), region->n_channels());
	} else {
		row[_columns.name] = region->name();
	}
}        

void
EditorRegions::populate_row_source (boost::shared_ptr<Region> region, TreeModel::Row const &row)
{
        if (boost::dynamic_pointer_cast<SilentFileSource>(region->source())) {
		row[_columns.path] = _("MISSING ") + region->source()->name();
	} else {
		row[_columns.path] = region->source()->name();
	}
}

void
EditorRegions::toggle_show_auto_regions ()
{
	_show_automatic_regions = toggle_show_auto_regions_action()->get_active();
	redisplay ();
}

void
EditorRegions::toggle_full ()
{
	set_full (toggle_full_action()->get_active ());
}

void
EditorRegions::set_full (bool f)
{
	if (f) {
		_display.expand_all ();
                expanded = true;
	} else {
		_display.collapse_all ();
                expanded = false;
	}
}

void
EditorRegions::show_context_menu (int button, int time)
{
	if (_menu == 0) {
		_menu = dynamic_cast<Menu*> (ActionManager::get_widget ("/RegionListMenu"));
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

	hide_action()->set_sensitive (have_shown);
	show_action()->set_sensitive (have_hidden);

	_menu->popup (button, time);
}

bool
EditorRegions::key_press (GdkEventKey* ev)
{
        TreeViewColumn *col;

        switch (ev->keyval) {
        case GDK_Tab:
        case GDK_ISO_Left_Tab:
                
                if (name_editable) {
                        name_editable->editing_done ();
                        name_editable = 0;
                }

                col = _display.get_column (0); // select&focus on name column

                if (Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)) {
                        treeview_select_previous (_display, _model, col);
                } else {
                        treeview_select_next (_display, _model, col);
                }

                return true;
                break;

        default:
                break;
        }

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
		return false;
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
		std::string s1;
		std::string s2;
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
	vector<string> paths;

	if (data.get_target() == "GTK_TREE_MODEL_ROW") {
		/* something is being dragged over the region list */
		_editor->_drags->abort ();
		_display.on_drag_data_received (context, x, y, data, info, time);
		return;
	}

	if (_editor->convert_drop_to_paths (paths, context, x, y, data, info, time) == 0) {
		framepos_t pos = 0;
		if (Profile->get_sae() || Config->get_only_copy_imported_files()) {
			_editor->do_import (paths, Editing::ImportDistinctFiles, Editing::ImportAsRegion, SrcBest, pos);
		} else {
			_editor->do_embed (paths, Editing::ImportDistinctFiles, ImportAsRegion, pos);
		}
		context->drag_finish (true, false, time);
	}
}

bool
EditorRegions::selection_filter (const RefPtr<TreeModel>& model, const TreeModel::Path& path, bool already_selected)
{
	/* not possible to select rows that do not represent regions, like "Hidden" */

        if (already_selected) {
                /* deselecting anything is OK with us */
                return true;
        }

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
EditorRegions::name_editing_started (CellEditable* ce, const Glib::ustring&)
{
        name_editable = ce;
}
                          
void
EditorRegions::name_edit (const std::string& path, const std::string& new_text)
{
        name_editable = 0;

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

/** @return Region that has been dragged out of the list, or 0 */
boost::shared_ptr<Region>
EditorRegions::get_dragged_region ()
{
	list<boost::shared_ptr<Region> > regions;
	TreeView* source;
	_display.get_object_drag_data (regions, &source);

	if (regions.empty()) {
		return boost::shared_ptr<Region> ();
	}
	
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

void
EditorRegions::locked_changed (std::string const & path)
{
	TreeIter i = _model->get_iter (path);
	if (i) {
		boost::shared_ptr<ARDOUR::Region> region = (*i)[_columns.region];
		if (region) {
			region->set_locked (!(*i)[_columns.locked]);
		}
	}
}

void
EditorRegions::glued_changed (std::string const & path)
{
	TreeIter i = _model->get_iter (path);
	if (i) {
		boost::shared_ptr<ARDOUR::Region> region = (*i)[_columns.region];
		if (region) {
			/* `glued' means MusicTime, and we're toggling here */
			region->set_position_lock_style ((*i)[_columns.glued] ? AudioTime : MusicTime);
		}
	}

}

void
EditorRegions::muted_changed (std::string const & path)
{
	TreeIter i = _model->get_iter (path);
	if (i) {
		boost::shared_ptr<ARDOUR::Region> region = (*i)[_columns.region];
		if (region) {
			region->set_muted (!(*i)[_columns.muted]);
		}
	}

}

void
EditorRegions::opaque_changed (std::string const & path)
{
	TreeIter i = _model->get_iter (path);
	if (i) {
		boost::shared_ptr<ARDOUR::Region> region = (*i)[_columns.region];
		if (region) {
			region->set_opaque (!(*i)[_columns.opaque]);
		}
	}

}

XMLNode &
EditorRegions::get_state () const
{
	XMLNode* node = new XMLNode (X_("RegionList"));

	node->add_property (X_("sort-type"), enum_2_string (_sort_type));

	RefPtr<Action> act = ActionManager::get_action (X_("RegionList"), X_("SortAscending"));
	bool const ascending = RefPtr<RadioAction>::cast_dynamic(act)->get_active ();
	node->add_property (X_("sort-ascending"), ascending ? "yes" : "no");
	node->add_property (X_("show-all"), toggle_full_action()->get_active() ? "yes" : "no");
	node->add_property (X_("show-automatic-regions"), _show_automatic_regions ? "yes" : "no");

	return *node;
}
		
void
EditorRegions::set_state (const XMLNode & node)
{
        bool changed = false;

	if (node.name() != X_("RegionList")) {
		return;
	}

	XMLProperty const * p = node.property (X_("sort-type"));
	if (p) {
		Editing::RegionListSortType const t = static_cast<Editing::RegionListSortType> (string_2_enum (p->value(), _sort_type));
                if (_sort_type != t) {
                        changed = true;
                }
		reset_sort_type (t, true);
		RefPtr<RadioAction> ract = sort_type_action (t);
		ract->set_active ();
	}

	p = node.property (X_("sort-ascending"));
	if (p) {
		bool const yn = string_is_affirmative (p->value ());
                SortType old_sort_type;
                int old_sort_column;

                _model->get_sort_column_id (old_sort_column, old_sort_type);
                if (old_sort_type != (yn ? SORT_ASCENDING : SORT_DESCENDING)) {
                        changed = true;
                }
		reset_sort_direction (yn);
		RefPtr<Action> act;
		if (yn) {
			act = ActionManager::get_action (X_("RegionList"), X_("SortAscending"));
		} else {
			act = ActionManager::get_action (X_("RegionList"), X_("SortDescending"));
		}

		RefPtr<RadioAction>::cast_dynamic(act)->set_active ();
	}

	p = node.property (X_("show-all"));
	if (p) {
		bool const yn = string_is_affirmative (p->value ());
                if (expanded != yn) {
                        changed = true;
                }
		set_full (yn);
		toggle_full_action()->set_active (yn);
	}

	p = node.property (X_("show-automatic-regions"));
	if (p) {
		bool const yn = string_is_affirmative (p->value ());
                if (yn != _show_automatic_regions) {
                        _show_automatic_regions = yn;
                        toggle_show_auto_regions_action()->set_active (yn);
                        /* no need to set changed because the above toggle 
                           will have triggered a redisplay 
                        */
                }
        }
        
        if (changed) {
                redisplay ();
        }
}

RefPtr<RadioAction>
EditorRegions::sort_type_action (Editing::RegionListSortType t) const
{
	const char* action = 0;

	switch (t) {
	case Editing::ByName:
		action = X_("SortByRegionName");
		break;
	case Editing::ByLength:
		action = X_("SortByRegionLength");
		break;
	case Editing::ByPosition:
		action = X_("SortByRegionPosition");
		break;
	case Editing::ByTimestamp:
		action = X_("SortByRegionTimestamp");
		break;
	case Editing::ByStartInFile:
		action = X_("SortByRegionStartinFile");
		break;
	case Editing::ByEndInFile:
		action = X_("SortByRegionEndinFile");
		break;
	case Editing::BySourceFileName:
		action = X_("SortBySourceFileName");
		break;
	case Editing::BySourceFileLength:
		action = X_("SortBySourceFileLength");
		break;
	case Editing::BySourceFileCreationDate:
		action = X_("SortBySourceFileCreationDate");
		break;
	case Editing::BySourceFileFS:
		action = X_("SortBySourceFilesystem");
		break;
	default:
		fatal << string_compose (_("programming error: %1: %2"), "EditorRegions: impossible sort type", (int) t) << endmsg;
		/*NOTREACHED*/
	}

	RefPtr<Action> act = ActionManager::get_action (X_("RegionList"), action);
	assert (act);

	return RefPtr<RadioAction>::cast_dynamic (act);
}

RefPtr<Action>
EditorRegions::hide_action () const
{
	return ActionManager::get_action (X_("RegionList"), X_("rlHide"));
	
}

RefPtr<Action>
EditorRegions::show_action () const
{
	return ActionManager::get_action (X_("RegionList"), X_("rlShow"));
}

RefPtr<ToggleAction>
EditorRegions::toggle_full_action () const
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("RegionList"), X_("rlShowAll"));
	assert (act);
	return Glib::RefPtr<ToggleAction>::cast_dynamic (act);
}

RefPtr<ToggleAction>
EditorRegions::toggle_show_auto_regions_action () const
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("RegionList"), X_("rlShowAuto"));
	assert (act);
	return Glib::RefPtr<ToggleAction>::cast_dynamic (act);
}
