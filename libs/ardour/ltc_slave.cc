/*
    Copyright (C) 2012 Paul Davis
    Witten by 2012 Robin Gareus <robin@gareus.org>

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
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>

#include "pbd/error.h"

#include "ardour/debug.h"
#include "ardour/slave.h"
#include "ardour/session.h"
#include "ardour/audioengine.h"
#include "ardour/audio_port.h"

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace MIDI;
using namespace PBD;
using namespace Timecode;

LTC_Slave::LTC_Slave (Session& s)
	: session (s)
{
	frames_per_ltc_frame = 1920.0; // samplerate / framerate
	ltc_transport_pos = 0;
	ltc_speed = 1.0;
	last_timestamp = 0;

	decoder = ltc_decoder_create((int) frames_per_ltc_frame, 128 /*queue size*/);
}

LTC_Slave::~LTC_Slave()
{
	if (did_reset_tc_format) {
		session.config.set_timecode_format (saved_tc_format);
	}

	ltc_decoder_free(decoder);
}

bool
LTC_Slave::give_slave_full_control_over_transport_speed() const
{
	return true; // DLL align to engine transport
	// return false; // for Session-level computed varispeed
}

ARDOUR::framecnt_t
LTC_Slave::resolution () const
{
	return (framecnt_t) (frames_per_ltc_frame);
}

ARDOUR::framecnt_t
LTC_Slave::seekahead_distance () const
{
	return (framecnt_t) (frames_per_ltc_frame * 2);
}

bool
LTC_Slave::locked () const
{
	return true;
}

bool
LTC_Slave::ok() const
{
	return true;
}

int
LTC_Slave::parse_ltc(const jack_nframes_t nframes, const jack_default_audio_sample_t * const in, const framecnt_t posinfo)
{
	jack_nframes_t i;
	unsigned char sound[8192];
	if (nframes > 8192) return 1;

	for (i = 0; i < nframes; i++) {
		const int snd=(int)rint((127.0*in[i])+128.0);
		sound[i] = (unsigned char) (snd&0xff);
	}
	ltc_decoder_write(decoder, sound, nframes, posinfo);
	return 0;
}

bool
LTC_Slave::process_ltc(framepos_t now, framecnt_t nframes)
{
	Time timecode;
	bool have_frame = false;

	framepos_t sess_pos = session.transport_frame(); // corresponds to now
	//sess_pos -= session.engine().frames_since_cycle_start();

	DEBUG_TRACE (DEBUG::LTC, string_compose ("LTC Process eng-tme: %1 eng-pos: %2\n", now, sess_pos));

	LTCFrameExt frame;
	while (ltc_decoder_read(decoder,&frame)) {
		SMPTETimecode stime;
		ltc_frame_to_time(&stime, &frame.ltc, 0);
#if 1
		fprintf(stdout, "LTC %02d:%02d:%02d%c%02d | %8lld %8lld%s\n",
			stime.hours,
			stime.mins,
			stime.secs,
			(frame.ltc.dfbit) ? '.' : ':',
			stime.frame,
			frame.off_start,
			frame.off_end,
			frame.reverse ? " R" : "  "
			);
#endif

		timecode.negative  = false;
		timecode.subframes  = 0;
		timecode.drop  = (frame.ltc.dfbit)? true : false;
		timecode.rate  = 25.0; // XXX

		/* when a full LTC frame is decoded, the timecode the LTC frame
		 * is referring has just passed.
		 * So we send the _next_ timecode which
		 * is expected to start at the end of the current frame
		 */
		int fps_i = ceil(timecode.rate);
		if (!frame.reverse) {
			ltc_frame_increment(&frame.ltc, fps_i , 0);
			ltc_frame_to_time(&stime, &frame.ltc, 0);
		} else {
			ltc_frame_decrement(&frame.ltc, fps_i , 0);
			int off = frame.off_end - frame.off_start;
			frame.off_start += off;
			frame.off_end += off;
		}

		timecode.hours   = stime.hours;
		timecode.minutes = stime.mins;
		timecode.seconds = stime.secs;
		timecode.frames  = stime.frame;

		framepos_t ltc_frame;
		session.timecode_to_sample (timecode, ltc_frame, true, false);

		double poff = (frame.off_end - now);

		ltc_transport_pos = ltc_frame - poff;
		frames_per_ltc_frame = (double(session.frame_rate()) / timecode.rate);
		//frames_per_ltc_frame = frame.off_end - frame.off_start; // the first one is off.

		DEBUG_TRACE (DEBUG::LTC, string_compose ("LTC frame: %1 poff: %2 pos :%3\n", ltc_frame, poff, ltc_transport_pos));

		if (last_timestamp == 0 || ((now - last_timestamp) > 4 * frames_per_ltc_frame) ) {
			init_ltc_dll(ltc_frame, frames_per_ltc_frame);
			ltc_speed = 1.0; // XXX
		} else {

			double e = (double(ltc_frame) - poff - double(sess_pos));
			// update DLL
			t0 = t1;
			t1 += b * e + e2;
			e2 += c * e;

			ltc_speed = (t1 - t0) / frames_per_ltc_frame;
			DEBUG_TRACE (DEBUG::LTC, string_compose ("LTC DLL t0:%1 t1:%2 err:%3 spd:%4 ddt:%5\n", t0, t1, e, ltc_speed, e2 - frames_per_ltc_frame));

		}
		last_timestamp = now;
		last_ltc_frame = ltc_frame;
		have_frame = true;
	}
	return have_frame;
}

