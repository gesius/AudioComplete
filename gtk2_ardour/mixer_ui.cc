/*
    Copyright (C) 2000-2004 Paul Davis

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

#include <algorithm>
#include <map>
#include <sigc++/bind.h>

#include <gtkmm/accelmap.h>

#include "pbd/convert.h"
#include "pbd/stacktrace.h"
#include <glibmm/thread.h>

#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/tearoff.h>
#include <gtkmm2ext/window_title.h>

#include "ardour/audio_track.h"
#include "ardour/plugin_manager.h"
#include "ardour/route_group.h"
#include "ardour/session.h"
#include "ardour/session_route.h"

#include "keyboard.h"
#include "mixer_ui.h"
#include "mixer_strip.h"
#include "monitor_section.h"
#include "plugin_selector.h"
#include "ardour_ui.h"
#include "prompter.h"
#include "utils.h"
#include "actions.h"
#include "gui_thread.h"
#include "mixer_group_tabs.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Glib;
using namespace Gtkmm2ext;
using namespace std;

using PBD::atoi;

Mixer_UI::Mixer_UI ()
	: Window (Gtk::WINDOW_TOPLEVEL)
{
	_strip_width = Config->get_default_narrow_ms() ? Narrow : Wide;
	track_menu = 0;
        _monitor_section = 0;
	no_track_list_redisplay = false;
	in_group_row_change = false;
	_visible = false;
	strip_redisplay_does_not_reset_order_keys = false;
	strip_redisplay_does_not_sync_order_keys = false;
	ignore_sync = false;

	Route::SyncOrderKeys.connect (*this, invalidator (*this), ui_bind (&Mixer_UI::sync_order_keys, this, _1), gui_context());

	scroller_base.add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	scroller_base.set_name ("MixerWindow");
	scroller_base.signal_button_release_event().connect (sigc::mem_fun(*this, &Mixer_UI::strip_scroller_button_release));
	// add as last item of strip packer
	strip_packer.pack_end (scroller_base, true, true);

	_group_tabs = new MixerGroupTabs (this);
	VBox* b = manage (new VBox);
	b->pack_start (*_group_tabs, PACK_SHRINK);
	b->pack_start (strip_packer);
	b->show_all ();

	scroller.add (*b);
	scroller.set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	setup_track_display ();

	group_model = ListStore::create (group_columns);
	group_display.set_model (group_model);
	group_display.append_column (_("Group"), group_columns.text);
	group_display.append_column (_("Show"), group_columns.visible);
	group_display.get_column (0)->set_data (X_("colnum"), GUINT_TO_POINTER(0));
	group_display.get_column (1)->set_data (X_("colnum"), GUINT_TO_POINTER(1));
	group_display.get_column (0)->set_expand(true);
	group_display.get_column (1)->set_expand(false);
	group_display.set_name ("MixerGroupList");
	group_display.get_selection()->set_mode (Gtk::SELECTION_SINGLE);
	group_display.set_reorderable (true);
	group_display.set_headers_visible (true);
	group_display.set_rules_hint (true);

	/* name is directly editable */

	CellRendererText* name_cell = dynamic_cast<CellRendererText*>(group_display.get_column_cell_renderer (0));
	name_cell->property_editable() = true;
	name_cell->signal_edited().connect (sigc::mem_fun (*this, &Mixer_UI::route_group_name_edit));

	/* use checkbox for the active column */

	CellRendererToggle* active_cell = dynamic_cast<CellRendererToggle*>(group_display.get_column_cell_renderer (1));
	active_cell->property_activatable() = true;
	active_cell->property_radio() = false;

	group_model->signal_row_changed().connect (sigc::mem_fun (*this, &Mixer_UI::route_group_row_change));
	/* We use this to notice drag-and-drop reorders of the group list */
	group_model->signal_row_deleted().connect (sigc::mem_fun (*this, &Mixer_UI::route_group_row_deleted));
	group_display.signal_button_press_event().connect (sigc::mem_fun (*this, &Mixer_UI::group_display_button_press), false);

	group_display_scroller.add (group_display);
	group_display_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	HBox* route_group_display_button_box = manage (new HBox());

	Button* route_group_add_button = manage (new Button ());
	Button* route_group_remove_button = manage (new Button ());

	Widget* w;

	w = manage (new Image (Stock::ADD, ICON_SIZE_BUTTON));
	w->show();
	route_group_add_button->add (*w);

	w = manage (new Image (Stock::REMOVE, ICON_SIZE_BUTTON));
	w->show();
	route_group_remove_button->add (*w);

	route_group_display_button_box->set_homogeneous (true);

	route_group_add_button->signal_clicked().connect (sigc::mem_fun (*this, &Mixer_UI::new_route_group));
	route_group_remove_button->signal_clicked().connect (sigc::mem_fun (*this, &Mixer_UI::remove_selected_route_group));

	route_group_display_button_box->add (*route_group_add_button);
	route_group_display_button_box->add (*route_group_remove_button);

	group_display_vbox.pack_start (group_display_scroller, true, true);
	group_display_vbox.pack_start (*route_group_display_button_box, false, false);

	group_display_frame.set_name ("BaseFrame");
	group_display_frame.set_shadow_type (Gtk::SHADOW_IN);
	group_display_frame.add (group_display_vbox);

	rhs_pane1.pack1 (track_display_frame);
	rhs_pane1.pack2 (group_display_frame);

	list_vpacker.pack_start (rhs_pane1, true, true);

	global_hpacker.pack_start (scroller, true, true);
#ifdef GTKOSX
	/* current gtk-quartz has dirty updates on borders like this one */
	global_hpacker.pack_start (out_packer, false, false, 0);
#else
	global_hpacker.pack_start (out_packer, false, false, 12);
