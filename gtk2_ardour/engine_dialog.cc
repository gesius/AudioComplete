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

#include <exception>
#include <vector>
#include <cmath>
#include <fstream>
#include <map>

#include <boost/scoped_ptr.hpp>

#include <gtkmm/messagedialog.h>

#include "pbd/error.h"
#include "pbd/xml++.h"
#include "pbd/unwind.h"

#include <gtkmm/alignment.h>
#include <gtkmm/stock.h>
#include <gtkmm/notebook.h>
#include <gtkmm2ext/utils.h>

#include "ardour/audio_backend.h"
#include "ardour/audioengine.h"
#include "ardour/mtdm.h"
#include "ardour/rc_configuration.h"
#include "ardour/types.h"

#include "pbd/convert.h"
#include "pbd/error.h"

#include "engine_dialog.h"
#include "gui_thread.h"
#include "i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace PBD;
using namespace Glib;

EngineControl::EngineControl ()
	: ArdourDialog (_("Audio/MIDI Setup"))
	, basic_packer (9, 3)
	, input_latency_adjustment (0, 0, 99999, 1)
	, input_latency (input_latency_adjustment)
	, output_latency_adjustment (0, 0, 99999, 1)
	, output_latency (output_latency_adjustment)
	, input_channels_adjustment (0, 0, 256, 1)
	, input_channels (input_channels_adjustment)
	, output_channels_adjustment (0, 0, 256, 1)
	, output_channels (output_channels_adjustment)
	, ports_adjustment (128, 8, 1024, 1, 16)
	, ports_spinner (ports_adjustment)
	, control_app_button (_("Device Control Panel"))
	, lm_start_stop_label (_("Measure latency"))
	, lm_use_button (_("Use results"))
	, lm_table (5, 2)
	, have_lm_results (false)
	, midi_refresh_button (_("Refresh list"))
	, aj_button (_("Start MIDI ALSA/JACK bridge"))
	, ignore_changes (0)
	, _desired_sample_rate (0)
{
	set_name (X_("AudioMIDISetup"));

	build_notebook ();

	get_vbox()->set_border_width (12);
	get_vbox()->pack_start (notebook);

	control_app_button.signal_clicked().connect (mem_fun (*this, &EngineControl::control_app_button_clicked));
	manage_control_app_sensitivity ();

	cancel_button = add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	ok_button = add_button (Gtk::Stock::OK, Gtk::RESPONSE_OK);
	apply_button = add_button (Gtk::Stock::APPLY, Gtk::RESPONSE_APPLY);

	/* Pick up any existing audio setup configuration, if appropriate */

	XMLNode* audio_setup = ARDOUR::Config->extra_xml ("AudioMIDISetup");
	
	/* push a change as if we altered the backend */
	backend_changed ();

	if (audio_setup) {
		set_state (*audio_setup);
	} 
}

void
EngineControl::on_response (int response_id)
{
	ArdourDialog::on_response (response_id);

	switch (response_id) {
	case RESPONSE_APPLY:
		push_state_to_backend (true);
		break;
	case RESPONSE_OK:
		push_state_to_backend (true);
		hide ();
		break;
	case RESPONSE_DELETE_EVENT: {
		GdkEventButton ev;
		ev.type = GDK_BUTTON_PRESS;
		ev.button = 1;
		on_delete_event ((GdkEventAny*) &ev);
		break;
	}
	default:
		hide ();
	}
}

