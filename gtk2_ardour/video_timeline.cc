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

#include <sigc++/bind.h>
#include "ardour/tempo.h"

#include "pbd/file_utils.h"
#include "ardour/session_directory.h"

#include "ardour_ui.h"
#include "public_editor.h"
#include "gui_thread.h"
#include "utils.h"
#include "canvas_impl.h"
#include "simpleline.h"
#include "utils_videotl.h"
#include "rgb_macros.h"
#include "video_timeline.h"

#include <gtkmm2ext/utils.h>
#include <pthread.h>
#include <curl/curl.h>

#include "i18n.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Timecode;

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

VideoTimeLine::VideoTimeLine (PublicEditor *ed, ArdourCanvas::Group *vbg, int initial_height)
	: editor (ed)
		, videotl_bar_group(vbg)
		, bar_height(initial_height)
{
	video_start_offset = 0L;
	video_offset = 0L;
	video_offset_p = 0L;
	video_duration = 0L;
	auto_set_session_fps = false;
	video_offset_lock = false;
	video_aspect_ratio = 4.0/3.0;
	open_video_monitor_dialog = 0;
	Config->ParameterChanged.connect (*this, invalidator (*this), ui_bind (&VideoTimeLine::parameter_changed, this, _1), gui_context());
	video_server_url = Config->get_video_server_url();
	server_docroot   = Config->get_video_server_docroot();
	video_filename = "";
	local_file = true;
	video_file_fps = 25.0;
	flush_frames = false;
	vmonitor=0;
	reopen_vmonitor=false;
	find_xjadeo();

	VtlUpdate.connect (*this, invalidator (*this), boost::bind (&PublicEditor::queue_visual_videotimeline_update, editor), gui_context());
	GuiUpdate.connect (*this, invalidator (*this), boost::bind (&VideoTimeLine::gui_update, this, _1), gui_context());
}

VideoTimeLine::~VideoTimeLine ()
{
	close_session();
}

/* close and save settings */
void
VideoTimeLine::save_session ()
{
	if (!_session) {
		return;
	}

	LocaleGuard lg (X_("POSIX"));

	bool is_dirty = false;

	XMLNode* prev = _session->extra_xml (X_("Videomonitor"));

	/* remember if vmonitor was open.. */
	XMLNode* node = new XMLNode(X_("Videomonitor"));

	node->add_property (X_("active"), (vmonitor && vmonitor->is_started())?"yes":"no");
	if (!prev || !(prev->property (X_("active")) && prev->property (X_("active"))->value() == node->property(X_("active"))->value()) ){
		_session->add_extra_xml (*node);
		is_dirty=true; // TODO not if !prev && value==default
	}

	/* VTL settings */
	node = _session->extra_xml (X_("Videotimeline"));

	if (node) {
		if (!(node->property(X_("id")) && node->property(X_("id"))->value() == id().to_s())) {
			node->add_property (X_("id"), id().to_s());
			is_dirty=true;
		}
	}

	/* remember timeline height.. */
	if (node) {
		int h = editor->get_videotl_bar_height();
		if (!(node->property(X_("Height")) && atoi(node->property(X_("Height"))->value().c_str())==h)) {
			node->add_property (X_("Height"), h);
			is_dirty=true;
		}
	}

	/* save video-offset-lock */
	if (node) {
		if (!(node->property(X_("VideoOffsetLock")) && atoi(node->property(X_("VideoOffsetLock"))->value().c_str())==video_offset_lock)) {
			node->add_property (X_("VideoOffsetLock"), video_offset_lock?X_("1"):X_("0"));
			is_dirty=true;
		}
	}
	/* save video-offset */
	if (node) {
		if (!(node->property(X_("VideoOffset")) && atoll(node->property(X_("VideoOffset"))->value().c_str())==video_offset)) {
			node->add_property (X_("VideoOffset"), video_offset);
			is_dirty=true;
		}
	}

	/* save 'auto_set_session_fps' */
	if (node) {
		if (!(node->property(X_("AutoFPS")) && atoi(node->property(X_("AutoFPS"))->value().c_str())==auto_set_session_fps)) {
			node->add_property (X_("AutoFPS"), auto_set_session_fps?X_("1"):X_("0"));
			is_dirty=true;
		}
	}
	if (is_dirty) {
		_session->set_dirty ();
	}
}

/* close and save settings */
void
VideoTimeLine::close_session ()
{
	close_video_monitor();
	save_session();

	remove_frames();
	video_filename = "";
	video_duration = 0L;
}

