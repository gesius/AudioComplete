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

*/

#ifndef __ardour_audio_region_h__
#define __ardour_audio_region_h__

#include <vector>
#include <list>

#include "pbd/fastlog.h"
#include "pbd/undo.h"

#include "ardour/ardour.h"
#include "ardour/automatable_controls.h"
#include "ardour/gain.h"
#include "ardour/logcurve.h"
#include "ardour/region.h"

class XMLNode;


namespace ARDOUR {

namespace Properties {
	extern PBD::PropertyDescriptor<bool> envelope_active;
	extern PBD::PropertyDescriptor<bool> default_fade_in;
	extern PBD::PropertyDescriptor<bool> default_fade_out;
	extern PBD::PropertyDescriptor<bool> fade_in_active;
	extern PBD::PropertyDescriptor<bool> fade_out_active;
	extern PBD::PropertyDescriptor<float> scale_amplitude;
}

class Route;
class Playlist;
class Session;
class Filter;
class AudioSource;


class AudioRegion : public Region
{
  public:
	static void make_property_quarks ();

	static PBD::PropertyChange FadeInChanged;
	static PBD::PropertyChange FadeOutChanged;
	static PBD::PropertyChange FadeInActiveChanged;
	static PBD::PropertyChange FadeOutActiveChanged;
	static PBD::PropertyChange EnvelopeActiveChanged;
	static PBD::PropertyChange ScaleAmplitudeChanged;
	static PBD::PropertyChange EnvelopeChanged;

	~AudioRegion();

	void copy_settings (boost::shared_ptr<const AudioRegion>);

	bool source_equivalent (boost::shared_ptr<const Region>) const;

	bool speed_mismatch (float) const;

	boost::shared_ptr<AudioSource> audio_source (uint32_t n=0) const;

	void   set_scale_amplitude (gain_t);
	gain_t scale_amplitude() const { return _scale_amplitude; }

	void normalize_to (float target_in_dB = 0.0f);

	bool envelope_active () const { return _envelope_active; }
	bool fade_in_active ()  const { return _fade_in_active; }
	bool fade_out_active () const { return _fade_out_active; }

	boost::shared_ptr<AutomationList> fade_in()  { return _fade_in; }
	boost::shared_ptr<AutomationList> fade_out() { return _fade_out; }
	boost::shared_ptr<AutomationList> envelope() { return _envelope; }

	virtual nframes_t read_peaks (PeakData *buf, nframes_t npeaks,
			nframes_t offset, nframes_t cnt,
			uint32_t chan_n=0, double samples_per_unit= 1.0) const;

	/* Readable interface */

	enum ReadOps {
		ReadOpsNone = 0x0,
		ReadOpsOwnAutomation = 0x1,
		ReadOpsOwnScaling = 0x2,
		ReadOpsCount = 0x4,
		ReadOpsFades = 0x8
	};

	virtual framecnt_t read (Sample*, framepos_t pos, framecnt_t cnt, int channel) const;
	virtual framecnt_t read_with_ops (Sample*, framepos_t pos, framecnt_t cnt, int channel, ReadOps rops) const;
	virtual framecnt_t readable_length() const { return length(); }

	virtual framecnt_t read_at (Sample *buf, Sample *mixdown_buf, float *gain_buf,
				     framepos_t position,
				     framecnt_t cnt,
				     uint32_t  chan_n      = 0,
				     framecnt_t read_frames = 0,
				     framecnt_t skip_frames = 0) const;
	
	virtual framecnt_t master_read_at (Sample *buf, Sample *mixdown_buf, float *gain_buf,
					   framepos_t position, framecnt_t cnt, uint32_t chan_n=0) const;
	
	virtual framecnt_t read_raw_internal (Sample*, framepos_t, framecnt_t, int channel) const;

	XMLNode& state (bool);
	int set_state (const XMLNode&, int version);

	static void set_default_fade (float steepness, framecnt_t len);
	bool fade_in_is_default () const;
	bool fade_out_is_default () const;

	enum FadeShape {
		Linear,
		Fast,
		Slow,
		LogA,
		LogB
	};