void
EngineControl::build_notebook ()
{
	using namespace Notebook_Helpers;
	Label* label;
	vector<string> strings;
	int row = 0;

	vector<const ARDOUR::AudioBackendInfo*> backends = ARDOUR::AudioEngine::instance()->available_backends();
	for (vector<const ARDOUR::AudioBackendInfo*>::const_iterator b = backends.begin(); b != backends.end(); ++b) {
		strings.push_back ((*b)->name);
	}

	set_popdown_strings (backend_combo, strings);
	backend_combo.set_active_text (strings.front());

	basic_packer.set_spacings (6);
	basic_packer.set_border_width (12);
	basic_packer.set_homogeneous (true);

	row = 0;

	AttachOptions xopt = AttachOptions (FILL|EXPAND);

	label = manage (left_aligned_label (_("Audio System:")));
	basic_packer.attach (*label, 0, 1, row, row + 1, xopt, (AttachOptions) 0);
	basic_packer.attach (backend_combo, 1, 2, row, row + 1, xopt, (AttachOptions) 0);
	row++;

	label = manage (left_aligned_label (_("Driver:")));
	basic_packer.attach (*label, 0, 1, row, row + 1, xopt, (AttachOptions) 0);
	basic_packer.attach (driver_combo, 1, 2, row, row + 1, xopt, (AttachOptions) 0);
	row++;

	label = manage (left_aligned_label (_("Device:")));
	basic_packer.attach (*label, 0, 1, row, row + 1, xopt, (AttachOptions) 0);
	basic_packer.attach (device_combo, 1, 2, row, row + 1, xopt, (AttachOptions) 0);
	row++;

	label = manage (left_aligned_label (_("Sample rate:")));
	basic_packer.attach (*label, 0, 1, row, row + 1, xopt, (AttachOptions) 0);
	basic_packer.attach (sample_rate_combo, 1, 2, row, row + 1, xopt, (AttachOptions) 0);
	row++;


	label = manage (left_aligned_label (_("Buffer size:")));
	basic_packer.attach (*label, 0, 1, row, row + 1, xopt, (AttachOptions) 0);
	basic_packer.attach (buffer_size_combo, 1, 2, row, row + 1, xopt, (AttachOptions) 0);
	buffer_size_duration_label.set_alignment (0.0); /* left-align */
	basic_packer.attach (buffer_size_duration_label, 2, 3, row, row+1, xopt, (AttachOptions) 0);
	row++;

	input_channels.set_name ("InputChannels");
	input_channels.set_flags(Gtk::CAN_FOCUS);
	input_channels.set_digits(0);
	input_channels.set_wrap(false);
	output_channels.set_editable (true);

	label = manage (left_aligned_label (_("Input Channels:")));
	basic_packer.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);
	basic_packer.attach (input_channels, 1, 2, row, row+1, xopt, (AttachOptions) 0);
	++row;

	output_channels.set_name ("OutputChannels");
	output_channels.set_flags(Gtk::CAN_FOCUS);
	output_channels.set_digits(0);
	output_channels.set_wrap(false);
	output_channels.set_editable (true);

	label = manage (left_aligned_label (_("Output Channels:")));
	basic_packer.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);
	basic_packer.attach (output_channels, 1, 2, row, row+1, xopt, (AttachOptions) 0);
	++row;

	input_latency.set_name ("InputLatency");
	input_latency.set_flags(Gtk::CAN_FOCUS);
	input_latency.set_digits(0);
	input_latency.set_wrap(false);
	input_latency.set_editable (true);

	label = manage (left_aligned_label (_("Hardware input latency:")));
	basic_packer.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);
	basic_packer.attach (input_latency, 1, 2, row, row+1, xopt, (AttachOptions) 0);
	label = manage (left_aligned_label (_("samples")));
	basic_packer.attach (*label, 2, 3, row, row+1, xopt, (AttachOptions) 0);
	++row;

	output_latency.set_name ("OutputLatency");
	output_latency.set_flags(Gtk::CAN_FOCUS);
	output_latency.set_digits(0);
	output_latency.set_wrap(false);
	output_latency.set_editable (true);

	label = manage (left_aligned_label (_("Hardware output latency:")));
	basic_packer.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);
	basic_packer.attach (output_latency, 1, 2, row, row+1, xopt, (AttachOptions) 0);
	label = manage (left_aligned_label (_("samples")));
	basic_packer.attach (*label, 2, 3, row, row+1, xopt, (AttachOptions) 0);
	++row;

	basic_hbox.pack_start (basic_packer, false, false);
	basic_vbox.pack_start (basic_hbox, false, false);

	Gtk::HBox* hpacker = manage (new HBox);
	hpacker->set_border_width (12);
	hpacker->pack_start (control_app_button, false, false);
	hpacker->show ();
	control_app_button.show();
	basic_vbox.pack_start (*hpacker);

	/* latency measurement tab */
	
	lm_title.set_markup (string_compose ("<span size=\"large\" weight=\"bold\">%1</span>", _("Latency Measurement Tool")));
	
	row = 0;
	lm_table.set_row_spacings (12);

	lm_table.attach (lm_title, 0, 2, row, row+1, xopt, (AttachOptions) 0);
	row++;

	Gtk::Label* preamble;

	preamble = manage (new Label);
	preamble->set_width_chars (60);
	preamble->set_line_wrap (true);
	preamble->set_markup (_("<span weight=\"bold\">Turn down the volume on your hardware to a very low level.</span>"));

	lm_table.attach (*preamble, 0, 2, row, row+1, AttachOptions(FILL|EXPAND), (AttachOptions) 0);
	row++;

	preamble = manage (new Label);
	preamble->set_width_chars (60);
	preamble->set_line_wrap (true);
	preamble->set_markup (_("Select two channels below and connect them using a cable or (less ideally) a speaker and microphone."));

	lm_table.attach (*preamble, 0, 2, row, row+1, AttachOptions(FILL|EXPAND), (AttachOptions) 0);
	row++;

	label = manage (new Label (_("Output channel")));
	lm_table.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);

	Gtk::Alignment* misc_align = manage (new Alignment (0.0, 0.5));
	misc_align->add (lm_output_channel_combo);
	lm_table.attach (*misc_align, 1, 2, row, row+1, xopt, (AttachOptions) 0);
	++row;

	label = manage (new Label (_("Input channel")));
	lm_table.attach (*label, 0, 1, row, row+1, xopt, (AttachOptions) 0);

	misc_align = manage (new Alignment (0.0, 0.5));
	misc_align->add (lm_input_channel_combo);
	lm_table.attach (*misc_align, 1, 2, row, row+1, FILL, (AttachOptions) 0);
	++row;

	xopt = AttachOptions(0);

	lm_measure_button.add (lm_start_stop_label);
	
	lm_measure_button.signal_toggled().connect (sigc::mem_fun (*this, &EngineControl::latency_button_toggled));
	lm_use_button.signal_clicked().connect (sigc::mem_fun (*this, &EngineControl::use_latency_button_clicked));
	lm_use_button.set_sensitive (false);
		

	preamble = manage (new Label);
	preamble->set_width_chars (60);
	preamble->set_line_wrap (true);
	preamble->set_markup (_("Once the channels are connected, click the \"Measure latency\" button."));
	lm_table.attach (*preamble, 0, 2, row, row+1, AttachOptions(FILL|EXPAND), (AttachOptions) 0);
	row++;

	lm_table.attach (lm_measure_button, 0, 2, row, row+1, xopt, (AttachOptions) 0);
	++row;
	lm_table.attach (lm_results, 0, 2, row, row+1, AttachOptions(FILL|EXPAND), (AttachOptions) 0);
	++row;


	preamble = manage (new Label);
	preamble->set_width_chars (60);
	preamble->set_line_wrap (true);
	preamble->set_markup (_("When satisfied with the results, click the \"Use results\" button."));
	lm_table.attach (*preamble, 0, 2, row, row+1, AttachOptions(FILL|EXPAND), (AttachOptions) 0);
	row++;

	lm_table.attach (lm_use_button, 0, 2, row, row+1, xopt, (AttachOptions) 0);
	++row;

	lm_results.set_markup ("<i>No measurement results yet</i>");

	lm_vbox.set_border_width (12);
	lm_vbox.pack_start (lm_table, false, false);

	/* pack it all up */

	notebook.pages().push_back (TabElem (basic_vbox, _("Audio")));
	notebook.pages().push_back (TabElem (midi_vbox, _("MIDI")));
	notebook.pages().push_back (TabElem (lm_vbox, _("Latency")));
	notebook.set_border_width (12);

	notebook.set_tab_pos (POS_RIGHT);
	notebook.show_all ();

	notebook.set_name ("SettingsNotebook");

	/* Connect to signals */

	backend_combo.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::backend_changed));
	driver_combo.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::driver_changed));
	sample_rate_combo.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::sample_rate_changed));
	buffer_size_combo.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::buffer_size_changed));
	device_combo.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::device_changed));

	input_latency.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::parameter_changed));
	output_latency.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::parameter_changed));
	input_channels.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::parameter_changed));
	output_channels.signal_changed().connect (sigc::mem_fun (*this, &EngineControl::parameter_changed));

	input_channels.signal_output().connect (sigc::bind (sigc::ptr_fun (&EngineControl::print_channel_count), &input_channels));
	output_channels.signal_output().connect (sigc::bind (sigc::ptr_fun (&EngineControl::print_channel_count), &output_channels));

	notebook.signal_switch_page().connect (sigc::mem_fun (*this, &EngineControl::on_switch_page));
}