/** load settings from session */
void
VideoTimeLine::set_session (ARDOUR::Session *s)
{
	SessionHandlePtr::set_session (s);
	if (!_session) { return ; }

	LocaleGuard lg (X_("POSIX"));

	XMLNode* node = _session->extra_xml (X_("Videotimeline"));
	if (node) {
		ARDOUR_UI::instance()->start_video_server((Gtk::Window*)0, false);

		set_id(*node);

		const XMLProperty* proph = node->property (X_("Height"));
		if (proph) {
			editor->set_video_timeline_height(atoi(proph->value().c_str()));
		}
#if 0 /* TODO THINK: set FPS first time only ?! */
		const XMLProperty* propasfps = node->property (X_("AutoFPS"));
		if (propasfps) {
			auto_set_session_fps = atoi(propasfps->value().c_str())?true:false;
		}
#endif

		const XMLProperty* propoffset = node->property (X_("VideoOffset"));
		if (propoffset) {
			video_offset = atoll(propoffset->value().c_str());
			video_offset_p = video_offset;
		}

		const XMLProperty* proplock = node->property (X_("VideoOffsetLock"));
		if (proplock) {
			video_offset_lock = atoi(proplock->value().c_str())?true:false;
		}

		const XMLProperty* localfile = node->property (X_("LocalFile"));
		if (localfile) {
			local_file = atoi(localfile->value().c_str())?true:false;
		}

		const XMLProperty* propf = node->property (X_("Filename"));
		video_file_info(propf->value(), local_file);
	}

	node = _session->extra_xml (X_("Videomonitor"));
	if (node) {
		const XMLProperty* prop = node->property (X_("active"));
		if (prop->value() == "yes" && found_xjadeo() && !video_filename.empty() && local_file) {
			open_video_monitor(false);
		}
	}

	_session->register_with_memento_command_factory(id(), this);
	_session->config.ParameterChanged.connect (*this, invalidator (*this), ui_bind (&VideoTimeLine::parameter_changed, this, _1), gui_context());
}

void
VideoTimeLine::save_undo ()
{
	video_offset_p = video_offset;
}

int
VideoTimeLine::set_state (const XMLNode& node, int /*version*/)
{
	LocaleGuard lg (X_("POSIX"));
	const XMLProperty* propoffset = node.property (X_("VideoOffset"));
	if (propoffset) {
		video_offset = atoll(propoffset->value().c_str());
	}
	ARDOUR_UI::instance()->flush_videotimeline_cache(true);
	return 0;
}

XMLNode&
VideoTimeLine::get_state ()
{
	XMLNode* node = new XMLNode (X_("Videotimeline"));
	LocaleGuard lg (X_("POSIX"));
	node->add_property (X_("VideoOffset"), video_offset_p);
	return *node;
}

void
VideoTimeLine::remove_frames ()
{
	for (VideoFrames::iterator i = video_frames.begin(); i != video_frames.end(); ++i ) {
		VideoImageFrame *frame = (*i);
		delete frame;
		(*i) = 0;
	}
	video_frames.clear();
}

VideoImageFrame *
VideoTimeLine::get_video_frame (framepos_t vfn, int cut, int rightend)
{
	if (vfn==0) cut=0;
	for (VideoFrames::iterator i = video_frames.begin(); i != video_frames.end(); ++i) {
		VideoImageFrame *frame = (*i);
		if (abs(frame->get_video_frame_number()-vfn)<=cut
		    && frame->get_rightend() == rightend) { return frame; }
	}
	return 0;
}

float
VideoTimeLine::get_apv()
{
	// XXX: dup code - TODO use this fn in update_video_timeline()
	float apv = -1; /* audio frames per video frame; */
	if (!_session) return apv;

	if (_session->config.get_use_video_file_fps()) {
		if (video_file_fps == 0 ) return apv;
	} else {
		if (_session->timecode_frames_per_second() == 0 ) return apv;
	}

	if (_session->config.get_videotimeline_pullup()) {
		apv = _session->frame_rate();
	} else {
		apv = _session->nominal_frame_rate();
	}
	if (_session->config.get_use_video_file_fps()) {
		apv /= video_file_fps;
	} else {
		apv /= _session->timecode_frames_per_second();
	}
	return apv;
}

