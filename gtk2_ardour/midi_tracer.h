#ifndef __ardour_gtk_midi_tracer_h__
#define __ardour_gtk_midi_tracer_h__

#include <gtkmm/textview.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/box.h>

#include "pbd/signals.h"
#include "pbd/ringbuffer.h"
#include "pbd/pool.h"
#include "midi++/types.h"
#include "ardour_window.h"

namespace MIDI {
	class Parser;
}

class MidiTracer : public ArdourWindow
{
  public:
	MidiTracer ();
	~MidiTracer();

  private:
	MIDI::Parser* parser;
	Gtk::TextView text;
	Gtk::ScrolledWindow scroller;
	Gtk::Adjustment line_count_adjustment;
	Gtk::SpinButton line_count_spinner;
	Gtk::Label line_count_label;
	Gtk::HBox line_count_box;
	struct timeval _last_receipt;
	
	bool autoscroll;
	bool show_hex;
	bool collect;
	bool show_delta_time;

	/** Incremented when an update is requested, decremented when one is handled; hence
	 *  equal to 0 when an update is not queued.  May temporarily be negative if a
	 *  update is handled before it was noted that it had just been queued.
	 */
	volatile gint _update_queued;

	RingBuffer<char *> fifo;
	Pool buffer_pool;
	static const size_t buffer_size = 256;

	void tracer (MIDI::Parser&, MIDI::byte*, size_t);
	void update ();

	Gtk::CheckButton autoscroll_button;
	Gtk::CheckButton base_button;
	Gtk::CheckButton collect_button;
	Gtk::CheckButton delta_time_button;
	Gtk::ComboBoxText _port_combo;

	void base_toggle ();
	void autoscroll_toggle ();
	void collect_toggle ();
	void delta_toggle ();

	void port_changed ();
	void ports_changed ();
	void disconnect ();
	PBD::ScopedConnection _parser_connection;
	PBD::ScopedConnection _manager_connection;
};

#endif /* __ardour_gtk_midi_tracer_h__ */