void
LTC_Slave::init_ltc_dll(framepos_t tme, double dt)
{
	omega = 2.0 * M_PI * dt / double(session.frame_rate());
	b = 1.4142135623730950488 * omega;
	c = omega * omega;

	e2 = dt;
	t0 = double(tme);
	t1 = t0 + e2;
	DEBUG_TRACE (DEBUG::LTC, string_compose ("[re-]init LTC DLL %1 %2 %3\n", t0, t1, e2));
}

/* main entry point from session_process.cc
 * called from jack_process callback context
 */
bool
LTC_Slave::speed_and_position (double& speed, framepos_t& pos)
{

	framepos_t now = session.engine().frame_time_at_cycle_start();
	framecnt_t nframes = session.engine().frames_per_cycle();
	jack_default_audio_sample_t *in;
	jack_latency_range_t ltc_latency;

	Port *ltcport = session.engine().ltc_input_port();
	ltcport->get_connected_latency_range(ltc_latency, false);
	in = (jack_default_audio_sample_t*) jack_port_get_buffer (ltcport->jack_port(), nframes);

	DEBUG_TRACE (DEBUG::LTC, string_compose ("LTC_Slave::speed_and_position - TID:%1 | latency: %2\n", ::pthread_self(), ltc_latency.max));

	if (in) {
		parse_ltc(nframes, in, now  + ltc_latency.max );
		if (!process_ltc(now, nframes)) {
			/* fly wheel */
			double elapsed = (now - last_timestamp) * ltc_speed;
			ltc_transport_pos = last_ltc_frame + elapsed;
			DEBUG_TRACE (DEBUG::LTC, string_compose ("LTC fly wheel elapsed: %1 @speed %2\n", elapsed, ltc_speed));
		}
	}

	if (((now - last_timestamp) > 4 * frames_per_ltc_frame) ) {
		DEBUG_TRACE (DEBUG::LTC, "LTC no-signal - reset\n");
		speed = ltc_speed = 0;
		pos = session.transport_frame();
		last_timestamp = 0;
		return true;
	}

	pos = ltc_transport_pos;
	speed = ltc_speed;

	return true;
}

Timecode::TimecodeFormat
LTC_Slave::apparent_timecode_format () const
{
	/* XXX to be computed, determined from incoming stream */
	return timecode_25;
}