#endif
	list_hpane.pack1(list_vpacker, true, true);
	list_hpane.pack2(global_hpacker, true, false);

	rhs_pane1.signal_size_allocate().connect (sigc::bind (sigc::mem_fun(*this, &Mixer_UI::pane_allocation_handler),
							static_cast<Gtk::Paned*> (&rhs_pane1)));
	list_hpane.signal_size_allocate().connect (sigc::bind (sigc::mem_fun(*this, &Mixer_UI::pane_allocation_handler),
							 static_cast<Gtk::Paned*> (&list_hpane)));

	global_vpacker.pack_start (list_hpane, true, true);

	add (global_vpacker);
	set_name ("MixerWindow");

	update_title ();

	set_wmclass (X_("ardour_mixer"), PROGRAM_NAME);

	add_accel_group (ActionManager::ui_manager->get_accel_group());

	signal_delete_event().connect (sigc::mem_fun (*this, &Mixer_UI::hide_window));
	add_events (Gdk::KEY_PRESS_MASK|Gdk::KEY_RELEASE_MASK);

	signal_configure_event().connect (sigc::mem_fun (*ARDOUR_UI::instance(), &ARDOUR_UI::configure_handler));

	_selection.RoutesChanged.connect (sigc::mem_fun(*this, &Mixer_UI::follow_strip_selection));

	route_group_display_button_box->show();
	route_group_add_button->show();
	route_group_remove_button->show();

	global_hpacker.show();
	global_vpacker.show();
	scroller.show();
	scroller_base.show();
	scroller_hpacker.show();
	mixer_scroller_vpacker.show();
	list_vpacker.show();
	group_display_button_label.show();
	group_display_button.show();
	group_display_scroller.show();
	group_display_vbox.show();
	group_display_frame.show();
	rhs_pane1.show();
	strip_packer.show();
	out_packer.show();
	list_hpane.show();
	group_display.show();

	auto_rebinding = FALSE;

	_in_group_rebuild_or_clear = false;

	MixerStrip::CatchDeletion.connect (*this, invalidator (*this), ui_bind (&Mixer_UI::remove_strip, this, _1), gui_context());

        MonitorSection::setup_knob_images ();

#ifndef DEFER_PLUGIN_SELECTOR_LOAD
	_plugin_selector = new PluginSelector (PluginManager::the_manager ());
#endif
}

Mixer_UI::~Mixer_UI ()
{
}

void
Mixer_UI::ensure_float (Window& win)
{
	win.set_transient_for (*this);
}

void
Mixer_UI::show_window ()
{
	present ();
	if (!_visible) {
		set_window_pos_and_size ();

		/* show/hide group tabs as required */
		parameter_changed ("show-group-tabs");

		/* now reset each strips width so the right widgets are shown */
		MixerStrip* ms;

		TreeModel::Children rows = track_model->children();
		TreeModel::Children::iterator ri;

		for (ri = rows.begin(); ri != rows.end(); ++ri) {
			ms = (*ri)[track_columns.strip];
			ms->set_width_enum (ms->get_width_enum (), ms->width_owner());
		}
	}
	_visible = true;
}

bool
Mixer_UI::hide_window (GdkEventAny *ev)
{
	get_window_pos_and_size ();

	_visible = false;
	return just_hide_it(ev, static_cast<Gtk::Window *>(this));
}


void
Mixer_UI::add_strip (RouteList& routes)
{
	ENSURE_GUI_THREAD (*this, &Mixer_UI::add_strip, routes)

	MixerStrip* strip;

	no_track_list_redisplay = true;
	strip_redisplay_does_not_sync_order_keys = true;

	for (RouteList::iterator x = routes.begin(); x != routes.end(); ++x) {
		boost::shared_ptr<Route> route = (*x);

		if (route->is_hidden()) {
			continue;
		}

                if (route->is_monitor()) {
                        if (!_monitor_section) {
                                _monitor_section = new MonitorSection (_session);
                                out_packer.pack_end (_monitor_section->tearoff(), false, false);
                        } else {
                                _monitor_section->set_session (_session);
                        }

                        _monitor_section->tearoff().show_all ();

                        XMLNode* mnode = ARDOUR_UI::instance()->tearoff_settings (X_("monitor-section"));
                        if (mnode) {
                                _monitor_section->tearoff().set_state (*mnode);
                        }

                        /* no regular strip shown for control out */

                        continue;
                }

		strip = new MixerStrip (*this, _session, route);
		strips.push_back (strip);

		Config->get_default_narrow_ms() ? _strip_width = Narrow : _strip_width = Wide;

		if (strip->width_owner() != strip) {
			strip->set_width_enum (_strip_width, this);
		}

		show_strip (strip);

		TreeModel::Row row = *(track_model->append());
		row[track_columns.text] = route->name();
		row[track_columns.visible] = strip->route()->is_master() ? true : strip->marked_for_display();
		row[track_columns.route] = route;
		row[track_columns.strip] = strip;

		if (route->order_key (N_("signal")) == -1) {
			route->set_order_key (N_("signal"), track_model->children().size()-1);
		}

		route->PropertyChanged.connect (*this, invalidator (*this), ui_bind (&Mixer_UI::strip_property_changed, this, _1, strip), gui_context());

		strip->WidthChanged.connect (sigc::mem_fun(*this, &Mixer_UI::strip_width_changed));
		strip->signal_button_release_event().connect (sigc::bind (sigc::mem_fun(*this, &Mixer_UI::strip_button_release_event), strip));
	}

	no_track_list_redisplay = false;

	redisplay_track_list ();

	strip_redisplay_does_not_sync_order_keys = false;
}

void
Mixer_UI::remove_strip (MixerStrip* strip)
{
	if (_session && _session->deletion_in_progress()) {
		/* its all being taken care of */
		return;
	}

	ENSURE_GUI_THREAD (*this, &Mixer_UI::remove_strip, strip);

	cerr << "Mixer UI removing strip for " << strip << endl;

	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator ri;
	list<MixerStrip *>::iterator i;

	if ((i = find (strips.begin(), strips.end(), strip)) != strips.end()) {
		strips.erase (i);
	}

	strip_redisplay_does_not_sync_order_keys = true;

	for (ri = rows.begin(); ri != rows.end(); ++ri) {
		if ((*ri)[track_columns.strip] == strip) {
			track_model->erase (ri);
			break;
		}
	}

	strip_redisplay_does_not_sync_order_keys = false;
}

