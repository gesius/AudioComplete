/*
    Copyright (C) 2010 Paul Davis
    Author: Robin Gareus <robin@gareus.org>

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
#ifdef WITH_VIDEOTIMELINE

#include "pbd/file_utils.h"
#include "gui_thread.h"
#include "ardour_ui.h"

#include <stdio.h>
#include "public_editor.h"
#include "video_monitor.h"

#include "i18n.h"

using namespace std;

VideoMonitor::VideoMonitor (PublicEditor *ed, std::string xjadeo_bin_path)
	: editor (ed)
{
	manually_seeked_frame = 0;
	fps =0.0; // = _session->timecode_frames_per_second();
	sync_by_manual_seek = false;
	_restore_settings_mask = 0;
	clock_connection = sigc::connection();
	debug_enable = false;

	process = new SystemExec(xjadeo_bin_path, X_("-R"));
	process->ReadStdout.connect (*this, invalidator (*this), boost::bind (&VideoMonitor::parse_output, this, _1 ,_2), gui_context());
	process->Terminated.connect (*this, invalidator (*this), boost::bind (&VideoMonitor::terminated, this), gui_context());
}

VideoMonitor::~VideoMonitor ()
{
	if (clock_connection.connected()) {
		clock_connection.disconnect();
	}
	delete process;
}

bool
VideoMonitor::start ()
{
	if (is_started()) {
		return true;
	}

	manually_seeked_frame = 0;
	sync_by_manual_seek = false;
	if (clock_connection.connected()) { clock_connection.disconnect(); }

	if (process->start(debug_enable?2:1)) {
		return false;
	}
	return true;
}

void
VideoMonitor::quit ()
{
	if (!is_started()) return;
	process->write_to_stdin("get windowsize\n");
	process->write_to_stdin("get windowpos\n");
	process->write_to_stdin("get letterbox\n");
	process->write_to_stdin("get fullscreen\n");
	process->write_to_stdin("get ontop\n");
	process->write_to_stdin("get offset\n");
	process->write_to_stdin("get osdcfg\n");
	process->write_to_stdin("quit\n");
#if 1
	/* wait for replies to the config requests above.
	 * the 'quit' command should result in process termination
	 * but in case it fails (communication failure, SIGSTOP, ??)
	 * here's a timeout..
	 */
	int timeout = 40;
	while (is_started() && --timeout) {
		usleep(50000);
	}
	if (timeout == 0) {
		printf("xjadeo connection: time-out. session may not be saved.\n");
	}
#endif
	process->terminate();
	if (clock_connection.connected()) { clock_connection.disconnect(); }
}

void
VideoMonitor::open (std::string filename)
{
	if (!is_started()) return;
	manually_seeked_frame = 0;
	sync_by_manual_seek = false;
	process->write_to_stdin("load " + filename + "\n");
	process->write_to_stdin("set fps -1\n");
	process->write_to_stdin("window resize 100%\n");
	process->write_to_stdin("window ontop on\n");
	process->write_to_stdin("set seekmode 1\n");
	process->write_to_stdin("set override 47\n");
	process->write_to_stdin("window letterbox on\n");
	process->write_to_stdin("osd mode 10\n");
	for(XJSettings::const_iterator it = xjadeo_settings.begin(); it != xjadeo_settings.end(); ++it) {
		if (skip_setting(it->first)) { continue; }
		process->write_to_stdin(it->first + " " + it->second + "\n");
	}
	xjadeo_sync_setup();
}

bool
VideoMonitor::skip_setting (std::string which)
{
	if (_restore_settings_mask & XJ_OSD && which == "osd mode") { return true; }
	if (_restore_settings_mask & XJ_LETTERBOX && which == "window letterbox") { return true; }
	if (_restore_settings_mask & XJ_WINDOW_SIZE && which == "window size") { return true; }
	if (_restore_settings_mask & XJ_WINDOW_POS && which == "window xy") { return true; }
	if (_restore_settings_mask & XJ_WINDOW_ONTOP && which == "window ontop") { return true; }
	if (_restore_settings_mask & XJ_LETTERBOX && which == "window letterbox") { return true; }
	if (_restore_settings_mask & XJ_OFFSET && which == "set offset") { return true; }
	if (_restore_settings_mask & XJ_FULLSCREEN && which == "window zoom") { return true; }
	return false;
}

