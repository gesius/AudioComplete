/*
    Copyright (C) 1999-2009 Paul Davis

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

#ifndef __ardour_ardour_h__
#define __ardour_ardour_h__

#include <map>
#include <string>

#include <limits.h>
#include <signal.h>

#include "pbd/signals.h"

#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/locale_guard.h"
#include "pbd/stateful.h"

#include "ardour/types.h"

#include <jack/jack.h> 

namespace MIDI {
	class MachineControl;
	class Port;
}

namespace ARDOUR {

	class AudioEngine;

	static const nframes_t max_frames = JACK_MAX_FRAMES;
	extern PBD::Signal1<void,std::string> BootMessage;

	int init (bool with_vst, bool try_optimization);
	void init_post_engine ();
	int cleanup ();
	bool no_auto_connect ();
	void make_property_quarks ();
        
	extern PBD::PropertyChange bounds_change;

	std::string get_ardour_revision ();
	extern const char* const ardour_config_info;

	void find_bindings_files (std::map<std::string,std::string>&);

	const layer_t max_layer = UCHAR_MAX;

	static inline microseconds_t get_microseconds () {
		return (microseconds_t) jack_get_time();
	}

	static const double SHUTTLE_FRACT_SPEED1=0.48412291827; /* derived from A1,A2 */

	void setup_fpu ();
}

#endif /* __ardour_ardour_h__ */