	void set_fade_in_active (bool yn);
	void set_fade_in_shape (FadeShape);
	void set_fade_in_length (framecnt_t);
	void set_fade_in (FadeShape, framecnt_t);
	void set_fade_in (boost::shared_ptr<AutomationList>);

	void set_fade_out_active (bool yn);
	void set_fade_out_shape (FadeShape);
	void set_fade_out_length (framecnt_t);
	void set_fade_out (FadeShape, framecnt_t);
	void set_fade_out (boost::shared_ptr<AutomationList>);

	void set_envelope_active (bool yn);
	void set_default_envelope ();

	int separate_by_channel (ARDOUR::Session&, std::vector<boost::shared_ptr<Region> >&) const;

	/* automation */

	boost::shared_ptr<Evoral::Control>
	control(const Evoral::Parameter& id, bool create=false) {
		return _automatable.control(id, create);
	}

	virtual boost::shared_ptr<const Evoral::Control>
	control(const Evoral::Parameter& id) const {
		return _automatable.control(id);
	}

	/* export */

	int exportme (ARDOUR::Session&, ARDOUR::ExportSpecification&);

	/* xfade/fade interactions */

	void suspend_fade_in ();
	void suspend_fade_out ();
	void resume_fade_in ();
	void resume_fade_out ();

	int get_transients (AnalysisFeatureList&, bool force_new = false);
	std::list<std::pair<frameoffset_t, framecnt_t> > find_silence (Sample, framecnt_t) const;

  private:
	friend class RegionFactory;
	friend class Crossfade;

	AudioRegion (boost::shared_ptr<AudioSource>);
	AudioRegion (const SourceList &);
	AudioRegion (boost::shared_ptr<const AudioRegion>, frameoffset_t offset = 0, bool offset_relative = true);
	AudioRegion (boost::shared_ptr<const AudioRegion>, const SourceList&);
	AudioRegion (boost::shared_ptr<AudioSource>, const XMLNode&);
	AudioRegion (SourceList &, const XMLNode&);

  private:
	PBD::Property<bool>     _envelope_active;
	PBD::Property<bool>     _default_fade_in;
	PBD::Property<bool>     _default_fade_out;
	PBD::Property<bool>     _fade_in_active;
	PBD::Property<bool>     _fade_out_active;
	PBD::Property<gain_t>   _scale_amplitude;
	
	void register_properties ();
	PBD::PropertyChange set_property (const PBD::PropertyBase& prop);
	void post_set ();

	void init ();
	void set_default_fades ();
	void set_default_fade_in ();
	void set_default_fade_out ();

	void recompute_gain_at_end ();
	void recompute_gain_at_start ();

	framecnt_t _read_at (const SourceList&, framecnt_t limit,
			     Sample *buf, Sample *mixdown_buffer, float *gain_buffer,
			     framepos_t position, framecnt_t cnt,
			     uint32_t chan_n = 0,
			     framecnt_t read_frames = 0,
			     framecnt_t skip_frames = 0,
			     ReadOps readops = ReadOps (~0)) const;

	void recompute_at_start ();
	void recompute_at_end ();

	void envelope_changed ();
	void fade_in_changed ();
	void fade_out_changed ();
	void source_offset_changed ();
	void listen_to_my_curves ();
	void connect_to_analysis_changed ();
	void connect_to_header_position_offset_changed ();

	AutomatableControls _automatable;

	boost::shared_ptr<AutomationList> _fade_in;
	boost::shared_ptr<AutomationList> _fade_out;
	boost::shared_ptr<AutomationList> _envelope;
	uint32_t                          _fade_in_suspended;
	uint32_t                          _fade_out_suspended;

  protected:
	/* default constructor for derived (compound) types */

	AudioRegion (Session& s, framepos_t, framecnt_t, std::string name);

	int _set_state (const XMLNode&, int version, PBD::PropertyChange& what_changed, bool send_signal);
};

} /* namespace ARDOUR */

/* access from C objects */

extern "C" {
	int    region_read_peaks_from_c   (void *arg, uint32_t npeaks, uint32_t start, uint32_t length, intptr_t data, uint32_t n_chan, double samples_per_unit);
	uint32_t region_length_from_c (void *arg);
	uint32_t sourcefile_length_from_c (void *arg, double);
}

#endif /* __ardour_audio_region_h__ */
