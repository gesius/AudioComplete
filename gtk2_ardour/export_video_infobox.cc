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

#include "ardour/session.h"
#include "export_video_infobox.h"
#include "i18n.h"

using namespace Gtk;
using namespace std;
using namespace PBD;
using namespace ARDOUR;

ExportVideoInfobox::ExportVideoInfobox (Session* s)
	: ArdourDialog (_("Video Export Info"))
	, showagain_checkbox (_("Don't show this dialog again. (Reset in Edit->Preferences)."))
{
	set_session (s);

	set_name ("ExportVideoInfobox");
	set_position (Gtk::WIN_POS_MOUSE);
	set_modal (true);
	set_skip_taskbar_hint (true);
	set_resizable (false);

	Gtk::Label* l;
	VBox* vbox = manage (new VBox);

	l = manage (new Label (_("<b>Video Export Info</b>"), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_use_markup ();
	vbox->pack_start (*l, false, true);
	l = manage (new Label (_("The video export is not recommended for mastering!\nThe video input file defaults to that listed from the video timeline, which may not the the best quality to start with. While 'ffmpeg' can produce high-quality files, this export lacks the possibility to tweak many settings. We recommend to use 'winff', 'devede' or 'dvdauthor' to mux & master. Nevertheless this video-export may come in handy to do quick snapshots, intermediates or online videos.\n\nThe file-format is determined by the extension you choose for the output file (.avi, .mov, .flv, .ogv; run 'ffmpeg -formats' for a full list of supported formats)\nNote: not all combinations do work, i.e., flv files require samplerates of 22.1kHz or 44.1kHz, mpeg containers can not be used with ac3 audio-codec, etc. If in doubt, use one of the presets."), Gtk::ALIGN_LEFT, Gtk::ALIGN_CENTER, false));
	l->set_size_request(700,-1);
	l->set_line_wrap();
	vbox->pack_start (*l, false, true,4);

	vbox->pack_start (*(manage (new  HSeparator())), true, true, 2);
	vbox->pack_start (showagain_checkbox, false, true, 2);

	get_vbox()->set_spacing (4);
	get_vbox()->pack_start (*vbox, false, false);

	showagain_checkbox.set_active(false);
	show_all_children ();
	add_button (Stock::OK, RESPONSE_ACCEPT);
}

ExportVideoInfobox::~ExportVideoInfobox ()
{
}
/*
void
ExportVideoInfobox::on_show ()
{
	Dialog::on_show ();
}
*/

#endif /* WITH_VIDEOTIMELINE */
