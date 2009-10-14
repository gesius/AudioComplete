/*
    Copyright (C) 2009 Paul Davis

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <cmath>
#include <algorithm>

#include "pbd/enumwriter.h"
#include "pbd/convert.h"

#include "ardour/midi_buffer.h"

#include "ardour/delivery.h"
#include "ardour/audio_buffer.h"
#include "ardour/amp.h"
#include "ardour/buffer_set.h"
#include "ardour/configuration.h"
#include "ardour/io.h"
#include "ardour/meter.h"
#include "ardour/mute_master.h"
#include "ardour/panner.h"
#include "ardour/port.h"
#include "ardour/session.h"

#include "i18n.h"

using namespace std;
using namespace PBD;
using namespace ARDOUR;

sigc::signal<void,nframes_t> Delivery::CycleStart;
sigc::signal<int>            Delivery::PannersLegal;
bool                         Delivery::panners_legal = false;

/* deliver to an existing IO object */

Delivery::Delivery (Session& s, boost::shared_ptr<IO> io, boost::shared_ptr<MuteMaster> mm, const string& name, Role r)
	: IOProcessor(s, boost::shared_ptr<IO>(), (role_requires_output_ports (r) ? io : boost::shared_ptr<IO>()), name)
	, _role (r)
	, _output_buffers (new BufferSet())
	, _current_gain (1.0)
	, _output_offset (0)
	, _no_outs_cuz_we_no_monitor (false)
	, _solo_level (0)
	, _solo_isolated (false)
	, _mute_master (mm)
	, no_panner_reset (false)

{
	_panner = boost::shared_ptr<Panner>(new Panner (_name, _session));

	if (_output) {
		_output->changed.connect (mem_fun (*this, &Delivery::output_changed));
	}

	CycleStart.connect (mem_fun (*this, &Delivery::cycle_start));
}

/* deliver to a new IO object */

Delivery::Delivery (Session& s, boost::shared_ptr<MuteMaster> mm, const string& name, Role r)
	: IOProcessor(s, false, (role_requires_output_ports (r) ? true : false), name)
	, _role (r)
	, _output_buffers (new BufferSet())
	, _current_gain (1.0)
	, _output_offset (0)
	, _no_outs_cuz_we_no_monitor (false)
	, _solo_level (0)
	, _solo_isolated (false)
	, _mute_master (mm)
	, no_panner_reset (false)
{
	_panner = boost::shared_ptr<Panner>(new Panner (_name, _session));

	if (_output) {
		_output->changed.connect (mem_fun (*this, &Delivery::output_changed));
	}

	CycleStart.connect (mem_fun (*this, &Delivery::cycle_start));
}

/* deliver to a new IO object, reconstruct from XML */

Delivery::Delivery (Session& s, boost::shared_ptr<MuteMaster> mm, const XMLNode& node)
	: IOProcessor (s, false, true, "reset")
	, _role (Role (0))
	, _output_buffers (new BufferSet())
	, _current_gain (1.0)
	, _output_offset (0)
	, _no_outs_cuz_we_no_monitor (false)
	, _solo_level (0)
	, _solo_isolated (false)
	, _mute_master (mm)
	, no_panner_reset (false)
{
	_panner = boost::shared_ptr<Panner>(new Panner (_name, _session));

	if (set_state (node)) {
		throw failed_constructor ();
	}

	if (_output) {
		_output->changed.connect (mem_fun (*this, &Delivery::output_changed));
	}

	CycleStart.connect (mem_fun (*this, &Delivery::cycle_start));
}

/* deliver to an existing IO object, reconstruct from XML */

Delivery::Delivery (Session& s, boost::shared_ptr<IO> out, boost::shared_ptr<MuteMaster> mm, const XMLNode& node)
	: IOProcessor (s, boost::shared_ptr<IO>(), out, "reset")
	, _role (Role (0))
	, _output_buffers (new BufferSet())
	, _current_gain (1.0)
	, _output_offset (0)
	, _no_outs_cuz_we_no_monitor (false)
	, _solo_level (0)
	, _solo_isolated (false)
	, _mute_master (mm)
	, no_panner_reset (false)
{
	_panner = boost::shared_ptr<Panner>(new Panner (_name, _session));

	if (set_state (node)) {
		throw failed_constructor ();
	}

	if (_output) {
		_output->changed.connect (mem_fun (*this, &Delivery::output_changed));
	}

	CycleStart.connect (mem_fun (*this, &Delivery::cycle_start));
}

std::string
Delivery::display_name () const
{
	switch (_role) {
	case Main:
		return _("main outs");
		break;
	case Listen:
		return _("listen");
		break;
	case Send:
	case Insert:
	default:
		return name();
	}
}

void
Delivery::cycle_start (nframes_t /*nframes*/)
{
	_output_offset = 0;
	_no_outs_cuz_we_no_monitor = false;
}

