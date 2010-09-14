/*
    Copyright (C) 1999-2002 Paul Davis

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

#ifndef __ardour_gui_h__
#define __ardour_gui_h__

#include <time.h>

/* need _BSD_SOURCE to get timersub macros */

#ifdef _BSD_SOURCE
#include <sys/time.h>
#else
#define _BSD_SOURCE
#include <sys/time.h>
#undef _BSD_SOURCE
#endif

#include <list>
#include <cmath>

#include <libgnomecanvasmm/canvas.h>

#include "pbd/xml++.h"
#include "pbd/controllable.h"
#include <gtkmm/box.h>
#include <gtkmm/frame.h>
#include <gtkmm/label.h>
#include <gtkmm/table.h>
#include <gtkmm/fixed.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/button.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treeview.h>
#include <gtkmm/menubar.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/adjustment.h>
#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/click_box.h>
#include <gtkmm2ext/stateful_button.h>
#include <gtkmm2ext/bindable_button.h>
#include "ardour/ardour.h"
#include "ardour/types.h"
#include "ardour/utils.h"
#include "ardour/session_handle.h"

#include "audio_clock.h"
#include "ardour_dialog.h"
#include "editing.h"
#include "ui_config.h"
#include "window_proxy.h"

class About;
class AddRouteDialog;
class ArdourStartup;
class ArdourKeyboard;
class AudioClock;
class BundleManager;
class ConnectionEditor;
class KeyEditor;
class LocationUIWindow;
class Mixer_UI;
class PublicEditor;
class RCOptionEditor;
class RouteParams_UI;
class SessionOptionEditor;
class Splash;
class ThemeManager;
class MidiTracer;
class WindowProxyBase;
class GlobalPortMatrixWindow;

namespace Gtkmm2ext {
	class TearOff;
}

namespace ARDOUR {
	class ControlProtocolInfo;
	class IO;
	class Port;
	class Route;
	class RouteGroup;
	class Location;
}

extern sigc::signal<void>  ColorsChanged;
extern sigc::signal<void>  DPIReset;

class ARDOUR_UI : public Gtkmm2ext::UI, public ARDOUR::SessionHandlePtr
{
  public:
	ARDOUR_UI (int *argcp, char **argvp[]);
	~ARDOUR_UI();

	bool run_startup (bool should_be_new, std::string load_template);

	void show_splash ();
	void hide_splash ();

        void launch_chat ();
        void launch_manual ();
        void launch_reference ();
	void show_about ();
	void hide_about ();

	void idle_load (const std::string& path);
	void finish();

	int load_session (const std::string& path, const std::string& snapshot, std::string mix_template = std::string());
	bool session_loaded;
	int build_session (const std::string& path, const std::string& snapshot, ARDOUR::BusProfile&);
	bool session_is_new() const { return _session_is_new; }

	ARDOUR::Session* the_session() { return _session; }

	bool will_create_new_session_automatically() const {
		return _will_create_new_session_automatically;
	}

	void set_will_create_new_session_automatically (bool yn) {
		_will_create_new_session_automatically = yn;
	}

	int get_session_parameters (bool quit_on_cancel, bool should_be_new = false, std::string load_template = "");
	void parse_cmdline_path (const std::string& cmdline_path, std::string& session_name, std::string& session_path, bool& existing_session);
	int  load_cmdline_session (const std::string& session_name, const std::string& session_path, bool& existing_session);
	int  build_session_from_nsd (const std::string& session_name, const std::string& session_path);
	bool ask_about_loading_existing_session (const std::string& session_path);

	/// @return true if session was successfully unloaded.
	int unload_session (bool hide_stuff = false);
	void close_session();

	int  save_state_canfail (std::string state_name = "", bool switch_to_it = false);
	void save_state (const std::string & state_name = "", bool switch_to_it = false);

	static double gain_to_slider_position (ARDOUR::gain_t g);
	static ARDOUR::gain_t slider_position_to_gain (double pos);

	static ARDOUR_UI *instance () { return theArdourUI; }
	static UIConfiguration *config () { return ui_config; }

	PublicEditor&	  the_editor(){return *editor;}
	Mixer_UI* the_mixer() { return mixer; }

	void toggle_key_editor ();
	void toggle_location_window ();
	void toggle_theme_manager ();
	void toggle_bundle_manager ();
	void toggle_big_clock_window ();
	void new_midi_tracer_window ();
	void toggle_route_params_window ();
	void toggle_editing_space();
	void toggle_keep_tearoffs();