bool
VideoMonitor::is_started ()
{
	return process->is_running();
}

void
VideoMonitor::parse_output (std::string d, size_t s)
{
	std::string line = d;
	std::string::size_type start = 0;
	std::string::size_type end = 0;

	while (1) {
		end = d.find('\n', start);
		if (end == std::string::npos) break;
		line = d.substr(start,end-start);
		start=end+1;
		if (line.length() <4 || line.at(0)!='@') continue;
#if 1 /* DEBUG */
		if (debug_enable) {
			printf("xjadeo: '%s'\n", line.c_str());
		}
#endif
		int status = atoi(line.substr(1,3).c_str());
		switch(status / 100) {
			case 4: /* errors */
				if (status == 403) {
					PBD::warning << _("Video Monitor: File Not Found.") << endmsg;
					/* check: we should only write from the main thread.
					 * However it should not matter for 'quit'.
					 */
					process->write_to_stdin("quit\n");
				}
			case 1: /* requested async notifications */
			case 3: /* warnings ; command succeeded, but status is negative. */
				break;
			case 2:
/* replies:
 * 201: var=<int>
 * 202: var=<double>
 * 210: var=<int>x<int>
 * 220: var=<string>
 * 228: var=<smpte-string>
 */
			{
				std::string::size_type equalsign = line.find('=');
				std::string::size_type comment = line.find('#');
				if (comment != std::string::npos) { line = line.substr(0,comment); }
				if (equalsign != std::string::npos) {
					std::string key = line.substr(5, equalsign - 5);
					std::string value = line.substr(equalsign + 1);
#if 0 /* DEBUG */
					std::cout << "parsed: " << key << " => " << value << std::endl;
#endif
					       if(key ==  "windowpos") {
						xjadeo_settings["window xy"] = value;
					} else if(key ==  "windowsize") {
						xjadeo_settings["window size"] = value;
					} else if(key ==  "windowontop") {
						xjadeo_settings["window ontop"] = value;
					} else if(key ==  "fullscreen") {
						xjadeo_settings["window zoom"] = value;
					} else if(key ==  "letterbox") {
						xjadeo_settings["window letterbox"] = value;
					} else if(key ==  "osdmode") {
						xjadeo_settings["osd mode"] = value;
					} else if(key ==  "offset") {
						xjadeo_settings["set offset"] = value;
					}
				}
			}
				break;
			default:
				break;
		}
	}
}

void
VideoMonitor::terminated ()
{
	save_session();
	Terminated();
}

void
VideoMonitor::save_session ()
{
	if (!_session) { return; }
	bool is_dirty = false;

	XMLNode* prev = _session->extra_xml (X_("XJSettings"));
	XMLNode* node = new XMLNode(X_("XJSettings"));
	XMLNodeList nlist;
	if (!prev) { is_dirty = true; }
	else { nlist = prev->children(); }

	for(XJSettings::const_iterator it = xjadeo_settings.begin(); it != xjadeo_settings.end(); ++it) {
	  XMLNode* child = node->add_child (X_("XJSetting"));
		child->add_property (X_("k"), it->first);
		child->add_property (X_("v"), it->second);
		if (!is_dirty) {
			bool found = false;
			XMLNodeConstIterator i;
			for (i = nlist.begin(); i != nlist.end(); ++i) {
				if ((*i)->property(X_("k"))->value() == it->first &&
				    (*i)->property(X_("v"))->value() == it->second ) {
					found=true;
					break;
				}
			}
			if (!found) {is_dirty = true;}
		}
	}

	if (is_dirty) {
	  _session->add_extra_xml (*node);
	  _session->set_dirty ();
	}
}


void
VideoMonitor::set_session (ARDOUR::Session *s)
{
	SessionHandlePtr::set_session (s);
	if (!_session) { return; }
	_session->config.ParameterChanged.connect (*this, invalidator (*this), ui_bind (&VideoMonitor::parameter_changed, this, _1), gui_context());
	XMLNode* node = _session->extra_xml (X_("XJSettings"));
	if (!node) { return;}
	xjadeo_settings.clear();

	XMLNodeList nlist = node->children();
	XMLNodeConstIterator niter;
	for (niter = nlist.begin(); niter != nlist.end(); ++niter) {
		xjadeo_settings[(*niter)->property(X_("k"))->value()] = (*niter)->property(X_("v"))->value();
  }
}