void
Delivery::increment_output_offset (nframes_t n)
{
	_output_offset += n;
}

bool
Delivery::visible () const
{
	return true;
}

bool
Delivery::can_support_io_configuration (const ChanCount& in, ChanCount& out) const
{
	if (_role == Main) {

		/* the out buffers will be set to point to the port output buffers
		   of our output object.
		*/

		if (_output) {
			if (_output->n_ports() != ChanCount::ZERO) {
				out = _output->n_ports();
				return true;
			} else {
				/* not configured yet - we will passthru */
				out = in;
				return true;
			}
		} else {
			fatal << "programming error: this should never be reached" << endmsg;
			/*NOTREACHED*/
		}


	} else if (_role == Insert) {

		/* the output buffers will be filled with data from the *input* ports
		   of this Insert.
		*/

		if (_input) {
			if (_input->n_ports() != ChanCount::ZERO) {
				out = _input->n_ports();
				return true;
			} else {
				/* not configured yet - we will passthru */
				out = in;
				return true;
			}
		} else {
			fatal << "programming error: this should never be reached" << endmsg;
			/*NOTREACHED*/
		}

	} else {
		fatal << "programming error: this should never be reached" << endmsg;
	}

	return false;
}

bool
Delivery::configure_io (ChanCount in, ChanCount out)
{
	/* check configuration by comparison with our I/O port configuration, if appropriate.
	   see ::can_support_io_configuration() for comments
	*/

	if (_role == Main) {

		if (_output) {
			if (_output->n_ports() != out) {
				if (_output->n_ports() != ChanCount::ZERO) {
					fatal << _name << " programming error: configure_io with nports = " << _output->n_ports() << " called with " << in << " and " << out << " with " << _output->n_ports() << " output ports" << endmsg;
					/*NOTREACHED*/
				} else {
					/* I/O not yet configured */
				}
			}
		}

	} else if (_role == Insert) {

		if (_input) {
			if (_input->n_ports() != in) {
				if (_input->n_ports() != ChanCount::ZERO) {
					fatal << _name << " programming error: configure_io called with " << in << " and " << out << " with " << _input->n_ports() << " input ports" << endmsg;
					/*NOTREACHED*/
				} else {
					/* I/O not yet configured */
				}
			}
		}
	}

	if (!Processor::configure_io (in, out)) {
		return false;
	}

	reset_panner ();

	return true;
}

void
Delivery::run (BufferSet& bufs, sframes_t start_frame, sframes_t end_frame, nframes_t nframes)
{
	assert (_output);

	PortSet& ports (_output->ports());
	gain_t tgain;

	if (_output->n_ports ().get (_output->default_type()) == 0) {
		goto out;
	}

	if (!_active && !_pending_active) {
		_output->silence (nframes);
		goto out;
	}

	/* this setup is not just for our purposes, but for anything that comes after us in the
	   processing pathway that wants to use this->output_buffers() for some reason.
	*/

	output_buffers().attach_buffers (ports, nframes, _output_offset);

	// this Delivery processor is not a derived type, and thus we assume
	// we really can modify the buffers passed in (it is almost certainly
	// the main output stage of a Route). Contrast with Send::run()
	// which cannot do this.

	tgain = target_gain ();

	if (tgain != _current_gain) {

		/* target gain has changed */

		Amp::apply_gain (bufs, nframes, _current_gain, tgain);
		_current_gain = tgain;

	} else if (tgain == 0.0) {

		/* we were quiet last time, and we're still supposed to be quiet.
		   Silence the outputs, and make sure the buffers are quiet too,
		*/

		_output->silence (nframes);
		Amp::apply_simple_gain (bufs, nframes, 0.0);
		goto out;

	} else if (tgain != 1.0) {

		/* target gain has not changed, but is not unity */
		Amp::apply_simple_gain (bufs, nframes, tgain);
	}

	if (_panner && _panner->npanners() && !_panner->bypassed()) {

		// Use the panner to distribute audio to output port buffers

		_panner->run (bufs, output_buffers(), start_frame, end_frame, nframes);

	} else {
		// Do a 1:1 copy of data to output ports

		if (bufs.count().n_audio() > 0 && ports.count().n_audio () > 0) {
			_output->copy_to_outputs (bufs, DataType::AUDIO, nframes, _output_offset);
		}

		if (bufs.count().n_midi() > 0 && ports.count().n_midi () > 0) {
			_output->copy_to_outputs (bufs, DataType::MIDI, nframes, _output_offset);
		}
	}

  out:
	_active = _pending_active;
}

XMLNode&
Delivery::state (bool full_state)
{
	XMLNode& node (IOProcessor::state (full_state));

	if (_role & Main) {
		node.add_property("type", "main-outs");
	} else if (_role & Listen) {
		node.add_property("type", "listen");
	} else {
		node.add_property("type", "delivery");
	}

	node.add_property("role", enum_2_string(_role));
	node.add_child_nocopy (_panner->state (full_state));

	return node;
}