void
VideoTimeLine::update_video_timeline()
{
	if (!_session) return;

	if (_session->config.get_use_video_file_fps()) {
		if (video_file_fps == 0 ) return;
	} else {
		if (_session->timecode_frames_per_second() == 0 ) return;
	}

	double frames_per_unit = editor->unit_to_frame(1.0);
	framepos_t leftmost_frame =  editor->leftmost_position();

	/* Outline:
	 * 1) calculate how many frames there should be in current zoom (plus 1 page on each side)
	 * 2) calculate first frame and distance between video-frames (according to zoom)
	 * 3) destroy/add frames
	 * 4) reposition existing frames
	 * 5) assign framenumber to frames -> request/decode video.
	 */

	/* video-file and session properties */
	double display_vframe_width; /* unit: pixels ; width of one thumbnail in the timeline */
	float apv; /* audio frames per video frame; */
	framepos_t leftmost_video_frame; /* unit: video-frame number ; temporary var -> vtl_start */

	/* variables needed to render videotimeline -- what needs to computed first */
	framepos_t vtl_start; /* unit: audio-frames ; first displayed video-frame */
	framepos_t vtl_dist;  /* unit: audio-frames ; distance between displayed video-frames */
	unsigned int visible_video_frames; /* number of frames that fit on current canvas */

	if (_session->config.get_videotimeline_pullup()) {
		apv = _session->frame_rate();
	} else {
		apv = _session->nominal_frame_rate();
	}
	if (_session->config.get_use_video_file_fps()) {
		apv /= video_file_fps;
	} else {
		apv /= _session->timecode_frames_per_second();
	}

	display_vframe_width = bar_height * video_aspect_ratio;

	if (apv > frames_per_unit * display_vframe_width) {
		/* high-zoom: need space between successive video-frames */
		vtl_dist = rint(apv);
	} else {
		/* continous timeline: skip video-frames */
		vtl_dist = ceil(display_vframe_width * frames_per_unit / apv) * apv;
	}

	assert (vtl_dist > 0);
	assert (apv > 0);

#define GOFFSET (video_offset)

	leftmost_video_frame = floor (floor((leftmost_frame - video_start_offset - GOFFSET ) / vtl_dist) * vtl_dist / apv);

	vtl_start = rint (GOFFSET + video_start_offset + leftmost_video_frame * apv);
	visible_video_frames = 2 + ceil(editor->current_page_frames() / vtl_dist); /* +2 left+right partial frames */

	/* expand timeline (cache next/prev page images) */
	vtl_start -= visible_video_frames * vtl_dist;
	visible_video_frames *=3;

	if (vtl_start < GOFFSET ) {
		visible_video_frames += ceil(vtl_start/vtl_dist);
		vtl_start = GOFFSET;
	}

	/* apply video-file constraints */
	if (vtl_start > video_start_offset + video_duration + GOFFSET ) {
		visible_video_frames = 0;
	}
	/* TODO optimize: compute rather than iterate */
	while (visible_video_frames > 0 && vtl_start + (visible_video_frames-1) * vtl_dist >= video_start_offset + video_duration + GOFFSET) {
		--visible_video_frames;
	}

	if (flush_frames) {
		remove_frames();
		flush_frames=false;
	}

	while (video_frames.size() < visible_video_frames) {
		VideoImageFrame *frame;
		frame = new VideoImageFrame(*editor, *videotl_bar_group, display_vframe_width, bar_height, video_server_url, translated_filename());
		frame->ImgChanged.connect (*this, invalidator (*this), boost::bind (&PublicEditor::queue_visual_videotimeline_update, editor), gui_context());
		video_frames.push_back(frame);
	}

	VideoFrames outdated_video_frames;
	std::list<int> remaining;

	outdated_video_frames = video_frames;

#if 1
	/* when zoomed out, ignore shifts by +-1 frame
	 * which can occur due to rounding errors when
	 * scrolling to a new leftmost-audio frame.
	 */
	int cut =1;
	if (vtl_dist/apv < 3.0) cut =0;
#else
	int cut =0;
#endif

	for (unsigned int vfcount=0; vfcount < visible_video_frames; ++vfcount){
		framepos_t vfpos = vtl_start + vfcount * vtl_dist; /* unit: audio-frames */
		framepos_t vframeno = rint ( (vfpos - GOFFSET) / apv); /* unit: video-frames */
		vfpos = (vframeno * apv ) + GOFFSET; /* audio-frame  corresponding to /rounded/ video-frame */

		int rightend = -1; /* unit: pixels */
		if (vfpos + vtl_dist > video_start_offset + video_duration + GOFFSET) {
			rightend = display_vframe_width * (video_start_offset + video_duration + GOFFSET - vfpos) / vtl_dist;
			//printf("lf(e): %lu\n", vframeno); // XXX
		}
		VideoImageFrame * frame = get_video_frame(vframeno, cut, rightend);
		if (frame) {
		  frame->set_position(vfpos-leftmost_frame);
			outdated_video_frames.remove(frame);
		} else {
			remaining.push_back(vfcount);
		}
	}

	for (VideoFrames::iterator i = outdated_video_frames.begin(); i != outdated_video_frames.end(); ++i ) {
		VideoImageFrame *frame = (*i);
		if (remaining.empty()) {
		  frame->set_position(-2 * vtl_dist); /* move off screen */
		} else {
			int vfcount=remaining.front();
			remaining.pop_front();
			framepos_t vfpos = vtl_start + vfcount * vtl_dist; /* unit: audio-frames */
			framepos_t vframeno = rint ((vfpos - GOFFSET) / apv);  /* unit: video-frames */
			int rightend = -1; /* unit: pixels */
			if (vfpos + vtl_dist > video_start_offset + video_duration + GOFFSET) {
				rightend = display_vframe_width * (video_start_offset + video_duration + GOFFSET - vfpos) / vtl_dist;
				//printf("lf(n): %lu\n", vframeno); // XXX
			}
			frame->set_position(vfpos-leftmost_frame);
			frame->set_videoframe(vframeno, rightend);
		}
	}
}

