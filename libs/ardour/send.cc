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

#include <iostream>
#include <algorithm>

#include "pbd/xml++.h"

#include "ardour/amp.h"
#include "ardour/send.h"
#include "ardour/session.h"
#include "ardour/port.h"
#include "ardour/audio_port.h"
#include "ardour/buffer_set.h"
#include "ardour/meter.h"
#include "ardour/panner.h"
#include "ardour/io.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace std;

Send::Send (Session& s, boost::shared_ptr<MuteMaster> mm, Role r)
	: Delivery (s, mm, string_compose (_("send %1"), (_bitslot = s.next_send_id()) + 1), r)
	, _metering (false)
{
	_amp.reset (new Amp (_session, _mute_master));
	_meter.reset (new PeakMeter (_session));

	ProcessorCreated (this); /* EMIT SIGNAL */
}

Send::Send (Session& s, boost::shared_ptr<MuteMaster> mm, const XMLNode& node, int version, Role r)
        : Delivery (s, mm, "send", r)
	, _metering (false)
{
	_amp.reset (new Amp (_session, _mute_master));
	_meter.reset (new PeakMeter (_session));

	if (set_state (node, version)) {
		throw failed_constructor();
	}

	ProcessorCreated (this); /* EMIT SIGNAL */
}

Send::~Send ()
{
	GoingAway ();
}

void
Send::activate ()
{
	_amp->activate ();
	_meter->activate ();

	Processor::activate ();
}

void
Send::deactivate ()
{
	_amp->deactivate ();
	_meter->deactivate ();
	_meter->reset ();
	
	Processor::deactivate ();
}

void
Send::run (BufferSet& bufs, sframes_t start_frame, sframes_t end_frame, nframes_t nframes)
{
	if (_output->n_ports() == ChanCount::ZERO) {
		_meter->reset ();
		_active = _pending_active;
		return;
	}

	if (!_active && !_pending_active) {
		_meter->reset ();
		_output->silence (nframes);
		_active = _pending_active;
		return;
	}

	// we have to copy the input, because deliver_output() may alter the buffers
	// in-place, which a send must never do.

	BufferSet& sendbufs = _session.get_mix_buffers (bufs.count());
	sendbufs.read_from (bufs, nframes);
	assert(sendbufs.count() == bufs.count());

	/* gain control */

	// Can't automate gain for sends or returns yet because we need different buffers
	// so that we don't overwrite the main automation data for the route amp
	// _amp->setup_gain_automation (start_frame, end_frame, nframes);
	_amp->run (sendbufs, start_frame, end_frame, nframes);

	/* deliver to outputs */

	Delivery::run (sendbufs, start_frame, end_frame, nframes);

	/* consider metering */

	if (_metering) {
		if (_amp->gain_control()->get_value() == 0) {
			_meter->reset();
		} else {
			_meter->run (*_output_buffers, start_frame, end_frame, nframes);
		}
	}

	/* _active was set to _pending_active by Delivery::run() */
}

XMLNode&
Send::get_state(void)
{
	return state (true);
}

XMLNode&
Send::state(bool full)
{
	XMLNode& node = Delivery::state(full);
	char buf[32];

	node.add_property ("type", "send");
	snprintf (buf, sizeof (buf), "%" PRIu32, _bitslot);
	node.add_property ("bitslot", buf);

	return node;
}

int
Send::set_state (const XMLNode& node, int version)
{
	XMLNodeList nlist = node.children();
	XMLNodeIterator niter;
	const XMLProperty* prop;

	if ((prop = node.property ("bitslot")) == 0) {
		_bitslot = _session.next_send_id();
	} else {
		sscanf (prop->value().c_str(), "%" PRIu32, &_bitslot);
		cerr << this << " scanned " << prop->value() << " to get " << _bitslot << endl;
		_session.mark_send_id (_bitslot);
	}

	const XMLNode* insert_node = &node;

	/* XXX need to load automation state & data for amp */

	Delivery::set_state (*insert_node, version);

	return 0;
}

bool
Send::can_support_io_configuration (const ChanCount& in, ChanCount& out) const
{
	/* sends have no impact at all on the channel configuration of the
	   streams passing through the route. so, out == in.
	*/

	out = in;
	return true;
}

bool
Send::configure_io (ChanCount in, ChanCount out)
{
	if (!_amp->configure_io (in, out) || !_meter->configure_io (in, out)) {
		return false;
	}

	if (!Processor::configure_io (in, out)) {
		return false;
	}

	reset_panner ();

	return true;
}

/** Set up the XML description of a send so that its name is unique.
 *  @param state XML send state.
 *  @param session Session.
 */
void
Send::make_unique (XMLNode &state, Session &session)
{
	uint32_t const bitslot = session.next_send_id() + 1;

	char buf[32];
	snprintf (buf, sizeof (buf), "%" PRIu32, bitslot);
	state.property("bitslot")->set_value (buf);

	string const name = string_compose (_("send %1"), bitslot);

	state.property("name")->set_value (name);

	XMLNode* io = state.child ("IO");

	if (io) {
		io->property("name")->set_value (name);
	}
}

bool
Send::set_name (const string& new_name)
{
	string unique_name;

	if (_role == Delivery::Send) {
		char buf[32];

		/* rip any existing numeric part of the name, and append the bitslot
		 */

		string::size_type last_letter = new_name.find_last_not_of ("0123456789");

		if (last_letter != string::npos) {
			unique_name = new_name.substr (0, last_letter + 1);
		} else {
			unique_name = new_name;
		}

		snprintf (buf, sizeof (buf), "%u", (_bitslot + 1));
		unique_name += buf;

	} else {
		unique_name = new_name;
	}

	return Delivery::set_name (unique_name);
}

bool
Send::display_to_user () const
{
	/* we ignore Deliver::_display_to_user */

	if (_role == Listen) {
		return false;
	}

	return true;
}