void
Mixer_UI::sync_order_keys (string const & src)
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator ri;

	if (src == N_("signal") || !_session || (_session->state_of_the_state() & (Session::Loading|Session::Deletion)) || rows.empty()) {
		return;
	}

	std::map<int,int> keys;

	bool changed = false;

	unsigned order = 0;
	for (ri = rows.begin(); ri != rows.end(); ++ri, ++order) {
		boost::shared_ptr<Route> route = (*ri)[track_columns.route];
		unsigned int old_key = order;
		unsigned int new_key = route->order_key (N_("signal"));

		keys[new_key] = old_key;

		if (new_key != old_key) {
			changed = true;
		}
	}

	if (keys.size() != rows.size()) {
		PBD::stacktrace (cerr, 20);
	}
	assert(keys.size() == rows.size());

	// Remove any gaps in keys caused by automation children tracks
	vector<int> neworder;
	for (std::map<int,int>::const_iterator i = keys.begin(); i != keys.end(); ++i) {
		neworder.push_back(i->second);
	}
	assert(neworder.size() == rows.size());

	if (changed) {
		strip_redisplay_does_not_reset_order_keys = true;
		track_model->reorder (neworder);
		strip_redisplay_does_not_reset_order_keys = false;
	}
}

void
Mixer_UI::follow_strip_selection ()
{
	for (list<MixerStrip *>::iterator i = strips.begin(); i != strips.end(); ++i) {
		(*i)->set_selected (_selection.selected ((*i)->route()));
	}
}

bool
Mixer_UI::strip_button_release_event (GdkEventButton *ev, MixerStrip *strip)
{
	if (ev->button == 1) {

		/* this allows the user to click on the strip to terminate comment
		   editing. XXX it needs improving so that we don't select the strip
		   at the same time.
		*/

		if (_selection.selected (strip->route())) {
			_selection.remove (strip->route());
		} else {
			if (Keyboard::modifier_state_equals (ev->state, Keyboard::TertiaryModifier)) {
				_selection.add (strip->route());
			} else {
				_selection.set (strip->route());
			}
		}
	}

	return true;
}

void
Mixer_UI::set_session (Session* sess)
{
	SessionHandlePtr::set_session (sess);

	if (_plugin_selector) {
		_plugin_selector->set_session (_session);
	}

	_group_tabs->set_session (sess);

	if (!_session) {
		return;
	}

	XMLNode* node = ARDOUR_UI::instance()->mixer_settings();
	set_state (*node);

	update_title ();

	initial_track_display ();

	_session->RouteAdded.connect (_session_connections, invalidator (*this), ui_bind (&Mixer_UI::add_strip, this, _1), gui_context());
	_session->route_group_added.connect (_session_connections, invalidator (*this), ui_bind (&Mixer_UI::add_route_group, this, _1), gui_context());
	_session->route_group_removed.connect (_session_connections, invalidator (*this), boost::bind (&Mixer_UI::route_groups_changed, this), gui_context());
	_session->route_groups_reordered.connect (_session_connections, invalidator (*this), boost::bind (&Mixer_UI::route_groups_changed, this), gui_context());
	_session->config.ParameterChanged.connect (_session_connections, invalidator (*this), ui_bind (&Mixer_UI::parameter_changed, this, _1), gui_context());
	_session->DirtyChanged.connect (_session_connections, invalidator (*this), boost::bind (&Mixer_UI::update_title, this), gui_context());
	_session->StateSaved.connect (_session_connections, invalidator (*this), ui_bind (&Mixer_UI::update_title, this), gui_context());

	Config->ParameterChanged.connect (*this, invalidator (*this), ui_bind (&Mixer_UI::parameter_changed, this, _1), gui_context ());

	route_groups_changed ();

	if (_visible) {
		show_window();
	}

	start_updating ();
}

void
Mixer_UI::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &Mixer_UI::session_going_away);

	_in_group_rebuild_or_clear = true;
	group_model->clear ();
	_in_group_rebuild_or_clear = false;

	_selection.clear ();
	track_model->clear ();

	for (list<MixerStrip *>::iterator i = strips.begin(); i != strips.end(); ++i) {
		delete (*i);
	}

        if (_monitor_section) {
                _monitor_section->tearoff().hide_visible ();
        }

	strips.clear ();

	stop_updating ();

	SessionHandlePtr::session_going_away ();

	_session = 0;
	update_title ();
}

void
Mixer_UI::show_strip (MixerStrip* ms)
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;

	for (i = rows.begin(); i != rows.end(); ++i) {

		MixerStrip* strip = (*i)[track_columns.strip];
		if (strip == ms) {
			(*i)[track_columns.visible] = true;
			break;
		}
	}
}

void
Mixer_UI::hide_strip (MixerStrip* ms)
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;

	for (i = rows.begin(); i != rows.end(); ++i) {

		MixerStrip* strip = (*i)[track_columns.strip];
		if (strip == ms) {
			(*i)[track_columns.visible] = false;
			break;
		}
	}
}

gint
Mixer_UI::start_updating ()
{
    fast_screen_update_connection = ARDOUR_UI::instance()->SuperRapidScreenUpdate.connect (sigc::mem_fun(*this, &Mixer_UI::fast_update_strips));
    return 0;
}

gint
Mixer_UI::stop_updating ()
{
    fast_screen_update_connection.disconnect();
    return 0;
}

void
Mixer_UI::fast_update_strips ()
{
	if (is_mapped () && _session) {
		for (list<MixerStrip *>::iterator i = strips.begin(); i != strips.end(); ++i) {
			(*i)->fast_update ();
		}
	}
}

