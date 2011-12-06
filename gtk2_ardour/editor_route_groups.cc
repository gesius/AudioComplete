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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <cstdlib>
#include <cmath>

#include <gtkmm2ext/gtk_ui.h>
#include "ardour/route_group.h"

#include "editor.h"
#include "keyboard.h"
#include "marker.h"
#include "time_axis_view.h"
#include "prompter.h"
#include "gui_thread.h"
#include "editor_group_tabs.h"
#include "route_group_dialog.h"
#include "route_time_axis.h"
#include "editor_routes.h"
#include "editor_route_groups.h"
#include "ardour_ui.h"

#include "ardour/route.h"
#include "ardour/session.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using Gtkmm2ext::Keyboard;

struct ColumnInfo {
    int         index;
    const char* label;
    const char* tooltip;
};

EditorRouteGroups::EditorRouteGroups (Editor* e)
	: EditorComponent (e)
	, _all_group_active_button (_("No Selection = All Tracks"))
	, _in_row_change (false)
	, _in_rebuild (false)
{
	_model = ListStore::create (_columns);
	_display.set_model (_model);

	_display.append_column ("", _columns.text);
	_display.append_column ("", _columns.gain);
	_display.append_column ("", _columns.gain_relative);
	_display.append_column ("", _columns.mute);
	_display.append_column ("", _columns.solo);
	_display.append_column ("", _columns.record);
	_display.append_column ("", _columns.select);
	_display.append_column ("", _columns.edits);
	_display.append_column ("", _columns.active_state);
	_display.append_column ("", _columns.is_visible);

	TreeViewColumn* col;
	Gtk::Label* l;

	ColumnInfo ci[] = {
		{ 0, _("Name"), _("Name of Group") },
		{ 1, _("G"), _("Sharing Gain?") },
		{ 2, _("Rel"), _("Relevative Gain Changes?") },
		{ 3, _("M"), _("Sharing Mute?") },
		{ 4, _("S"), _("Sharing Solo?") },
		{ 5, _("Rec"), _("Sharing Record-enable Status?") },
		{ 6, _("Sel"), _("Sharing Selected Status?") },
		{ 7, _("E"), _("Sharing Editing?") },
		{ 8, _("A"), _("Sharing Active Status?") },
		{ 9, _("Show"), _("Group is visible?") },
		{ -1, 0, 0 }
	};

	for (int i = 0; ci[i].index >= 0; ++i) {
		col = _display.get_column (ci[i].index);
		l = manage (new Label (ci[i].label));
		ARDOUR_UI::instance()->set_tip (*l, ci[i].tooltip);
		col->set_widget (*l);
		l->show ();

		col->set_data (X_("colnum"), GUINT_TO_POINTER(i));
		if (i == 0) {
			col->set_expand (true);
		} else {
			col->set_expand (false);
			col->set_alignment (ALIGN_CENTER);
		}
	}

	_display.set_headers_visible (true);

	/* name is directly editable */

	CellRendererText* name_cell = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
	name_cell->property_editable() = true;
	name_cell->signal_edited().connect (sigc::mem_fun (*this, &EditorRouteGroups::name_edit));
	
	/* use checkbox for the active + visible columns */
	
	for (int i = 1; ci[i].index >= 0; ++i) {
		CellRendererToggle* active_cell = dynamic_cast <CellRendererToggle*> (_display.get_column_cell_renderer (i));
		active_cell->property_activatable() = true;
		active_cell->property_radio() = false;
	}

	_model->signal_row_changed().connect (sigc::mem_fun (*this, &EditorRouteGroups::row_change));
	/* What signal would you guess was emitted when the rows of your treeview are reordered
	   by a drag and drop?  signal_rows_reordered?  That would be far too easy.
	   No, signal_row_deleted().
	 */
	_model->signal_row_deleted().connect (sigc::mem_fun (*this, &EditorRouteGroups::row_deleted));

	_display.set_name ("EditGroupList");
	_display.get_selection()->set_mode (SELECTION_SINGLE);
	_display.set_headers_visible (true);
	_display.set_reorderable (true);
	_display.set_rules_hint (true);
	_display.set_size_request (75, -1);

	_scroller.add (_display);
	_scroller.set_policy (POLICY_AUTOMATIC, POLICY_AUTOMATIC);

	_display.signal_button_press_event().connect (sigc::mem_fun(*this, &EditorRouteGroups::button_press_event), false);

	HBox* button_box = manage (new HBox());
	button_box->set_homogeneous (true);

	Button* add_button = manage (new Button ());
	Button* remove_button = manage (new Button ());

	Widget* w;

	w = manage (new Image (Stock::ADD, ICON_SIZE_BUTTON));
	w->show();
	add_button->add (*w);

	w = manage (new Image (Stock::REMOVE, ICON_SIZE_BUTTON));
	w->show();
	remove_button->add (*w);

	add_button->signal_clicked().connect (sigc::hide_return (sigc::mem_fun (*this, &EditorRouteGroups::run_new_group_dialog)));
	remove_button->signal_clicked().connect (sigc::mem_fun (*this, &EditorRouteGroups::remove_selected));

	button_box->pack_start (*add_button);
	button_box->pack_start (*remove_button);

	_all_group_active_button.show ();

	_display_packer.pack_start (_scroller, true, true);
	_display_packer.pack_start (_all_group_active_button, false, false);
	_display_packer.pack_start (*button_box, false, false);

	_all_group_active_button.signal_toggled().connect (sigc::mem_fun (*this, &EditorRouteGroups::all_group_toggled));
	_all_group_active_button.set_name (X_("EditorRouteGroupsAllGroupButton"));
	ARDOUR_UI::instance()->set_tip (_all_group_active_button, _("Activate this button to operate on all tracks when none are selected."));
}

