/*
    Copyright (C) 2006 Paul Davis 
	Written by Dave Robillard, 2006

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

#include <vector>

#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <pbd/mountpoint.h>
#include <pbd/pathscanner.h>
#include <pbd/stl_delete.h>
#include <pbd/strsplit.h>

#include <glibmm/miscutils.h>

#include <evoral/SMFReader.hpp>
#include <evoral/Control.hpp>

#include <ardour/audioengine.h>
#include <ardour/event_type_map.h>
#include <ardour/midi_model.h>
#include <ardour/midi_ring_buffer.h>
#include <ardour/session.h>
#include <ardour/smf_source.h>
#include <ardour/tempo.h>

#include "i18n.h"

using namespace ARDOUR;

string SMFSource::_search_path;

/** Constructor used for new internal-to-session files.  File cannot exist. */
SMFSource::SMFSource(Session& s, std::string path, Flag flags)
	: MidiSource(s, region_name_from_path(path, false))
	, Evoral::SMF()
	, _flags(flags)
	, _allow_remove_if_empty(true)
	, _last_ev_time_beats(0.0)
	, _last_ev_time_frames(0)
{
	if (init(path, false)) {
		throw failed_constructor ();
	}
	
	if (create(path)) {
		throw failed_constructor ();
	}

	assert(_name.find("/") == string::npos);
}

/** Constructor used for existing internal-to-session files.  File must exist. */
SMFSource::SMFSource(Session& s, const XMLNode& node)
	: MidiSource(s, node)
	, _flags(Flag(Writable|CanRename))
	, _allow_remove_if_empty(true)
	, _last_ev_time_beats(0.0)
	, _last_ev_time_frames(0)
{
	if (set_state(node)) {
		throw failed_constructor ();
	}
	
	if (init(_name, true)) {
		throw failed_constructor ();
	}
	
	if (open(_path)) {
		throw failed_constructor ();
	}
	
	assert(_name.find("/") == string::npos);
}

SMFSource::~SMFSource ()
{
	if (removable()) {
		unlink (_path.c_str());
	}
}

bool
SMFSource::removable () const
{
	return (_flags & Removable) && ((_flags & RemoveAtDestroy) ||
			((_flags & RemovableIfEmpty) && is_empty()));
}

int
SMFSource::init (string pathstr, bool must_exist)
{
	bool is_new = false;

	if (!find (pathstr, must_exist, is_new)) {
		cerr << "cannot find " << pathstr << " with me = " << must_exist << endl;
		return -1;
	}

	if (is_new && must_exist) {
		return -1;
	}

	assert(_name.find("/") == string::npos);
	return 0;
}

/** All stamps in audio frames */
nframes_t
SMFSource::read_unlocked (MidiRingBuffer<nframes_t>& dst, nframes_t start, nframes_t dur,
		nframes_t stamp_offset, nframes_t negative_stamp_offset) const
{
	//cerr << "SMF read_unlocked " << name() << " read "
	//<< start << ", count=" << dur << ", offset=" << stamp_offset << endl;

	int ret;
	uint64_t time = 0; // in SMF ticks, 1 tick per _ppqn

	_read_data_count = 0;

	// Output parameters for read_event (which will allocate scratch in buffer as needed)
	uint32_t ev_delta_t = 0;
	uint32_t ev_type    = 0;
	uint32_t ev_size    = 0;
	uint8_t* ev_buffer  = 0;

	size_t scratch_size = 0; // keep track of scratch to minimize reallocs

	// FIXME: assumes tempo never changes after start
	const Tempo& tempo = _session.tempo_map().tempo_at(_timeline_position);
	const double frames_per_beat = tempo.frames_per_beat(
			_session.engine().frame_rate(),
			_session.tempo_map().meter_at(_timeline_position));
	
	const uint64_t start_ticks = (uint64_t)((start / frames_per_beat) * ppqn());

	if (_last_read_end == 0 || start != _last_read_end) {
		cerr << "SMFSource::read_unlocked seeking to " << start << endl;
		Evoral::SMF::seek_to_start();
		while (time < start_ticks) {
			ret = read_event(&ev_delta_t, &ev_size, &ev_buffer);
			if (ret == -1) { // EOF
				_last_read_end = start + dur;
				return dur;
			}
			time += ev_delta_t; // accumulate delta time
		}
	}
	
	_last_read_end = start + dur;

	while (true) {
		ret = read_event(&ev_delta_t, &ev_size, &ev_buffer);
		if (ret == -1) { // EOF
			break;
		}
		
		time += ev_delta_t; // accumulate delta time

		if (ret == 0) { // meta-event (skipped, just accumulate time)
			continue;
		}
		
		ev_type = EventTypeMap::instance().midi_event_type(ev_buffer[0]);

		assert(time >= start_ticks);
		const nframes_t ev_frame_time = (nframes_t)(
				((time / (double)ppqn()) * frames_per_beat)) + stamp_offset;

		if (ev_frame_time < start + dur) {
			dst.write(ev_frame_time - negative_stamp_offset, ev_type, ev_size, ev_buffer);
		} else {
			break;
		}

		_read_data_count += ev_size;

		if (ev_size > scratch_size) {
			scratch_size = ev_size;
		}
		ev_size = scratch_size; // ensure read_event only allocates if necessary
	}
	
	return dur;
}