EngineControl::~EngineControl ()
{

}

void
EngineControl::disable_latency_tab ()
{
	vector<string> empty;
	set_popdown_strings (lm_output_channel_combo, empty);
	set_popdown_strings (lm_input_channel_combo, empty);
	lm_measure_button.set_sensitive (false);
	lm_use_button.set_sensitive (false);
}

void
EngineControl::enable_latency_tab ()
{
	vector<string> outputs;
	ARDOUR::AudioEngine::instance()->get_physical_outputs (ARDOUR::DataType::AUDIO, outputs);
	set_popdown_strings (lm_output_channel_combo, outputs);
	lm_output_channel_combo.set_active_text (outputs.front());

	vector<string> inputs;
	ARDOUR::AudioEngine::instance()->get_physical_inputs (ARDOUR::DataType::AUDIO, inputs);
	set_popdown_strings (lm_input_channel_combo, inputs);
	lm_input_channel_combo.set_active_text (inputs.front());

	lm_measure_button.set_sensitive (true);
}

void
EngineControl::setup_midi_tab_for_backend ()
{
	string backend = backend_combo.get_active_text ();

	Gtkmm2ext::container_clear (midi_vbox);

	midi_vbox.set_border_width (12);
	midi_device_table.set_border_width (12);

	if (backend == "JACK") {
		setup_midi_tab_for_jack ();
	}

	midi_vbox.pack_start (midi_device_table, true, true);
	midi_vbox.pack_start (midi_refresh_button, false, false);
	midi_vbox.show_all ();

	midi_refresh_button.signal_clicked().connect (sigc::mem_fun (*this, &EngineControl::refresh_midi_display));
}