void
Mixer_UI::set_all_strips_visibility (bool yn)
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;

	no_track_list_redisplay = true;

	for (i = rows.begin(); i != rows.end(); ++i) {

		TreeModel::Row row = (*i);
		MixerStrip* strip = row[track_columns.strip];

		if (strip == 0) {
			continue;
		}

		if (strip->route()->is_master() || strip->route()->is_monitor()) {
			continue;
		}

		(*i)[track_columns.visible] = yn;
	}

	no_track_list_redisplay = false;
	redisplay_track_list ();
}


void
Mixer_UI::set_all_audio_visibility (int tracks, bool yn)
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;

	no_track_list_redisplay = true;

	for (i = rows.begin(); i != rows.end(); ++i) {
		TreeModel::Row row = (*i);
		MixerStrip* strip = row[track_columns.strip];

		if (strip == 0) {
			continue;
		}

		if (strip->route()->is_master() || strip->route()->is_monitor()) {
			continue;
		}

		boost::shared_ptr<AudioTrack> at = strip->audio_track();

		switch (tracks) {
		case 0:
			(*i)[track_columns.visible] = yn;
			break;

		case 1:
			if (at) { /* track */
				(*i)[track_columns.visible] = yn;
			}
			break;

		case 2:
			if (!at) { /* bus */
				(*i)[track_columns.visible] = yn;
			}
			break;
		}
	}

	no_track_list_redisplay = false;
	redisplay_track_list ();
}

void
Mixer_UI::hide_all_routes ()
{
	set_all_strips_visibility (false);
}

void
Mixer_UI::show_all_routes ()
{
	set_all_strips_visibility (true);
}

void
Mixer_UI::show_all_audiobus ()
{
	set_all_audio_visibility (2, true);
}
void
Mixer_UI::hide_all_audiobus ()
{
	set_all_audio_visibility (2, false);
}

void
Mixer_UI::show_all_audiotracks()
{
	set_all_audio_visibility (1, true);
}
void
Mixer_UI::hide_all_audiotracks ()
{
	set_all_audio_visibility (1, false);
}

void
Mixer_UI::track_list_reorder (const TreeModel::Path&, const TreeModel::iterator&, int* /*new_order*/)
{
	strip_redisplay_does_not_sync_order_keys = true;
	_session->set_remote_control_ids();
	redisplay_track_list ();
	strip_redisplay_does_not_sync_order_keys = false;
}

void
Mixer_UI::track_list_change (const Gtk::TreeModel::Path&, const Gtk::TreeModel::iterator&)
{
	// never reset order keys because of a property change
	strip_redisplay_does_not_reset_order_keys = true;
	_session->set_remote_control_ids();
	redisplay_track_list ();
	strip_redisplay_does_not_reset_order_keys = false;
}

void
Mixer_UI::track_list_delete (const Gtk::TreeModel::Path&)
{
	/* this could require an order sync */
	if (_session && !_session->deletion_in_progress()) {
		_session->set_remote_control_ids();
		redisplay_track_list ();
	}
}

void
Mixer_UI::redisplay_track_list ()
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;
	long order;

	if (no_track_list_redisplay) {
		return;
	}

	for (order = 0, i = rows.begin(); i != rows.end(); ++i, ++order) {
		MixerStrip* strip = (*i)[track_columns.strip];

		if (strip == 0) {
			/* we're in the middle of changing a row, don't worry */
			continue;
		}

		if (!strip_redisplay_does_not_reset_order_keys) {
			strip->route()->set_order_key (N_("signal"), order);
		}

		bool const visible = (*i)[track_columns.visible];

		if (visible) {
			strip->set_gui_property ("visible", true);

			if (strip->packed()) {

				if (strip->route()->is_master() || strip->route()->is_monitor()) {
					out_packer.reorder_child (*strip, -1);
				} else {
					strip_packer.reorder_child (*strip, -1); /* put at end */
				}

			} else {

				if (strip->route()->is_master() || strip->route()->is_monitor()) {
					out_packer.pack_start (*strip, false, false);
				} else {
					strip_packer.pack_start (*strip, false, false);
				}
				strip->set_packed (true);
			}

		} else {

			strip->set_gui_property ("visible", false);

			if (strip->route()->is_master() || strip->route()->is_monitor()) {
				/* do nothing, these cannot be hidden */
			} else {
				if (strip->packed()) {
					strip_packer.remove (*strip);
					strip->set_packed (false);
				}
			}
		}
	}

	if (!strip_redisplay_does_not_reset_order_keys && !strip_redisplay_does_not_sync_order_keys) {
		_session->sync_order_keys (N_("signal"));
	}

	// Resigc::bind all of the midi controls automatically

	if (auto_rebinding) {
		auto_rebind_midi_controls ();
	}

	_group_tabs->set_dirty ();
}

void
Mixer_UI::strip_width_changed ()
{
	_group_tabs->set_dirty ();

#ifdef GTKOSX
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;
	long order;

	for (order = 0, i = rows.begin(); i != rows.end(); ++i, ++order) {
		MixerStrip* strip = (*i)[track_columns.strip];

		if (strip == 0) {
			continue;
		}

		bool visible = (*i)[track_columns.visible];

		if (visible) {
			strip->queue_draw();
		}
	}
#endif

}

void
Mixer_UI::set_auto_rebinding( bool val )
{
	if( val == TRUE )
	{
		auto_rebinding = TRUE;
		Session::AutoBindingOff();
	}
	else
	{
		auto_rebinding = FALSE;
		Session::AutoBindingOn();
	}
}

void
Mixer_UI::toggle_auto_rebinding()
{
	if (auto_rebinding)
	{
		set_auto_rebinding( FALSE );
	}

	else
	{
		set_auto_rebinding( TRUE );
	}

	auto_rebind_midi_controls();
}

