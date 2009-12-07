/*
    Copyright (C) 2006 Paul Davis

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

#ifndef __ardour_meter_h__
#define __ardour_meter_h__

#include <vector>
#include <sigc++/slot.h>
#include "ardour/types.h"
#include "ardour/processor.h"
#include "pbd/fastlog.h"

namespace ARDOUR {

class BufferSet;
class ChanCount;
class Session;

class Metering {
  public:
	static void               update_meters ();
	static sigc::signal<void> Meter;

	static sigc::connection   connect (sigc::slot<void> the_slot);
	static void               disconnect (sigc::connection& c);

  private:
	/* this object is not meant to be instantiated */
	Metering();

	static Glib::StaticMutex    m_meter_signal_lock;
};

/** Meters peaks on the input and stores them for access.
 */
class PeakMeter : public Processor {
public:
	PeakMeter(Session& s) : Processor(s, "Meter") {}
	PeakMeter(Session&s, const XMLNode& node);

	void meter();
	void reset ();
	void reset_max ();

	bool can_support_io_configuration (const ChanCount& in, ChanCount& out) const;
	bool configure_io (ChanCount in, ChanCount out);
	
	/* special method for meter, to ensure that it can always handle the maximum
	   number of streams in the route, no matter where we put it.
	*/

	void reset_max_channels (const ChanCount&);

	/* tell the meter than no matter how many channels it can handle,
	   `in' is the number it is actually going be handling from
	   now on.
	*/

	void reflect_inputs (const ChanCount& in);

	/** Compute peaks */
	void run (BufferSet& bufs, sframes_t start_frame, sframes_t end_frame, nframes_t nframes, bool);

	float peak_power (uint32_t n) {
		if (n < _visible_peak_power.size()) {
			return _visible_peak_power[n];
		} else {
			return minus_infinity();
		}
	}

	float max_peak_power (uint32_t n) {
		if (n < _max_peak_power.size()) {
			return _max_peak_power[n];
		} else {
			return minus_infinity();
		}
	}

	XMLNode& state (bool full);
	
private:
	friend class IO;
	
	uint32_t current_meters;
	
	std::vector<float> _peak_power;
	std::vector<float> _visible_peak_power;
	std::vector<float> _max_peak_power;
};


} // namespace ARDOUR

#endif // __ardour_meter_h__