void
EngineControl::setup_midi_tab_for_jack ()
{
	midi_vbox.pack_start (aj_button, false, false);
}	

void
EngineControl::refresh_midi_display ()
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	assert (backend);

	vector<string> midi_inputs;
	vector<string> midi_outputs;
	int row  = 0;
	AttachOptions xopt = AttachOptions (FILL|EXPAND);
	Gtk::Label* l;

	Gtkmm2ext::container_clear (midi_device_table);

	backend->get_physical_inputs (ARDOUR::DataType::MIDI, midi_inputs);
	backend->get_physical_outputs (ARDOUR::DataType::MIDI, midi_outputs);

	midi_device_table.set_spacings (6);
	midi_device_table.set_homogeneous (true);
	midi_device_table.resize (midi_inputs.size() + midi_outputs.size() + 3, 1);

	l = manage (new Label);
	l->set_markup (string_compose ("<span size=\"large\" weight=\"bold\">%1</span>", _("MIDI Inputs")));
	midi_device_table.attach (*l, 0, 1, row, row + 1, xopt, AttachOptions (0));
	l->set_alignment (0, 0.5);
	row++;
	l->show ();
	
	for (vector<string>::iterator p = midi_inputs.begin(); p != midi_inputs.end(); ++p) {
		l = manage (new Label ((*p).substr ((*p).find_last_of (':') + 1)));
		l->set_alignment (0, 0.5);
		midi_device_table.attach (*l, 0, 1, row, row + 1, xopt, AttachOptions (0));
		l->show ();
		row++;
	}

	row++; // extra row of spacing

	l = manage (new Label);
	l->set_markup (string_compose ("<span size=\"large\" weight=\"bold\">%1</span>", _("MIDI Outputs")));
	midi_device_table.attach (*l, 0, 1, row, row + 1, xopt, AttachOptions (0));
	l->set_alignment (0, 0.5);
	row++;
	l->show ();

	for (vector<string>::iterator p = midi_outputs.begin(); p != midi_outputs.end(); ++p) {
		l = manage (new Label ((*p).substr ((*p).find_last_of (':') + 1)));
		l->set_alignment (0, 0.5);
		midi_device_table.attach (*l, 0, 1, row, row + 1, xopt, AttachOptions (0));
		l->show ();
		row++;
	}
}

void
EngineControl::backend_changed ()
{
	if (ignore_changes) {
		return;
	}

	string backend_name = backend_combo.get_active_text();
	boost::shared_ptr<ARDOUR::AudioBackend> backend;

	if (!(backend = ARDOUR::AudioEngine::instance()->set_backend (backend_name, "ardour", ""))) {
		/* eh? setting the backend failed... how ? */
		return;
	}

	setup_midi_tab_for_backend ();

	if (backend->requires_driver_selection()) {
		vector<string> drivers = backend->enumerate_drivers();
		driver_combo.set_sensitive (true);
		set_popdown_strings (driver_combo, drivers);
		driver_combo.set_active_text (drivers.front());
		driver_changed ();
	} else {
		driver_combo.set_sensitive (false);
		list_devices ();
	}
	
	maybe_display_saved_state ();
}

bool
EngineControl::print_channel_count (Gtk::SpinButton* sb)
{
	uint32_t cnt = (uint32_t) sb->get_value();
	if (cnt == 0) {
		sb->set_text (_("all available channels"));
	} else {
		char buf[32];
		snprintf (buf, sizeof (buf), "%d", cnt);
		sb->set_text (buf);
	}
	return true;
}

void
EngineControl::list_devices ()
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	assert (backend);

	/* now fill out devices, mark sample rates, buffer sizes insensitive */
	    
	vector<ARDOUR::AudioBackend::DeviceStatus> all_devices = backend->enumerate_devices ();
	
	/* NOTE: Ardour currently does not display the "available" field of the
	 * returned devices.
	 *
	 * Doing so would require a different GUI widget than the combo
	 * box/popdown that we currently use, since it has no way to list
	 * items that are not selectable. Something more like a popup menu,
	 * which could have unselectable items, would be appropriate.
	 */

	vector<string> available_devices;

	for (vector<ARDOUR::AudioBackend::DeviceStatus>::const_iterator i = all_devices.begin(); i != all_devices.end(); ++i) {
		available_devices.push_back (i->name);
	}

	ignore_changes++;
	set_popdown_strings (device_combo, available_devices);
	ignore_changes--;

	if (!available_devices.empty()) {
		sample_rate_combo.set_sensitive (true);
		buffer_size_combo.set_sensitive (true);
		input_latency.set_sensitive (true);
		output_latency.set_sensitive (true);
		input_channels.set_sensitive (true);
		output_channels.set_sensitive (true);
						
		/* changing the text in the combo will trigger device_changed()
		   which should populate the parameter controls
		*/
		
		device_combo.set_active_text (available_devices.front());
	} else {
		sample_rate_combo.set_sensitive (true);
		buffer_size_combo.set_sensitive (true);
		input_latency.set_sensitive (true);
		output_latency.set_sensitive (true);
		input_channels.set_sensitive (true);
		output_channels.set_sensitive (true);
	}
}
	