std::string
VideoTimeLine::translated_filename ()
{
	if (!local_file){
		return video_filename;
	} else {
		return video_map_path(server_docroot, video_filename);
	}
}

bool
VideoTimeLine::video_file_info (std::string filename, bool local)
{

	local_file = local;
	if (filename.at(0) == G_DIR_SEPARATOR || !local_file) {
		video_filename = filename;
	}  else {
		video_filename = Glib::build_filename (_session->session_directory().video_path(), filename);
	}

	long long int _duration;
	double _start_offset;

	if (!video_query_info(
			video_server_url, translated_filename(),
			video_file_fps, _duration, _start_offset, video_aspect_ratio)) {
		warning << _("Parsing video file info failed. Is the Video Server running? Is the file readable by the Video Server? Does the docroot match? Is it a video file?") << endmsg;
		return false;
	}
	video_duration = _duration * _session->nominal_frame_rate() / video_file_fps;
	video_start_offset = _start_offset * _session->nominal_frame_rate();

	if (auto_set_session_fps && video_file_fps != _session->timecode_frames_per_second()) {
		switch ((int)floorf(video_file_fps*1000.0)) {
			case 23976:
				_session->config.set_timecode_format(timecode_23976);
				break;
			case 24000:
				_session->config.set_timecode_format(timecode_24);
				break;
			case 24975:
			case 24976:
				_session->config.set_timecode_format(timecode_24976);
				break;
			case 25000:
				_session->config.set_timecode_format(timecode_25);
				break;
			case 29970:
				_session->config.set_timecode_format(timecode_2997drop);
				break;
			case 30000:
				_session->config.set_timecode_format(timecode_30);
				break;
			case 59940:
				_session->config.set_timecode_format(timecode_5994);
				break;
			case 60000:
				_session->config.set_timecode_format(timecode_60);
				break;
			default:
				warning << _("Failed to set session-framerate: ") << video_file_fps << _(" does not have a corresponding option setting in Ardour.") << endmsg; /* TODO: gettext arg */
				break;
		}
		_session->config.set_video_pullup(0); /* TODO only set if set_timecode_format() was successful ?!*/
	}
	if (video_file_fps != _session->timecode_frames_per_second()) {
		warning << _("Video file's framerate is not equal to Ardour session timecode's framerate: ")
		        << video_file_fps << _(" vs ") << _session->timecode_frames_per_second() << endmsg;
	}
	flush_local_cache ();

	_session->maybe_update_session_range(
			MAX(get_offset(), 0),
			MAX(get_offset() + get_duration(), 0)
			);

	if (found_xjadeo() && local_file) {
		GuiUpdate("set-xjadeo-sensitive-on");
		if (vmonitor && vmonitor->is_started()) {
			vmonitor->set_fps(video_file_fps);
			vmonitor->open(video_filename);
		}
	} else if (!local_file) {
#if 1 /* temp debug/devel message */
		// TODO - call xjremote remotely.
		printf("the given video file can not be accessed on localhost, video monitoring is not currently supported for this case\n");
		GuiUpdate("set-xjadeo-sensitive-off");
#endif
	}
	VtlUpdate();
	return true;
}

