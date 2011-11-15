/*
    Copyright (C) 2004 Paul Davis

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

#ifndef __ardour_gtk_processor_box__
#define __ardour_gtk_processor_box__

#include <cmath>
#include <vector>

#include <boost/function.hpp>

#include <gtkmm/box.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/menu.h>
#include <gtkmm/scrolledwindow.h>
#include "gtkmm2ext/dndtreeview.h"
#include "gtkmm2ext/auto_spin.h"
#include "gtkmm2ext/click_box.h"
#include "gtkmm2ext/dndvbox.h"
#include "gtkmm2ext/pixfader.h"

#include "pbd/stateful.h"
#include "pbd/signals.h"

#include "ardour/types.h"
#include "ardour/ardour.h"
#include "ardour/plugin_insert.h"
#include "ardour/port_insert.h"
#include "ardour/processor.h"
#include "ardour/route.h"
#include "ardour/session_handle.h"

#include "pbd/fastlog.h"

#include "plugin_interest.h"
#include "io_selector.h"
#include "send_ui.h"
#include "enums.h"
#include "window_proxy.h"
#include "ardour_button.h"

class MotionController;
class PluginSelector;
class PluginUIWindow;
class RouteProcessorSelection;
class MixerStrip;

namespace ARDOUR {
	class Connection;
	class IO;
	class Insert;
	class Plugin;
	class PluginInsert;
	class PortInsert;
	class Route;
	class Send;
	class Session;
}

class ProcessorBox;

/** A WindowProxy for Processor UI windows; it knows how to ask a ProcessorBox
 *  to create a UI window for a particular processor.
 */
class ProcessorWindowProxy : public WindowProxy<Gtk::Window>
{
public:
	ProcessorWindowProxy (std::string const &, XMLNode const *, ProcessorBox *, boost::weak_ptr<ARDOUR::Processor>);

	void show ();
	bool rc_configured () const {
		return false;
	}

	boost::weak_ptr<ARDOUR::Processor> processor () const {
		return _processor;
	}

	bool marked;

private:
	ProcessorBox* _processor_box;
	boost::weak_ptr<ARDOUR::Processor> _processor;
};

class ProcessorEntry : public Gtkmm2ext::DnDVBoxChild, public sigc::trackable
{
public:
	ProcessorEntry (boost::shared_ptr<ARDOUR::Processor>, Width);

	Gtk::EventBox& action_widget ();
	Gtk::Widget& widget ();
	std::string drag_text () const;
	void set_visual_state (Gtkmm2ext::VisualState, bool);

	enum Position {
		PreFader,
		Fader,
		PostFader
	};

	void set_position (Position);
	boost::shared_ptr<ARDOUR::Processor> processor () const;
	void set_enum_width (Width);
	virtual void set_pixel_width (int) {}

	/** Hide any widgets that should be hidden */
	virtual void hide_things () {}

protected:
	ArdourButton _button;
	Gtk::VBox _vbox;
	Position _position;

	virtual void setup_visuals ();

private:
	void led_clicked();
	void processor_active_changed ();
	void processor_property_changed (const PBD::PropertyChange&);
	std::string name (Width) const;
	void setup_tooltip ();

	boost::shared_ptr<ARDOUR::Processor> _processor;
	Width _width;
	Gtk::StateType _visual_state;
	PBD::ScopedConnection active_connection;
	PBD::ScopedConnection name_connection;
};

class SendProcessorEntry : public ProcessorEntry
{
public:
	SendProcessorEntry (boost::shared_ptr<ARDOUR::Send>, Width);

	static void setup_slider_pix ();

	void set_enum_width (Width, int);
	void set_pixel_width (int);

private:
	void show_gain ();
	void gain_adjusted ();
	void setup_gain_adjustment ();

	boost::shared_ptr<ARDOUR::Send> _send;
	Gtk::Adjustment _adjustment;
	Gtkmm2ext::HSliderController _fader;
	bool _ignore_gain_change;
	PBD::ScopedConnectionList _send_connections;
	ARDOUR::DataType _data_type;

	static Glib::RefPtr<Gdk::Pixbuf> _slider;
};