void
VideoMonitor::clear_session_state ()
{
	xjadeo_settings.clear();
	if (!_session) { return; }
	XMLNode* node = new XMLNode(X_("XJSettings"));
	_session->add_extra_xml (*node);
	_session->set_dirty ();
}

bool
VideoMonitor::set_custom_setting (const std::string k, const std::string v)
{
	xjadeo_settings[k] = v;
	return true; /* TODO: check if key is valid */
}

const std::string
VideoMonitor::get_custom_setting (const std::string k)
{
	return (xjadeo_settings[k]);
}
#define NO_OFFSET (1<<31) //< skip setting or modifying offset --  TODO check ARDOUR::frameoffset_t max value.
void
VideoMonitor::srsupdate ()
{
	if (!_session) { return; }
	if (editor->dragging_playhead()) { return ;}
	manual_seek(_session->audible_frame(), false, NO_OFFSET);
}

void
VideoMonitor::set_offset (ARDOUR::frameoffset_t offset)
{
	if (!is_started()) { return; }
	if (!_session) { return; }
	if (offset == NO_OFFSET ) { return; }

	framecnt_t video_frame_offset;
	framecnt_t audio_frame_rate;
	if (_session->config.get_videotimeline_pullup()) {
		audio_frame_rate = _session->frame_rate();
	} else {
		audio_frame_rate = _session->nominal_frame_rate();
	}

	/* Note: pull-up/down are applied here: frame_rate() vs. nominal_frame_rate() */
	if (_session->config.get_use_video_file_fps()) {
		video_frame_offset = floor(offset * fps / audio_frame_rate);
	} else {
		video_frame_offset = floor(offset * _session->timecode_frames_per_second() / audio_frame_rate);
	}

	// TODO remember if changed..
	std::ostringstream osstream1; osstream1 << -1 * video_frame_offset;
	process->write_to_stdin("set offset " + osstream1.str() + "\n");
}

void
VideoMonitor::manual_seek (framepos_t when, bool force, ARDOUR::frameoffset_t offset)
{
	if (!is_started()) { return; }
	if (!_session) { return; }
	framecnt_t video_frame;
	framecnt_t audio_frame_rate;
	if (_session->config.get_videotimeline_pullup()) {
		audio_frame_rate = _session->frame_rate();
	} else {
		audio_frame_rate = _session->nominal_frame_rate();
	}

	/* Note: pull-up/down are applied here: frame_rate() vs. nominal_frame_rate() */
	if (_session->config.get_use_video_file_fps()) {
		video_frame = floor(when * fps / audio_frame_rate);
	} else {
		video_frame = floor(when * _session->timecode_frames_per_second() / audio_frame_rate);
	}
	if (video_frame < 0 ) video_frame = 0;

	if (video_frame == manually_seeked_frame) { return; }
	manually_seeked_frame = video_frame;

#if 0 /* DEBUG */
	std::cout <<"seek: " << video_frame << std::endl;
#endif
	std::ostringstream osstream; osstream << video_frame;
	process->write_to_stdin("seek " + osstream.str() + "\n");

	set_offset(offset);
}

void
VideoMonitor::parameter_changed (std::string const & p)
{
	if (!is_started()) { return; }
	if (!_session) { return; }
	if (p != "external-sync" && p != "sync-source") {
		return;
	}
	xjadeo_sync_setup();
}

void
VideoMonitor::xjadeo_sync_setup ()
{
	if (!is_started()) { return; }
	if (!_session) { return; }

	bool my_manual_seek = true;
	if (_session->config.get_external_sync()) {
		if (ARDOUR::Config->get_sync_source() == ARDOUR::JACK)
			my_manual_seek = false;
	}

	if (my_manual_seek != sync_by_manual_seek) {
		if (sync_by_manual_seek) {
			if (clock_connection.connected()) {
				clock_connection.disconnect();
			}
			process->write_to_stdin("jack connect\n");
		} else {
			process->write_to_stdin("jack disconnect\n");
			clock_connection = ARDOUR_UI::SuperRapidScreenUpdate.connect (sigc::mem_fun (*this, &VideoMonitor::srsupdate));
		}
		sync_by_manual_seek = my_manual_seek;
	}
}
#endif /* WITH_VIDEOTIMELINE */
