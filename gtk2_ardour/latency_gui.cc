#include <inttypes.h>

#include <iomanip>
#include "ardour/latent.h"
#include "pbd/convert.h"
#include <gtkmm2ext/utils.h>

#include "latency_gui.h"

#include "i18n.h"

using namespace PBD;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace ARDOUR;


static const gchar *_unit_strings[] = {
	N_("sample"),
	N_("msec"),
	N_("period"),
	0
};

std::vector<std::string> LatencyGUI::unit_strings;

std::string
LatencyBarController::get_label (int&)
{
	double const nframes = _latency_gui->adjustment.get_value();
	std::stringstream s;

	if (nframes < (_latency_gui->sample_rate / 1000.0)) {
		s << ((framepos_t) rint (nframes)) << " samples";
	} else {
		s << std::fixed << std::setprecision (2) << (nframes / (_latency_gui->sample_rate / 1000.0)) << " msecs";
	}

	return s.str ();
}

LatencyGUI::LatencyGUI (Latent& l, framepos_t sr, framepos_t psz)
	: _latent (l),
	  initial_value (_latent.signal_latency()),
	  sample_rate (sr),
	  period_size (psz),
	  ignored (new PBD::IgnorableControllable()),
	  /* max 1 second, step by frames, page by msecs */
	  adjustment (initial_value, 0.0, sample_rate, 1.0, sample_rate / 1000.0f),
	  bc (adjustment, this),
	  reset_button (_("Reset"))
{
	Widget* w;

	if (!unit_strings.empty()) {
		unit_strings = I18N (_unit_strings);
	}

	set_popdown_strings (units_combo, unit_strings);
	units_combo.set_active_text (unit_strings.front());

	w = manage (new Image (Stock::ADD, ICON_SIZE_BUTTON));
	w->show ();
	plus_button.add (*w);
	w = manage (new Image (Stock::REMOVE, ICON_SIZE_BUTTON));
	w->show ();
	minus_button.add (*w);

	hbox1.pack_start (bc, true, true);

	hbox2.set_homogeneous (false);
	hbox2.set_spacing (12);
	hbox2.pack_start (reset_button);
	hbox2.pack_start (minus_button);
	hbox2.pack_start (plus_button);
	hbox2.pack_start (units_combo, true, true);

	minus_button.signal_clicked().connect (sigc::bind (sigc::mem_fun (*this, &LatencyGUI::change_latency_from_button), -1));
	plus_button.signal_clicked().connect (sigc::bind (sigc::mem_fun (*this, &LatencyGUI::change_latency_from_button), 1));
	reset_button.signal_clicked().connect (sigc::mem_fun (*this, &LatencyGUI::reset));

	adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &LatencyGUI::finish));

	bc.set_size_request (-1, 25);
	bc.set_style (BarController::LeftToRight);
	bc.set_use_parent (true);
	bc.set_name (X_("PluginSlider"));

	set_spacing (12);
	pack_start (hbox1, true, true);
	pack_start (hbox2, true, true);
}

void
LatencyGUI::finish ()
{
	framepos_t new_value = (framepos_t) adjustment.get_value();
	if (new_value != initial_value) {
		_latent.set_user_latency (new_value);
	}
}

void
LatencyGUI::reset ()
{
	_latent.set_user_latency (0);
	adjustment.set_value (initial_value);
}

void
LatencyGUI::refresh ()
{
	initial_value = _latent.signal_latency();
	adjustment.set_value (initial_value);
}

void
LatencyGUI::change_latency_from_button (int dir)
{
	std::string unitstr = units_combo.get_active_text();
	double shift = 0.0;

	if (unitstr == unit_strings[0]) {
		shift = 1;
	} else if (unitstr == unit_strings[1]) {
		shift = (sample_rate / 1000.0);
	} else if (unitstr == unit_strings[2]) {
		shift = period_size;
	} else {
		fatal << string_compose (_("programming error: %1 (%2)"), X_("illegal string in latency GUI units combo"), unitstr)
		      << endmsg;
		/*NOTREACHED*/
	}

	if (dir > 0) {
		adjustment.set_value (adjustment.get_value() + shift);
	} else {
		adjustment.set_value (adjustment.get_value() - shift);
	}
}

LatencyDialog::LatencyDialog (const std::string& title, Latent& l, framepos_t sr, framepos_t psz)
	: ArdourDialog (title, false, true),
	  lwidget (l, sr, psz)
{

	get_vbox()->pack_start (lwidget);
	add_button (Stock::CANCEL, RESPONSE_CANCEL);
	add_button (Stock::APPLY, RESPONSE_REJECT);
	add_button (Stock::OK, RESPONSE_ACCEPT);

	show_all ();

	while (true) {
		int ret = run ();

		switch (ret) {
		case RESPONSE_ACCEPT:
			return;
			break;

		case RESPONSE_REJECT:
			lwidget.finish ();
			break;
		default:
			return;
		}
	}
}