/** All stamps in audio frames */
nframes_t
SMFSource::write_unlocked (MidiRingBuffer<nframes_t>& src, nframes_t dur)
{
	_write_data_count = 0;
		
	nframes_t         time;
	Evoral::EventType type;
	uint32_t          size;

	size_t   buf_capacity = 4;
	uint8_t* buf          = (uint8_t*)malloc(buf_capacity);
	
	if (_model && ! _model->writing()) {
		_model->start_write();
	}

	Evoral::MIDIEvent<nframes_t> ev(0, 0.0, 4, NULL, true);

	while (true) {
		bool ret = src.peek_time(&time);
		if (!ret || time - _timeline_position > _length + dur) {
			break;
		}

		ret = src.read_prefix(&time, &type, &size);
		if (!ret) {
			break;
		}

		if (size > buf_capacity) {
			buf_capacity = size;
			buf = (uint8_t*)realloc(buf, size);
		}

		ret = src.read_contents(size, buf);
		if (!ret) {
			cerr << "ERROR: Read time/size but not buffer, corrupt MIDI ring buffer" << endl;
			break;
		}
		
		assert(time >= _timeline_position);
		time -= _timeline_position;
		
		ev.set(buf, size, time);
		ev.set_event_type(EventTypeMap::instance().midi_event_type(ev.buffer()[0]));
		if (!(ev.is_channel_event() || ev.is_smf_meta_event() || ev.is_sysex())) {
			cerr << "SMFSource: WARNING: caller tried to write non SMF-Event of type "
					<< std::hex << int(ev.buffer()[0]) << endl;
			continue;
		}
		
		append_event_unlocked_frames(ev);
	}

	if (_model) {
		set_default_controls_interpolation();
	}

	Evoral::SMF::flush();
	free(buf);

	const nframes_t oldlen = _length;
	update_length(oldlen, dur);

	ViewDataRangeReady(_timeline_position + oldlen, dur); /* EMIT SIGNAL */

	return dur;
}
		

/** Append an event with a timestamp in beats (double) */
void
SMFSource::append_event_unlocked_beats(const Evoral::Event<double>& ev)
{
	if (ev.size() == 0)  {
		return;
	}

	/*printf("SMFSource: %s - append_event_unlocked_beats time = %lf, size = %u, data = ",
			name().c_str(), ev.time(), ev.size()); 
	for (size_t i = 0; i < ev.size(); ++i) printf("%X ", ev.buffer()[i]); printf("\n");*/
	
	assert(ev.time() >= 0);
	if (ev.time() < _last_ev_time_beats) {
		cerr << "SMFSource: Warning: Skipping event with non-monotonic time" << endl;
		return;
	}
	
	const double delta_time_beats   = ev.time() - _last_ev_time_beats;
	const uint32_t delta_time_ticks = (uint32_t)lrint(delta_time_beats * (double)ppqn());

	Evoral::SMF::append_event_delta(delta_time_ticks, ev.size(), ev.buffer());
	_last_ev_time_beats = ev.time();

	_write_data_count += ev.size();

	if (_model) {
		_model->append(ev);
	}
}

/** Append an event with a timestamp in frames (nframes_t) */
void
SMFSource::append_event_unlocked_frames(const Evoral::Event<nframes_t>& ev)
{
	if (ev.size() == 0)  {
		return;
	}

	/*printf("SMFSource: %s - append_event_unlocked_frames time = %u, size = %u, data = ",
			name().c_str(), ev.time(), ev.size()); 
	for (size_t i=0; i < ev.size(); ++i) printf("%X ", ev.buffer()[i]); printf("\n");*/
	
	assert(ev.time() >= 0);
	if (ev.time() < _last_ev_time_frames) {
		cerr << "SMFSource: Warning: Skipping event with non-monotonic time" << endl;
		return;
	}
	
	// FIXME: assumes tempo never changes after start
	const Tempo& tempo = _session.tempo_map().tempo_at(_timeline_position);
	const double frames_per_beat = tempo.frames_per_beat(
			_session.engine().frame_rate(),
			_session.tempo_map().meter_at(_timeline_position));

	uint32_t delta_time = (uint32_t)((ev.time() - _last_ev_time_frames)
			/ frames_per_beat * (double)ppqn());

	Evoral::SMF::append_event_delta(delta_time, ev.size(), ev.buffer());
	_last_ev_time_frames = ev.time();

	_write_data_count += ev.size();

	if (_model) {
		double beat_time = ev.time() / frames_per_beat;
		const Evoral::Event<double> beat_ev(
				ev.event_type(), beat_time, ev.size(), (uint8_t*)ev.buffer());
		_model->append(beat_ev);
	}
}


