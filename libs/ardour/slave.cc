/*
    Copyright (C) 2002 Paul Davis

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

#include "ardour/audioengine.h"
#include "ardour/session.h"
#include "ardour/slave.h"

namespace ARDOUR {

TempoMap&
SlaveSessionProxy::tempo_map() const
{
	return session.tempo_map();
}

nframes_t
SlaveSessionProxy::frame_rate() const
{
	return session.frame_rate();
}

framepos_t
SlaveSessionProxy::audible_frame() const
{
	return session.audible_frame();
}

framepos_t
SlaveSessionProxy::transport_frame() const
{
	return session.transport_frame();
}

nframes_t
SlaveSessionProxy::frames_since_cycle_start() const
{
	return session.engine().frames_since_cycle_start();
}

framepos_t
SlaveSessionProxy::frame_time() const
{
	return session.engine().frame_time();
}

void
SlaveSessionProxy::request_locate(framepos_t frame, bool with_roll)
{
	session.request_locate(frame, with_roll);
}

void
SlaveSessionProxy::request_transport_speed(double speed)
{
	session.request_transport_speed(speed);
}

} // namespace ARDOUR