bool
VideoTimeLine::check_server ()
{
	bool ok = false;
	char url[1024];
	snprintf(url, sizeof(url), "%s%sstatus"
			, video_server_url.c_str()
			, (video_server_url.length()>0 && video_server_url.at(video_server_url.length()-1) == '/')?"":"/"
			);
	char *res=curl_http_get(url, NULL);
	if (res) {
		if (strstr(res, "status: ok, online.")) { ok = true; }
		free(res);
	}
	return ok;
}

void
VideoTimeLine::gui_update(std::string const & t) {
	/* this is to be called via GuiUpdate() only. */
	ENSURE_GUI_THREAD (*this, &VideoTimeLine::queue_visual_videotimeline_update)
	if (t == "videotimeline-update") {
		editor->queue_visual_videotimeline_update();
	} else if (t == "set-xjadeo-active-off") {
		editor->toggle_xjadeo_proc(0);
	} else if (t == "set-xjadeo-active-on") {
		editor->toggle_xjadeo_proc(1);
	} else if (t == "set-xjadeo-sensitive-on") {
		editor->set_xjadeo_sensitive(true);
	} else if (t == "set-xjadeo-sensitive-off") {
		editor->toggle_xjadeo_proc(0);
		//close_video_monitor();
		editor->set_xjadeo_sensitive(false);
	}
}

void
VideoTimeLine::set_height (int height) {
	bar_height = height;
	flush_local_cache();
}

void
VideoTimeLine::vmon_update () {
	if (vmonitor && vmonitor->is_started()) {
		vmonitor->set_offset( GOFFSET); // TODO proper re-init xjadeo w/o restart not just offset.
	}
}

void
VideoTimeLine::flush_local_cache () {
	flush_frames = true;
	vmon_update();
}

void
VideoTimeLine::flush_cache () {
	flush_local_cache();
	char url[1024];
	snprintf(url, sizeof(url), "%s%sadmin/flush_cache"
			, video_server_url.c_str()
			, (video_server_url.length()>0 && video_server_url.at(video_server_url.length()-1) == '/')?"":"/"
			);
	char *res=curl_http_get(url, NULL);
	if (res) {
		free (res);
	}
	if (vmonitor && vmonitor->is_started()) {
		reopen_vmonitor=true;
		vmonitor->quit();
	}
	video_file_info(video_filename, local_file);
}

/* config */
void
VideoTimeLine::parameter_changed (std::string const & p)
{
	if (p == "video-server-url") {
		set_video_server_url (Config->get_video_server_url ());
	} else if (p == "video-server-docroot") {
		set_video_server_docroot (Config->get_video_server_docroot ());
	}
	if (p == "use-video-file-fps" || p == "videotimeline-pullup" ) { /* session->config parameter */
		VtlUpdate();
	}
}

void
VideoTimeLine::set_video_server_url(std::string vsu) {
	flush_local_cache ();
	video_server_url = vsu;
	VtlUpdate();
}

void
VideoTimeLine::set_video_server_docroot(std::string vsr) {
	flush_local_cache ();
	server_docroot = vsr;
	VtlUpdate();
}

/* video-monitor for this timeline */
void
VideoTimeLine::find_xjadeo () {
	std::string xjadeo_file_path;
	if (getenv("XJREMOTE")) {
		_xjadeo_bin = strdup(getenv("XJREMOTE")); // XXX TODO: free it?!
	} else if (find_file_in_search_path (SearchPath(Glib::getenv("PATH")), X_("xjremote"), xjadeo_file_path)) {
		_xjadeo_bin = xjadeo_file_path;
	}
	else if (Glib::file_test(X_("/Applications/Jadeo.app/Contents/MacOS/xjremote"), Glib::FILE_TEST_EXISTS|Glib::FILE_TEST_IS_EXECUTABLE)) {
		_xjadeo_bin = X_("/Applications/Jadeo.app/Contents/MacOS/xjremote");
	}
	/* TODO: win32: allow to configure PATH to xjremote */
	else if (Glib::file_test(X_("C:\\Program Files\\xjadeo\\xjremote.exe"), Glib::FILE_TEST_EXISTS)) {
		_xjadeo_bin = X_("C:\\Program Files\\xjadeo\\xjremote.exe");
	}
	else if (Glib::file_test(X_("C:\\Program Files\\xjadeo\\xjremote.bat"), Glib::FILE_TEST_EXISTS)) {
		_xjadeo_bin = X_("C:\\Program Files\\xjadeo\\xjremote.bat");
	}
	else  {
		_xjadeo_bin = X_("");
		warning << _("Video-monitor 'xjadeo' was not found. Please install http://xjadeo.sf.net/ "
				"(a custom path to xjadeo can be specified by setting the XJREMOTE environment variable. "
				"It should point to an application compatible with xjadeo's remote-control interface 'xjremote').")
			<< endmsg;
	}
}