class PluginInsertProcessorEntry : public ProcessorEntry
{
public:
	PluginInsertProcessorEntry (boost::shared_ptr<ARDOUR::PluginInsert>, Width);

	void hide_things ();

private:
	void setup_visuals ();
	void plugin_insert_splitting_changed ();

	/* XXX: this seems a little ridiculous just for a simple scaleable icon */
	class SplittingIcon : public Gtk::DrawingArea {
	private:
		bool on_expose_event (GdkEventExpose *);
	};

	boost::shared_ptr<ARDOUR::PluginInsert> _plugin_insert;
	SplittingIcon _splitting_icon;
	PBD::ScopedConnection _splitting_connection;
};

class ProcessorBox : public Gtk::HBox, public PluginInterestedObject, public ARDOUR::SessionHandlePtr
{
  public:
	enum ProcessorOperation {
		ProcessorsCut,
		ProcessorsCopy,
		ProcessorsPaste,
		ProcessorsDelete,
		ProcessorsSelectAll,
		ProcessorsToggleActive,
		ProcessorsAB,
	};

	ProcessorBox (ARDOUR::Session*, boost::function<PluginSelector*()> get_plugin_selector,
		      RouteProcessorSelection&, MixerStrip* parent, bool owner_is_mixer = false);
	~ProcessorBox ();

	void set_route (boost::shared_ptr<ARDOUR::Route>);
	void set_width (Width);

	void update();

	void processor_operation (ProcessorOperation);

	void select_all_processors ();
	void deselect_all_processors ();
	void select_all_plugins ();
	void select_all_inserts ();
	void select_all_sends ();

	void hide_things ();

	Gtk::Window* get_processor_ui (boost::shared_ptr<ARDOUR::Processor>) const;
	void toggle_edit_processor (boost::shared_ptr<ARDOUR::Processor>);
	void toggle_processor_controls (boost::shared_ptr<ARDOUR::Processor>);

	sigc::signal<void,boost::shared_ptr<ARDOUR::Processor> > ProcessorSelected;
	sigc::signal<void,boost::shared_ptr<ARDOUR::Processor> > ProcessorUnselected;

	static void register_actions();

  private:

	/* prevent copy construction */
	ProcessorBox (ProcessorBox const &);

	boost::shared_ptr<ARDOUR::Route>  _route;
	MixerStrip*         _parent_strip; // null if in RouteParamsUI
	bool                _owner_is_mixer;
	bool                 ab_direction;
	PBD::ScopedConnectionList _mixer_strip_connections;
	PBD::ScopedConnectionList _route_connections;

	boost::function<PluginSelector*()> _get_plugin_selector;

	boost::shared_ptr<ARDOUR::Processor> _processor_being_created;

	/** Index at which to place a new plugin (based on where the menu was opened), or -1 to
	 *  put at the end of the plugin list.
	 */
	int _placement;

	RouteProcessorSelection& _rr_selection;

	void route_going_away ();

	Gtkmm2ext::DnDVBox<ProcessorEntry> processor_display;
	Gtk::ScrolledWindow    processor_scroller;

	void object_drop (Gtkmm2ext::DnDVBox<ProcessorEntry> *, ProcessorEntry *, Glib::RefPtr<Gdk::DragContext> const &);

	Width _width;

	Gtk::Menu *send_action_menu;
	void build_send_action_menu ();

	void new_send ();
	void show_send_controls ();

	Gtk::Menu *processor_menu;
	gint processor_menu_map_handler (GdkEventAny *ev);
	Gtk::Menu * build_processor_menu ();
	void build_processor_tooltip (Gtk::EventBox&, std::string);
	void show_processor_menu (int);
	Gtk::Menu* build_possible_aux_menu();

	void choose_aux (boost::weak_ptr<ARDOUR::Route>);
	void choose_send ();
	void send_io_finished (IOSelector::Result, boost::weak_ptr<ARDOUR::Processor>, IOSelectorWindow*);
	void return_io_finished (IOSelector::Result, boost::weak_ptr<ARDOUR::Processor>, IOSelectorWindow*);
	void choose_insert ();
	void choose_plugin ();
	bool use_plugins (const SelectedPlugins&);

