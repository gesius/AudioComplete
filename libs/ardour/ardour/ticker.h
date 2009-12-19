/*
    Copyright (C) 2008 Hans Baier

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

    $Id$
*/


#include "midi++/jack.h"
#include "pbd/signals.h"

#include "ardour/types.h"
#include "ardour/session_handle.h"


#ifndef TICKER_H_
#define TICKER_H_

namespace ARDOUR
{

class Session;

class Ticker : public SessionHandlePtr
{
public:
	Ticker() {};
	virtual ~Ticker() {}

	virtual void tick(
		const nframes_t& transport_frames,
		const BBT_Time& transport_bbt,
		const Timecode::Time& transport_timecode) = 0;

	void set_session (Session* s);
};

class MidiClockTicker : public Ticker
{
	/// Singleton
private:
	MidiClockTicker() : _midi_port(0), _ppqn(24), _last_tick(0.0) {};
	MidiClockTicker( const MidiClockTicker& );
	MidiClockTicker& operator= (const MidiClockTicker&);

public:
	virtual ~MidiClockTicker() {};

	static MidiClockTicker& instance() {
		static MidiClockTicker _instance;
		return _instance;
	}

	void tick(
		const nframes_t& transport_frames,
		const BBT_Time& transport_bbt,
		const Timecode::Time& transport_timecode);

	void set_session (Session* s);
	void session_going_away();

	/// slot for the signal session::MIDIClock_PortChanged
	void update_midi_clock_port();

	/// slot for the signal session::TransportStateChange
	void transport_state_changed();

	/// slot for the signal session::PositionChanged
	void position_changed(nframes_t position);

	/// slot for the signal session::TransportLooped
	void transport_looped();

	/// pulses per quarter note (default 24)
	void set_ppqn(int ppqn) { _ppqn = ppqn; }

private:
	MIDI::Port*  _midi_port;
	int          _ppqn;
	double       _last_tick;

	double one_ppqn_in_frames(nframes_t transport_position);

	void send_midi_clock_event(nframes_t offset);
	void send_start_event(nframes_t offset);
	void send_continue_event(nframes_t offset);
	void send_stop_event(nframes_t offset);
};

}

#endif /* TICKER_H_ */