	Gtk::Tooltips& tooltips() { return _tooltips; }

	static sigc::signal<void,bool> Blink;
	static sigc::signal<void>      RapidScreenUpdate;
	static sigc::signal<void>      SuperRapidScreenUpdate;
	static sigc::signal<void,nframes_t, bool, nframes_t> Clock;

	XMLNode* editor_settings() const;
	XMLNode* mixer_settings () const;
	XMLNode* keyboard_settings () const;
        XMLNode* tearoff_settings (const char*) const;

	void save_ardour_state ();
	gboolean configure_handler (GdkEventConfigure* conf);

	void do_transport_locate (nframes_t position);
	void halt_on_xrun_message ();
	void xrun_handler (nframes_t);
	void create_xrun_marker (nframes_t);

	AudioClock primary_clock;
	AudioClock secondary_clock;
	AudioClock preroll_clock;
	AudioClock postroll_clock;

	void store_clock_modes ();
	void restore_clock_modes ();
	void reset_main_clocks ();

        void synchronize_sync_source_and_video_pullup ();

	void add_route (Gtk::Window* float_window);

	void session_add_audio_track (int input_channels, int32_t output_channels, ARDOUR::TrackMode mode, ARDOUR::RouteGroup* route_group, uint32_t how_many) {
		session_add_audio_route (true, false, input_channels, output_channels, mode, route_group, how_many);
	}

	void session_add_audio_bus (bool aux,  int input_channels, int32_t output_channels, ARDOUR::RouteGroup* route_group, uint32_t how_many) {
		session_add_audio_route (false, aux, input_channels, output_channels, ARDOUR::Normal, route_group, how_many);
	}

	void session_add_midi_track (ARDOUR::RouteGroup* route_group, uint32_t how_many) {
		session_add_midi_route (true, route_group, how_many);
	}

	/*void session_add_midi_bus () {
		session_add_midi_route (false);
	}*/

	int  create_engine ();
	void post_engine ();

	gint exit_on_main_window_close (GdkEventAny *);

	void maximise_editing_space ();
	void restore_editing_space ();

	void setup_profile ();
	void setup_theme ();
	void setup_tooltips ();

	void set_shuttle_fract (double);

	void add_window_proxy (WindowProxyBase *);
	void remove_window_proxy (WindowProxyBase *);
	
  protected:
	friend class PublicEditor;

	void toggle_clocking ();
	void toggle_auto_play ();
	void toggle_auto_input ();
	void toggle_punch ();
	void unset_dual_punch ();
	bool ignore_dual_punch;
	void toggle_punch_in ();
	void toggle_punch_out ();
	void show_loop_punch_ruler_and_disallow_hide ();
	void reenable_hide_loop_punch_ruler_if_appropriate ();
	void toggle_auto_return ();
	void toggle_click ();

	void toggle_session_auto_loop ();

	void toggle_rc_options_window ();
	void toggle_session_options_window ();

  private:
	ArdourStartup*      _startup;
	ARDOUR::AudioEngine *engine;
	Gtk::Tooltips        _tooltips;

	void                goto_editor_window ();
	void                goto_mixer_window ();
	void                toggle_editor_mixer_on_top ();
	bool                _mixer_on_top;

	Gtk::ToggleButton   preroll_button;
	Gtk::ToggleButton   postroll_button;

	int  setup_windows ();
	void setup_transport ();
	void setup_clock ();

	static ARDOUR_UI *theArdourUI;

	void backend_audio_error (bool we_set_params, Gtk::Window* toplevel = 0);
	void startup ();
	void shutdown ();

	int  ask_about_saving_session (const std::string & why);

	/* periodic safety backup, to be precise */
	gint autosave_session();
	void update_autosave();
	sigc::connection _autosave_connection;

	void map_transport_state ();
	int32_t do_engine_start ();

	void engine_halted (const char* reason, bool free_reason);
	void engine_stopped ();
	void engine_running ();

	void use_config ();

	static gint _blink  (void *);
	void blink ();
	gint blink_timeout_tag;
	bool blink_on;
	void start_blinking ();
	void stop_blinking ();

	void about_signal_response(int response);

  private:
	Gtk::VBox     top_packer;

	sigc::connection clock_signal_connection;
	void         update_clocks ();
	void         start_clocking ();
	void         stop_clocking ();