XMLNode&
SMFSource::get_state ()
{
	XMLNode& root (MidiSource::get_state());
	char buf[16];
	snprintf (buf, sizeof (buf), "0x%x", (int)_flags);
	root.add_property ("flags", buf);
	return root;
}

int
SMFSource::set_state (const XMLNode& node)
{
	const XMLProperty* prop;

	if (MidiSource::set_state (node)) {
		return -1;
	}

	if ((prop = node.property (X_("flags"))) != 0) {
		int ival;
		sscanf (prop->value().c_str(), "0x%x", &ival);
		_flags = Flag (ival);
	} else {
		_flags = Flag (0);
	}

	assert(_name.find("/") == string::npos);

	return 0;
}

void
SMFSource::mark_for_remove ()
{
	if (!writable()) {
		return;
	}
	_flags = Flag (_flags | RemoveAtDestroy);
}

void
SMFSource::mark_streaming_midi_write_started (NoteMode mode, nframes_t start_frame)
{
	MidiSource::mark_streaming_midi_write_started (mode, start_frame);
	Evoral::SMF::begin_write ();
	_last_ev_time_beats = 0.0;
	_last_ev_time_frames = 0;
}

void
SMFSource::mark_streaming_write_completed ()
{
	MidiSource::mark_streaming_write_completed();

	if (!writable()) {
		return;
	}
	
	_model->set_edited(false);
	Evoral::SMF::end_write ();
}

void
SMFSource::mark_take (string id)
{
	if (writable()) {
		_take_id = id;
	}
}

int
SMFSource::move_to_trash (const string trash_dir_name)
{
	if (!writable()) {
		return -1;
	}

	/* don't move the file across filesystems, just stick it in the
	   trash_dir_name directory on whichever filesystem it was already on
	*/
	
	Glib::ustring newpath;
	newpath = Glib::path_get_dirname (_path);
	newpath = Glib::path_get_dirname (newpath); 

	newpath += string("/") + trash_dir_name + "/";
	newpath += Glib::path_get_basename (_path);

	/* the new path already exists, try versioning */
	if (access (newpath.c_str(), F_OK) == 0) {
		char buf[PATH_MAX+1];
		int version = 1;
		string newpath_v;

		snprintf (buf, sizeof (buf), "%s.%d", newpath.c_str(), version);
		newpath_v = buf;

		while (access (newpath_v.c_str(), F_OK) == 0 && version < 999) {
			snprintf (buf, sizeof (buf), "%s.%d", newpath.c_str(), ++version);
			newpath_v = buf;
		}
		
		if (version == 999) {
			PBD::error << string_compose (
					_("there are already 1000 files with names like %1; versioning discontinued"),
					newpath) << endmsg;
		} else {
			newpath = newpath_v;
		}
	}

	if (::rename (_path.c_str(), newpath.c_str()) != 0) {
		PBD::error << string_compose (
				_("cannot rename midi file source from %1 to %2 (%3)"),
				_path, newpath, strerror (errno)) << endmsg;
		return -1;
	}
	
	_path = newpath;
	
	/* file can not be removed twice, since the operation is not idempotent */
	_flags = Flag (_flags & ~(RemoveAtDestroy|Removable|RemovableIfEmpty));

	return 0;
}

bool
SMFSource::safe_file_extension(const Glib::ustring& file)
{
	return (file.rfind(".mid") != Glib::ustring::npos);
}