void
EditorRouteGroups::remove_selected ()
{
	Glib::RefPtr<TreeSelection> selection = _display.get_selection();
	TreeView::Selection::ListHandle_Path rows = selection->get_selected_rows ();

	if (rows.empty()) {
		return;
	}

	TreeView::Selection::ListHandle_Path::iterator i = rows.begin();
	TreeIter iter;

	/* selection mode is single, so rows.begin() is it */

	if ((iter = _model->get_iter (*i))) {

		RouteGroup* rg = (*iter)[_columns.routegroup];

		if (rg) {
			_session->remove_route_group (*rg);
		}
	}
}

void
EditorRouteGroups::button_clicked ()
{
	run_new_group_dialog ();
}

gint
EditorRouteGroups::button_press_event (GdkEventButton* ev)
{
	TreeModel::Path path;
	TreeIter iter;
	RouteGroup* group = 0;
	TreeViewColumn* column;
	int cellx;
	int celly;

	bool const p = _display.get_path_at_pos ((int)ev->x, (int)ev->y, path, column, cellx, celly);

	if (p) {
		iter = _model->get_iter (path);
	}

	if (iter) {
		group = (*iter)[_columns.routegroup];
	}

	if (Keyboard::is_context_menu_event (ev)) {
		_editor->_group_tabs->get_menu(group)->popup (1, ev->time);
		return true;
	}

	if (!p) {
		return 1;
	}

	switch (GPOINTER_TO_UINT (column->get_data (X_("colnum")))) {
	case 0:
		if (Keyboard::is_edit_event (ev)) {
			if ((iter = _model->get_iter (path))) {
				if ((group = (*iter)[_columns.routegroup]) != 0) {
#ifdef GTKOSX
					_display.queue_draw();
#endif
					return true;
				}
			}

		}
		break;

	case 1:
		if ((iter = _model->get_iter (path))) {
			bool gain = (*iter)[_columns.gain];
			(*iter)[_columns.gain] = !gain;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 2:
		if ((iter = _model->get_iter (path))) {
			bool gain_relative = (*iter)[_columns.gain_relative];
			(*iter)[_columns.gain_relative] = !gain_relative;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 3:
		if ((iter = _model->get_iter (path))) {
			bool mute = (*iter)[_columns.mute];
			(*iter)[_columns.mute] = !mute;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 4:
		if ((iter = _model->get_iter (path))) {
			bool solo = (*iter)[_columns.solo];
			(*iter)[_columns.solo] = !solo;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 5:
		if ((iter = _model->get_iter (path))) {
			bool record = (*iter)[_columns.record];
			(*iter)[_columns.record] = !record;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 6:
		if ((iter = _model->get_iter (path))) {
			bool select = (*iter)[_columns.select];
			(*iter)[_columns.select] = !select;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 7:
		if ((iter = _model->get_iter (path))) {
			bool edits = (*iter)[_columns.edits];
			(*iter)[_columns.edits] = !edits;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 8:
		if ((iter = _model->get_iter (path))) {
			bool active_state = (*iter)[_columns.active_state];
			(*iter)[_columns.active_state] = !active_state;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;

	case 9:
		if ((iter = _model->get_iter (path))) {
			bool is_visible = (*iter)[_columns.is_visible];
			(*iter)[_columns.is_visible] = !is_visible;
#ifdef GTKOSX
			_display.queue_draw();
#endif
			return true;
		}
		break;
		
	default:
		break;
	}

	return false;
 }

void
EditorRouteGroups::row_change (const Gtk::TreeModel::Path&, const Gtk::TreeModel::iterator& iter)
{
	RouteGroup* group;

	if (_in_row_change) {
		return;
	}

	if ((group = (*iter)[_columns.routegroup]) == 0) {
		return;
	}

	PropertyList plist;
	plist.add (Properties::name, string ((*iter)[_columns.text]));
	
	bool val = (*iter)[_columns.gain];
	plist.add (Properties::gain, val);
	val = (*iter)[_columns.gain_relative];
	plist.add (Properties::relative, val);
	val = (*iter)[_columns.mute];
	plist.add (Properties::mute, val);
	val = (*iter)[_columns.solo];
	plist.add (Properties::solo, val);
	val = (*iter)[_columns.record];
	plist.add (Properties::recenable, val);
	val = (*iter)[_columns.select];
	plist.add (Properties::select, val);
	val = (*iter)[_columns.edits];
	plist.add (Properties::edit, val);
	val = (*iter)[_columns.active_state];
	plist.add (Properties::route_active, val);

	group->set_hidden (!(*iter)[_columns.is_visible], this);

	group->apply_changes (plist);
}

void
EditorRouteGroups::add (RouteGroup* group)
{
	ENSURE_GUI_THREAD (*this, &EditorRouteGroups::add, group)
	bool focus = false;

	TreeModel::Row row = *(_model->append());

	row[_columns.gain] = group->is_gain ();
	row[_columns.gain_relative] = group->is_relative ();
	row[_columns.mute] = group->is_mute ();
	row[_columns.solo] = group->is_solo ();
	row[_columns.record] = group->is_recenable();
	row[_columns.select] = group->is_select ();
	row[_columns.edits] = group->is_edit ();
	row[_columns.active_state] = group->is_route_active ();
	row[_columns.is_visible] = !group->is_hidden();

	_in_row_change = true;

	row[_columns.routegroup] = group;

	if (!group->name().empty()) {
		row[_columns.text] = group->name();
	} else {
		row[_columns.text] = _("unnamed");
		focus = true;
	}

	group->PropertyChanged.connect (_property_changed_connections, MISSING_INVALIDATOR, ui_bind (&EditorRouteGroups::property_changed, this, group, _1), gui_context());

	if (focus) {
		TreeViewColumn* col = _display.get_column (0);
		CellRendererText* name_cell = dynamic_cast<CellRendererText*>(_display.get_column_cell_renderer (0));
		_display.set_cursor (_model->get_path (row), *col, *name_cell, true);
	}

	_in_row_change = false;

	_editor->_group_tabs->set_dirty ();
}

void
EditorRouteGroups::groups_changed ()
{
	ENSURE_GUI_THREAD (*this, &EditorRouteGroups::groups_changed);

	_in_rebuild = true;

	/* just rebuild the while thing */

	_model->clear ();

	if (_session) {
		_session->foreach_route_group (sigc::mem_fun (*this, &EditorRouteGroups::add));
	}

	_in_rebuild = false;
}

void
EditorRouteGroups::property_changed (RouteGroup* group, const PropertyChange& change)
{
	_in_row_change = true;

	Gtk::TreeModel::Children children = _model->children();

	for(Gtk::TreeModel::Children::iterator iter = children.begin(); iter != children.end(); ++iter) {
		if (group == (*iter)[_columns.routegroup]) {
			(*iter)[_columns.text] = group->name();
			(*iter)[_columns.gain] = group->is_gain ();
			(*iter)[_columns.gain_relative] = group->is_relative ();
			(*iter)[_columns.mute] = group->is_mute ();
			(*iter)[_columns.solo] = group->is_solo ();
			(*iter)[_columns.record] = group->is_recenable ();
			(*iter)[_columns.select] = group->is_select ();
			(*iter)[_columns.edits] = group->is_edit ();
			(*iter)[_columns.active_state] = group->is_route_active ();
			(*iter)[_columns.is_visible] = !group->is_hidden();
		}
	}

	_in_row_change = false;

	if (change.contains (Properties::name) || change.contains (Properties::active)) {
		_editor->_group_tabs->set_dirty ();
	}

	for (TrackViewList::const_iterator i = _editor->get_track_views().begin(); i != _editor->get_track_views().end(); ++i) {
		if ((*i)->route_group() == group) {
			if (group->is_hidden ()) {
				_editor->hide_track_in_display (*i);
			} else {
				_editor->_routes->show_track_in_display (**i);
			}
		}
	}
}

void
EditorRouteGroups::name_edit (const std::string& path, const std::string& new_text)
{
	RouteGroup* group;
	TreeIter iter;

	if ((iter = _model->get_iter (path))) {

		if ((group = (*iter)[_columns.routegroup]) == 0) {
			return;
		}

		if (new_text != group->name()) {
			group->set_name (new_text);
		}
	}
}

void
EditorRouteGroups::clear ()
{
	_display.set_model (Glib::RefPtr<Gtk::TreeStore> (0));
	_model->clear ();
	_display.set_model (_model);
}

void
EditorRouteGroups::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);

	if (_session) {

		RouteGroup& arg (_session->all_route_group());

		arg.PropertyChanged.connect (all_route_groups_changed_connection, MISSING_INVALIDATOR, ui_bind (&EditorRouteGroups::all_group_changed, this, _1), gui_context());

		_session->route_group_added.connect (_session_connections, MISSING_INVALIDATOR, ui_bind (&EditorRouteGroups::add, this, _1), gui_context());
		_session->route_group_removed.connect (
			_session_connections, MISSING_INVALIDATOR, boost::bind (&EditorRouteGroups::groups_changed, this), gui_context()
			);
		_session->route_groups_reordered.connect (
			_session_connections, MISSING_INVALIDATOR, boost::bind (&EditorRouteGroups::groups_changed, this), gui_context()
			);
	}

	PBD::PropertyChange pc;
	pc.add (Properties::select);
	pc.add (Properties::active);
	all_group_changed (pc);

	groups_changed ();
}

void
EditorRouteGroups::run_new_group_dialog ()
{
	RouteList rl;

	return _editor->_group_tabs->run_new_group_dialog (rl);
}

void
EditorRouteGroups::all_group_toggled ()
{
	if (_session) {
		_session->all_route_group().set_select (_all_group_active_button.get_active());
	}
}

void
EditorRouteGroups::all_group_changed (const PropertyChange&)
{
	if (_session) {
		RouteGroup& arg (_session->all_route_group());
		_all_group_active_button.set_active (arg.is_active() && arg.is_select());
	} else {
		_all_group_active_button.set_active (false);
	}
}

/** Called when a model row is deleted, but also when the model is
 *  reordered by a user drag-and-drop; the latter is what we are
 *  interested in here.
 */
void
EditorRouteGroups::row_deleted (Gtk::TreeModel::Path const &)
{
	if (_in_rebuild) {
		/* We need to ignore this in cases where we're not doing a drag-and-drop
		   re-order.
		*/
		return;
	}

	/* Re-write the session's route group list so that the new order is preserved */

	list<RouteGroup*> new_list;

	Gtk::TreeModel::Children children = _model->children();
	for (Gtk::TreeModel::Children::iterator i = children.begin(); i != children.end(); ++i) {
		new_list.push_back ((*i)[_columns.routegroup]);
	}

	_session->reorder_route_groups (new_list);
}