	void manage_window (Gtk::Window&);

	AudioClock   big_clock;
	ActionWindowProxy<Gtk::Window>* big_clock_window;
        int original_big_clock_width;
        int original_big_clock_height;
        double original_big_clock_font_size;

	void big_clock_size_allocate (Gtk::Allocation&);
	bool idle_big_clock_text_resizer (int width, int height);
	void big_clock_realized ();
	bool big_clock_resize_in_progress;
	int  big_clock_height;

	void float_big_clock (Gtk::Window* parent);
	bool main_window_state_event_handler (GdkEventWindowState*, bool window_was_editor);

	void update_transport_clocks (nframes_t pos);
	void record_state_changed ();

	std::list<MidiTracer*> _midi_tracer_windows;

	/* Transport Control */

	void detach_tearoff (Gtk::Box* parent, Gtk::Widget* contents);
	void reattach_tearoff (Gtk::Box* parent, Gtk::Widget* contents, int32_t order);

	Gtkmm2ext::TearOff*      transport_tearoff;
	Gtk::Frame               transport_frame;
	Gtk::HBox                transport_tearoff_hbox;
	Gtk::HBox                play_range_hbox;
	Gtk::VBox                play_range_vbox;
	Gtk::HBox                transport_hbox;
	Gtk::Fixed               transport_base;
	Gtk::Fixed               transport_button_base;
	Gtk::Frame               transport_button_frame;
	Gtk::HBox                transport_button_hbox;
	Gtk::VBox                transport_button_vbox;
	Gtk::HBox                transport_option_button_hbox;
	Gtk::VBox                transport_option_button_vbox;
	Gtk::HBox                transport_clock_hbox;
	Gtk::VBox                transport_clock_vbox;
	Gtk::HBox                primary_clock_hbox;
	Gtk::HBox                secondary_clock_hbox;


	struct TransportControllable : public PBD::Controllable {
	    enum ToggleType {
		    Roll = 0,
		    Stop,
		    RecordEnable,
		    GotoStart,
		    GotoEnd,
		    AutoLoop,
		    PlaySelection,
		    ShuttleControl

	    };

	    TransportControllable (std::string name, ARDOUR_UI&, ToggleType);
	    void set_value (double);
	    double get_value (void) const;

	    void set_id (const std::string&);

	    ARDOUR_UI& ui;
	    ToggleType type;
	};

	boost::shared_ptr<TransportControllable> roll_controllable;
	boost::shared_ptr<TransportControllable> stop_controllable;
	boost::shared_ptr<TransportControllable> goto_start_controllable;
	boost::shared_ptr<TransportControllable> goto_end_controllable;
	boost::shared_ptr<TransportControllable> auto_loop_controllable;
	boost::shared_ptr<TransportControllable> play_selection_controllable;
	boost::shared_ptr<TransportControllable> rec_controllable;
	boost::shared_ptr<TransportControllable> shuttle_controllable;
	BindingProxy shuttle_controller_binding_proxy;

	void set_transport_controllable_state (const XMLNode&);
	XMLNode& get_transport_controllable_state ();

	BindableButton roll_button;
	BindableButton stop_button;
	BindableButton goto_start_button;
	BindableButton goto_end_button;
	BindableButton auto_loop_button;
	BindableButton play_selection_button;
	BindableButton rec_button;
	Gtk::ToggleButton join_play_range_button;

	void toggle_external_sync ();
	void toggle_time_master ();
	void toggle_video_sync ();

	Gtk::DrawingArea  shuttle_box;
	Gtk::EventBox     speed_display_box;
	Gtk::Label        speed_display_label;
	Gtk::Button       shuttle_units_button;
	Gtk::ComboBoxText shuttle_style_button;
	Gtk::Menu*        shuttle_unit_menu;
	Gtk::Menu*        shuttle_style_menu;
	float             shuttle_max_speed;
	Gtk::Menu*        shuttle_context_menu;

	void build_shuttle_context_menu ();
	void show_shuttle_context_menu ();
	void shuttle_style_changed();
	void shuttle_unit_clicked ();
	void set_shuttle_max_speed (float);
	void update_speed_display ();
	float last_speed_displayed;

	gint shuttle_box_button_press (GdkEventButton*);
	gint shuttle_box_button_release (GdkEventButton*);
	gint shuttle_box_scroll (GdkEventScroll*);
	gint shuttle_box_motion (GdkEventMotion*);
	gint shuttle_box_expose (GdkEventExpose*);
	gint mouse_shuttle (double x, bool force);
	void use_shuttle_fract (bool force);