void
VideoTimeLine::open_video_monitor(bool interactive) {
	if (!found_xjadeo()) return;
	if (!vmonitor) {
		vmonitor = new VideoMonitor(editor, _xjadeo_bin);
		vmonitor->set_session(_session);
		vmonitor->Terminated.connect (sigc::mem_fun (*this, &VideoTimeLine::terminated_video_monitor));
	} else if (vmonitor->is_started()) {
		return;
	}

	int xj_settings_mask = vmonitor->restore_settings_mask();
	if (_session) {
		/* load mask from Session */
		XMLNode* node = _session->extra_xml (X_("XJRestoreSettings"));
		if (node) {
			const XMLProperty* prop = node->property (X_("mask"));
			if (prop) {
				xj_settings_mask = atoi(prop->value().c_str());
			}
		}
	}

	if (interactive && Config->get_video_monitor_setup_dialog()) {
		if (open_video_monitor_dialog == 0) {
			open_video_monitor_dialog = new OpenVideoMonitorDialog(_session);
		}
		if (open_video_monitor_dialog->is_visible()) {
			return;
		}
		open_video_monitor_dialog->setup_settings_mask(xj_settings_mask);
		open_video_monitor_dialog->set_filename(video_filename);
		Gtk::ResponseType r = (Gtk::ResponseType) open_video_monitor_dialog->run ();
		open_video_monitor_dialog->hide();
		if (r != Gtk::RESPONSE_ACCEPT) {
			GuiUpdate("set-xjadeo-active-off");
			return;
		}

		if (_session && (xj_settings_mask != open_video_monitor_dialog->xj_settings_mask()) ) {
			/* save mask to Session */
			XMLNode* node = new XMLNode(X_("XJRestoreSettings"));
			node->add_property (X_("mask"), (const long) open_video_monitor_dialog->xj_settings_mask() );
			_session->add_extra_xml (*node);
			_session->set_dirty ();
		}

		if (open_video_monitor_dialog->show_again()) {
			Config->set_video_monitor_setup_dialog(false);
		}
#if 1
		vmonitor->set_debug(open_video_monitor_dialog->enable_debug());
#endif
		vmonitor->restore_settings_mask(open_video_monitor_dialog->xj_settings_mask());
	} else {
		vmonitor->restore_settings_mask(xj_settings_mask);
	}


	if (!vmonitor->start()) {
		warning << "launching xjadeo failed.." << endmsg;
		close_video_monitor();
	} else {
		GuiUpdate("set-xjadeo-active-on");
		vmonitor->set_fps(video_file_fps);
		vmonitor->open(video_filename);
	}
}

void
VideoTimeLine::close_video_monitor() {
	if (vmonitor && vmonitor->is_started()) {
		vmonitor->quit();
	}
}

void
VideoTimeLine::terminated_video_monitor () {
	if (vmonitor) {
		delete vmonitor;
	}
	GuiUpdate("set-xjadeo-active-off");
	vmonitor=0;
  if (reopen_vmonitor) {
		reopen_vmonitor=false;
		open_video_monitor(false);
	}
}

/*
void
VideoTimeLine::clear_video_monitor_session_state ()
{
	if (vmonitor) {
		vmonitor->clear_session_state();
	} else {
	  if (!_session) { return; }
		XMLNode* node = new XMLNode(X_("XJSettings"));
		_session->add_extra_xml (*node);
		_session->set_dirty ();
	}
}
*/

void
VideoTimeLine::manual_seek_video_monitor (framepos_t pos)
{
	if (!vmonitor) { return; }
	if (!vmonitor->is_started()) { return; }
	if (!vmonitor->synced_by_manual_seeks()) { return; }
	vmonitor->manual_seek(pos, false, GOFFSET); // XXX -> set offset in xjadeo
}

#endif /* WITH_VIDEOTIMELINE */