void
EngineControl::driver_changed ()
{
	if (ignore_changes) {
		return;
	}

	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	assert (backend);

	backend->set_driver (driver_combo.get_active_text());
	list_devices ();

	maybe_display_saved_state ();
}

void
EngineControl::device_changed ()
{
	if (ignore_changes) {
		return;
	}

	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	assert (backend);
	string device_name = device_combo.get_active_text ();
	vector<string> s;

	/* don't allow programmatic change to sample_rate_combo to cause a
	   recursive call to this method.
	*/
	   
	ignore_changes++;

	/* sample rates */
	
	string desired;

	vector<float> sr = backend->available_sample_rates (device_name);
	for (vector<float>::const_iterator x = sr.begin(); x != sr.end(); ++x) {
		char buf[32];
		if (fmod (*x, 1000.0f)) {
			snprintf (buf, sizeof (buf), "%.1f kHz", (*x)/1000.0);
		} else {
			snprintf (buf, sizeof (buf), "%.0f kHz", (*x)/1000.0);
		}
		s.push_back (buf);
		if (*x == _desired_sample_rate) {
			desired = buf;
		}
	}

	if (!s.empty()) {
		set_popdown_strings (sample_rate_combo, s);
	
		if (desired.empty()) {
			sample_rate_combo.set_active_text (s.front());
		} else {
			sample_rate_combo.set_active_text (desired);
		}
	} else {
		/* hmm ... how to tell the user about the fact that we have no
		 * available sample rates.
		 */
	}
		 

	vector<uint32_t> bs = backend->available_buffer_sizes(device_name);
	s.clear ();
	for (vector<uint32_t>::const_iterator x = bs.begin(); x != bs.end(); ++x) {
		char buf[32];
		/* Translators: "samples" is always plural here, so no
		   need for plural+singular forms.
		*/
		snprintf (buf, sizeof (buf), _("%u samples"), *x);
		s.push_back (buf);
	}

	if (!s.empty()) {
		set_popdown_strings (buffer_size_combo, s);
		buffer_size_combo.set_active_text (s.front());
		show_buffer_duration ();
	} else {
		/* hmm ... how to tell the user about the fact that we have no
		 * available buffer sizes.
		 */
	}

	manage_control_app_sensitivity ();

	ignore_changes--;

	/* pick up any saved state for this device */

	maybe_display_saved_state ();

	/* and push it to the backend */

	push_state_to_backend (false);
}	

void 
EngineControl::sample_rate_changed ()
{
	if (ignore_changes) {
		return;
	}

	/* reset the strings for buffer size to show the correct msec value
	   (reflecting the new sample rate).
	*/

	show_buffer_duration ();
	push_state_to_backend (false);
	save_state ();

}

void 
EngineControl::buffer_size_changed ()
{
	if (ignore_changes) {
		return;
	}

	show_buffer_duration ();
	push_state_to_backend (false);
	save_state ();
}

void
EngineControl::show_buffer_duration ()
{

	/* buffer sizes  - convert from just samples to samples + msecs for
	 * the displayed string
	 */

	string bs_text = buffer_size_combo.get_active_text ();
	uint32_t samples = atoi (bs_text); /* will ignore trailing text */
	uint32_t rate = get_rate();

	/* Translators: "msecs" is ALWAYS plural here, so we do not
	   need singular form as well.
	*/
	/* Developers: note the hard-coding of a double buffered model
	   in the (2 * samples) computation of latency. we always start
	   the audiobackend in this configuration.
	*/
	char buf[32];
	snprintf (buf, sizeof (buf), _("(%.1f msecs)"), (2 * samples) / (rate/1000.0));
	buffer_size_duration_label.set_text (buf);
}

void
EngineControl::parameter_changed ()
{
	if (!ignore_changes) {
		save_state ();
	}
}

EngineControl::State*
EngineControl::get_matching_state (const string& backend,
				   const string& driver,
				   const string& device)
{
	for (StateList::iterator i = states.begin(); i != states.end(); ++i) {
		if ((*i).backend == backend &&
		    (*i).driver == driver &&
		    (*i).device == device) {
			return &(*i);
		}
	}
	return 0;
}

EngineControl::State*
EngineControl::get_saved_state_for_currently_displayed_backend_and_device ()
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();

	if (backend) {
		return get_matching_state (backend_combo.get_active_text(),
					   (backend->requires_driver_selection() ? (std::string) driver_combo.get_active_text() : string()),
					   device_combo.get_active_text());
	}


	return get_matching_state (backend_combo.get_active_text(),
				   string(),
				   device_combo.get_active_text());
}