	bool   shuttle_grabbed;
	double shuttle_fract;

	Gtkmm2ext::StatefulToggleButton punch_in_button;
	Gtkmm2ext::StatefulToggleButton punch_out_button;
	Gtkmm2ext::StatefulToggleButton auto_return_button;
	Gtkmm2ext::StatefulToggleButton auto_play_button;
	Gtkmm2ext::StatefulToggleButton auto_input_button;
	Gtkmm2ext::StatefulToggleButton click_button;
	Gtkmm2ext::StatefulToggleButton time_master_button;
	Gtkmm2ext::StatefulToggleButton sync_button;

	Gtk::ToggleButton auditioning_alert_button;
	Gtk::ToggleButton solo_alert_button;

	Gtk::VBox alert_box;

	void solo_blink (bool);
	void sync_blink (bool);
	void audition_blink (bool);

	void soloing_changed (bool);
	void auditioning_changed (bool);
	void _auditioning_changed (bool);

	bool solo_alert_press (GdkEventButton* ev);
	bool audition_alert_press (GdkEventButton* ev);

	void big_clock_value_changed ();
	void primary_clock_value_changed ();
	void secondary_clock_value_changed ();

	/* called by Blink signal */

	void transport_rec_enable_blink (bool onoff);

	Gtk::Menu*        session_popup_menu;

	struct RecentSessionModelColumns : public Gtk::TreeModel::ColumnRecord {
	    RecentSessionModelColumns() {
		    add (visible_name);
		    add (fullpath);
	    }
	    Gtk::TreeModelColumn<std::string> visible_name;
	    Gtk::TreeModelColumn<std::string> fullpath;
	};

	RecentSessionModelColumns    recent_session_columns;
	Gtk::TreeView                recent_session_display;
	Glib::RefPtr<Gtk::TreeStore> recent_session_model;

	ArdourDialog*     session_selector_window;
	Gtk::FileChooserDialog* open_session_selector;

	void build_session_selector();
	void redisplay_recent_sessions();
	void recent_session_row_activated (const Gtk::TreePath& path, Gtk::TreeViewColumn* col);

	struct RecentSessionsSorter {
		bool operator() (std::pair<std::string,std::string> a, std::pair<std::string,std::string> b) const {
		    return cmp_nocase(a.first, b.first) == -1;
	    }
	};

	/* menu bar and associated stuff */

	Gtk::MenuBar* menu_bar;
	Gtk::EventBox menu_bar_base;
	Gtk::HBox     menu_hbox;

	void use_menubar_as_top_menubar ();
	void build_menu_bar ();

	Gtk::Label   wall_clock_label;
	Gtk::EventBox wall_clock_box;
	gint update_wall_clock ();

	Gtk::Label   disk_space_label;
	Gtk::EventBox disk_space_box;
	void update_disk_space ();

	Gtk::Label   cpu_load_label;
	Gtk::EventBox cpu_load_box;
	void update_cpu_load ();

	Gtk::Label   buffer_load_label;
	Gtk::EventBox buffer_load_box;
	void update_buffer_load ();

	Gtk::Label   sample_rate_label;
	Gtk::EventBox sample_rate_box;
	void update_sample_rate (nframes_t);

	gint every_second ();
	gint every_point_one_seconds ();
	gint every_point_zero_one_seconds ();

	sigc::connection second_connection;
	sigc::connection point_one_second_connection;
	sigc::connection point_oh_five_second_connection;
	sigc::connection point_zero_one_second_connection;

	gint session_menu (GdkEventButton *);

	bool _will_create_new_session_automatically;

	void open_session ();
	void open_recent_session ();
	void save_template ();

	void edit_metadata ();
	void import_metadata ();

	void session_add_audio_route (bool disk, bool aux, int32_t input_channels, int32_t output_channels, ARDOUR::TrackMode mode, ARDOUR::RouteGroup *, uint32_t how_many);
	void session_add_midi_route (bool disk, ARDOUR::RouteGroup *, uint32_t how_many);

	void set_transport_sensitivity (bool);

	void remove_last_capture ();

