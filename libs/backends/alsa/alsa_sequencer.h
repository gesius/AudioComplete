/*
 * Copyright (C) 2014 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __libbackend_alsa_sequencer_h__
#define __libbackend_alsa_sequencer_h__

#include <stdint.h>
#include <poll.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "pbd/ringbuffer.h"
#include "ardour/types.h"

namespace ARDOUR {

class AlsaSeqMidiIO {
public:
	AlsaSeqMidiIO (const char *port_name, const bool input);
	virtual ~AlsaSeqMidiIO ();

	int state (void) const { return _state; }
	int start ();
	int stop ();

	void setup_timing (const size_t samples_per_period, const float samplerate);
	void sync_time(uint64_t);

	virtual void* main_process_thread () = 0;

protected:
	pthread_t _main_thread;
	pthread_mutex_t _notify_mutex;
	pthread_cond_t _notify_ready;

	int  _state;
	bool  _running;

	snd_seq_t *_seq;
	//snd_seq_addr_t _port;
	int _port;

	int _npfds;
	struct pollfd *_pfds;

	double _sample_length_us;
	double _period_length_us;
	size_t _samples_per_period;
	uint64_t _clock_monotonic;

	struct MidiEventHeader {
		uint64_t time;
		size_t size;
		MidiEventHeader(const uint64_t t, const size_t s)
			: time(t)
			, size(s) {}
	};

	RingBuffer<uint8_t>* _rb;

private:
	void init (const char *device_name, const bool input);
};

class AlsaSeqMidiOut : public AlsaSeqMidiIO
{
public:
	AlsaSeqMidiOut (const char *port_name);

	void* main_process_thread ();
	int send_event (const pframes_t, const uint8_t *, const size_t);
};

class AlsaSeqMidiIn : public AlsaSeqMidiIO
{
public:
	AlsaSeqMidiIn (const char *port_name);

	void* main_process_thread ();
	size_t recv_event (pframes_t &, uint8_t *, size_t &);

private:
	int queue_event (const uint64_t, const uint8_t *, const size_t);
};

} // namespace

#endif