EngineControl::State*
EngineControl::save_state ()
{
	bool existing = true;
	State* state = get_saved_state_for_currently_displayed_backend_and_device ();

	if (!state) {
		existing = false;
		state = new State;
	}
	
	state->backend = backend_combo.get_active_text ();
	state->driver = driver_combo.get_active_text ();
	state->device = device_combo.get_active_text ();
	state->buffer_size = buffer_size_combo.get_active_text ();
	state->sample_rate = sample_rate_combo.get_active_text ();
	state->input_latency = (uint32_t) input_latency.get_value();
	state->output_latency = (uint32_t) output_latency.get_value();
	state->input_channels = (uint32_t) input_channels.get_value();
	state->output_channels = (uint32_t) output_channels.get_value();

	if (!existing) {
		states.push_back (*state);
	}

	return state;
}

void
EngineControl::maybe_display_saved_state ()
{
	State* state = get_saved_state_for_currently_displayed_backend_and_device ();

	if (state) {
		ignore_changes++;
		if (!_desired_sample_rate) {
			sample_rate_combo.set_active_text (state->sample_rate);
		}
		buffer_size_combo.set_active_text (state->buffer_size);
		/* call this explicitly because we're ignoring changes to
		   the controls at this point.
		*/
		show_buffer_duration ();
		input_latency.set_value (state->input_latency);
		output_latency.set_value (state->output_latency);
		ignore_changes--;
	}
}
	
XMLNode&
EngineControl::get_state ()
{
	XMLNode* root = new XMLNode ("AudioMIDISetup");
	std::string path;

	if (!states.empty()) {
		XMLNode* state_nodes = new XMLNode ("EngineStates");
		
		for (StateList::const_iterator i = states.begin(); i != states.end(); ++i) {
			
			XMLNode* node = new XMLNode ("State");
			
			node->add_property ("backend", (*i).backend);
			node->add_property ("driver", (*i).driver);
			node->add_property ("device", (*i).device);
			node->add_property ("sample-rate", (*i).sample_rate);
			node->add_property ("buffer-size", (*i).buffer_size);
			node->add_property ("input-latency", (*i).input_latency);
			node->add_property ("output-latency", (*i).output_latency);
			node->add_property ("input-channels", (*i).input_channels);
			node->add_property ("output-channels", (*i).output_channels);
			node->add_property ("active", (*i).active ? "yes" : "no");
			
			state_nodes->add_child_nocopy (*node);
		}
		
		root->add_child_nocopy (*state_nodes);
	}

	return *root;
}

void
EngineControl::set_state (const XMLNode& root)
{
	XMLNodeList          clist, cclist;
	XMLNodeConstIterator citer, cciter;
	XMLNode* child;
	XMLNode* grandchild;
	XMLProperty* prop = NULL;

	if (root.name() != "AudioMIDISetup") {
		return;
	}

	clist = root.children();

	states.clear ();

	for (citer = clist.begin(); citer != clist.end(); ++citer) {

		child = *citer;
		
		if (child->name() != "EngineStates") {
			continue;
		}

		cclist = child->children();

		for (cciter = cclist.begin(); cciter != cclist.end(); ++cciter) {
			State state;
			
			grandchild = *cciter;

			if (grandchild->name() != "State") {
				continue;
			}
			
			if ((prop = grandchild->property ("backend")) == 0) {
				continue;
			}
			state.backend = prop->value ();
			
			if ((prop = grandchild->property ("driver")) == 0) {
				continue;
			}
			state.driver = prop->value ();
			
			if ((prop = grandchild->property ("device")) == 0) {
				continue;
			}
			state.device = prop->value ();
			
			if ((prop = grandchild->property ("sample-rate")) == 0) {
				continue;
			}
			state.sample_rate = prop->value ();
			
			if ((prop = grandchild->property ("buffer-size")) == 0) {
				continue;
			}
			state.buffer_size = prop->value ();
			
			if ((prop = grandchild->property ("input-latency")) == 0) {
				continue;
			}
			state.input_latency = atoi (prop->value ());
			
			if ((prop = grandchild->property ("output-latency")) == 0) {
				continue;
			}
			state.output_latency = atoi (prop->value ());
			
			if ((prop = grandchild->property ("input-channels")) == 0) {
				continue;
			}
			state.input_channels = atoi (prop->value ());
			
			if ((prop = grandchild->property ("output-channels")) == 0) {
				continue;
			}
			state.output_channels = atoi (prop->value ());

			if ((prop = grandchild->property ("active")) == 0) {
				continue;
			}
			state.active = string_is_affirmative (prop->value ());
			
			states.push_back (state);
		}
	}

	/* now see if there was an active state and switch the setup to it */
	
	for (StateList::const_iterator i = states.begin(); i != states.end(); ++i) {
		if ((*i).active) {
			ignore_changes++;
			backend_combo.set_active_text ((*i).backend);
			driver_combo.set_active_text ((*i).driver);
			device_combo.set_active_text ((*i).device);
			sample_rate_combo.set_active_text ((*i).sample_rate);
			buffer_size_combo.set_active_text ((*i).buffer_size);
			input_latency.set_value ((*i).input_latency);
			output_latency.set_value ((*i).output_latency);
			ignore_changes--;

			push_state_to_backend (false);
			break;
		}
	}
}