void
Mixer_UI::auto_rebind_midi_controls ()
{
	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;
	int pos;

	// Create bindings for all visible strips and remove those that are not visible
	pos = 1;  // 0 is reserved for the master strip
	for (i = rows.begin(); i != rows.end(); ++i) {
		MixerStrip* strip = (*i)[track_columns.strip];

		if ( (*i)[track_columns.visible] == true ) {  // add bindings for
			// make the actual binding
			//cout<<"Auto Binding:  Visible Strip Found: "<<strip->name()<<endl;

			int controlValue = pos;
			if( strip->route()->is_master() ) {
				controlValue = 0;
			}
			else {
				pos++;
			}

			PBD::Controllable::CreateBinding ( strip->solo_button->get_controllable().get(), controlValue, 0);
			PBD::Controllable::CreateBinding ( strip->mute_button->get_controllable().get(), controlValue, 1);

			if( strip->is_audio_track() ) {
				PBD::Controllable::CreateBinding ( strip->rec_enable_button->get_controllable().get(), controlValue, 2);
			}

			PBD::Controllable::CreateBinding ( strip->gpm.get_controllable().get(), controlValue, 3);
			PBD::Controllable::CreateBinding ( strip->panners.get_controllable().get(), controlValue, 4);

		}
		else {  // Remove any existing binding
			PBD::Controllable::DeleteBinding ( strip->solo_button->get_controllable().get() );
			PBD::Controllable::DeleteBinding ( strip->mute_button->get_controllable().get() );

			if( strip->is_audio_track() ) {
				PBD::Controllable::DeleteBinding ( strip->rec_enable_button->get_controllable().get() );
			}

			PBD::Controllable::DeleteBinding ( strip->gpm.get_controllable().get() );
			PBD::Controllable::DeleteBinding ( strip->panners.get_controllable().get() ); // This only takes the first panner if there are multiples...
		}

	} // for

}

struct SignalOrderRouteSorter {
    bool operator() (boost::shared_ptr<Route> a, boost::shared_ptr<Route> b) {
	    /* use of ">" forces the correct sort order */
	    return a->order_key (N_("signal")) < b->order_key (N_("signal"));
    }
};

void
Mixer_UI::initial_track_display ()
{
	boost::shared_ptr<RouteList> routes = _session->get_routes();
	RouteList copy (*routes);
	SignalOrderRouteSorter sorter;

	copy.sort (sorter);

	no_track_list_redisplay = true;

	track_model->clear ();

	add_strip (copy);

	no_track_list_redisplay = false;

	redisplay_track_list ();
}

void
Mixer_UI::show_track_list_menu ()
{
	if (track_menu == 0) {
		build_track_menu ();
	}

	track_menu->popup (1, gtk_get_current_event_time());
}

bool
Mixer_UI::track_display_button_press (GdkEventButton* ev)
{
	if (Keyboard::is_context_menu_event (ev)) {
		show_track_list_menu ();
		return true;
	}

	TreeIter iter;
	TreeModel::Path path;
	TreeViewColumn* column;
	int cellx;
	int celly;

	if (!track_display.get_path_at_pos ((int)ev->x, (int)ev->y, path, column, cellx, celly)) {
		return false;
	}

	switch (GPOINTER_TO_UINT (column->get_data (X_("colnum")))) {
	case 0:
		/* allow normal processing to occur */
		return false;

	case 1: /* visibility */

		if ((iter = track_model->get_iter (path))) {
			MixerStrip* strip = (*iter)[track_columns.strip];
			if (strip) {

				if (!strip->route()->is_master() && !strip->route()->is_monitor()) {
					bool visible = (*iter)[track_columns.visible];
					(*iter)[track_columns.visible] = !visible;
				}
#ifdef GTKOSX
				track_display.queue_draw();
#endif
			}
		}
		return true;

	default:
		break;
	}

	return false;
}


void
Mixer_UI::build_track_menu ()
{
	using namespace Menu_Helpers;
	using namespace Gtk;

	track_menu = new Menu;
	track_menu->set_name ("ArdourContextMenu");
	MenuList& items = track_menu->items();

	items.push_back (MenuElem (_("Show All"), sigc::mem_fun(*this, &Mixer_UI::show_all_routes)));
	items.push_back (MenuElem (_("Hide All"), sigc::mem_fun(*this, &Mixer_UI::hide_all_routes)));
	items.push_back (MenuElem (_("Show All Audio Tracks"), sigc::mem_fun(*this, &Mixer_UI::show_all_audiotracks)));
	items.push_back (MenuElem (_("Hide All Audio Tracks"), sigc::mem_fun(*this, &Mixer_UI::hide_all_audiotracks)));
	items.push_back (MenuElem (_("Show All Audio Busses"), sigc::mem_fun(*this, &Mixer_UI::show_all_audiobus)));
	items.push_back (MenuElem (_("Hide All Audio Busses"), sigc::mem_fun(*this, &Mixer_UI::hide_all_audiobus)));

}

void
Mixer_UI::strip_property_changed (const PropertyChange& what_changed, MixerStrip* mx)
{
	if (!what_changed.contains (ARDOUR::Properties::name)) {
		return;
	}

	ENSURE_GUI_THREAD (*this, &Mixer_UI::strip_name_changed, what_changed, mx)

	TreeModel::Children rows = track_model->children();
	TreeModel::Children::iterator i;

	for (i = rows.begin(); i != rows.end(); ++i) {
		if ((*i)[track_columns.strip] == mx) {
			(*i)[track_columns.text] = mx->route()->name();
			return;
		}
	}

	error << _("track display list item for renamed strip not found!") << endmsg;
}

