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

#ifndef __ardour_gtk_editor_route_h__
#define __ardour_gtk_editor_route_h__

#include "pbd/signals.h"
#include "editor_component.h"

class EditorRoutes : public EditorComponent, public PBD::ScopedConnectionList
{
public:
	EditorRoutes (Editor *);

	void set_session (ARDOUR::Session *);

	Gtk::Widget& widget () {
		return _scroller;
	}

	void move_selected_tracks (bool);
	void show_track_in_display (TimeAxisView &);
	
	void suspend_redisplay () {
		_no_redisplay = true;
	}
	
	void resume_redisplay () {
		_no_redisplay = false;
		redisplay ();
	}
	
	void redisplay ();
	void update_visibility ();
	void routes_added (std::list<RouteTimeAxisView*> routes);
	void route_removed (TimeAxisView *);
	void hide_track_in_display (TimeAxisView &);
	std::list<TimeAxisView*> views () const;
	void hide_all_tracks (bool);
	void clear ();
	void sync_order_keys (std::string const &);

private:

	void initial_display ();
	void on_tv_rec_enable_toggled (Glib::ustring const &);
	void on_tv_mute_enable_toggled (Glib::ustring const &);
	void on_tv_solo_enable_toggled (Glib::ustring const &);
	void on_tv_solo_isolate_toggled (Glib::ustring const &);
	void on_tv_solo_safe_toggled (Glib::ustring const &);
	void build_menu ();
	void show_menu ();
	void route_deleted (Gtk::TreeModel::Path const &);
	void visible_changed (Glib::ustring const &);
	void reordered (Gtk::TreeModel::Path const &, Gtk::TreeModel::iterator const &, int *);
	bool button_press (GdkEventButton *);
	void route_property_changed (const PBD::PropertyChange&, boost::weak_ptr<ARDOUR::Route>);
	void handle_gui_changes (std::string const &, void *);
	void update_rec_display ();
	void update_mute_display ();
	void update_solo_display (bool);
	void update_solo_isolate_display ();
	void update_solo_safe_display ();
	void set_all_tracks_visibility (bool);
	void set_all_audio_visibility (int, bool);
	void show_all_routes ();
	void hide_all_routes ();
	void show_all_audiotracks ();
	void hide_all_audiotracks ();
	void show_all_audiobus ();
	void hide_all_audiobus ();
	void show_tracks_with_regions_at_playhead ();
	
	void display_drag_data_received (
		Glib::RefPtr<Gdk::DragContext> const &, gint, gint, Gtk::SelectionData const &, guint, guint
		);
	
	void track_list_reorder (Gtk::TreeModel::Path const &, Gtk::TreeModel::iterator const & iter, int* new_order);
	bool selection_filter (Glib::RefPtr<Gtk::TreeModel> const &, Gtk::TreeModel::Path const &, bool);
	void name_edit (Glib::ustring const &, Glib::ustring const &);
	void solo_changed_so_update_mute ();

	struct ModelColumns : public Gtk::TreeModel::ColumnRecord {
		ModelColumns() {
			add (text);
			add (visible);
			add (rec_enabled);
			add (mute_state);
			add (solo_state);
			add (solo_isolate_state);
			add (solo_safe_state);
			add (is_track);
			add (tv);
			add (route);
			add (name_editable);
		}
		
		Gtk::TreeModelColumn<Glib::ustring>  text;
		Gtk::TreeModelColumn<bool>           visible;
		Gtk::TreeModelColumn<bool>           rec_enabled;
		Gtk::TreeModelColumn<uint32_t>       mute_state;
		Gtk::TreeModelColumn<uint32_t>       solo_state;
		Gtk::TreeModelColumn<uint32_t>       solo_isolate_state;
		Gtk::TreeModelColumn<uint32_t>       solo_safe_state;
		Gtk::TreeModelColumn<bool>           is_track;
		Gtk::TreeModelColumn<TimeAxisView*>  tv;
		Gtk::TreeModelColumn<boost::shared_ptr<ARDOUR::Route> >  route;
		Gtk::TreeModelColumn<bool>           name_editable;
	};

	Gtk::ScrolledWindow _scroller;
	Gtkmm2ext::DnDTreeView<boost::shared_ptr<ARDOUR::Route> > _display;
	Glib::RefPtr<Gtk::ListStore> _model;
	ModelColumns _columns;
	
	bool _ignore_reorder;
	bool _no_redisplay;
	bool _redisplay_does_not_sync_order_keys;
	bool _redisplay_does_not_reset_order_keys;
	
	Gtk::Menu* _menu;
};

#endif /* __ardour_gtk_editor_route_h__ */
