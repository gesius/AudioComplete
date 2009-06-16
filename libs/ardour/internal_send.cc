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

#include <iostream>

#include "pbd/error.h"
#include "pbd/failed_constructor.h"

#include "ardour/amp.h"
#include "ardour/internal_send.h"
#include "ardour/meter.h"
#include "ardour/route.h"
#include "ardour/session.h"

#include "i18n.h"

using namespace PBD;
using namespace ARDOUR;

InternalSend::InternalSend (Session& s, boost::shared_ptr<MuteMaster> mm, boost::shared_ptr<Route> sendto)
	: Send (s, mm, true)
	, _send_to (sendto)
{
	if ((target = _send_to->get_return_buffer ()) == 0) {
		throw failed_constructor();
	}

	_send_to->GoingAway.connect (mem_fun (*this, &InternalSend::send_to_going_away));
}

InternalSend::InternalSend (Session& s, boost::shared_ptr<MuteMaster> mm, const XMLNode& node)
	: Send (s, mm, node, true)
{
	set_state (node);
}

InternalSend::~InternalSend ()
{
	if (_send_to) {
		_send_to->release_return_buffer ();
	}

	connect_c.disconnect ();
}

void
InternalSend::send_to_going_away ()
{
	target = 0;
	_send_to.reset ();
	_send_to_id = "0";
}

void
InternalSend::run (BufferSet& bufs, sframes_t start_frame, sframes_t end_frame, nframes_t nframes)
{
	if (!_active || !target || !_send_to) {
		_meter->reset ();
		return;
	}

	// we have to copy the input, because we may alter the buffers with the amp
	// in-place, which a send must never do.
	
	BufferSet& sendbufs = _session.get_mix_buffers (bufs.count());
	sendbufs.read_from (bufs, nframes);
	assert(sendbufs.count() == bufs.count());

	/* gain control */

	// Can't automate gain for sends or returns yet because we need different buffers
	// so that we don't overwrite the main automation data for the route amp
	// _amp->setup_gain_automation (start_frame, end_frame, nframes);
	_amp->run (sendbufs, start_frame, end_frame, nframes);

	/* consider metering */
	
	if (_metering) {
		if (_amp->gain_control()->get_value() == 0) {
			_meter->reset();
		} else {
			_meter->run (sendbufs, start_frame, end_frame, nframes);
		}
	}

	/* deliver to target */

	target->merge_from (sendbufs, nframes);
}

bool
InternalSend::feeds (boost::shared_ptr<Route> other) const
{
	return _send_to == other;
}

XMLNode&
InternalSend::state (bool full)
{
	XMLNode& node (Send::state (full));

	/* this replaces any existing property */

	node.add_property ("type", "intsend");

	if (_send_to) {
		node.add_property ("target", _send_to->id().to_s());
	}
	
	return node;
}

XMLNode&
InternalSend::get_state()
{
	return state (true);
}

int
InternalSend::set_state (const XMLNode& node)
{
	const XMLProperty* prop;

	if ((prop = node.property ("target")) != 0) {

		_send_to_id = prop->value();

		/* if we're loading a session, the target route may not have been
		   create yet. make sure we defer till we are sure that it should
		   exist.
		*/

		if (!IO::connecting_legal) {
			connect_c = IO::ConnectingLegal.connect (mem_fun (*this, &InternalSend::connect_when_legal));
			std::cerr << "connect later!\n";
		} else {
			std::cerr << "connect NOW!\n";
			connect_when_legal ();
		}
	}
	
	return 0;
}

int
InternalSend::connect_when_legal ()
{
	std::cerr << "IOP/send connecting now that its legal\n";
	
	connect_c.disconnect ();

	if (_send_to_id == "0") {
		/* it vanished before we could connect */
		return 0;
	}

	if ((_send_to = _session.route_by_id (_send_to_id)) == 0) {
		error << X_("cannot find route to connect to") << endmsg;
		std::cerr << "cannot find route with ID " << _send_to_id << std::endl;
	} else {
		std::cerr << "got target send as " << _send_to << std::endl;
	}
	
	if ((target = _send_to->get_return_buffer ()) == 0) {
		error << X_("target for internal send has no return buffer") << endmsg;
	}

	return 0;
}

bool 
InternalSend::can_support_io_configuration (const ChanCount& in, ChanCount& out) const
{
	out = in;
	return true;
}