bool
Mixer_UI::group_display_button_press (GdkEventButton* ev)
{
	TreeModel::Path path;
	TreeViewColumn* column;
	int cellx;
	int celly;

	if (!group_display.get_path_at_pos ((int)ev->x, (int)ev->y, path, column, cellx, celly)) {
		return false;
	}

	TreeIter iter = group_model->get_iter (path);
	if (!iter) {
		return false;
	}

	RouteGroup* group = (*iter)[group_columns.group];

	if (Keyboard::is_context_menu_event (ev)) {
		_group_tabs->get_menu(group)->popup (1, ev->time);
		return true;
	}

	switch (GPOINTER_TO_UINT (column->get_data (X_("colnum")))) {
	case 0:
		if (Keyboard::is_edit_event (ev)) {
			if (group) {
				// edit_route_group (group);
#ifdef GTKOSX
				group_display.queue_draw();
#endif
				return true;
			}
		}
		break;

	case 1:
	{
		bool visible = (*iter)[group_columns.visible];
		(*iter)[group_columns.visible] = !visible;
#ifdef GTKOSX
		group_display.queue_draw();
#endif
		return true;
	}

	default:
		break;
	}

	return false;
 }

void
Mixer_UI::activate_all_route_groups ()
{
	_session->foreach_route_group (sigc::bind (sigc::mem_fun (*this, &Mixer_UI::set_route_group_activation), true));
}

void
Mixer_UI::disable_all_route_groups ()
{
	_session->foreach_route_group (sigc::bind (sigc::mem_fun (*this, &Mixer_UI::set_route_group_activation), false));
}

void
Mixer_UI::route_groups_changed ()
{
	ENSURE_GUI_THREAD (*this, &Mixer_UI::route_groups_changed);

	_in_group_rebuild_or_clear = true;

	/* just rebuild the while thing */

	group_model->clear ();

	{
		TreeModel::Row row;
		row = *(group_model->append());
		row[group_columns.visible] = true;
		row[group_columns.text] = (_("-all-"));
		row[group_columns.group] = 0;
	}

	_session->foreach_route_group (sigc::mem_fun (*this, &Mixer_UI::add_route_group));

	_group_tabs->set_dirty ();
	_in_group_rebuild_or_clear = false;
}

void
Mixer_UI::new_route_group ()
{
	RouteList rl;

	_group_tabs->run_new_group_dialog (rl);
}

void
Mixer_UI::remove_selected_route_group ()
{
	Glib::RefPtr<TreeSelection> selection = group_display.get_selection();
	TreeView::Selection::ListHandle_Path rows = selection->get_selected_rows ();

	if (rows.empty()) {
		return;
	}

	TreeView::Selection::ListHandle_Path::iterator i = rows.begin();
	TreeIter iter;

	/* selection mode is single, so rows.begin() is it */

	if ((iter = group_model->get_iter (*i))) {

		RouteGroup* rg = (*iter)[group_columns.group];

		if (rg) {
			_session->remove_route_group (*rg);
		}
	}
}

void
Mixer_UI::route_group_property_changed (RouteGroup* group, const PropertyChange& change)
{
	if (in_group_row_change) {
		return;
	}

	/* force an update of any mixer strips that are using this group,
	   otherwise mix group names don't change in mixer strips
	*/

	for (list<MixerStrip *>::iterator i = strips.begin(); i != strips.end(); ++i) {
		if ((*i)->route_group() == group) {
			(*i)->route_group_changed();
		}
	}

	TreeModel::iterator i;
	TreeModel::Children rows = group_model->children();
	Glib::RefPtr<TreeSelection> selection = group_display.get_selection();

	in_group_row_change = true;

	for (i = rows.begin(); i != rows.end(); ++i) {
		if ((*i)[group_columns.group] == group) {
			(*i)[group_columns.visible] = !group->is_hidden ();
			(*i)[group_columns.text] = group->name ();
			break;
		}
	}

	in_group_row_change = false;

	if (change.contains (Properties::name)) {
		_group_tabs->set_dirty ();
	}

	for (list<MixerStrip*>::iterator j = strips.begin(); j != strips.end(); ++j) {
		if ((*j)->route_group() == group) {
			if (group->is_hidden ()) {
				hide_strip (*j);
			} else {
				show_strip (*j);
			}
		}
	}
}

void
Mixer_UI::route_group_name_edit (const std::string& path, const std::string& new_text)
{
	RouteGroup* group;
	TreeIter iter;

	if ((iter = group_model->get_iter (path))) {

		if ((group = (*iter)[group_columns.group]) == 0) {
			return;
		}

		if (new_text != group->name()) {
			group->set_name (new_text);
		}
	}
}

void
Mixer_UI::route_group_row_change (const Gtk::TreeModel::Path&, const Gtk::TreeModel::iterator& iter)
{
	RouteGroup* group;

	if (in_group_row_change) {
		return;
	}

	if ((group = (*iter)[group_columns.group]) == 0) {
		return;
	}

	std::string name = (*iter)[group_columns.text];

	if (name != group->name()) {
		group->set_name (name);
	}

	bool hidden = !(*iter)[group_columns.visible];

	if (hidden != group->is_hidden ()) {
		group->set_hidden (hidden, this);
	}
}

/** Called when a group model row is deleted, but also when the model is
 *  reordered by a user drag-and-drop; the latter is what we are
 *  interested in here.
 */
void
Mixer_UI::route_group_row_deleted (Gtk::TreeModel::Path const &)
{
	if (_in_group_rebuild_or_clear) {
		return;
	}

	/* Re-write the session's route group list so that the new order is preserved */

	list<RouteGroup*> new_list;

	Gtk::TreeModel::Children children = group_model->children();
	for (Gtk::TreeModel::Children::iterator i = children.begin(); i != children.end(); ++i) {
		RouteGroup* g = (*i)[group_columns.group];
		if (g) {
			new_list.push_back (g);
		}
	}

	_session->reorder_route_groups (new_list);
}