int
EngineControl::push_state_to_backend (bool start)
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();

	if (!backend) {
		return 0;
	 }

	/* grab the parameters from the GUI and apply them */

	try {
		if (backend->requires_driver_selection()) {
			if (backend->set_driver (get_driver())) {
				return -1;
			}
		}

		if (backend->set_device_name (get_device_name())) {
			return -1;
		}

		if (backend->set_sample_rate (get_rate())) {
			error << string_compose (_("Cannot set sample rate to %1"), get_rate()) << endmsg;
			return -1;
		}
		if (backend->set_buffer_size (get_buffer_size())) {
			error << string_compose (_("Cannot set buffer size to %1"), get_buffer_size()) << endmsg;
			return -1;
		}
		if (backend->set_input_channels (get_input_channels())) {
			error << string_compose (_("Cannot set input channels to %1"), get_input_channels()) << endmsg;
			return -1;
		}
		if (backend->set_output_channels (get_output_channels())) {
			error << string_compose (_("Cannot set output channels to %1"), get_output_channels()) << endmsg;
			return -1;
		}
		if (backend->set_systemic_input_latency (get_input_latency())) {
			error << string_compose (_("Cannot set input latency to %1"), get_input_latency()) << endmsg;
			return -1;
		}
		if (backend->set_systemic_output_latency (get_output_latency())) {
			error << string_compose (_("Cannot set output latency to %1"), get_output_latency()) << endmsg;
			return -1;
		}

		/* get a pointer to the current state object, creating one if
		 * necessary
		 */

		State* state = get_saved_state_for_currently_displayed_backend_and_device ();

		if (!state) {
			state = save_state ();
			assert (state);
		}

		/* all off */

		for (StateList::iterator i = states.begin(); i != states.end(); ++i) {
			(*i).active = false;
		}

		/* mark this one active (to be used next time the dialog is
		 * shown)
		 */

		state->active = true;
		
		if (start) {
			if (ARDOUR::AudioEngine::instance()->start()) {
				return -1;
			}

			/* schedule a redisplay of MIDI ports */

			Glib::signal_timeout().connect (sigc::bind_return (sigc::mem_fun (*this, &EngineControl::refresh_midi_display), false), 1000);
		}

		manage_control_app_sensitivity ();
		return 0;

	} catch (...) {
		cerr << "exception thrown...\n";
		return -1;
	}
}

uint32_t
EngineControl::get_rate () const
{
	double r = atof (sample_rate_combo.get_active_text ());
	/* the string may have been translated with an abbreviation for
	 * thousands, so use a crude heuristic to fix this.
	 */
	if (r < 1000.0) {
		r *= 1000.0;
	}
	return lrint (r);
}

uint32_t
EngineControl::get_buffer_size () const
{
	string txt = buffer_size_combo.get_active_text ();
	uint32_t samples;

	if (sscanf (txt.c_str(), "%d", &samples) != 1) {
		throw exception ();
	}

	return samples;
}

uint32_t
EngineControl::get_input_channels() const
{
	return (uint32_t) input_channels_adjustment.get_value();
}

uint32_t
EngineControl::get_output_channels() const
{
	return (uint32_t) output_channels_adjustment.get_value();
}

uint32_t
EngineControl::get_input_latency() const
{
	return (uint32_t) input_latency_adjustment.get_value();
}

uint32_t
EngineControl::get_output_latency() const
{
	return (uint32_t) output_latency_adjustment.get_value();
}

string
EngineControl::get_driver () const
{
	return driver_combo.get_active_text ();
}

string
EngineControl::get_device_name () const
{
	return device_combo.get_active_text ();
}

void
EngineControl::control_app_button_clicked ()
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	
	if (!backend) {
		return;
	}
	
	backend->launch_control_app ();
}

void
EngineControl::manage_control_app_sensitivity ()
{
	boost::shared_ptr<ARDOUR::AudioBackend> backend = ARDOUR::AudioEngine::instance()->current_backend();
	
	if (!backend) {
		return;
	}
	
	string appname = backend->control_app_name();

	if (appname.empty()) {
		control_app_button.set_sensitive (false);
	} else {
		control_app_button.set_sensitive (true);
	}
}

void
EngineControl::set_desired_sample_rate (uint32_t sr)
{
	_desired_sample_rate = sr;
	device_changed ();
}

