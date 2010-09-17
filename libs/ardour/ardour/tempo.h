/*
    Copyright (C) 2000 Paul Davis

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

#ifndef __ardour_tempo_h__
#define __ardour_tempo_h__

#include <list>
#include <string>
#include <vector>
#include <cmath>
#include <glibmm/thread.h>

#include "pbd/undo.h"
#include "pbd/stateful.h"
#include "pbd/statefuldestructible.h"


#include "ardour/ardour.h"

class XMLNode;

namespace ARDOUR {
class Meter;
class Tempo {
  public:
	Tempo (double bpm, double type=4.0) // defaulting to quarter note
		: _beats_per_minute (bpm), _note_type(type) {}

	double beats_per_minute () const { return _beats_per_minute;}
	double note_type () const { return _note_type;}
	double frames_per_beat (nframes_t sr, const Meter& meter) const;

  protected:
	double _beats_per_minute;
	double _note_type;
};

class Meter {
  public:
	static const double ticks_per_beat;

	Meter (double bpb, double bt)
		: _beats_per_bar (bpb), _note_type (bt) {}

	double beats_per_bar () const { return _beats_per_bar; }
	double note_divisor() const { return _note_type; }

	double frames_per_bar (const Tempo&, nframes_t sr) const;

  protected:
	/** The number of beats in a bar.  This is a real value because
	    there are musical traditions on our planet that do not limit
	    themselves to integral numbers of beats per bar.
	*/
	double _beats_per_bar;

	/** The type of "note" that a beat represents.  For example, 4.0 is
	    a quarter (crotchet) note, 8.0 is an eighth (quaver) note, etc.
	*/
	double _note_type;
};

class MetricSection {
  public:
	MetricSection (const BBT_Time& start)
		: _start (start), _frame (0), _movable (true) {}
	MetricSection (framepos_t start)
		: _frame (start), _movable (true) {}

	virtual ~MetricSection() {}

	const BBT_Time& start() const { return _start; }
	framepos_t     frame() const { return _frame; }

	void set_movable (bool yn) { _movable = yn; }
	bool movable() const { return _movable; }

	virtual void set_frame (framepos_t f) {
		_frame = f;
	}

	virtual void set_start (const BBT_Time& w) {
		_start = w;
	}

	/* MeterSections are not stateful in the full sense,
	   but we do want them to control their own
	   XML state information.
	*/
	virtual XMLNode& get_state() const = 0;

	int compare (MetricSection *, bool) const;

  private:
	BBT_Time       _start;
	framepos_t    _frame;
	bool           _movable;
};

class MeterSection : public MetricSection, public Meter {
  public:
	MeterSection (const BBT_Time& start, double bpb, double note_type)
		: MetricSection (start), Meter (bpb, note_type) {}
	MeterSection (framepos_t start, double bpb, double note_type)
		: MetricSection (start), Meter (bpb, note_type) {}
	MeterSection (const XMLNode&);

	static const std::string xml_state_node_name;

	XMLNode& get_state() const;
};

class TempoSection : public MetricSection, public Tempo {
  public:
	TempoSection (const BBT_Time& start, double qpm, double note_type)
		: MetricSection (start), Tempo (qpm, note_type) {}
	TempoSection (framepos_t start, double qpm, double note_type)
		: MetricSection (start), Tempo (qpm, note_type) {}
	TempoSection (const XMLNode&);

	static const std::string xml_state_node_name;

	XMLNode& get_state() const;
};

typedef std::list<MetricSection*> Metrics;

/** Helper class that we use to be able to keep track of which
    meter *AND* tempo are in effect at a given point in time.
*/
class TempoMetric {
  public:
	TempoMetric (const Meter& m, const Tempo& t) : _meter (&m), _tempo (&t), _frame (0) {}
	
	void set_tempo (const Tempo& t)    { _tempo = &t; }
	void set_meter (const Meter& m)    { _meter = &m; }
	void set_frame (framepos_t f)      { _frame = f; }
	void set_start (const BBT_Time& t) { _start = t; }
	
	const Meter&    meter() const { return *_meter; }
	const Tempo&    tempo() const { return *_tempo; }
	framepos_t      frame() const { return _frame; }
	const BBT_Time& start() const { return _start; }
	
  private:
	const Meter*   _meter;
	const Tempo*   _tempo;
	framepos_t     _frame;
	BBT_Time       _start;
};

class TempoMap : public PBD::StatefulDestructible
{
  public:
	TempoMap (nframes_t frame_rate);
	~TempoMap();

	/* measure-based stuff */