void
Mixer_UI::add_route_group (RouteGroup* group)
{
	ENSURE_GUI_THREAD (*this, &Mixer_UI::add_route_group, group)
	bool focus = false;

	in_group_row_change = true;

	TreeModel::Row row = *(group_model->append());
	row[group_columns.visible] = !group->is_hidden ();
	row[group_columns.group] = group;
	if (!group->name().empty()) {
		row[group_columns.text] = group->name();
	} else {
		row[group_columns.text] = _("unnamed");
		focus = true;
	}

	group->PropertyChanged.connect (*this, invalidator (*this), ui_bind (&Mixer_UI::route_group_property_changed, this, group, _1), gui_context());

	if (focus) {
		TreeViewColumn* col = group_display.get_column (0);
		CellRendererText* name_cell = dynamic_cast<CellRendererText*>(group_display.get_column_cell_renderer (0));
		group_display.set_cursor (group_model->get_path (row), *col, *name_cell, true);
	}

	_group_tabs->set_dirty ();

	in_group_row_change = false;
}

bool
Mixer_UI::strip_scroller_button_release (GdkEventButton* ev)
{
	using namespace Menu_Helpers;

	if (Keyboard::is_context_menu_event (ev)) {
		ARDOUR_UI::instance()->add_route (this);
		return true;
	}

	return false;
}

void
Mixer_UI::set_strip_width (Width w)
{
	_strip_width = w;

	for (list<MixerStrip*>::iterator i = strips.begin(); i != strips.end(); ++i) {
		(*i)->set_width_enum (w, this);
	}
}

void
Mixer_UI::set_window_pos_and_size ()
{
	resize (m_width, m_height);
	move (m_root_x, m_root_y);
}

	void
Mixer_UI::get_window_pos_and_size ()
{
	get_position(m_root_x, m_root_y);
	get_size(m_width, m_height);
}

int
Mixer_UI::set_state (const XMLNode& node)
{
	const XMLProperty* prop;
	XMLNode* geometry;

	m_width = default_width;
	m_height = default_height;
	m_root_x = 1;
	m_root_y = 1;

	if ((geometry = find_named_node (node, "geometry")) != 0) {

		XMLProperty* prop;

		if ((prop = geometry->property("x_size")) == 0) {
			prop = geometry->property ("x-size");
		}
		if (prop) {
			m_width = atoi(prop->value());
		}
		if ((prop = geometry->property("y_size")) == 0) {
			prop = geometry->property ("y-size");
		}
		if (prop) {
			m_height = atoi(prop->value());
		}

		if ((prop = geometry->property ("x_pos")) == 0) {
			prop = geometry->property ("x-pos");
		}
		if (prop) {
			m_root_x = atoi (prop->value());

		}
		if ((prop = geometry->property ("y_pos")) == 0) {
			prop = geometry->property ("y-pos");
		}
		if (prop) {
			m_root_y = atoi (prop->value());
		}
	}

	set_window_pos_and_size ();

	if ((prop = node.property ("narrow-strips"))) {
		if (string_is_affirmative (prop->value())) {
			set_strip_width (Narrow);
		} else {
			set_strip_width (Wide);
		}
	}

	if ((prop = node.property ("show-mixer"))) {
		if (string_is_affirmative (prop->value())) {
		       _visible = true;
		}
	}

	return 0;
}

XMLNode&
Mixer_UI::get_state (void)
{
	XMLNode* node = new XMLNode ("Mixer");

	if (is_realized()) {
		Glib::RefPtr<Gdk::Window> win = get_window();

		get_window_pos_and_size ();

		XMLNode* geometry = new XMLNode ("geometry");
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", m_width);
		geometry->add_property(X_("x_size"), string(buf));
		snprintf(buf, sizeof(buf), "%d", m_height);
		geometry->add_property(X_("y_size"), string(buf));
		snprintf(buf, sizeof(buf), "%d", m_root_x);
		geometry->add_property(X_("x_pos"), string(buf));
		snprintf(buf, sizeof(buf), "%d", m_root_y);
		geometry->add_property(X_("y_pos"), string(buf));

		// written only for compatibility, they are not used.
		snprintf(buf, sizeof(buf), "%d", 0);
		geometry->add_property(X_("x_off"), string(buf));
		snprintf(buf, sizeof(buf), "%d", 0);
		geometry->add_property(X_("y_off"), string(buf));

		snprintf(buf,sizeof(buf), "%d",gtk_paned_get_position (static_cast<Paned*>(&rhs_pane1)->gobj()));
		geometry->add_property(X_("mixer_rhs_pane1_pos"), string(buf));
		snprintf(buf,sizeof(buf), "%d",gtk_paned_get_position (static_cast<Paned*>(&list_hpane)->gobj()));
		geometry->add_property(X_("mixer_list_hpane_pos"), string(buf));

		node->add_child_nocopy (*geometry);
	}

	node->add_property ("narrow-strips", _strip_width == Narrow ? "yes" : "no");

	node->add_property ("show-mixer", _visible ? "yes" : "no");

	return *node;
}


void
Mixer_UI::pane_allocation_handler (Allocation&, Gtk::Paned* which)
{
	int pos;
	XMLProperty* prop = 0;
	char buf[32];
	XMLNode* node = ARDOUR_UI::instance()->mixer_settings();
	XMLNode* geometry;
	int height;
	static int32_t done[3] = { 0, 0, 0 };

	height = default_height;

	if ((geometry = find_named_node (*node, "geometry")) != 0) {

		if ((prop = geometry->property ("y_size")) == 0) {
			prop = geometry->property ("y-size");
		}
		if (prop) {
			height = atoi (prop->value());
		}
	}

	if (which == static_cast<Gtk::Paned*> (&rhs_pane1)) {

		if (done[0]) {
			return;
		}

		if (!geometry || (prop = geometry->property("mixer-rhs-pane1-pos")) == 0) {
			pos = height / 3;
			snprintf (buf, sizeof(buf), "%d", pos);
		} else {
			pos = atoi (prop->value());
		}

		if ((done[0] = GTK_WIDGET(rhs_pane1.gobj())->allocation.height > pos)) {
			rhs_pane1.set_position (pos);
		}

	} else if (which == static_cast<Gtk::Paned*> (&list_hpane)) {

		if (done[2]) {
			return;
		}

		if (!geometry || (prop = geometry->property("mixer-list-hpane-pos")) == 0) {
			pos = 75;
			snprintf (buf, sizeof(buf), "%d", pos);
		} else {
			pos = atoi (prop->value());
		}

		if ((done[2] = GTK_WIDGET(list_hpane.gobj())->allocation.width > pos)) {
			list_hpane.set_position (pos);
		}
	}
}
void
Mixer_UI::scroll_left ()
{
	Adjustment* adj = scroller.get_hscrollbar()->get_adjustment();
	/* stupid GTK: can't rely on clamping across versions */
	scroller.get_hscrollbar()->set_value (max (adj->get_lower(), adj->get_value() - adj->get_step_increment()));
}

