/*
    Copyright (C) 2000-2006 Paul Davis

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

    $Id: midiregion.h 733 2006-08-01 17:19:38Z drobilla $
*/

#ifndef __ardour_midi_region_h__
#define __ardour_midi_region_h__

#include <vector>

#include "pbd/fastlog.h"
#include "pbd/undo.h"

#include "ardour/ardour.h"
#include "ardour/gain.h"
#include "ardour/logcurve.h"
#include "ardour/midi_model.h"
#include "ardour/midi_source.h"
#include "ardour/region.h"

class XMLNode;

namespace ARDOUR {

class Route;
class Playlist;
class Session;
class MidiFilter;
class MidiSource;
class MidiStateTracker;
template<typename T> class MidiRingBuffer;

class MidiRegion : public Region
{
  public:
	~MidiRegion();

	boost::shared_ptr<MidiSource> midi_source (uint32_t n=0) const;

	/* Stub Readable interface */
	virtual nframes_t read (Sample*, sframes_t /*pos*/, nframes_t /*cnt*/, int /*channel*/) const { return 0; }
	virtual sframes_t readable_length() const { return length(); }

	nframes_t read_at (Evoral::EventSink<nframes_t>& dst,
			   sframes_t position,
			   nframes_t dur,
			   uint32_t  chan_n = 0,
			   NoteMode  mode = Sustained,
			   MidiStateTracker* tracker = 0) const;

	nframes_t master_read_at (MidiRingBuffer<nframes_t>& dst,
			sframes_t position,
			nframes_t dur,
			uint32_t  chan_n = 0,
			NoteMode  mode = Sustained) const;

	XMLNode& state (bool);
	int      set_state (const XMLNode&, int version);

	int separate_by_channel (ARDOUR::Session&, std::vector< boost::shared_ptr<Region> >&) const;

	/* automation */

	boost::shared_ptr<Evoral::Control>
	control(const Evoral::Parameter& id, bool create=false) {
		return model()->data().control(id, create);
	}

	virtual boost::shared_ptr<const Evoral::Control>
	control(const Evoral::Parameter& id) const {
		return model()->data().control(id);
	}

	/* export */

	int exportme (ARDOUR::Session&, ARDOUR::ExportSpecification&);

	boost::shared_ptr<MidiModel> model()             { return midi_source()->model(); }
	boost::shared_ptr<const MidiModel> model() const { return midi_source()->model(); }

  private:
	friend class RegionFactory;

	MidiRegion (boost::shared_ptr<MidiSource>, nframes_t start, nframes_t length);
	MidiRegion (boost::shared_ptr<MidiSource>, nframes_t start, nframes_t length, const string& name, layer_t = 0, Region::Flag flags = Region::DefaultFlags);
	MidiRegion (const SourceList &, nframes_t start, nframes_t length, const string& name, layer_t = 0, Region::Flag flags = Region::DefaultFlags);
	MidiRegion (boost::shared_ptr<const MidiRegion>, nframes_t start, nframes_t length, const string& name, layer_t = 0, Region::Flag flags = Region::DefaultFlags);
	MidiRegion (boost::shared_ptr<const MidiRegion>);
	MidiRegion (boost::shared_ptr<MidiSource>, const XMLNode&);
	MidiRegion (const SourceList &, const XMLNode&);

  private:
	nframes_t _read_at (const SourceList&, Evoral::EventSink<nframes_t>& dst,
			    sframes_t position,
			    nframes_t dur,
			    uint32_t chan_n = 0,
			    NoteMode mode = Sustained, 
			    MidiStateTracker* tracker = 0) const;

	void recompute_at_start ();
	void recompute_at_end ();

	void set_position_internal (nframes_t pos, bool allow_bbt_recompute);

	void switch_source(boost::shared_ptr<Source> source);

  protected:

	int set_live_state (const XMLNode&, int version, Change&, bool send);
};

} /* namespace ARDOUR */


#endif /* __ardour_midi_region_h__ */