	enum BBTPointType {
		Bar,
		Beat,
	};

	struct BBTPoint {
		BBTPointType type;
		framepos_t  frame;
		const Meter* meter;
		const Tempo* tempo;
		uint32_t bar;
		uint32_t beat;

		BBTPoint (const Meter& m, const Tempo& t, framepos_t f,
				BBTPointType ty, uint32_t b, uint32_t e)
			: type (ty), frame (f), meter (&m), tempo (&t), bar (b), beat (e) {}
	};

	typedef std::vector<BBTPoint> BBTPointList;

	template<class T> void apply_with_metrics (T& obj, void (T::*method)(const Metrics&)) {
		Glib::RWLock::ReaderLock lm (lock);
		(obj.*method)(*metrics);
	}

	BBTPointList *get_points (framepos_t start, framepos_t end) const;

	void      bbt_time (framepos_t when, BBT_Time&) const;
	framecnt_t frame_time (const BBT_Time&) const;
	framecnt_t bbt_duration_at (framepos_t, const BBT_Time&, int dir) const;

	void bbt_time_add (framepos_t origin, BBT_Time& start, const BBT_Time& shift);

	static const Tempo& default_tempo() { return _default_tempo; }
	static const Meter& default_meter() { return _default_meter; }

	const Tempo& tempo_at (framepos_t) const;
	const Meter& meter_at (framepos_t) const;

	const TempoSection& tempo_section_at (framepos_t);

	void add_tempo(const Tempo&, BBT_Time where);
	void add_meter(const Meter&, BBT_Time where);

	void add_tempo(const Tempo&, framepos_t where);
	void add_meter(const Meter&, framepos_t where);

	void move_tempo (TempoSection&, const BBT_Time& to);
	void move_meter (MeterSection&, const BBT_Time& to);

	void remove_tempo(const TempoSection&);
	void remove_meter(const MeterSection&);

	void replace_tempo (TempoSection& existing, const Tempo& replacement);
	void replace_meter (MeterSection& existing, const Meter& replacement);

	framepos_t round_to_bar  (framepos_t frame, int dir);
	framepos_t round_to_beat (framepos_t frame, int dir);
	framepos_t round_to_beat_subdivision (framepos_t fr, int sub_num, int dir);
	framepos_t round_to_tick (framepos_t frame, int dir);

	void set_length (framepos_t frames);

	XMLNode& get_state (void);
	int set_state (const XMLNode&, int version);

	void dump (std::ostream&) const;
	void clear ();

	TempoMetric metric_at (BBT_Time bbt) const;
	TempoMetric metric_at (framepos_t) const;
	void bbt_time_with_metric (framepos_t, BBT_Time&, const TempoMetric&) const;

	BBT_Time bbt_add (const BBT_Time&, const BBT_Time&, const TempoMetric&) const;
	BBT_Time bbt_add (const BBT_Time& a, const BBT_Time& b) const;
	BBT_Time bbt_subtract (const BBT_Time&, const BBT_Time&) const;

	void change_existing_tempo_at (framepos_t, double bpm, double note_type);
	void change_initial_tempo (double bpm, double note_type);

	void insert_time (framepos_t, framecnt_t);

	int n_tempos () const;
	int n_meters () const;

	nframes_t frame_rate () const { return _frame_rate; }

  private:
	static Tempo    _default_tempo;
	static Meter    _default_meter;

	Metrics*             metrics;
	nframes_t           _frame_rate;
	framepos_t          last_bbt_when;
	bool                 last_bbt_valid;
	BBT_Time             last_bbt;
	mutable Glib::RWLock lock;

	void timestamp_metrics (bool use_bbt);

	framepos_t round_to_type (framepos_t fr, int dir, BBTPointType);

	framepos_t frame_time_unlocked (const BBT_Time&) const;

	void bbt_time_unlocked (framepos_t, BBT_Time&) const;

	framecnt_t bbt_duration_at_unlocked (const BBT_Time& when, const BBT_Time& bbt, int dir) const;

	const MeterSection& first_meter() const;
	const TempoSection& first_tempo() const;

	framecnt_t count_frames_between (const BBT_Time&, const BBT_Time&) const;
	framecnt_t count_frames_between_metrics (const Meter&, const Tempo&,
                                                 const BBT_Time&, const BBT_Time&) const;

	int move_metric_section (MetricSection&, const BBT_Time& to);
	void do_insert (MetricSection* section, bool with_bbt);
};

}; /* namespace ARDOUR */

#endif /* __ardour_tempo_h__ */
