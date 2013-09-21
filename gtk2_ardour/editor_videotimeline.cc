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

#include <jack/types.h>

#include <glib/gstdio.h>

#include "ardour/profile.h"
#include "ardour/rc_configuration.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"

#include "ardour_ui.h"
#include "editor.h"
#include "canvas/rectangle.h"
#include "editing.h"
#include "audio_time_axis.h"
#include "video_image_frame.h"
#include "export_video_dialog.h"
#include "export_video_infobox.h"
#include "interthread_progress_window.h"

#include "pbd/openuri.h"
#include "i18n.h"

using namespace std;

void
Editor::set_video_timeline_height (const int h)
{
	if (videotl_bar_height == h) { return; }
	if (h < 2 || h > 8) { return; }
  videotl_bar_height = h;
	videotl_label.set_size_request (-1, (int)timebar_height * videotl_bar_height);
	ARDOUR_UI::instance()->video_timeline->set_height(videotl_bar_height * timebar_height);
	update_ruler_visibility();
}

void
Editor::update_video_timeline (bool flush)
{
	if (flush) {
		ARDOUR_UI::instance()->video_timeline->flush_local_cache();
	}
	if (!ruler_video_action->get_active()) return;
	ARDOUR_UI::instance()->video_timeline->update_video_timeline();
}

bool
Editor::is_video_timeline_locked ()
{
	return ARDOUR_UI::instance()->video_timeline->is_offset_locked();
}

void
Editor::set_video_timeline_locked (const bool l)
{
	ARDOUR_UI::instance()->video_timeline->set_offset_locked(l);
}

void
Editor::toggle_video_timeline_locked ()
{
	ARDOUR_UI::instance()->video_timeline->toggle_offset_locked();
}

void
Editor::embed_audio_from_video (std::string path, framepos_t n)
{
	vector<std::string> paths;
	paths.push_back(path);
#if 0
	do_import (paths, Editing::ImportDistinctFiles, Editing::ImportAsTrack, ARDOUR::SrcBest, n);
#else
	current_interthread_info = &import_status;
	import_status.current = 1;
	import_status.total = paths.size ();
	import_status.all_done = false;

	ImportProgressWindow ipw (&import_status, _("Import"), _("Cancel Import"));
	ipw.show ();

	boost::shared_ptr<ARDOUR::Track> track;
	bool ok = (import_sndfiles (paths, Editing::ImportDistinctFiles, Editing::ImportAsTrack, ARDOUR::SrcBest, n, 1, 1, track, false) == 0);
	if (ok && track) {
		boost::shared_ptr<ARDOUR::Playlist> pl = track->playlist();
		pl->find_next_region(n, ARDOUR::End, 0)->set_video_locked(true);
		_session->save_state ("");
	}

	import_status.all_done = true;
#endif
	::g_unlink(path.c_str());
}

void
Editor::export_video ()
{
	if (ARDOUR::Config->get_show_video_export_info()) {
		ExportVideoInfobox infobox (_session);
		Gtk::ResponseType rv = (Gtk::ResponseType) infobox.run();
		if (infobox.show_again()) {
			ARDOUR::Config->set_show_video_export_info(false);
		}
		switch (rv) {
			case GTK_RESPONSE_YES:
				PBD::open_uri (ARDOUR::Config->get_reference_manual_url() + "/video-timeline/operations/#export");
				break;
			default:
				break;
		}
	}
	ExportVideoDialog dialog (_session, get_selection().time);
	Gtk::ResponseType r = (Gtk::ResponseType) dialog.run();
	(void) r; // keep gcc quiet
	dialog.hide();
}
