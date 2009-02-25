/*
    Copyright (C) 2007 Paul Davis 

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

#ifndef __ardour_silentfilesource_h__ 
#define __ardour_silentfilesource_h__

#include <cstring>
#include "ardour/audiofilesource.h"

namespace ARDOUR {

class SilentFileSource : public AudioFileSource {
public:
	int update_header (sframes_t when, struct tm&, time_t) { return 0; }
	int flush_header () { return 0; }
	float sample_rate () const { return _sample_rate; }

	void set_length (nframes_t len) { _length = len; }
	
	bool destructive() const { return false; }
	bool can_be_analysed() const { return false; } 

protected:
	friend class SourceFactory;

	SilentFileSource (Session& s, const XMLNode& x, nframes_t len, float srate)
		: Source (s, x)
		, AudioFileSource (s, x, false)
		, _sample_rate(srate)
	{
		_length = len;
	}

	nframes_t read_unlocked (Sample *dst, sframes_t start, nframes_t cnt) const {
		memset (dst, 0, sizeof (Sample) * cnt);
		return cnt;
	}

	nframes_t write_unlocked (Sample *dst, nframes_t cnt) { return 0; }

	void set_header_timeline_position () {}

 	int read_peaks_with_fpp (PeakData *peaks, nframes_t npeaks, sframes_t start, nframes_t cnt,
			double samples_per_unit, nframes_t fpp) const {
		memset (peaks, 0, sizeof (PeakData) * npeaks);
		return 0;
	}
	
	float _sample_rate;
};

} // namespace ARDOUR

#endif /* __ardour_audiofilesource_h__ */