void
Mixer_UI::scroll_right ()
{
	Adjustment* adj = scroller.get_hscrollbar()->get_adjustment();
	/* stupid GTK: can't rely on clamping across versions */
	scroller.get_hscrollbar()->set_value (min (adj->get_upper(), adj->get_value() + adj->get_step_increment()));
}

bool
Mixer_UI::on_key_press_event (GdkEventKey* ev)
{
	switch (ev->keyval) {
	case GDK_Left:
		scroll_left ();
		return true;

	case GDK_Right:
		scroll_right ();
		return true;

	default:
		break;
	}

	return key_press_focus_accelerator_handler (*this, ev);
}

bool
Mixer_UI::on_key_release_event (GdkEventKey* ev)
{
	return Gtk::Window::on_key_release_event (ev);
	// return key_press_focus_accelerator_handler (*this, ev);
}


bool
Mixer_UI::on_scroll_event (GdkEventScroll* ev)
{
	switch (ev->direction) {
	case GDK_SCROLL_LEFT:
		scroll_left ();
		return true;
	case GDK_SCROLL_UP:
		if (ev->state & Keyboard::TertiaryModifier) {
			scroll_left ();
			return true;
		}
		return false;

	case GDK_SCROLL_RIGHT:
		scroll_right ();
		return true;

	case GDK_SCROLL_DOWN:
		if (ev->state & Keyboard::TertiaryModifier) {
			scroll_right ();
			return true;
		}
		return false;
	}

	return false;
}


void
Mixer_UI::parameter_changed (string const & p)
{
	if (p == "show-group-tabs") {
		bool const s = _session->config.get_show_group_tabs ();
		if (s) {
			_group_tabs->show ();
		} else {
			_group_tabs->hide ();
		}
	} else if (p == "default-narrow_ms") {
		bool const s = Config->get_default_narrow_ms ();
		for (list<MixerStrip*>::iterator i = strips.begin(); i != strips.end(); ++i) {
			(*i)->set_width_enum (s ? Narrow : Wide, this);
		}
	}
}

void
Mixer_UI::set_route_group_activation (RouteGroup* g, bool a)
{
	g->set_active (a, this);
}

PluginSelector*
Mixer_UI::plugin_selector()
{
#ifdef DEFER_PLUGIN_SELECTOR_LOAD
	if (!_plugin_selector)
		_plugin_selector = new PluginSelector (PluginManager::the_manager ());
#endif

	return _plugin_selector;
}

void
Mixer_UI::setup_track_display ()
{
	track_model = ListStore::create (track_columns);
	track_display.set_model (track_model);
	track_display.append_column (_("Strips"), track_columns.text);
	track_display.append_column (_("Show"), track_columns.visible);
	track_display.get_column (0)->set_data (X_("colnum"), GUINT_TO_POINTER(0));
	track_display.get_column (1)->set_data (X_("colnum"), GUINT_TO_POINTER(1));
	track_display.get_column (0)->set_expand(true);
	track_display.get_column (1)->set_expand(false);
	track_display.set_name (X_("MixerTrackDisplayList"));
	track_display.get_selection()->set_mode (Gtk::SELECTION_NONE);
	track_display.set_reorderable (true);
	track_display.set_headers_visible (true);

	track_model->signal_row_deleted().connect (sigc::mem_fun (*this, &Mixer_UI::track_list_delete));
	track_model->signal_row_changed().connect (sigc::mem_fun (*this, &Mixer_UI::track_list_change));
	track_model->signal_rows_reordered().connect (sigc::mem_fun (*this, &Mixer_UI::track_list_reorder));

	CellRendererToggle* track_list_visible_cell = dynamic_cast<CellRendererToggle*>(track_display.get_column_cell_renderer (1));
	track_list_visible_cell->property_activatable() = true;
	track_list_visible_cell->property_radio() = false;

	track_display.signal_button_press_event().connect (sigc::mem_fun (*this, &Mixer_UI::track_display_button_press), false);

	track_display_scroller.add (track_display);
	track_display_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	VBox* v = manage (new VBox);
	v->show ();
	v->pack_start (track_display_scroller, true, true);

	Button* b = manage (new Button);
	b->show ();
	Widget* w = manage (new Image (Stock::ADD, ICON_SIZE_BUTTON));
	w->show ();
	b->add (*w);

	b->signal_clicked().connect (sigc::mem_fun (*this, &Mixer_UI::new_track_or_bus));

	v->pack_start (*b, false, false);

	track_display_frame.set_name("BaseFrame");
	track_display_frame.set_shadow_type (Gtk::SHADOW_IN);
	track_display_frame.add (*v);

	track_display_scroller.show();
	track_display_frame.show();
	track_display.show();
}

void
Mixer_UI::new_track_or_bus ()
{
	ARDOUR_UI::instance()->add_route (this);
}


void
Mixer_UI::update_title ()
{
	if (_session) {
		string n;
		
		if (_session->snap_name() != _session->name()) {
			n = _session->snap_name ();
		} else {
			n = _session->name ();
		}

		if (_session->dirty ()) {
			n = "*" + n;
		}
		
		WindowTitle title (n);
		title += _("Mixer");
		title += Glib::get_application_name ();
		set_title (title.get_string());

	} else {
		
		WindowTitle title (X_("Mixer"));
		title += Glib::get_application_name ();
		set_title (title.get_string());
	}
}

		