void
EngineControl::on_switch_page (GtkNotebookPage*, guint page_num)
{
	if (page_num == 0) {
		cancel_button->set_sensitive (true);
		ok_button->set_sensitive (true);
		apply_button->set_sensitive (true);
	} else {
		cancel_button->set_sensitive (false);
		ok_button->set_sensitive (false);
		apply_button->set_sensitive (false);
	}

	if (page_num == 1) {
		/* MIDI tab */
		refresh_midi_display ();
	}

	if (page_num == 2) {
		/* latency tab */

		if (!ARDOUR::AudioEngine::instance()->running()) {
			
			PBD::Unwinder<uint32_t> protect_ignore_changes (ignore_changes, ignore_changes + 1);
			
			/* save any existing latency values */
			
			uint32_t il = (uint32_t) input_latency.get_value ();
			uint32_t ol = (uint32_t) input_latency.get_value ();

			/* reset to zero so that our new test instance of JACK
			   will be clean of any existing latency measures.
			*/
			
			input_latency.set_value (0);
			output_latency.set_value (0);
			
			push_state_to_backend (false);

			/* reset control */

			input_latency.set_value (il);
			output_latency.set_value (ol);

		} 

		if (ARDOUR::AudioEngine::instance()->prepare_for_latency_measurement()) {
			disable_latency_tab ();
		}

		enable_latency_tab ();

	} else {
		ARDOUR::AudioEngine::instance()->stop_latency_detection();
	}
}

/* latency measurement */

bool
EngineControl::check_latency_measurement ()
{
        MTDM* mtdm = ARDOUR::AudioEngine::instance()->mtdm ();

        if (mtdm->resolve () < 0) {
		lm_results.set_markup (string_compose ("<span foreground=\"red\">%1</span>", _("No signal detected ")));
                return true;
        }

        if (mtdm->err () > 0.3) {
                mtdm->invert ();
                mtdm->resolve ();
        }

        char buf[128];
	ARDOUR::framecnt_t const sample_rate = ARDOUR::AudioEngine::instance()->sample_rate();

        if (sample_rate == 0) {
                lm_results.set_text (_("Disconnected from audio engine"));
		ARDOUR::AudioEngine::instance()->stop_latency_detection ();
                return false;
        }

	uint32_t frames_total = mtdm->del();
	cerr << "total = " << frames_total << " delay = " << ARDOUR::AudioEngine::instance()->latency_signal_delay() << endl;
	uint32_t extra = frames_total - ARDOUR::AudioEngine::instance()->latency_signal_delay();

        snprintf (buf, sizeof (buf), "%u samples %10.3lf ms", extra, extra * 1000.0f/sample_rate);

        bool solid = true;

        if (mtdm->err () > 0.2) {
                strcat (buf, " ??");
                solid = false;
        }

        if (mtdm->inv ()) {
                strcat (buf, " (Inv)");
                solid = false;
        }

        if (solid) {
                lm_measure_button.set_active (false);
		lm_use_button.set_sensitive (true);
                strcat (buf, " (set)");
		have_lm_results = true;
        }
	
        lm_results.set_text (buf);

        return true;
}

void
EngineControl::start_latency_detection ()
{
	ARDOUR::AudioEngine::instance()->set_latency_input_port (lm_input_channel_combo.get_active_text());
	ARDOUR::AudioEngine::instance()->set_latency_output_port (lm_output_channel_combo.get_active_text());
	ARDOUR::AudioEngine::instance()->start_latency_detection ();
	lm_results.set_text (_("Detecting ..."));
	latency_timeout = Glib::signal_timeout().connect (mem_fun (*this, &EngineControl::check_latency_measurement), 250);
	lm_start_stop_label.set_text (_("Cancel measurement"));
	have_lm_results = false;
	lm_input_channel_combo.set_sensitive (false);
	lm_output_channel_combo.set_sensitive (false);
}

void
EngineControl::end_latency_detection ()
{
	ARDOUR::AudioEngine::instance()->stop_latency_detection ();
	latency_timeout.disconnect ();
	lm_start_stop_label.set_text (_("Measure latency"));
	if (!have_lm_results) {
		lm_results.set_markup ("<i>No measurement results yet</i>");
	}
	lm_input_channel_combo.set_sensitive (true);
	lm_output_channel_combo.set_sensitive (true);
}

void
EngineControl::latency_button_toggled ()
{
        if (lm_measure_button.get_active ()) {
		start_latency_detection ();
	} else {
		end_latency_detection ();
        }
}

void
EngineControl::use_latency_button_clicked ()
{
        MTDM* mtdm = ARDOUR::AudioEngine::instance()->mtdm ();

	if (!mtdm) {
		return;
	}

	uint32_t frames_total = mtdm->del();
	uint32_t extra = frames_total - ARDOUR::AudioEngine::instance()->latency_signal_delay();
	uint32_t one_way = extra/2;

	input_latency_adjustment.set_value (one_way);
	output_latency_adjustment.set_value (one_way);
}

bool
EngineControl::on_delete_event (GdkEventAny* ev)
{
	if (notebook.get_current_page() == 2) {
		/* currently on latency tab - be sure to clean up */
		end_latency_detection ();
	}
	return ArdourDialog::on_delete_event (ev);
}