int
Delivery::set_state (const XMLNode& node)
{
	const XMLProperty* prop;

	if (IOProcessor::set_state (node)) {
		return -1;
	}

	if ((prop = node.property ("role")) != 0) {
		_role = Role (string_2_enum (prop->value(), _role));
		// std::cerr << this << ' ' << _name << " set role to " << enum_2_string (_role) << std::endl;
	} else {
		// std::cerr << this << ' ' << _name << " NO ROLE INFO\n";
	}

	XMLNode* pan_node = node.child (X_("Panner"));

	if (pan_node) {
		_panner->set_state (*pan_node);
	}

	reset_panner ();

	return 0;
}

void
Delivery::reset_panner ()
{
	if (panners_legal) {
		if (!no_panner_reset) {

			uint32_t ntargets;

			if (_output) {
				ntargets = _output->n_ports().n_audio();
			} else {
				ntargets = _configured_output.n_audio();
			}

			_panner->reset (ntargets, pans_required());
		}
	} else {
		panner_legal_c.disconnect ();
		panner_legal_c = PannersLegal.connect (mem_fun (*this, &Delivery::panners_became_legal));
	}
}

int
Delivery::panners_became_legal ()
{
	uint32_t ntargets;

	if (_output) {
		ntargets = _output->n_ports().n_audio();
	} else {
		ntargets = _configured_output.n_audio();
	}

	_panner->reset (ntargets, pans_required());
	_panner->load (); // automation
	panner_legal_c.disconnect ();
	return 0;
}

void
Delivery::defer_pan_reset ()
{
	no_panner_reset = true;
}

void
Delivery::allow_pan_reset ()
{
	no_panner_reset = false;
	reset_panner ();
}


int
Delivery::disable_panners (void)
{
	panners_legal = false;
	return 0;
}

int
Delivery::reset_panners ()
{
	panners_legal = true;
	return PannersLegal ();
}


void
Delivery::start_pan_touch (uint32_t which)
{
	if (which < _panner->npanners()) {
		_panner->pan_control(which)->start_touch();
	}
}

void
Delivery::end_pan_touch (uint32_t which)
{
	if (which < _panner->npanners()) {
		_panner->pan_control(which)->stop_touch();
	}

}

void
Delivery::transport_stopped (sframes_t frame)
{
	_panner->transport_stopped (frame);
}

void
Delivery::flush (nframes_t nframes, nframes64_t time)
{
	/* io_lock, not taken: function must be called from Session::process() calltree */

	PortSet& ports (_output->ports());

	for (PortSet::iterator i = ports.begin(); i != ports.end(); ++i) {
		(*i).flush_buffers (nframes, time, _output_offset);
	}
}

void
Delivery::transport_stopped ()
{
	/* turn off any notes that are on */

	PortSet& ports (_output->ports());

	for (PortSet::iterator i = ports.begin(); i != ports.end(); ++i) {
		(*i).transport_stopped ();
	}
}

gain_t
Delivery::target_gain ()
{
	/* if we've been requested to deactivate, our target gain is zero */

	if (!_pending_active) {
		return 0.0;
	}

	/* if we've been told not to output because its a monitoring situation and
	   we're not monitoring, then be quiet.
	*/

	if (_no_outs_cuz_we_no_monitor) {
		return 0.0;
	}

	gain_t desired_gain;

	if (_solo_level) {
		desired_gain = 1.0;
	} else {

		MuteMaster::MutePoint mp;

		switch (_role) {
		case Main:
			mp = MuteMaster::Main;
			break;
		case Listen:
			mp = MuteMaster::Listen;
			break;
		case Send:
		case Insert:
		case Aux:
			/* XXX FIX ME this is wrong, we need per-delivery muting */
			mp = MuteMaster::PreFader;
			break;
		}

		if (_solo_isolated) {

			/* ... but we are isolated from all that nonsense */

			desired_gain = _mute_master->mute_gain_at (mp);

		} else if (_session.soloing()) {

			desired_gain = min (Config->get_solo_mute_gain(), _mute_master->mute_gain_at (mp));

		} else {
			desired_gain = _mute_master->mute_gain_at (mp);
		}
	}

	return desired_gain;
}

void
Delivery::no_outs_cuz_we_no_monitor (bool yn)
{
	_no_outs_cuz_we_no_monitor = yn;
}

bool
Delivery::set_name (const std::string& name)
{
	bool ret = IOProcessor::set_name (name);

	if (ret) {
		ret = _panner->set_name (name);
	}

	return ret;
}

void
Delivery::output_changed (IOChange change, void* /*src*/)
{
	if (change & ARDOUR::ConfigurationChanged) {
		reset_panner ();
	}
}