// FIXME: Merge this with audiofilesource somehow (make a generic filesource?)
bool
SMFSource::find (string pathstr, bool must_exist, bool& isnew)
{
	bool ret = false;

	isnew = false;

	if (pathstr[0] != '/') {

		/* non-absolute pathname: find pathstr in search path */

		vector<string> dirs;
		int cnt;
		string fullpath;
		string keeppath;

		if (_search_path.length() == 0) {
			PBD::error << _("FileSource: search path not set") << endmsg;
			goto out;
		}

		split (_search_path, dirs, ':');

		cnt = 0;
		
		for (vector<string>::iterator i = dirs.begin(); i != dirs.end(); ++i) {
			fullpath = *i;
			if (fullpath[fullpath.length()-1] != '/') {
				fullpath += '/';
			}
			fullpath += pathstr;
			
			if (access (fullpath.c_str(), R_OK) == 0) {
				keeppath = fullpath;
				++cnt;
			} 
		}

		if (cnt > 1) {

			PBD::error << string_compose (_("FileSource: \"%1\" is ambigous when searching %2\n\t"), pathstr, _search_path) << endmsg;
			goto out;

		} else if (cnt == 0) {

			if (must_exist) {
				PBD::error << string_compose(_("Filesource: cannot find required file (%1): while searching %2"), pathstr, _search_path) << endmsg;
				goto out;
			} else {
				isnew = true;
			}
		}
		
		_name = pathstr;
		_path = keeppath;
		ret = true;

	} else {
		
		/* external files and/or very very old style sessions include full paths */
		
		_path = pathstr;
		_name = pathstr.substr (pathstr.find_last_of ('/') + 1);
		
		if (access (_path.c_str(), R_OK) != 0) {

			/* file does not exist or we cannot read it */

			if (must_exist) {
				PBD::error << string_compose(_("Filesource: cannot find required file (%1): %2"), _path, strerror (errno)) << endmsg;
				goto out;
			}
			
			if (errno != ENOENT) {
				PBD::error << string_compose(_("Filesource: cannot check for existing file (%1): %2"), _path, strerror (errno)) << endmsg;
				goto out;
			}
			
			/* a new file */

			isnew = true;
			ret = true;

		} else {
			
			/* already exists */

			ret = true;
		}
	}
	
  out:
	return ret;
}

void
SMFSource::set_search_path (string p)
{
	_search_path = p;
}


void
SMFSource::set_allow_remove_if_empty (bool yn)
{
	if (writable()) {
		_allow_remove_if_empty = yn;
	}
}

int
SMFSource::set_source_name (string newname, bool destructive)
{
	//Glib::Mutex::Lock lm (_lock); FIXME
	string oldpath = _path;
	string newpath = Session::change_midi_path_by_name (oldpath, _name, newname, destructive);

	if (newpath.empty()) {
		PBD::error << string_compose (_("programming error: %1"), "cannot generate a changed midi path") << endmsg;
		return -1;
	}

	if (rename (oldpath.c_str(), newpath.c_str()) != 0) {
		PBD::error << string_compose (_("cannot rename midi file for %1 to %2"), _name, newpath) << endmsg;
		return -1;
	}

	_name = Glib::path_get_basename (newpath);
	_path = newpath;

	return 0;
}

void
SMFSource::load_model(bool lock, bool force_reload)
{
	if (_writing) {
		return;
	}
	
	if (lock) {
		Glib::Mutex::Lock lm (_lock);
	}

	if (_model && !force_reload) {
		return;
	}

	if (! _model) {
		_model = boost::shared_ptr<MidiModel>(new MidiModel(this));
		cerr << _name << " loaded new model " << _model.get() << endl;
	} else {
		cerr << _name << " reloading model " << _model.get()
			<< " (" << _model->n_notes() << " notes)" <<endl;
		_model->clear();
	}

	_model->start_write();
	Evoral::SMF::seek_to_start();

	uint64_t time = 0; /* in SMF ticks */
	Evoral::Event<double> ev;
	
	size_t scratch_size = 0; // keep track of scratch and minimize reallocs
	
	uint32_t delta_t = 0;
	uint32_t size    = 0;
	uint8_t* buf     = NULL;
	int ret;
	while ((ret = read_event(&delta_t, &size, &buf)) >= 0) {
		time += delta_t;
		ev.set(buf, size, time / (double)ppqn());
		
		if (ret > 0) { // didn't skip (meta) event
			ev.set_event_type(EventTypeMap::instance().midi_event_type(buf[0]));
			_model->append(ev);
		}

		if (ev.size() > scratch_size) {
			scratch_size = ev.size();
		}
		ev.size() = scratch_size; // ensure read_event only allocates if necessary
	}

	set_default_controls_interpolation();
	
	_model->end_write(false);
	_model->set_edited(false);

	_model_iter = _model->begin();

	free(buf);
}

#define LINEAR_INTERPOLATION_MODE_WORKS_PROPERLY 0

void
SMFSource::set_default_controls_interpolation()
{
	// set interpolation style to defaults, can be changed by the GUI later
	Evoral::ControlSet::Controls controls = _model->controls();
	for (Evoral::ControlSet::Controls::iterator c = controls.begin(); c != controls.end(); ++c) {
		(*c).second->list()->set_interpolation(
			// to be enabled when ControlList::rt_safe_earliest_event_linear_unlocked works properly
			#if LINEAR_INTERPOLATION_MODE_WORKS_PROPERLY
			EventTypeMap::instance().interpolation_of((*c).first));
			#else
			Evoral::ControlList::Discrete);
			#endif
	}
}


void
SMFSource::destroy_model()
{
	//cerr << _name << " destroying model " << _model.get() << endl;
	_model.reset();
}

void
SMFSource::flush_midi()
{
	Evoral::SMF::end_write();
}