	bool no_processor_redisplay;

	bool enter_notify (GdkEventCrossing *ev);
	bool leave_notify (GdkEventCrossing *ev);
	bool processor_button_press_event (GdkEventButton *, ProcessorEntry *);
	bool processor_button_release_event (GdkEventButton *, ProcessorEntry *);
	void redisplay_processors ();
	void add_processor_to_display (boost::weak_ptr<ARDOUR::Processor>);
	void reordered ();
	void report_failed_reorder ();
	void route_processors_changed (ARDOUR::RouteProcessorChange);
	void processor_menu_unmapped ();

	void processors_reordered (const Gtk::TreeModel::Path&, const Gtk::TreeModel::iterator&, int*);
	void compute_processor_sort_keys ();

	void all_processors_active(bool state);
	void all_plugins_active(bool state);
	void ab_plugins ();

	typedef std::vector<boost::shared_ptr<ARDOUR::Processor> > ProcSelection;

	void cut_processors (const ProcSelection&);
	void copy_processors (const ProcSelection&);
	void delete_processors (const ProcSelection&);
	void paste_processors ();
	void paste_processors (boost::shared_ptr<ARDOUR::Processor> before);
	void processors_up ();
	void processors_down ();

	void delete_dragged_processors (const std::list<boost::shared_ptr<ARDOUR::Processor> >&);
	void clear_processors ();
	void clear_processors (ARDOUR::Placement);
	void rename_processors ();

	void for_selected_processors (void (ProcessorBox::*pmf)(boost::shared_ptr<ARDOUR::Processor>));
	void get_selected_processors (ProcSelection&) const;

	bool can_cut() const;

	static Glib::RefPtr<Gtk::Action> cut_action;
	static Glib::RefPtr<Gtk::Action> paste_action;
	static Glib::RefPtr<Gtk::Action> rename_action;
	static Glib::RefPtr<Gtk::Action> edit_action;
	static Glib::RefPtr<Gtk::Action> controls_action;
	void paste_processor_state (const XMLNodeList&, boost::shared_ptr<ARDOUR::Processor>);

	void activate_processor (boost::shared_ptr<ARDOUR::Processor>);
	void deactivate_processor (boost::shared_ptr<ARDOUR::Processor>);
	void hide_processor_editor (boost::shared_ptr<ARDOUR::Processor>);
	void rename_processor (boost::shared_ptr<ARDOUR::Processor>);

	gint idle_delete_processor (boost::weak_ptr<ARDOUR::Processor>);

	void weird_plugin_dialog (ARDOUR::Plugin& p, ARDOUR::Route::ProcessorStreams streams);
	void on_size_allocate (Gtk::Allocation &);

	void setup_entry_positions ();

	static ProcessorBox* _current_processor_box;

	static void rb_choose_aux (boost::weak_ptr<ARDOUR::Route>);
	static void rb_choose_plugin ();
	static void rb_choose_insert ();
	static void rb_choose_send ();
	static void rb_clear ();
	static void rb_clear_pre ();
	static void rb_clear_post ();
	static void rb_cut ();
	static void rb_copy ();
	static void rb_paste ();
	static void rb_delete ();
	static void rb_rename ();
	static void rb_select_all ();
	static void rb_deselect_all ();
	static void rb_activate_all ();
	static void rb_deactivate_all ();
	static void rb_ab_plugins ();
	static void rb_edit ();
	static void rb_controls ();

	void route_property_changed (const PBD::PropertyChange&);
	std::string generate_processor_title (boost::shared_ptr<ARDOUR::PluginInsert> pi);

	std::list<ProcessorWindowProxy*> _processor_window_proxies;
	void set_processor_ui (boost::shared_ptr<ARDOUR::Processor>, Gtk::Window *);
	void maybe_add_processor_to_ui_list (boost::weak_ptr<ARDOUR::Processor>);

	bool one_processor_can_be_edited ();
	bool processor_can_be_edited (boost::shared_ptr<ARDOUR::Processor>);

	void mixer_strip_delivery_changed (boost::weak_ptr<ARDOUR::Delivery>);
};

#endif /* __ardour_gtk_processor_box__ */
