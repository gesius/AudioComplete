/*
    Copyright (C) 2001 Paul Davis

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

#include <glibmm/thread.h>

#include "pbd/error.h"

#include "ardour/audio_diskstream.h"
#include "ardour/audioregion.h"
#include "ardour/audioengine.h"
#include "ardour/delivery.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/auditioner.h"
#include "ardour/audioplaylist.h"
#include "ardour/audio_port.h"
#include "ardour/panner.h"
#include "ardour/data_type.h"
#include "ardour/region_factory.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

#include "i18n.h"

Auditioner::Auditioner (Session& s)
	: AudioTrack (s, "auditioner", Route::Hidden)
        , current_frame (0)
        , _auditioning (0)
        , length (0)
        , via_monitor (false)
{
}

int
Auditioner::init ()
{
        if (Track::init ()) {
                return -1;
        }

	string left = _session.config.get_auditioner_output_left();
	string right = _session.config.get_auditioner_output_right();

	vector<string> outputs;
	_session.engine().get_physical_outputs (DataType::AUDIO, outputs);

	if (left == "default") {
                if (_session.monitor_out()) {
                        left = _session.monitor_out()->input()->audio (0)->name();
                        via_monitor = true;
                } else {
			if (outputs.size() > 0) {
				left = outputs[0];
			}
                }
	}

	if (right == "default") {
                if (_session.monitor_out()) {
                        right = _session.monitor_out()->input()->audio (1)->name();
                        via_monitor = true;
                } else {
			if (outputs.size() > 1) {
				right = outputs[1];
			}
                }
	}

	if ((left.length() == 0) && (right.length() == 0)) {
		warning << _("no outputs available for auditioner - manual connection required") << endmsg;
		return -1;
	}

	_main_outs->defer_pan_reset ();

	if (left.length()) {
		_output->add_port (left, this, DataType::AUDIO);
	}

	if (right.length()) {
		_output->add_port (right, this, DataType::AUDIO);
	}

	_main_outs->allow_pan_reset ();
	_main_outs->reset_panner ();

	_output->changed.connect_same_thread (*this, boost::bind (&Auditioner::output_changed, this, _1, _2));

        return 0;
}

Auditioner::~Auditioner ()
{
}

AudioPlaylist&
Auditioner::prepare_playlist ()
{
	// FIXME auditioner is still audio-only
	boost::shared_ptr<AudioPlaylist> apl = boost::dynamic_pointer_cast<AudioPlaylist>(_diskstream->playlist());
	assert(apl);

	apl->clear ();
	return *apl;
}

void
Auditioner::audition_current_playlist ()
{
	if (g_atomic_int_get (&_auditioning)) {
		/* don't go via session for this, because we are going
		   to remain active.
		*/
		cancel_audition ();
	}

	Glib::Mutex::Lock lm (lock);
	_diskstream->seek (0);
	length = _diskstream->playlist()->get_extent().second;
	current_frame = 0;

	/* force a panner reset now that we have all channels */

	_main_outs->panner()->reset (n_outputs().n_audio(), _diskstream->n_channels().n_audio());

	g_atomic_int_set (&_auditioning, 1);
}

void
Auditioner::audition_region (boost::shared_ptr<Region> region)
{
	if (g_atomic_int_get (&_auditioning)) {
		/* don't go via session for this, because we are going
		   to remain active.
		*/
		cancel_audition ();
	}

	if (boost::dynamic_pointer_cast<AudioRegion>(region) == 0) {
		error << _("Auditioning of non-audio regions not yet supported") << endmsg;
		return;
	}

	Glib::Mutex::Lock lm (lock);

	/* copy it */

	boost::shared_ptr<AudioRegion> the_region (boost::dynamic_pointer_cast<AudioRegion> (RegionFactory::create (region)));
	the_region->set_position (0, this);

	_diskstream->playlist()->drop_regions ();
	_diskstream->playlist()->add_region (the_region, 0, 1);

	if (_diskstream->n_channels().n_audio() < the_region->n_channels()) {
		audio_diskstream()->add_channel (the_region->n_channels() - _diskstream->n_channels().n_audio());
	} else if (_diskstream->n_channels().n_audio() > the_region->n_channels()) {
		audio_diskstream()->remove_channel (_diskstream->n_channels().n_audio() - the_region->n_channels());
	}

        ProcessorStreams ps;
        if (configure_processors (&ps)) {
                error << string_compose (_("Cannot setup auditioner processing flow for %1 channels"), 
                                         _diskstream->n_channels()) << endmsg;
                return;
        }

	/* force a panner reset now that we have all channels */

	_main_outs->reset_panner();

	length = the_region->length();

	int dir;
	nframes_t offset = the_region->sync_offset (dir);

	/* can't audition from a negative sync point */

	if (dir < 0) {
		offset = 0;
	}

	_diskstream->seek (offset);
	current_frame = offset;

	g_atomic_int_set (&_auditioning, 1);
}

int
Auditioner::play_audition (nframes_t nframes)
{
	bool need_butler = false;
	nframes_t this_nframes;
	int ret;

	if (g_atomic_int_get (&_auditioning) == 0) {
		silence (nframes);
		return 0;
	}

	this_nframes = min (nframes, length - current_frame);

	if ((ret = roll (this_nframes, current_frame, current_frame + nframes, false, false, false, need_butler)) != 0) {
		silence (nframes);
		return ret;
	}

	current_frame += this_nframes;

	if (current_frame >= length) {
		_session.cancel_audition ();
		return 0;
	} else {
		return need_butler ? 1 : 0;
	}
}

void
Auditioner::output_changed (IOChange change, void* /*src*/)
{
	if (change & ConnectionsChanged) {
		string phys;
		vector<string> connections;
		vector<string> outputs;
		_session.engine().get_physical_outputs (DataType::AUDIO, outputs);
		if (_output->nth (0)->get_connections (connections)) {
			if (outputs.size() > 0) {
				phys = outputs[0];
			}
			if (phys != connections[0]) {
				_session.config.set_auditioner_output_left (connections[0]);
			} else {
				_session.config.set_auditioner_output_left ("default");
			}
		} else {
			_session.config.set_auditioner_output_left ("");
		}

		connections.clear ();

		if (_output->nth (1)->get_connections (connections)) {
			if (outputs.size() > 1) {
				phys = outputs[1];
			}
			if (phys != connections[0]) {
				_session.config.set_auditioner_output_right (connections[0]);
			} else {
				_session.config.set_auditioner_output_right ("default");
			}
		} else {
			_session.config.set_auditioner_output_right ("");
		}
	}
}
