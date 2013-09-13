/*
    Copyright (C) 2010 Paul Davis

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

#ifndef __gtk2_ardour_engine_dialog_h__
#define __gtk2_ardour_engine_dialog_h__

#include <map>
#include <vector>
#include <string>

#include <gtkmm/checkbutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/table.h>
#include <gtkmm/expander.h>
#include <gtkmm/box.h>
#include <gtkmm/buttonbox.h>
#include <gtkmm/button.h>

#include "pbd/signals.h"

#include "ardour_dialog.h"

class EngineControl : public ArdourDialog, public PBD::ScopedConnectionList {
  public:
    EngineControl ();
    ~EngineControl ();
    
    static bool need_setup ();
    
    XMLNode& get_state ();
    void set_state (const XMLNode&);
    
    void set_desired_sample_rate (uint32_t);
    
  private:
    Gtk::Notebook notebook;

    /* core fields used by all backends */

    Gtk::ComboBoxText backend_combo;
    Gtk::ComboBoxText sample_rate_combo;
    Gtk::ComboBoxText buffer_size_combo;
    Gtk::Label        buffer_size_duration_label;
    Gtk::Adjustment input_latency_adjustment;
    Gtk::SpinButton input_latency;
    Gtk::Adjustment output_latency_adjustment;
    Gtk::SpinButton output_latency;
    Gtk::Adjustment input_channels_adjustment;
    Gtk::SpinButton input_channels;
    Gtk::Adjustment output_channels_adjustment;
    Gtk::SpinButton output_channels;
    Gtk::Adjustment ports_adjustment;
    Gtk::SpinButton ports_spinner;

    Gtk::Button     control_app_button;

    /* latency measurement */

    Gtk::ComboBoxText lm_output_channel_combo;
    Gtk::ComboBoxText lm_input_channel_combo;
    Gtk::ToggleButton lm_measure_button;
    Gtk::Button       lm_use_button;
    Gtk::Label        lm_title;
    Gtk::Label        lm_preamble;
    Gtk::Label        lm_results;
    Gtk::Table        lm_table;
    Gtk::VBox         lm_vbox;

    /* JACK specific */
    
    Gtk::CheckButton realtime_button;
    Gtk::CheckButton no_memory_lock_button;
    Gtk::CheckButton unlock_memory_button;
    Gtk::CheckButton soft_mode_button;
    Gtk::CheckButton monitor_button;
    Gtk::CheckButton force16bit_button;
    Gtk::CheckButton hw_monitor_button;
    Gtk::CheckButton hw_meter_button;
    Gtk::CheckButton verbose_output_button;
    
    Gtk::ComboBoxText preset_combo;
    Gtk::ComboBoxText serverpath_combo;
    Gtk::ComboBoxText driver_combo;
    Gtk::ComboBoxText device_combo;
    Gtk::ComboBoxText timeout_combo;
    Gtk::ComboBoxText dither_mode_combo;
    Gtk::ComboBoxText audio_mode_combo;
    Gtk::ComboBoxText midi_driver_combo;
    
    Gtk::Table basic_packer;
    Gtk::Table midi_packer;
    Gtk::HBox basic_hbox;
    Gtk::VBox basic_vbox;
    Gtk::HBox midi_hbox;

    uint32_t ignore_changes;
    
    static bool engine_running ();
    
    void driver_changed ();
    void backend_changed ();
    void sample_rate_changed ();
    void buffer_size_changed ();
    void parameter_changed ();

    uint32_t get_rate() const;
    uint32_t get_buffer_size() const;
    uint32_t get_input_channels() const;
    uint32_t get_output_channels() const;
    uint32_t get_input_latency() const;
    uint32_t get_output_latency() const;
    std::string get_device_name() const;
    std::string get_driver() const;

    void device_changed ();
    void list_devices ();
    void show_buffer_duration ();

    struct State {
	std::string backend;
	std::string driver;
	std::string device;
	std::string sample_rate;
	std::string buffer_size;
	uint32_t input_latency;
	uint32_t output_latency;
	uint32_t input_channels;
	uint32_t output_channels;
	bool active;

	State() : active (false) {};
    };
    
    typedef std::list<State> StateList;

    StateList states;

    State* get_matching_state (const std::string& backend,
			       const std::string& driver,
			       const std::string& device);
    State* get_saved_state_for_currently_displayed_backend_and_device ();
    void maybe_display_saved_state ();
    State* save_state ();

    static bool print_channel_count (Gtk::SpinButton*);

    void build_notebook ();

    void on_response (int);
    void control_app_button_clicked ();
    void use_latency_button_clicked ();
    void manage_control_app_sensitivity ();
    int push_state_to_backend (bool start);
    uint32_t _desired_sample_rate;

    /* latency measurement */
    void latency_button_toggled ();
    bool check_latency_measurement ();
    sigc::connection latency_timeout;
    void enable_latency_tab ();
    void disable_latency_tab ();

    void on_switch_page (GtkNotebookPage*, guint page_num);
};

#endif /* __gtk2_ardour_engine_dialog_h__ */