	void transport_goto_zero ();
	void transport_goto_start ();
	void transport_goto_end ();
	void transport_goto_wallclock ();
	void transport_stop ();
	void transport_stop_and_forget_capture ();
	void transport_record (bool roll);
	void transport_roll ();
	void transport_play_selection();
	void transport_forward (int option);
	void transport_rewind (int option);
	void transport_loop ();
	void toggle_roll (bool with_abort, bool roll_out_of_bounded_mode);

	bool _session_is_new;
	void set_session (ARDOUR::Session *);
	void connect_dependents_to_session (ARDOUR::Session *);
	void we_have_dependents ();

	void setup_session_options ();

	guint32  last_key_press_time;

	void snapshot_session (bool switch_to_it);

	Mixer_UI   *mixer;
	int         create_mixer ();

	PublicEditor     *editor;
	int         create_editor ();

	RouteParams_UI *route_params;
	int             create_route_params ();

	BundleManager *bundle_manager;
	void create_bundle_manager ();

	ActionWindowProxy<LocationUIWindow>* location_ui;
	int               create_location_ui ();
	void              handle_locations_change (ARDOUR::Location*);

	ActionWindowProxy<GlobalPortMatrixWindow>* _global_port_matrix[ARDOUR::DataType::num_types];
	void toggle_global_port_matrix (ARDOUR::DataType);

	static UIConfiguration *ui_config;
	ThemeManager *theme_manager;

	/* Key bindings editor */

	KeyEditor *key_editor;

	/* RC Options window */

	RCOptionEditor *rc_option_editor;

	SessionOptionEditor *session_option_editor;

	/* route dialog */

	AddRouteDialog *add_route_dialog;

	/* Keyboard Handling */

	ArdourKeyboard* keyboard;
	
	/* Keymap handling */

	void install_actions ();

	void toggle_record_enable (uint32_t);

	uint32_t rec_enabled_streams;
	void count_recenabled_streams (ARDOUR::Route&);

	About* about;
	Splash* splash;
	void pop_back_splash ();

	/* cleanup */

	Gtk::MenuItem *cleanup_item;

	void display_cleanup_results (ARDOUR::CleanupReport& rep, const gchar* list_title,
				      const std::string& plural_msg, const std::string& singular_msg);
	void cleanup ();
	void flush_trash ();

	bool have_configure_timeout;
	ARDOUR::microseconds_t last_configure_time;
	gint configure_timeout ();

	ARDOUR::microseconds_t last_peak_grab;
	ARDOUR::microseconds_t last_shuttle_request;

	bool have_disk_speed_dialog_displayed;
	void disk_speed_dialog_gone (int ignored_response, Gtk::MessageDialog*);
	void disk_overrun_handler ();
	void disk_underrun_handler ();

	void session_dialog (std::string);
	int pending_state_dialog ();
	int sr_mismatch_dialog (nframes_t, nframes_t);

	void disconnect_from_jack ();
	void reconnect_to_jack ();
	void set_jack_buffer_size (nframes_t);

	Gtk::MenuItem* jack_disconnect_item;
	Gtk::MenuItem* jack_reconnect_item;
	Gtk::Menu*     jack_bufsize_menu;

	Glib::RefPtr<Gtk::ActionGroup> common_actions;

	void editor_realized ();

	std::vector<std::string> positional_sync_strings;

	void toggle_send_midi_feedback ();
	void toggle_use_mmc ();
	void toggle_send_mmc ();
	void toggle_send_mtc ();
	void toggle_send_midi_clock ();

	void toggle_use_osc ();

	void parameter_changed (std::string);

	bool first_idle ();

	void no_memory_warning ();
	void check_memory_locking ();

	bool check_audioengine();
	void audioengine_setup ();

	void display_message (const char *prefix, gint prefix_len,
			Glib::RefPtr<Gtk::TextBuffer::Tag> ptag, Glib::RefPtr<Gtk::TextBuffer::Tag> mtag,
			const char *msg);
	Gtk::Label status_bar_label;
        bool status_bar_button_press (GdkEventButton*);
	Gtk::ToggleButton error_log_button;

	void loading_message (const std::string& msg);
	void end_loading_messages ();

	void platform_specific ();
	void platform_setup ();
	void fontconfig_dialog ();
        void toggle_translations ();

	PBD::ScopedConnectionList forever_connections;

        void step_edit_status_change (bool);

	/* these are used only in response to a platform-specific "ShouldQuit" signal
	 */
	bool idle_finish ();
	void queue_finish ();

	std::list<WindowProxyBase*> _window_proxies;
};

#endif /* __ardour_gui_h__ */

