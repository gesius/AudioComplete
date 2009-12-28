/*
    Copyright (C) 2000-2009 Paul Davis

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

/* Note: public Editor methods are documented in public_editor.h */

#define __STDC_LIMIT_MACROS 1
#include <stdint.h>
#include <unistd.h>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>
#include <map>

#include "ardour_ui.h"
/*
 * ardour_ui.h include was moved to the top of the list
 * due to a conflicting definition of 'Style' between
 * Apple's MacTypes.h and BarController.
 */

#include <boost/none.hpp>

#include <sigc++/bind.h>

#include "pbd/convert.h"
#include "pbd/error.h"
#include "pbd/enumwriter.h"
#include "pbd/memento_command.h"
#include "pbd/unknown_type.h"

#include <glibmm/miscutils.h>
#include <gtkmm/image.h>
#include <gdkmm/color.h>
#include <gdkmm/bitmap.h>

#include <gtkmm2ext/grouped_buttons.h>
#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/tearoff.h>
#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/window_title.h>
#include <gtkmm2ext/choice.h>
#include <gtkmm2ext/cell_renderer_pixbuf_toggle.h>

#include "ardour/audio_diskstream.h"
#include "ardour/audio_track.h"
#include "ardour/audioplaylist.h"
#include "ardour/audioregion.h"
#include "ardour/location.h"
#include "ardour/midi_region.h"
#include "ardour/plugin_manager.h"
#include "ardour/profile.h"
#include "ardour/route_group.h"
#include "ardour/session_directory.h"
#include "ardour/session_route.h"
#include "ardour/session_state_utils.h"
#include "ardour/tempo.h"
#include "ardour/utils.h"
#include "ardour/session_playlists.h"

#include "control_protocol/control_protocol.h"

#include "editor.h"
#include "keyboard.h"
#include "marker.h"
#include "playlist_selector.h"
#include "audio_region_view.h"
#include "rgb_macros.h"
#include "selection.h"
#include "audio_streamview.h"
#include "time_axis_view.h"
#include "audio_time_axis.h"
#include "utils.h"
#include "crossfade_view.h"
#include "canvas-noevent-text.h"
#include "editing.h"
#include "public_editor.h"
#include "crossfade_edit.h"
#include "canvas_impl.h"
#include "actions.h"
#include "sfdb_ui.h"
#include "gui_thread.h"
#include "simpleline.h"
#include "rhythm_ferret.h"
#include "actions.h"
#include "tempo_lines.h"
#include "analysis_window.h"
#include "bundle_manager.h"
#include "global_port_matrix.h"
#include "editor_drag.h"
#include "editor_group_tabs.h"
#include "automation_time_axis.h"
#include "editor_routes.h"
#include "midi_time_axis.h"
#include "mixer_strip.h"
#include "editor_route_groups.h"
#include "editor_regions.h"
#include "editor_locations.h"
#include "editor_snapshots.h"

#include "i18n.h"

#ifdef WITH_CMT
#include "imageframe_socket_handler.h"
#endif

using namespace std;
using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Glib;
using namespace Gtkmm2ext;
using namespace Editing;

using PBD::internationalize;
using PBD::atoi;
using Gtkmm2ext::Keyboard;

const double Editor::timebar_height = 15.0;

#include "editor_xpms"

static const gchar *_snap_type_strings[] = {
	N_("CD Frames"),
	N_("Timecode Frames"),
	N_("Timecode Seconds"),
	N_("Timecode Minutes"),
	N_("Seconds"),
	N_("Minutes"),
	N_("Beats/32"),
	N_("Beats/16"),
	N_("Beats/8"),
	N_("Beats/4"),
	N_("Beats/3"),
	N_("Beats"),
	N_("Bars"),
	N_("Marks"),
	N_("Region starts"),
	N_("Region ends"),
	N_("Region syncs"),
	N_("Region bounds"),
	0
};

static const gchar *_snap_mode_strings[] = {
	N_("No Grid"),
	N_("Grid"),
	N_("Magnetic"),
	0
};

static const gchar *_edit_point_strings[] = {
	N_("Playhead"),
	N_("Marker"),
	N_("Mouse"),
	0
};

static const gchar *_zoom_focus_strings[] = {
	N_("Left"),
	N_("Right"),
	N_("Center"),
	N_("Playhead"),
 	N_("Mouse"),
 	N_("Edit point"),
	0
};

#ifdef USE_RUBBERBAND
static const gchar *_rb_opt_strings[] = {
	N_("Mushy"),
	N_("Smooth"),
	N_("Balanced multitimbral mixture"),
	N_("Unpitched percussion with stable notes"),
	N_("Crisp monophonic instrumental"),
	N_("Unpitched solo percussion"),
	0
};
#endif

/* Soundfile  drag-n-drop */

Gdk::Cursor* Editor::cross_hair_cursor = 0;
Gdk::Cursor* Editor::selector_cursor = 0;
Gdk::Cursor* Editor::trimmer_cursor = 0;
Gdk::Cursor* Editor::grabber_cursor = 0;
Gdk::Cursor* Editor::grabber_edit_point_cursor = 0;
Gdk::Cursor* Editor::zoom_cursor = 0;
Gdk::Cursor* Editor::time_fx_cursor = 0;
Gdk::Cursor* Editor::fader_cursor = 0;
Gdk::Cursor* Editor::speaker_cursor = 0;
Gdk::Cursor* Editor::midi_pencil_cursor = 0;
Gdk::Cursor* Editor::midi_select_cursor = 0;
Gdk::Cursor* Editor::midi_resize_cursor = 0;
Gdk::Cursor* Editor::midi_erase_cursor = 0;
Gdk::Cursor* Editor::wait_cursor = 0;
Gdk::Cursor* Editor::timebar_cursor = 0;
Gdk::Cursor* Editor::transparent_cursor = 0;

void
show_me_the_size (Requisition* r, const char* what)
{
	cerr << "size of " << what << " = " << r->width << " x " << r->height << endl;
}

Editor::Editor ()
	  /* time display buttons */
	: minsec_label (_("Mins:Secs"))
	, bbt_label (_("Bars:Beats"))
	, timecode_label (_("Timecode"))
	, frame_label (_("Samples"))
	, tempo_label (_("Tempo"))
	, meter_label (_("Meter"))
	, mark_label (_("Location Markers"))
	, range_mark_label (_("Range Markers"))
	, transport_mark_label (_("Loop/Punch Ranges"))
	, cd_mark_label (_("CD Markers"))
	, edit_packer (4, 4, true)

	  /* the values here don't matter: layout widgets
	     reset them as needed.
	  */

	, vertical_adjustment (0.0, 0.0, 10.0, 400.0)
	, horizontal_adjustment (0.0, 0.0, 20.0, 1200.0)

	  /* tool bar related */

	, zoom_range_clock (X_("zoomrange"), false, X_("ZoomRangeClock"), true, false, true)

	, toolbar_selection_clock_table (2,3)

	, automation_mode_button (_("mode"))
	, global_automation_button (_("automation"))

	, midi_panic_button (_("Panic"))

#ifdef WITH_CMT
	, image_socket_listener(0)
#endif

	  /* nudge */

	, nudge_clock (X_("nudge"), false, X_("NudgeClock"), true, false, true)
	, meters_running(false)
	, _pending_locate_request (false)

{
	constructed = false;

	/* we are a singleton */

	PublicEditor::_instance = this;

	_have_idled = false;

	selection = new Selection (this);
	cut_buffer = new Selection (this);

	clicked_regionview = 0;
	clicked_axisview = 0;
	clicked_routeview = 0;
	clicked_crossfadeview = 0;
	clicked_control_point = 0;
	last_update_frame = 0;
	_drag = 0;
	current_mixer_strip = 0;
	current_bbt_points = 0;
	tempo_lines = 0;

	snap_type_strings =  I18N (_snap_type_strings);
	snap_mode_strings =  I18N (_snap_mode_strings);
	zoom_focus_strings = I18N (_zoom_focus_strings);
	edit_point_strings = I18N (_edit_point_strings);
#ifdef USE_RUBBERBAND
	rb_opt_strings = I18N (_rb_opt_strings);
#endif

	snap_threshold = 5.0;
	bbt_beat_subdivision = 4;
	_canvas_width = 0;
	_canvas_height = 0;
	last_autoscroll_x = 0;
	last_autoscroll_y = 0;
	autoscroll_active = false;
	autoscroll_timeout_tag = -1;
	interthread_progress_window = 0;
	logo_item = 0;

	analysis_window = 0;

	current_interthread_info = 0;
	_show_measures = true;
	_show_waveforms_recording = true;
	show_gain_after_trim = false;
	verbose_cursor_on = true;
	last_item_entered = 0;
	last_item_entered_n = 0;

	have_pending_keyboard_selection = false;
	_follow_playhead = true;
	_xfade_visibility = true;
	editor_ruler_menu = 0;
	no_ruler_shown_update = false;
	marker_menu = 0;
	start_end_marker_menu = 0;
	range_marker_menu = 0;
	marker_menu_item = 0;
	tm_marker_menu = 0;
	transport_marker_menu = 0;
	new_transport_marker_menu = 0;
	editor_mixer_strip_width = Wide;
	show_editor_mixer_when_tracks_arrive = false;
	region_edit_menu_split_multichannel_item = 0;
	region_edit_menu_split_item = 0;
	temp_location = 0;
	leftmost_frame = 0;
	current_stepping_trackview = 0;
	entered_track = 0;
	entered_regionview = 0;
	entered_marker = 0;
	clear_entered_track = false;
	_new_regionviews_show_envelope = false;
	current_timefx = 0;
	playhead_cursor = 0;
	button_release_can_deselect = true;
	_dragging_playhead = false;
	_dragging_edit_point = false;
	select_new_marker = false;
	rhythm_ferret = 0;
	_bundle_manager = 0;
	for (ARDOUR::DataType::iterator i = ARDOUR::DataType::begin(); i != ARDOUR::DataType::end(); ++i) {
		_global_port_matrix[*i] = 0;
	}
	allow_vertical_scroll = false;
	no_save_visual = false;
	resize_idle_id = -1;

	scrubbing_direction = 0;

	sfbrowser = 0;

	location_marker_color = ARDOUR_UI::config()->canvasvar_LocationMarker.get();
	location_range_color = ARDOUR_UI::config()->canvasvar_LocationRange.get();
	location_cd_marker_color = ARDOUR_UI::config()->canvasvar_LocationCDMarker.get();
	location_loop_color = ARDOUR_UI::config()->canvasvar_LocationLoop.get();
	location_punch_color = ARDOUR_UI::config()->canvasvar_LocationPunch.get();

	_edit_point = EditAtMouse;
	_internal_editing = false;
	current_canvas_cursor = 0;

	frames_per_unit = 2048; /* too early to use reset_zoom () */

	zoom_focus = ZoomFocusLeft;
	set_zoom_focus (ZoomFocusLeft);
 	zoom_range_clock.ValueChanged.connect (sigc::mem_fun(*this, &Editor::zoom_adjustment_changed));

	bbt_label.set_name ("EditorTimeButton");
	bbt_label.set_size_request (-1, (int)timebar_height);
	bbt_label.set_alignment (1.0, 0.5);
	bbt_label.set_padding (5,0);
	bbt_label.hide ();
	bbt_label.set_no_show_all();
	minsec_label.set_name ("EditorTimeButton");
	minsec_label.set_size_request (-1, (int)timebar_height);
	minsec_label.set_alignment (1.0, 0.5);
	minsec_label.set_padding (5,0);
	minsec_label.hide ();
	minsec_label.set_no_show_all();
	timecode_label.set_name ("EditorTimeButton");
	timecode_label.set_size_request (-1, (int)timebar_height);
	timecode_label.set_alignment (1.0, 0.5);
	timecode_label.set_padding (5,0);
	timecode_label.hide ();
	timecode_label.set_no_show_all();
	frame_label.set_name ("EditorTimeButton");
	frame_label.set_size_request (-1, (int)timebar_height);
	frame_label.set_alignment (1.0, 0.5);
	frame_label.set_padding (5,0);
	frame_label.hide ();
	frame_label.set_no_show_all();

	tempo_label.set_name ("EditorTimeButton");
	tempo_label.set_size_request (-1, (int)timebar_height);
	tempo_label.set_alignment (1.0, 0.5);
	tempo_label.set_padding (5,0);
	tempo_label.hide();
	tempo_label.set_no_show_all();
	meter_label.set_name ("EditorTimeButton");
	meter_label.set_size_request (-1, (int)timebar_height);
	meter_label.set_alignment (1.0, 0.5);
	meter_label.set_padding (5,0);
	meter_label.hide();
	meter_label.set_no_show_all();
	mark_label.set_name ("EditorTimeButton");
	mark_label.set_size_request (-1, (int)timebar_height);
	mark_label.set_alignment (1.0, 0.5);
	mark_label.set_padding (5,0);
	mark_label.hide();
	mark_label.set_no_show_all();
	cd_mark_label.set_name ("EditorTimeButton");
	cd_mark_label.set_size_request (-1, (int)timebar_height);
	cd_mark_label.set_alignment (1.0, 0.5);
	cd_mark_label.set_padding (5,0);
	cd_mark_label.hide();
	cd_mark_label.set_no_show_all();
	range_mark_label.set_name ("EditorTimeButton");
	range_mark_label.set_size_request (-1, (int)timebar_height);
	range_mark_label.set_alignment (1.0, 0.5);
	range_mark_label.set_padding (5,0);
	range_mark_label.hide();
	range_mark_label.set_no_show_all();
	transport_mark_label.set_name ("EditorTimeButton");
	transport_mark_label.set_size_request (-1, (int)timebar_height);
	transport_mark_label.set_alignment (1.0, 0.5);
	transport_mark_label.set_padding (5,0);
	transport_mark_label.hide();
	transport_mark_label.set_no_show_all();

	initialize_rulers ();
	initialize_canvas ();
	_summary = new EditorSummary (this);

	selection->TimeChanged.connect (sigc::mem_fun(*this, &Editor::time_selection_changed));
	selection->TracksChanged.connect (sigc::mem_fun(*this, &Editor::track_selection_changed));
	editor_regions_selection_changed_connection = selection->RegionsChanged.connect (sigc::mem_fun(*this, &Editor::region_selection_changed));
	selection->PointsChanged.connect (sigc::mem_fun(*this, &Editor::point_selection_changed));
	selection->MarkersChanged.connect (sigc::mem_fun(*this, &Editor::marker_selection_changed));

	edit_controls_vbox.set_spacing (0);
	horizontal_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &Editor::scroll_canvas_horizontally), false);
	vertical_adjustment.signal_value_changed().connect (sigc::mem_fun(*this, &Editor::tie_vertical_scrolling), true);
	track_canvas->signal_map_event().connect (sigc::mem_fun (*this, &Editor::track_canvas_map_handler));

	HBox* h = manage (new HBox);
	_group_tabs = new EditorGroupTabs (this);
	h->pack_start (*_group_tabs, PACK_SHRINK);
	h->pack_start (edit_controls_vbox);
	controls_layout.add (*h);

	ARDOUR_UI::instance()->tooltips().set_tip (*_group_tabs, _("Groups: context-click for possible operations"));

	controls_layout.set_name ("EditControlsBase");
	controls_layout.add_events (Gdk::SCROLL_MASK);
	controls_layout.signal_scroll_event().connect (sigc::mem_fun(*this, &Editor::control_layout_scroll), false);

	controls_layout.add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK|Gdk::ENTER_NOTIFY_MASK|Gdk::LEAVE_NOTIFY_MASK);
	controls_layout.signal_button_release_event().connect (sigc::mem_fun(*this, &Editor::edit_controls_button_release));
	controls_layout_size_request_connection = controls_layout.signal_size_request().connect (sigc::mem_fun (*this, &Editor::controls_layout_size_request));

	build_cursors ();

	ArdourCanvas::Canvas* time_pad = manage(new ArdourCanvas::Canvas());
	ArdourCanvas::SimpleLine* pad_line_1 = manage(new ArdourCanvas::SimpleLine(*time_pad->root(),
			0.0, 1.0, 100.0, 1.0));
	pad_line_1->property_color_rgba() = 0xFF0000FF;
	pad_line_1->show();
	time_pad->show();

	time_canvas_vbox.set_size_request (-1, (int)(timebar_height * visible_timebars) + 2);
	time_canvas_vbox.set_size_request (-1, -1);

	ruler_label_event_box.add (ruler_label_vbox);
	ruler_label_event_box.set_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	ruler_label_event_box.signal_button_release_event().connect (sigc::mem_fun(*this, &Editor::ruler_label_button_release));

	time_button_event_box.add (time_button_vbox);
	time_button_event_box.set_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	time_button_event_box.signal_button_release_event().connect (sigc::mem_fun(*this, &Editor::ruler_label_button_release));

	/* these enable us to have a dedicated window (for cursor setting, etc.)
	   for the canvas areas.
	*/

	track_canvas_event_box.add (*track_canvas);

	time_canvas_event_box.add (time_canvas_vbox);
	time_canvas_event_box.set_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK|Gdk::POINTER_MOTION_MASK);

	edit_packer.set_col_spacings (0);
	edit_packer.set_row_spacings (0);
	edit_packer.set_homogeneous (false);
	edit_packer.set_border_width (0);
	edit_packer.set_name ("EditorWindow");

	edit_packer.attach (zoom_vbox,               0, 1, 0, 2,    SHRINK,        FILL, 0, 0);
	/* labels for the rulers */
	edit_packer.attach (ruler_label_event_box,   1, 2, 0, 1,    FILL,        SHRINK, 0, 0);
	/* labels for the marker "tracks" */
	edit_packer.attach (time_button_event_box,   1, 2, 1, 2,    FILL,        SHRINK, 0, 0);
	/* the rulers */
	edit_packer.attach (time_canvas_event_box,   2, 3, 0, 1,    FILL|EXPAND, FILL, 0, 0);
	/* track controls */
	edit_packer.attach (controls_layout,         0, 2, 2, 3,    FILL,        FILL|EXPAND, 0, 0);
	/* main canvas */
	edit_packer.attach (track_canvas_event_box,  2, 3, 1, 3,    FILL|EXPAND, FILL|EXPAND, 0, 0);

	bottom_hbox.set_border_width (2);
	bottom_hbox.set_spacing (3);

	_route_groups = new EditorRouteGroups (this);
	_routes = new EditorRoutes (this);
	_regions = new EditorRegions (this);
	_snapshots = new EditorSnapshots (this);
	_locations = new EditorLocations (this);

	Gtk::Label* nlabel;

	nlabel = manage (new Label (_("Regions")));
	nlabel->set_angle (-90);
	the_notebook.append_page (_regions->widget (), *nlabel);
	nlabel = manage (new Label (_("Tracks/Busses")));
	nlabel->set_angle (-90);
	the_notebook.append_page (_routes->widget (), *nlabel);
	nlabel = manage (new Label (_("Snapshots")));
	nlabel->set_angle (-90);
	the_notebook.append_page (_snapshots->widget (), *nlabel);
	nlabel = manage (new Label (_("Route Groups")));
	nlabel->set_angle (-90);
	the_notebook.append_page (_route_groups->widget (), *nlabel);
	nlabel = manage (new Label (_("Ranges & Marks")));
	nlabel->set_angle (-90);
	the_notebook.append_page (_locations->widget (), *nlabel);

	the_notebook.set_show_tabs (true);
	the_notebook.set_scrollable (true);
	the_notebook.popup_disable ();
	the_notebook.set_tab_pos (Gtk::POS_RIGHT);
	the_notebook.show_all ();
	
	post_maximal_editor_width = 0;
	post_maximal_pane_position = 0;

	VPaned *editor_summary_pane = manage(new VPaned());
	editor_summary_pane->pack1(edit_packer);

	Button* summary_arrows_left_left = manage (new Button);
	summary_arrows_left_left->add (*manage (new Arrow (ARROW_LEFT, SHADOW_NONE)));
	summary_arrows_left_left->signal_clicked().connect (sigc::mem_fun (*this, &Editor::horizontal_scroll_left));
	Button* summary_arrows_left_right = manage (new Button);
	summary_arrows_left_right->add (*manage (new Arrow (ARROW_RIGHT, SHADOW_NONE)));
	summary_arrows_left_right->signal_clicked().connect (sigc::mem_fun (*this, &Editor::horizontal_scroll_right));
	VBox* summary_arrows_left = manage (new VBox);
	summary_arrows_left->pack_start (*summary_arrows_left_left);
	summary_arrows_left->pack_start (*summary_arrows_left_right);

	Button* summary_arrows_right_left = manage (new Button);
	summary_arrows_right_left->add (*manage (new Arrow (ARROW_LEFT, SHADOW_NONE)));
	summary_arrows_right_left->signal_clicked().connect (sigc::mem_fun (*this, &Editor::horizontal_scroll_left));
	Button* summary_arrows_right_right = manage (new Button);
	summary_arrows_right_right->add (*manage (new Arrow (ARROW_RIGHT, SHADOW_NONE)));
	summary_arrows_right_right->signal_clicked().connect (sigc::mem_fun (*this, &Editor::horizontal_scroll_right));
	VBox* summary_arrows_right = manage (new VBox);
	summary_arrows_right->pack_start (*summary_arrows_right_left);
	summary_arrows_right->pack_start (*summary_arrows_right_right);

	Frame* summary_frame = manage (new Frame);
	summary_frame->set_shadow_type (Gtk::SHADOW_ETCHED_IN);
	summary_frame->add (*_summary);
	summary_frame->show ();

	_summary_hbox.pack_start (*summary_arrows_left, false, false);
	_summary_hbox.pack_start (*summary_frame, true, true);
	_summary_hbox.pack_start (*summary_arrows_right, false, false);
	
	editor_summary_pane->pack2 (_summary_hbox);

	edit_pane.pack1 (*editor_summary_pane, true, true);
	edit_pane.pack2 (the_notebook, false, true);

	edit_pane.signal_size_allocate().connect (sigc::bind (sigc::mem_fun(*this, &Editor::pane_allocation_handler), static_cast<Paned*> (&edit_pane)));

	top_hbox.pack_start (toolbar_frame, false, true);

	HBox *hbox = manage (new HBox);
	hbox->pack_start (edit_pane, true, true);

	global_vpacker.pack_start (top_hbox, false, false);
	global_vpacker.pack_start (*hbox, true, true);

	global_hpacker.pack_start (global_vpacker, true, true);

	set_name ("EditorWindow");
	add_accel_group (ActionManager::ui_manager->get_accel_group());

	status_bar_hpacker.show ();

	vpacker.pack_end (status_bar_hpacker, false, false);
	vpacker.pack_end (global_hpacker, true, true);

	/* register actions now so that set_state() can find them and set toggles/checks etc */

	register_actions ();

	setup_toolbar ();
	setup_midi_toolbar ();

	_snap_type = SnapToBeat;
	set_snap_to (_snap_type);
	_snap_mode = SnapOff;
	set_snap_mode (_snap_mode);
	set_mouse_mode (MouseObject, true);
	set_edit_point_preference (EditAtMouse, true);

	XMLNode* node = ARDOUR_UI::instance()->editor_settings();
	set_state (*node, Stateful::loading_state_version);

	_playlist_selector = new PlaylistSelector();
	_playlist_selector->signal_delete_event().connect (sigc::bind (sigc::ptr_fun (just_hide_it), static_cast<Window *> (_playlist_selector)));

	RegionView::RegionViewGoingAway.connect (*this, ui_bind (&Editor::catch_vanishing_regionview, this, _1), gui_context());

	/* nudge stuff */

	nudge_forward_button.add (*(manage (new Image (::get_icon("nudge_right")))));
	nudge_backward_button.add (*(manage (new Image (::get_icon("nudge_left")))));

	ARDOUR_UI::instance()->tooltips().set_tip (nudge_forward_button, _("Nudge Region/Selection Forwards"));
	ARDOUR_UI::instance()->tooltips().set_tip (nudge_backward_button, _("Nudge Region/Selection Backwards"));

	nudge_forward_button.set_name ("TransportButton");
	nudge_backward_button.set_name ("TransportButton");

	fade_context_menu.set_name ("ArdourContextMenu");

	/* icons, titles, WM stuff */

	list<Glib::RefPtr<Gdk::Pixbuf> > window_icons;
	Glib::RefPtr<Gdk::Pixbuf> icon;

	if ((icon = ::get_icon ("ardour_icon_16px")) != 0) {
		window_icons.push_back (icon);
	}
	if ((icon = ::get_icon ("ardour_icon_22px")) != 0) {
		window_icons.push_back (icon);
	}
	if ((icon = ::get_icon ("ardour_icon_32px")) != 0) {
		window_icons.push_back (icon);
	}
	if ((icon = ::get_icon ("ardour_icon_48px")) != 0) {
		window_icons.push_back (icon);
	}
	if (!window_icons.empty()) {
		set_icon_list (window_icons);
		set_default_icon_list (window_icons);
	}

	WindowTitle title(Glib::get_application_name());
	title += _("Editor");
	set_title (title.get_string());
	set_wmclass (X_("ardour_editor"), "Ardour");

	add (vpacker);
	add_events (Gdk::KEY_PRESS_MASK|Gdk::KEY_RELEASE_MASK);

	signal_configure_event().connect (sigc::mem_fun (*ARDOUR_UI::instance(), &ARDOUR_UI::configure_handler));
	signal_delete_event().connect (sigc::mem_fun (*ARDOUR_UI::instance(), &ARDOUR_UI::exit_on_main_window_close));

	/* allow external control surfaces/protocols to do various things */

	ControlProtocol::ZoomToSession.connect (*this, boost::bind (&Editor::temporal_zoom_session, this), gui_context());
	ControlProtocol::ZoomIn.connect (*this, boost::bind (&Editor::temporal_zoom_step, this, false), gui_context());
	ControlProtocol::ZoomOut.connect (*this, boost::bind (&Editor::temporal_zoom_step, this, true), gui_context());
	ControlProtocol::ScrollTimeline.connect (*this, ui_bind (&Editor::control_scroll, this, _1), gui_context());
	BasicUI::AccessAction.connect (*this, ui_bind (&Editor::access_action, this, _1, _2), gui_context());
	
	/* problematic: has to return a value and thus cannot be x-thread */

	Session::AskAboutPlaylistDeletion.connect_same_thread (*this, boost::bind (&Editor::playlist_deletion_dialog, this, _1));

	Config->ParameterChanged.connect (*this, ui_bind (&Editor::parameter_changed, this, _1), gui_context());

	TimeAxisView::CatchDeletion.connect (*this, ui_bind (&Editor::timeaxisview_deleted, this, _1), gui_context());

	_last_normalization_value = 0;

	constructed = true;
	instant_save ();
}

Editor::~Editor()
{
#ifdef WITH_CMT
	if(image_socket_listener) {
		if(image_socket_listener->is_connected())
		{
			image_socket_listener->close_connection() ;
		}

		delete image_socket_listener ;
		image_socket_listener = 0 ;
	}
#endif

	delete _routes;
	delete _route_groups;
	delete track_canvas;
	delete _drag;
}

void
Editor::add_toplevel_controls (Container& cont)
{
	vpacker.pack_start (cont, false, false);
	cont.show_all ();
}

void
Editor::catch_vanishing_regionview (RegionView *rv)
{
	/* note: the selection will take care of the vanishing
	   audioregionview by itself.
	*/

	if (_drag && rv->get_canvas_group() == _drag->item() && !_drag->ending()) {
		_drag->end_grab (0);
		delete _drag;
		_drag = 0;
	}

	if (clicked_regionview == rv) {
		clicked_regionview = 0;
	}

	if (entered_regionview == rv) {
		set_entered_regionview (0);
	}
}

void
Editor::set_entered_regionview (RegionView* rv)
{
	if (rv == entered_regionview) {
		return;
	}

	if (entered_regionview) {
		entered_regionview->exited ();
	}

	if ((entered_regionview = rv) != 0) {
		entered_regionview->entered ();
	}
}

void
Editor::set_entered_track (TimeAxisView* tav)
{
	if (entered_track) {
		entered_track->exited ();
	}

	if ((entered_track = tav) != 0) {
		entered_track->entered ();
	}
}

void
Editor::show_window ()
{
	if (! is_visible ()) {
		show_all ();

		/* re-hide editor list if necessary */
		editor_list_button_toggled ();

		/* re-hide summary widget if necessary */
		parameter_changed ("show-summary");

		parameter_changed ("show-edit-group-tabs");

		/* now reset all audio_time_axis heights, because widgets might need
		   to be re-hidden
		*/

		TimeAxisView *tv;

		for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
			tv = (static_cast<TimeAxisView*>(*i));
			tv->reset_height ();
		}
	}

	present ();
}

void
Editor::instant_save ()
{
	if (!constructed || !ARDOUR_UI::instance()->session_loaded) {
		return;
	}

	if (_session) {
		_session->add_instant_xml(get_state());
	} else {
		Config->add_instant_xml(get_state());
	}
}

void
Editor::zoom_adjustment_changed ()
{
	if (_session == 0) {
		return;
	}

	double fpu = zoom_range_clock.current_duration() / _canvas_width;

	if (fpu < 1.0) {
		fpu = 1.0;
		zoom_range_clock.set ((nframes64_t) floor (fpu * _canvas_width));
	} else if (fpu > _session->current_end_frame() / _canvas_width) {
		fpu = _session->current_end_frame() / _canvas_width;
		zoom_range_clock.set ((nframes64_t) floor (fpu * _canvas_width));
	}

	temporal_zoom (fpu);
}

void
Editor::control_scroll (float fraction)
{
	ENSURE_GUI_THREAD (*this, &Editor::control_scroll, fraction)

	if (!_session) {
		return;
	}

	double step = fraction * current_page_frames();

	/*
		_control_scroll_target is an optional<T>

		it acts like a pointer to an nframes64_t, with
		a operator conversion to boolean to check
		that it has a value could possibly use
		playhead_cursor->current_frame to store the
		value and a boolean in the class to know
		when it's out of date
	*/

	if (!_control_scroll_target) {
		_control_scroll_target = _session->transport_frame();
		_dragging_playhead = true;
	}

	if ((fraction < 0.0f) && (*_control_scroll_target < (nframes64_t) fabs(step))) {
		*_control_scroll_target = 0;
	} else if ((fraction > 0.0f) && (max_frames - *_control_scroll_target < step)) {
		*_control_scroll_target = max_frames - (current_page_frames()*2); // allow room for slop in where the PH is on the screen
	} else {
		*_control_scroll_target += (nframes64_t) floor (step);
	}

	/* move visuals, we'll catch up with it later */

	playhead_cursor->set_position (*_control_scroll_target);
	UpdateAllTransportClocks (*_control_scroll_target);

	if (*_control_scroll_target > (current_page_frames() / 2)) {
		/* try to center PH in window */
		reset_x_origin (*_control_scroll_target - (current_page_frames()/2));
	} else {
		reset_x_origin (0);
	}

	/*
		Now we do a timeout to actually bring the session to the right place
		according to the playhead. This is to avoid reading disk buffers on every
		call to control_scroll, which is driven by ScrollTimeline and therefore
		probably by a control surface wheel which can generate lots of events.
	*/
	/* cancel the existing timeout */

	control_scroll_connection.disconnect ();

	/* add the next timeout */

	control_scroll_connection = Glib::signal_timeout().connect (sigc::bind (sigc::mem_fun (*this, &Editor::deferred_control_scroll), *_control_scroll_target), 250);
}

bool
Editor::deferred_control_scroll (nframes64_t /*target*/)
{
	_session->request_locate (*_control_scroll_target, _session->transport_rolling());
	// reset for next stream
	_control_scroll_target = boost::none;
	_dragging_playhead = false;
	return false;
}

void
Editor::access_action (std::string action_group, std::string action_item)
{
	if (!_session) {
		return;
	}

	ENSURE_GUI_THREAD (*this, &Editor::access_action, action_group, action_item)

	RefPtr<Action> act;
	act = ActionManager::get_action( action_group.c_str(), action_item.c_str() );

	if (act) {
		act->activate();
	}


}

void
Editor::on_realize ()
{
	Window::on_realize ();
	Realized ();
}

void
Editor::start_scrolling ()
{
	scroll_connection = ARDOUR_UI::instance()->SuperRapidScreenUpdate.connect
		(sigc::mem_fun(*this, &Editor::update_current_screen));

}

void
Editor::stop_scrolling ()
{
	scroll_connection.disconnect ();
}

void
Editor::map_position_change (nframes64_t frame)
{
	ENSURE_GUI_THREAD (*this, &Editor::map_position_change, frame)

	if (_session == 0 || !_follow_playhead) {
		return;
	}

	center_screen (frame);
	playhead_cursor->set_position (frame);
}

void
Editor::center_screen (nframes64_t frame)
{
	double page = _canvas_width * frames_per_unit;

	/* if we're off the page, then scroll.
	 */

	if (frame < leftmost_frame || frame >= leftmost_frame + page) {
		center_screen_internal (frame, page);
	}
}

void
Editor::center_screen_internal (nframes64_t frame, float page)
{
	page /= 2;

	if (frame > page) {
		frame -= (nframes64_t) page;
	} else {
		frame = 0;
	}

	reset_x_origin (frame);
}

void
Editor::handle_new_duration ()
{
	if (!_session) {
		return;
	}

	ENSURE_GUI_THREAD (*this, &Editor::handle_new_duration)
	nframes64_t new_end = _session->current_end_frame() + (nframes64_t) floorf (current_page_frames() * 0.10f);

	horizontal_adjustment.set_upper (new_end / frames_per_unit);
	horizontal_adjustment.set_page_size (current_page_frames()/frames_per_unit);

	if (horizontal_adjustment.get_value() + _canvas_width > horizontal_adjustment.get_upper()) {
		horizontal_adjustment.set_value (horizontal_adjustment.get_upper() - _canvas_width);
	}
	//cerr << "Editor::handle_new_duration () called ha v:l:u:ps:lcf = " << horizontal_adjustment.get_value() << ":" << horizontal_adjustment.get_lower() << ":" << horizontal_adjustment.get_upper() << ":" << horizontal_adjustment.get_page_size() << ":" << endl;//DEBUG
}

void
Editor::update_title ()
{
	ENSURE_GUI_THREAD (*this, &Editor::update_title)

	if (_session) {
		bool dirty = _session->dirty();

		string session_name;

		if (_session->snap_name() != _session->name()) {
			session_name = _session->snap_name();
		} else {
			session_name = _session->name();
		}

		if (dirty) {
			session_name = "*" + session_name;
		}

		WindowTitle title(session_name);
		title += Glib::get_application_name();
		set_title (title.get_string());
	}
}

void
Editor::set_session (Session *t)
{
	SessionHandlePtr::set_session (t);

	if (!_session) {
		return;
	}

	zoom_range_clock.set_session (_session);
	_playlist_selector->set_session (_session);
	nudge_clock.set_session (_session);
	_summary->set_session (_session);
	_group_tabs->set_session (_session);
	_route_groups->set_session (_session);
	_regions->set_session (_session);
	_snapshots->set_session (_session);
	_routes->set_session (_session);
	_locations->set_session (_session);

	if (rhythm_ferret) {
		rhythm_ferret->set_session (_session);
	}

	if (analysis_window) {
		analysis_window->set_session (_session);
	}

	if (sfbrowser) {
		sfbrowser->set_session (_session);
	}

	compute_fixed_ruler_scale ();

	/* there are never any selected regions at startup */

	sensitize_the_right_region_actions (false);

	XMLNode* node = ARDOUR_UI::instance()->editor_settings();
	set_state (*node, Stateful::loading_state_version);

	/* catch up with the playhead */

	_session->request_locate (playhead_cursor->current_frame);

	update_title ();

	/* These signals can all be emitted by a non-GUI thread. Therefore the
	   handlers for them must not attempt to directly interact with the GUI,
	   but use Gtkmm2ext::UI::instance()->call_slot();
	*/

	_session->TransportStateChange.connect (_session_connections, boost::bind (&Editor::map_transport_state, this), gui_context());
	_session->PositionChanged.connect (_session_connections, ui_bind (&Editor::map_position_change, this, _1), gui_context());
	_session->RouteAdded.connect (_session_connections, ui_bind (&Editor::handle_new_route, this, _1), gui_context());
	_session->DurationChanged.connect (_session_connections, boost::bind (&Editor::handle_new_duration, this), gui_context());
	_session->DirtyChanged.connect (_session_connections, boost::bind (&Editor::update_title, this), gui_context());
	_session->TimecodeOffsetChanged.connect (_session_connections, boost::bind (&Editor::update_just_timecode, this), gui_context());
	_session->tempo_map().StateChanged.connect (_session_connections, ui_bind (&Editor::tempo_map_changed, this, _1), gui_context());
	_session->Located.connect (_session_connections, boost::bind (&Editor::located, this), gui_context());
	_session->config.ParameterChanged.connect (_session_connections, ui_bind (&Editor::parameter_changed, this, _1), gui_context());
	_session->StateSaved.connect (_session_connections, ui_bind (&Editor::session_state_saved, this, _1), gui_context());
	_session->locations()->added.connect (_session_connections, ui_bind (&Editor::add_new_location, this, _1), gui_context());
	_session->locations()->removed.connect (_session_connections, ui_bind (&Editor::location_gone, this, _1), gui_context());
	_session->locations()->changed.connect (_session_connections, boost::bind (&Editor::refresh_location_display, this), gui_context());
	_session->locations()->StateChanged.connect (_session_connections, ui_bind (&Editor::refresh_location_display_s, this, _1), gui_context());
	_session->locations()->end_location()->changed.connect (_session_connections, ui_bind (&Editor::end_location_changed, this, _1), gui_context());
	_session->history().Changed.connect (_session_connections, boost::bind (&Editor::history_changed, this), gui_context());

	if (Profile->get_sae()) {
		BBT_Time bbt;
		bbt.bars = 0;
		bbt.beats = 0;
		bbt.ticks = 120;
		nframes_t pos = _session->tempo_map().bbt_duration_at (0, bbt, 1);
		nudge_clock.set_mode(AudioClock::BBT);
		nudge_clock.set (pos, true, 0, AudioClock::BBT);

	} else {
		nudge_clock.set (_session->frame_rate() * 5, true, 0, AudioClock::Timecode); // default of 5 seconds
	}

	playhead_cursor->canvas_item.show ();

	Location* loc = _session->locations()->auto_loop_location();
	if (loc == 0) {
		loc = new Location (0, _session->current_end_frame(), _("Loop"),(Location::Flags) (Location::IsAutoLoop | Location::IsHidden));
		if (loc->start() == loc->end()) {
			loc->set_end (loc->start() + 1);
		}
		_session->locations()->add (loc, false);
		_session->set_auto_loop_location (loc);
	} else {
		// force name
		loc->set_name (_("Loop"));
	}

	loc = _session->locations()->auto_punch_location();
	if (loc == 0) {
		loc = new Location (0, _session->current_end_frame(), _("Punch"), (Location::Flags) (Location::IsAutoPunch | Location::IsHidden));
		if (loc->start() == loc->end()) {
			loc->set_end (loc->start() + 1);
		}
		_session->locations()->add (loc, false);
		_session->set_auto_punch_location (loc);
	} else {
		// force name
		loc->set_name (_("Punch"));
	}

	boost::function<void (string)> pc (boost::bind (&Editor::parameter_changed, this, _1));
	Config->map_parameters (pc);
	_session->config.map_parameters (pc);

	refresh_location_display ();
	handle_new_duration ();

	restore_ruler_visibility ();
	//tempo_map_changed (Change (0));
	_session->tempo_map().apply_with_metrics (*this, &Editor::draw_metric_marks);

	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		(static_cast<TimeAxisView*>(*i))->set_samples_per_unit (frames_per_unit);
	}

	start_scrolling ();

	switch (_snap_type) {
	case SnapToRegionStart:
	case SnapToRegionEnd:
	case SnapToRegionSync:
	case SnapToRegionBoundary:
		build_region_boundary_cache ();
		break;

	default:
		break;
	}

	/* register for undo history */
	_session->register_with_memento_command_factory(_id, this);

	start_updating ();
}

void
Editor::build_cursors ()
{
	using namespace Gdk;

	Gdk::Color mbg ("#000000" ); /* Black */
	Gdk::Color mfg ("#0000ff" ); /* Blue. */

	{
		RefPtr<Bitmap> source, mask;
		source = Bitmap::create (mag_bits, mag_width, mag_height);
		mask = Bitmap::create (magmask_bits, mag_width, mag_height);
		zoom_cursor = new Gdk::Cursor (source, mask, mfg, mbg, mag_x_hot, mag_y_hot);
	}

	Gdk::Color fbg ("#ffffff" );
	Gdk::Color ffg  ("#000000" );

	{
		RefPtr<Bitmap> source, mask;

		source = Bitmap::create (fader_cursor_bits, fader_cursor_width, fader_cursor_height);
		mask = Bitmap::create (fader_cursor_mask_bits, fader_cursor_width, fader_cursor_height);
		fader_cursor = new Gdk::Cursor (source, mask, ffg, fbg, fader_cursor_x_hot, fader_cursor_y_hot);
	}

	{
		RefPtr<Bitmap> source, mask;
		source = Bitmap::create (speaker_cursor_bits, speaker_cursor_width, speaker_cursor_height);
		mask = Bitmap::create (speaker_cursor_mask_bits, speaker_cursor_width, speaker_cursor_height);
		speaker_cursor = new Gdk::Cursor (source, mask, ffg, fbg, speaker_cursor_x_hot, speaker_cursor_y_hot);
	}

	{
		RefPtr<Bitmap> bits;
		char pix[4] = { 0, 0, 0, 0 };
		bits = Bitmap::create (pix, 2, 2);
		Gdk::Color c;
		transparent_cursor = new Gdk::Cursor (bits, bits, c, c, 0, 0);
	}

	{
		RefPtr<Bitmap> bits;
		char pix[4] = { 0, 0, 0, 0 };
		bits = Bitmap::create (pix, 2, 2);
		Gdk::Color c;
		transparent_cursor = new Gdk::Cursor (bits, bits, c, c, 0, 0);
	}


	grabber_cursor = new Gdk::Cursor (HAND2);

	{
		Glib::RefPtr<Gdk::Pixbuf> grabber_edit_point_pixbuf (::get_icon ("grabber_edit_point"));
		grabber_edit_point_cursor = new Gdk::Cursor (Gdk::Display::get_default(), grabber_edit_point_pixbuf, 5, 17);
	}

	cross_hair_cursor = new Gdk::Cursor (CROSSHAIR);
	trimmer_cursor =  new Gdk::Cursor (SB_H_DOUBLE_ARROW);
	selector_cursor = new Gdk::Cursor (XTERM);
	time_fx_cursor = new Gdk::Cursor (SIZING);
	wait_cursor = new Gdk::Cursor  (WATCH);
	timebar_cursor = new Gdk::Cursor(LEFT_PTR);
	midi_pencil_cursor = new Gdk::Cursor (PENCIL);
	midi_select_cursor = new Gdk::Cursor (CENTER_PTR);
	midi_resize_cursor = new Gdk::Cursor (SIZING);
	midi_erase_cursor = new Gdk::Cursor (DRAPED_BOX);
}

/** Pop up a context menu for when the user clicks on a fade in or fade out */
void
Editor::popup_fade_context_menu (int button, int32_t time, ArdourCanvas::Item* item, ItemType item_type)
{
	using namespace Menu_Helpers;
	AudioRegionView* arv = static_cast<AudioRegionView*> (item->get_data ("regionview"));

	if (arv == 0) {
		fatal << _("programming error: fade in canvas item has no regionview data pointer!") << endmsg;
		/*NOTREACHED*/
	}

	MenuList& items (fade_context_menu.items());

	items.clear ();

	switch (item_type) {
	case FadeInItem:
	case FadeInHandleItem:
		if (arv->audio_region()->fade_in_active()) {
			items.push_back (MenuElem (_("Deactivate"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_active), false)));
		} else {
			items.push_back (MenuElem (_("Activate"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_active), true)));
		}

		items.push_back (SeparatorElem());

		if (Profile->get_sae()) {
			items.push_back (MenuElem (_("Linear"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::Linear)));
			items.push_back (MenuElem (_("Slowest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::Fast)));
		} else {
			items.push_back (MenuElem (_("Linear"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::Linear)));
			items.push_back (MenuElem (_("Slowest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::Fast)));
			items.push_back (MenuElem (_("Slow"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::LogB)));
			items.push_back (MenuElem (_("Fast"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::LogA)));
			items.push_back (MenuElem (_("Fastest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_in_shape), AudioRegion::Slow)));
		}

		break;

	case FadeOutItem:
	case FadeOutHandleItem:
		if (arv->audio_region()->fade_out_active()) {
			items.push_back (MenuElem (_("Deactivate"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_active), false)));
		} else {
			items.push_back (MenuElem (_("Activate"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_active), true)));
		}

		items.push_back (SeparatorElem());

		if (Profile->get_sae()) {
			items.push_back (MenuElem (_("Linear"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::Linear)));
			items.push_back (MenuElem (_("Slowest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::Slow)));
		} else {
			items.push_back (MenuElem (_("Linear"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::Linear)));
			items.push_back (MenuElem (_("Slowest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::Slow)));
			items.push_back (MenuElem (_("Slow"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::LogA)));
			items.push_back (MenuElem (_("Fast"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::LogB)));
			items.push_back (MenuElem (_("Fastest"), sigc::bind (sigc::mem_fun (*this, &Editor::set_fade_out_shape), AudioRegion::Fast)));
		}

		break;

	default:
		fatal << _("programming error: ")
		      << X_("non-fade canvas item passed to popup_fade_context_menu()")
		      << endmsg;
		/*NOTREACHED*/
	}

	fade_context_menu.popup (button, time);
}

void
Editor::popup_track_context_menu (int button, int32_t time, ItemType item_type, bool with_selection, nframes64_t frame)
{
	using namespace Menu_Helpers;
	Menu* (Editor::*build_menu_function)(nframes64_t);
	Menu *menu;

	switch (item_type) {
	case RegionItem:
	case RegionViewName:
	case RegionViewNameHighlight:
		if (with_selection) {
			build_menu_function = &Editor::build_track_selection_context_menu;
		} else {
			build_menu_function = &Editor::build_track_region_context_menu;
		}
		break;

	case SelectionItem:
		if (with_selection) {
			build_menu_function = &Editor::build_track_selection_context_menu;
		} else {
			build_menu_function = &Editor::build_track_context_menu;
		}
		break;

	case CrossfadeViewItem:
		build_menu_function = &Editor::build_track_crossfade_context_menu;
		break;

	case StreamItem:
		if (clicked_routeview->get_diskstream()) {
			build_menu_function = &Editor::build_track_context_menu;
		} else {
			build_menu_function = &Editor::build_track_bus_context_menu;
		}
		break;

	default:
		/* probably shouldn't happen but if it does, we don't care */
		return;
	}

	menu = (this->*build_menu_function)(frame);
	menu->set_name ("ArdourContextMenu");

	/* now handle specific situations */

	switch (item_type) {
	case RegionItem:
	case RegionViewName:
	case RegionViewNameHighlight:
		if (!with_selection) {
			if (region_edit_menu_split_item) {
				if (clicked_regionview && clicked_regionview->region()->covers (get_preferred_edit_position())) {
					ActionManager::set_sensitive (ActionManager::edit_point_in_region_sensitive_actions, true);
				} else {
					ActionManager::set_sensitive (ActionManager::edit_point_in_region_sensitive_actions, false);
				}
			}
			/*
			if (region_edit_menu_split_multichannel_item) {
				if (clicked_regionview && clicked_regionview->region().n_channels() > 1) {
					// GTK2FIX find the action, change its sensitivity
					// region_edit_menu_split_multichannel_item->set_sensitive (true);
				} else {
					// GTK2FIX see above
					// region_edit_menu_split_multichannel_item->set_sensitive (false);
				}
			}*/
		}
		break;

	case SelectionItem:
		break;

	case CrossfadeViewItem:
		break;

	case StreamItem:
		break;

	default:
		/* probably shouldn't happen but if it does, we don't care */
		return;
	}

	if (item_type != SelectionItem && clicked_routeview && clicked_routeview->audio_track()) {

		/* Bounce to disk */

		using namespace Menu_Helpers;
		MenuList& edit_items  = menu->items();

		edit_items.push_back (SeparatorElem());

		switch (clicked_routeview->audio_track()->freeze_state()) {
		case AudioTrack::NoFreeze:
			edit_items.push_back (MenuElem (_("Freeze"), sigc::mem_fun(*this, &Editor::freeze_route)));
			break;

		case AudioTrack::Frozen:
			edit_items.push_back (MenuElem (_("Unfreeze"), sigc::mem_fun(*this, &Editor::unfreeze_route)));
			break;

		case AudioTrack::UnFrozen:
			edit_items.push_back (MenuElem (_("Freeze"), sigc::mem_fun(*this, &Editor::freeze_route)));
			break;
		}

	}

	if (item_type == StreamItem && clicked_routeview) {
		clicked_routeview->build_underlay_menu(menu);
	}

	menu->popup (button, time);
}

Menu*
Editor::build_track_context_menu (nframes64_t)
{
	using namespace Menu_Helpers;

 	MenuList& edit_items = track_context_menu.items();
	edit_items.clear();

	add_dstream_context_items (edit_items);
	return &track_context_menu;
}

Menu*
Editor::build_track_bus_context_menu (nframes64_t)
{
	using namespace Menu_Helpers;

 	MenuList& edit_items = track_context_menu.items();
	edit_items.clear();

	add_bus_context_items (edit_items);
	return &track_context_menu;
}

Menu*
Editor::build_track_region_context_menu (nframes64_t frame)
{
	using namespace Menu_Helpers;
	MenuList& edit_items  = track_region_context_menu.items();
	edit_items.clear();

	RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*> (clicked_axisview);

	if (rtv) {
		boost::shared_ptr<Diskstream> ds;
		boost::shared_ptr<Playlist> pl;

		if ((ds = rtv->get_diskstream()) && ((pl = ds->playlist()))) {
			Playlist::RegionList* regions = pl->regions_at ((nframes64_t) floor ( (double)frame * ds->speed()));

 			if (selection->regions.size() > 1) {
 				// there's already a multiple selection: just add a
 				// single region context menu that will act on all
 				// selected regions
 				boost::shared_ptr<Region> dummy_region; // = NULL
 				add_region_context_items (rtv->view(), dummy_region, edit_items);
 			} else {
 				for (Playlist::RegionList::iterator i = regions->begin(); i != regions->end(); ++i) {
 					add_region_context_items (rtv->view(), (*i), edit_items);
 				}
			}

			delete regions;
		}
	}

	add_dstream_context_items (edit_items);

	return &track_region_context_menu;
}

Menu*
Editor::build_track_crossfade_context_menu (nframes64_t frame)
{
	using namespace Menu_Helpers;
	MenuList& edit_items  = track_crossfade_context_menu.items();
	edit_items.clear ();

	AudioTimeAxisView* atv = dynamic_cast<AudioTimeAxisView*> (clicked_axisview);

	if (atv) {
		boost::shared_ptr<Diskstream> ds;
		boost::shared_ptr<Playlist> pl;
		boost::shared_ptr<AudioPlaylist> apl;

		if ((ds = atv->get_diskstream()) && ((pl = ds->playlist()) != 0) && ((apl = boost::dynamic_pointer_cast<AudioPlaylist> (pl)) != 0)) {

			Playlist::RegionList* regions = pl->regions_at (frame);
			AudioPlaylist::Crossfades xfades;

			apl->crossfades_at (frame, xfades);

			bool many = xfades.size() > 1;

			for (AudioPlaylist::Crossfades::iterator i = xfades.begin(); i != xfades.end(); ++i) {
				add_crossfade_context_items (atv->audio_view(), (*i), edit_items, many);
			}

			if (selection->regions.size() > 1) {
				// there's already a multiple selection: just add a
				// single region context menu that will act on all
				// selected regions
				boost::shared_ptr<Region> dummy_region; // = NULL
				add_region_context_items (atv->audio_view(), dummy_region, edit_items);
			} else {
				for (Playlist::RegionList::iterator i = regions->begin(); i != regions->end(); ++i) {
					add_region_context_items (atv->audio_view(), (*i), edit_items);
				}
			}
			delete regions;
		}
	}

	add_dstream_context_items (edit_items);

	return &track_crossfade_context_menu;
}

void
Editor::analyze_region_selection()
{
	if (analysis_window == 0) {
		analysis_window = new AnalysisWindow();

		if (_session != 0)
			analysis_window->set_session(_session);

		analysis_window->show_all();
	}

	analysis_window->set_regionmode();
	analysis_window->analyze();

	analysis_window->present();
}

void
Editor::analyze_range_selection()
{
	if (analysis_window == 0) {
		analysis_window = new AnalysisWindow();

		if (_session != 0)
			analysis_window->set_session(_session);

		analysis_window->show_all();
	}

	analysis_window->set_rangemode();
	analysis_window->analyze();

	analysis_window->present();
}

Menu*
Editor::build_track_selection_context_menu (nframes64_t)
{
	using namespace Menu_Helpers;
	MenuList& edit_items  = track_selection_context_menu.items();
	edit_items.clear ();

	add_selection_context_items (edit_items);
	// edit_items.push_back (SeparatorElem());
	// add_dstream_context_items (edit_items);

	return &track_selection_context_menu;
}

/** Add context menu items relevant to crossfades.
 * @param edit_items List to add the items to.
 */
void
Editor::add_crossfade_context_items (AudioStreamView* /*view*/, boost::shared_ptr<Crossfade> xfade, Menu_Helpers::MenuList& edit_items, bool many)
{
	using namespace Menu_Helpers;
	Menu     *xfade_menu = manage (new Menu);
	MenuList& items       = xfade_menu->items();
	xfade_menu->set_name ("ArdourContextMenu");
	string str;

	if (xfade->active()) {
		str = _("Mute");
	} else {
		str = _("Unmute");
	}

	items.push_back (MenuElem (str, sigc::bind (sigc::mem_fun(*this, &Editor::toggle_xfade_active), boost::weak_ptr<Crossfade> (xfade))));
	items.push_back (MenuElem (_("Edit"), sigc::bind (sigc::mem_fun(*this, &Editor::edit_xfade), boost::weak_ptr<Crossfade> (xfade))));

	if (xfade->can_follow_overlap()) {

		if (xfade->following_overlap()) {
			str = _("Convert to short");
		} else {
			str = _("Convert to full");
		}

		items.push_back (MenuElem (str, sigc::bind (sigc::mem_fun(*this, &Editor::toggle_xfade_length), xfade)));
	}

	if (many) {
		str = xfade->out()->name();
		str += "->";
		str += xfade->in()->name();
	} else {
		str = _("Crossfade");
	}

	edit_items.push_back (MenuElem (str, *xfade_menu));
	edit_items.push_back (SeparatorElem());
}

void
Editor::xfade_edit_left_region ()
{
	if (clicked_crossfadeview) {
		clicked_crossfadeview->left_view.show_region_editor ();
	}
}

void
Editor::xfade_edit_right_region ()
{
	if (clicked_crossfadeview) {
		clicked_crossfadeview->right_view.show_region_editor ();
	}
}

void
Editor::add_region_context_items (StreamView* sv, boost::shared_ptr<Region> region, Menu_Helpers::MenuList& edit_items)
{
	using namespace Menu_Helpers;
	Gtk::MenuItem* foo_item;
	Menu     *region_menu = manage (new Menu);
	MenuList& items       = region_menu->items();
	region_menu->set_name ("ArdourContextMenu");

	boost::shared_ptr<AudioRegion> ar;
	boost::shared_ptr<MidiRegion>  mr;

	if (region) {
		ar = boost::dynamic_pointer_cast<AudioRegion> (region);
		mr = boost::dynamic_pointer_cast<MidiRegion> (region);

		/* when this particular menu pops up, make the relevant region
		   become selected.
		*/

		region_menu->signal_map_event().connect (
			sigc::bind (sigc::mem_fun(*this, &Editor::set_selected_regionview_from_map_event), sv, boost::weak_ptr<Region>(region)));

		items.push_back (MenuElem (_("Rename..."), sigc::mem_fun(*this, &Editor::rename_region)));
		if (mr && internal_editing()) {
			items.push_back (MenuElem (_("List editor..."), sigc::mem_fun(*this, &Editor::show_midi_list_editor)));
		} else {
			items.push_back (MenuElem (_("Region Properties..."), sigc::mem_fun(*this, &Editor::edit_region)));
		}
	}

	items.push_back (MenuElem (_("Raise to top layer"), sigc::mem_fun(*this, &Editor::raise_region_to_top)));
	items.push_back (MenuElem (_("Lower to bottom layer"), sigc::mem_fun  (*this, &Editor::lower_region_to_bottom)));
	items.push_back (SeparatorElem());
	items.push_back (MenuElem (_("Define Sync Point"), sigc::mem_fun(*this, &Editor::set_region_sync_from_edit_point)));
	if (_edit_point == EditAtMouse) {
		items.back ().set_sensitive (false);
	}
	items.push_back (MenuElem (_("Remove Sync Point"), sigc::mem_fun(*this, &Editor::remove_region_sync)));
	items.push_back (SeparatorElem());

	items.push_back (MenuElem (_("Audition"), sigc::mem_fun(*this, &Editor::play_selected_region)));
	items.push_back (MenuElem (_("Export"), sigc::mem_fun(*this, &Editor::export_region)));
	items.push_back (MenuElem (_("Bounce"), sigc::mem_fun(*this, &Editor::bounce_region_selection)));

	if (ar) {
		items.push_back (MenuElem (_("Spectral Analysis"), sigc::mem_fun(*this, &Editor::analyze_region_selection)));
	}

	items.push_back (SeparatorElem());

	sigc::connection fooc;
	boost::shared_ptr<Region> region_to_check;

	if (region) {
		region_to_check = region;
	} else {
		region_to_check = selection->regions.front()->region();
	}

	items.push_back (CheckMenuElem (_("Lock")));
	CheckMenuItem* region_lock_item = static_cast<CheckMenuItem*>(&items.back());
	if (region_to_check->locked()) {
		region_lock_item->set_active();
	}
	region_lock_item->signal_activate().connect (sigc::mem_fun(*this, &Editor::toggle_region_lock));

	items.push_back (CheckMenuElem (_("Glue to Bars & Beats")));
	CheckMenuItem* bbt_glue_item = static_cast<CheckMenuItem*>(&items.back());

	switch (region_to_check->positional_lock_style()) {
	case Region::MusicTime:
		bbt_glue_item->set_active (true);
		break;
	default:
		bbt_glue_item->set_active (false);
		break;
	}

	bbt_glue_item->signal_activate().connect (sigc::bind (sigc::mem_fun (*this, &Editor::set_region_lock_style), Region::MusicTime));

	items.push_back (CheckMenuElem (_("Mute")));
	CheckMenuItem* region_mute_item = static_cast<CheckMenuItem*>(&items.back());
	fooc = region_mute_item->signal_activate().connect (sigc::mem_fun(*this, &Editor::toggle_region_mute));
	if (region_to_check->muted()) {
		fooc.block (true);
		region_mute_item->set_active();
		fooc.block (false);
	}

	if (!Profile->get_sae()) {
		items.push_back (CheckMenuElem (_("Opaque")));
		CheckMenuItem* region_opaque_item = static_cast<CheckMenuItem*>(&items.back());
		fooc = region_opaque_item->signal_activate().connect (sigc::mem_fun(*this, &Editor::toggle_region_opaque));
		if (region_to_check->opaque()) {
			fooc.block (true);
			region_opaque_item->set_active();
			fooc.block (false);
		}
	}

	items.push_back (CheckMenuElem (_("Original Position"), sigc::mem_fun(*this, &Editor::naturalize)));
	if (region_to_check->at_natural_position()) {
		items.back().set_sensitive (false);
	}

	items.push_back (SeparatorElem());

	if (ar) {

		RegionView* rv = sv->find_view (ar);
		AudioRegionView* arv = dynamic_cast<AudioRegionView*>(rv);

		if (!Profile->get_sae()) {
			items.push_back (MenuElem (_("Reset Envelope"), sigc::mem_fun(*this, &Editor::reset_region_gain_envelopes)));

			items.push_back (CheckMenuElem (_("Envelope Visible")));
			CheckMenuItem* region_envelope_visible_item = static_cast<CheckMenuItem*> (&items.back());
			fooc = region_envelope_visible_item->signal_activate().connect (sigc::mem_fun(*this, &Editor::toggle_gain_envelope_visibility));
			if (arv->envelope_visible()) {
				fooc.block (true);
				region_envelope_visible_item->set_active (true);
				fooc.block (false);
			}

			items.push_back (CheckMenuElem (_("Envelope Active")));
			CheckMenuItem* region_envelope_active_item = static_cast<CheckMenuItem*> (&items.back());
			fooc = region_envelope_active_item->signal_activate().connect (sigc::mem_fun(*this, &Editor::toggle_gain_envelope_active));

			if (ar->envelope_active()) {
				fooc.block (true);
				region_envelope_active_item->set_active (true);
				fooc.block (false);
			}

			items.push_back (SeparatorElem());
		}

		items.push_back (MenuElem (_("Normalize"), sigc::mem_fun(*this, &Editor::normalize_region)));
		if (ar->scale_amplitude() != 1) {
			items.push_back (MenuElem (_("Reset Gain"), sigc::mem_fun(*this, &Editor::reset_region_scale_amplitude)));
		}

	} else if (mr) {
		items.push_back (MenuElem (_("Quantize"), sigc::mem_fun(*this, &Editor::quantize_region)));
		items.push_back (SeparatorElem());
	}

	items.push_back (MenuElem (_("Strip Silence..."), sigc::mem_fun (*this, &Editor::strip_region_silence)));
	items.push_back (MenuElem (_("Reverse"), sigc::mem_fun(*this, &Editor::reverse_region)));
	items.push_back (SeparatorElem());

	/* range related stuff */

	items.push_back (MenuElem (_("Add Single Range"), sigc::mem_fun (*this, &Editor::add_location_from_audio_region)));
	items.push_back (MenuElem (_("Add Range Markers"), sigc::mem_fun (*this, &Editor::add_locations_from_audio_region)));
	if (selection->regions.size() < 2) {
		items.back().set_sensitive (false);
	}

	items.push_back (MenuElem (_("Set Range Selection"), sigc::mem_fun (*this, &Editor::set_selection_from_region)));
	items.push_back (SeparatorElem());

	/* Nudge region */

	Menu *nudge_menu = manage (new Menu());
	MenuList& nudge_items = nudge_menu->items();
	nudge_menu->set_name ("ArdourContextMenu");

	nudge_items.push_back (MenuElem (_("Nudge fwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_forward), false, false))));
	nudge_items.push_back (MenuElem (_("Nudge bwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_backward), false, false))));
	nudge_items.push_back (MenuElem (_("Nudge fwd by capture offset"), (sigc::mem_fun(*this, &Editor::nudge_forward_capture_offset))));
	nudge_items.push_back (MenuElem (_("Nudge bwd by capture offset"), (sigc::mem_fun(*this, &Editor::nudge_backward_capture_offset))));

	items.push_back (MenuElem (_("Nudge"), *nudge_menu));
	items.push_back (SeparatorElem());

	Menu *trim_menu = manage (new Menu);
	MenuList& trim_items = trim_menu->items();
	trim_menu->set_name ("ArdourContextMenu");

	trim_items.push_back (MenuElem (_("Start to edit point"), sigc::mem_fun(*this, &Editor::trim_region_from_edit_point)));
	foo_item = &trim_items.back();
	if (_edit_point == EditAtMouse) {
		foo_item->set_sensitive (false);
	}
	trim_items.push_back (MenuElem (_("Edit point to end"), sigc::mem_fun(*this, &Editor::trim_region_to_edit_point)));
	foo_item = &trim_items.back();
	if (_edit_point == EditAtMouse) {
		foo_item->set_sensitive (false);
	}
	trim_items.push_back (MenuElem (_("Trim To Loop"), sigc::mem_fun(*this, &Editor::trim_region_to_loop)));
	trim_items.push_back (MenuElem (_("Trim To Punch"), sigc::mem_fun(*this, &Editor::trim_region_to_punch)));

	items.push_back (MenuElem (_("Trim"), *trim_menu));
	items.push_back (SeparatorElem());

	items.push_back (MenuElem (_("Split"), (sigc::mem_fun(*this, &Editor::split))));
	region_edit_menu_split_item = &items.back();

	if (_edit_point == EditAtMouse) {
		region_edit_menu_split_item->set_sensitive (false);
	}

	items.push_back (MenuElem (_("Make Mono Regions"), (sigc::mem_fun(*this, &Editor::split_multichannel_region))));
	region_edit_menu_split_multichannel_item = &items.back();

	items.push_back (MenuElem (_("Duplicate"), (sigc::bind (sigc::mem_fun(*this, &Editor::duplicate_dialog), false))));
	items.push_back (MenuElem (_("Multi-Duplicate"), (sigc::bind (sigc::mem_fun(*this, &Editor::duplicate_dialog), true))));
	items.push_back (MenuElem (_("Fill Track"), (sigc::mem_fun(*this, &Editor::region_fill_track))));
	items.push_back (SeparatorElem());
	items.push_back (MenuElem (_("Remove"), sigc::mem_fun(*this, &Editor::remove_selected_regions)));

	/* OK, stick the region submenu at the top of the list, and then add
	   the standard items.
	*/

	/* we have to hack up the region name because "_" has a special
	   meaning for menu titles.
	*/

	string::size_type pos = 0;
	string menu_item_name = (region) ? region->name() : _("Selected regions");

	while ((pos = menu_item_name.find ("_", pos)) != string::npos) {
		menu_item_name.replace (pos, 1, "__");
		pos += 2;
	}

	edit_items.push_back (MenuElem (menu_item_name, *region_menu));
	edit_items.push_back (SeparatorElem());
}

/** Add context menu items relevant to selection ranges.
 * @param edit_items List to add the items to.
 */
void
Editor::add_selection_context_items (Menu_Helpers::MenuList& edit_items)
{
	using namespace Menu_Helpers;

	edit_items.push_back (MenuElem (_("Play Range"), sigc::mem_fun(*this, &Editor::play_selection)));
	edit_items.push_back (MenuElem (_("Loop Range"), sigc::bind (sigc::mem_fun(*this, &Editor::set_loop_from_selection), true)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Spectral Analysis"), sigc::mem_fun(*this, &Editor::analyze_range_selection)));

	if (!selection->regions.empty()) {
		edit_items.push_back (SeparatorElem());
		edit_items.push_back (MenuElem (_("Extend Range to End of Region"), sigc::bind (sigc::mem_fun(*this, &Editor::extend_selection_to_end_of_region), false)));
		edit_items.push_back (MenuElem (_("Extend Range to Start of Region"), sigc::bind (sigc::mem_fun(*this, &Editor::extend_selection_to_start_of_region), false)));
	}

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Silence Range"), sigc::mem_fun(*this, &Editor::separate_region_from_selection)));
	edit_items.push_back (MenuElem (_("Convert to Region in Region List"), sigc::mem_fun(*this, &Editor::new_region_from_selection)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Select All in Range"), sigc::mem_fun(*this, &Editor::select_all_selectables_using_time_selection)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Set Loop from Range"), sigc::bind (sigc::mem_fun(*this, &Editor::set_loop_from_selection), false)));
	edit_items.push_back (MenuElem (_("Set Punch from Range"), sigc::mem_fun(*this, &Editor::set_punch_from_selection)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Add Range Markers"), sigc::mem_fun (*this, &Editor::add_location_from_selection)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Crop Region to Range"), sigc::mem_fun(*this, &Editor::crop_region_to_selection)));
	edit_items.push_back (MenuElem (_("Fill Range with Region"), sigc::mem_fun(*this, &Editor::region_fill_selection)));
	edit_items.push_back (MenuElem (_("Duplicate Range"), sigc::bind (sigc::mem_fun(*this, &Editor::duplicate_dialog), false)));

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Consolidate Range"), sigc::bind (sigc::mem_fun(*this, &Editor::bounce_range_selection), true, false)));
	edit_items.push_back (MenuElem (_("Consolidate Range With Processing"), sigc::bind (sigc::mem_fun(*this, &Editor::bounce_range_selection), true, true)));
	edit_items.push_back (MenuElem (_("Bounce Range to Region List"), sigc::bind (sigc::mem_fun(*this, &Editor::bounce_range_selection), false, false)));
	edit_items.push_back (MenuElem (_("Bounce Range to Region List With Processing"), sigc::bind (sigc::mem_fun(*this, &Editor::bounce_range_selection), false, true)));
	edit_items.push_back (MenuElem (_("Export Range"), sigc::mem_fun(*this, &Editor::export_range)));
}


void
Editor::add_dstream_context_items (Menu_Helpers::MenuList& edit_items)
{
	using namespace Menu_Helpers;

	/* Playback */

	Menu *play_menu = manage (new Menu);
	MenuList& play_items = play_menu->items();
	play_menu->set_name ("ArdourContextMenu");

	play_items.push_back (MenuElem (_("Play from edit point"), sigc::mem_fun(*this, &Editor::play_from_edit_point)));
	play_items.push_back (MenuElem (_("Play from start"), sigc::mem_fun(*this, &Editor::play_from_start)));
	play_items.push_back (MenuElem (_("Play region"), sigc::mem_fun(*this, &Editor::play_selected_region)));
	play_items.push_back (SeparatorElem());
	play_items.push_back (MenuElem (_("Loop Region"), sigc::mem_fun(*this, &Editor::loop_selected_region)));

	edit_items.push_back (MenuElem (_("Play"), *play_menu));

	/* Selection */

	Menu *select_menu = manage (new Menu);
	MenuList& select_items = select_menu->items();
	select_menu->set_name ("ArdourContextMenu");

	select_items.push_back (MenuElem (_("Select All in track"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_in_track), Selection::Set)));
	select_items.push_back (MenuElem (_("Select All"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all), Selection::Set)));
	select_items.push_back (MenuElem (_("Invert selection in track"), sigc::mem_fun(*this, &Editor::invert_selection_in_track)));
	select_items.push_back (MenuElem (_("Invert selection"), sigc::mem_fun(*this, &Editor::invert_selection)));
	select_items.push_back (SeparatorElem());
	select_items.push_back (MenuElem (_("Set range to loop range"), sigc::mem_fun(*this, &Editor::set_selection_from_loop)));
	select_items.push_back (MenuElem (_("Set range to punch range"), sigc::mem_fun(*this, &Editor::set_selection_from_punch)));
	select_items.push_back (SeparatorElem());
	select_items.push_back (MenuElem (_("Select All After Edit Point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_edit), true)));
	select_items.push_back (MenuElem (_("Select All Before Edit Point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_edit), false)));
	select_items.push_back (MenuElem (_("Select All After Playhead"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_cursor), playhead_cursor, true)));
	select_items.push_back (MenuElem (_("Select All Before Playhead"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_cursor), playhead_cursor, false)));
	select_items.push_back (MenuElem (_("Select All Between Playhead & Edit Point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_between), false)));
	select_items.push_back (MenuElem (_("Select All Within Playhead & Edit Point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_between), true)));
	select_items.push_back (MenuElem (_("Select Range Between Playhead & Edit Point"), sigc::mem_fun(*this, &Editor::select_range_between)));

	edit_items.push_back (MenuElem (_("Select"), *select_menu));

	/* Cut-n-Paste */

	Menu *cutnpaste_menu = manage (new Menu);
	MenuList& cutnpaste_items = cutnpaste_menu->items();
	cutnpaste_menu->set_name ("ArdourContextMenu");

	cutnpaste_items.push_back (MenuElem (_("Cut"), sigc::mem_fun(*this, &Editor::cut)));
	cutnpaste_items.push_back (MenuElem (_("Copy"), sigc::mem_fun(*this, &Editor::copy)));
	cutnpaste_items.push_back (MenuElem (_("Paste"), sigc::bind (sigc::mem_fun(*this, &Editor::paste), 1.0f)));

	cutnpaste_items.push_back (SeparatorElem());

	cutnpaste_items.push_back (MenuElem (_("Align"), sigc::bind (sigc::mem_fun(*this, &Editor::align), ARDOUR::SyncPoint)));
	cutnpaste_items.push_back (MenuElem (_("Align Relative"), sigc::bind (sigc::mem_fun(*this, &Editor::align_relative), ARDOUR::SyncPoint)));

	cutnpaste_items.push_back (SeparatorElem());

	edit_items.push_back (MenuElem (_("Edit"), *cutnpaste_menu));

	/* Adding new material */

	edit_items.push_back (SeparatorElem());
	edit_items.push_back (MenuElem (_("Insert Selected Region"), sigc::bind (sigc::mem_fun(*this, &Editor::insert_region_list_selection), 1.0f)));
	edit_items.push_back (MenuElem (_("Insert Existing Media"), sigc::bind (sigc::mem_fun(*this, &Editor::add_external_audio_action), ImportToTrack)));

	/* Nudge track */

	Menu *nudge_menu = manage (new Menu());
	MenuList& nudge_items = nudge_menu->items();
	nudge_menu->set_name ("ArdourContextMenu");

	edit_items.push_back (SeparatorElem());
	nudge_items.push_back (MenuElem (_("Nudge entire track fwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), false, true))));
	nudge_items.push_back (MenuElem (_("Nudge track after edit point fwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), true, true))));
	nudge_items.push_back (MenuElem (_("Nudge entire track bwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), false, false))));
	nudge_items.push_back (MenuElem (_("Nudge track after edit point bwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), true, false))));

	edit_items.push_back (MenuElem (_("Nudge"), *nudge_menu));
}

void
Editor::add_bus_context_items (Menu_Helpers::MenuList& edit_items)
{
	using namespace Menu_Helpers;

	/* Playback */

	Menu *play_menu = manage (new Menu);
	MenuList& play_items = play_menu->items();
	play_menu->set_name ("ArdourContextMenu");

	play_items.push_back (MenuElem (_("Play from edit point"), sigc::mem_fun(*this, &Editor::play_from_edit_point)));
	play_items.push_back (MenuElem (_("Play from start"), sigc::mem_fun(*this, &Editor::play_from_start)));
	edit_items.push_back (MenuElem (_("Play"), *play_menu));

	/* Selection */

	Menu *select_menu = manage (new Menu);
	MenuList& select_items = select_menu->items();
	select_menu->set_name ("ArdourContextMenu");

	select_items.push_back (MenuElem (_("Select All in track"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_in_track), Selection::Set)));
	select_items.push_back (MenuElem (_("Select All"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all), Selection::Set)));
	select_items.push_back (MenuElem (_("Invert selection in track"), sigc::mem_fun(*this, &Editor::invert_selection_in_track)));
	select_items.push_back (MenuElem (_("Invert selection"), sigc::mem_fun(*this, &Editor::invert_selection)));
	select_items.push_back (SeparatorElem());
	select_items.push_back (MenuElem (_("Select all after edit point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_edit), true)));
	select_items.push_back (MenuElem (_("Select all before edit point"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_edit), false)));
	select_items.push_back (MenuElem (_("Select all after playhead"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_cursor), playhead_cursor, true)));
	select_items.push_back (MenuElem (_("Select all before playhead"), sigc::bind (sigc::mem_fun(*this, &Editor::select_all_selectables_using_cursor), playhead_cursor, false)));

	edit_items.push_back (MenuElem (_("Select"), *select_menu));

	/* Cut-n-Paste */

	Menu *cutnpaste_menu = manage (new Menu);
	MenuList& cutnpaste_items = cutnpaste_menu->items();
	cutnpaste_menu->set_name ("ArdourContextMenu");

	cutnpaste_items.push_back (MenuElem (_("Cut"), sigc::mem_fun(*this, &Editor::cut)));
	cutnpaste_items.push_back (MenuElem (_("Copy"), sigc::mem_fun(*this, &Editor::copy)));
	cutnpaste_items.push_back (MenuElem (_("Paste"), sigc::bind (sigc::mem_fun(*this, &Editor::paste), 1.0f)));

	Menu *nudge_menu = manage (new Menu());
	MenuList& nudge_items = nudge_menu->items();
	nudge_menu->set_name ("ArdourContextMenu");

	edit_items.push_back (SeparatorElem());
	nudge_items.push_back (MenuElem (_("Nudge entire track fwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), false, true))));
	nudge_items.push_back (MenuElem (_("Nudge track after edit point fwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), true, true))));
	nudge_items.push_back (MenuElem (_("Nudge entire track bwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), false, false))));
	nudge_items.push_back (MenuElem (_("Nudge track after edit point bwd"), (sigc::bind (sigc::mem_fun(*this, &Editor::nudge_track), true, false))));

	edit_items.push_back (MenuElem (_("Nudge"), *nudge_menu));
}

SnapType
Editor::snap_type() const
{
	return _snap_type;
}

SnapMode
Editor::snap_mode() const
{
	return _snap_mode;
}

void
Editor::set_snap_to (SnapType st)
{
	unsigned int snap_ind = (unsigned int)st;

	_snap_type = st;

	if (snap_ind > snap_type_strings.size() - 1) {
		snap_ind = 0;
		_snap_type = (SnapType)snap_ind;
	}

	string str = snap_type_strings[snap_ind];

	if (str != snap_type_selector.get_active_text()) {
		snap_type_selector.set_active_text (str);
	}

	instant_save ();

	switch (_snap_type) {
	case SnapToAThirtysecondBeat:
	case SnapToASixteenthBeat:
	case SnapToAEighthBeat:
	case SnapToAQuarterBeat:
	case SnapToAThirdBeat:
		compute_bbt_ruler_scale (leftmost_frame, leftmost_frame + current_page_frames());
		update_tempo_based_rulers ();
		break;

	case SnapToRegionStart:
	case SnapToRegionEnd:
	case SnapToRegionSync:
	case SnapToRegionBoundary:
		build_region_boundary_cache ();
		break;

	default:
		/* relax */
		break;
    }
}

void
Editor::set_snap_mode (SnapMode mode)
{
	_snap_mode = mode;
	string str = snap_mode_strings[(int)mode];

	if (str != snap_mode_selector.get_active_text ()) {
		snap_mode_selector.set_active_text (str);
	}

	instant_save ();
}
void
Editor::set_edit_point_preference (EditPoint ep, bool force)
{
	bool changed = (_edit_point != ep);

	_edit_point = ep;
	string str = edit_point_strings[(int)ep];

	if (str != edit_point_selector.get_active_text ()) {
		edit_point_selector.set_active_text (str);
	}

	set_canvas_cursor ();

	if (!force && !changed) {
		return;
	}

	const char* action=NULL;

	switch (_edit_point) {
	case EditAtPlayhead:
		action = "edit-at-playhead";
		break;
	case EditAtSelectedMarker:
		action = "edit-at-marker";
		break;
	case EditAtMouse:
		action = "edit-at-mouse";
		break;
	}

	Glib::RefPtr<Action> act = ActionManager::get_action ("Editor", action);
	if (act) {
		Glib::RefPtr<RadioAction>::cast_dynamic(act)->set_active (true);
	}

	nframes64_t foo;
	bool in_track_canvas;

	if (!mouse_frame (foo, in_track_canvas)) {
		in_track_canvas = false;
	}

	reset_canvas_action_sensitivity (in_track_canvas);

	instant_save ();
}

int
Editor::set_state (const XMLNode& node, int /*version*/)
{
	const XMLProperty* prop;
	XMLNode* geometry;
	int x, y, xoff, yoff;
	Gdk::Geometry g;

	if ((prop = node.property ("id")) != 0) {
		_id = prop->value ();
	}

	g.base_width = default_width;
	g.base_height = default_height;
	x = 1;
	y = 1;
	xoff = 0;
	yoff = 21;

	if ((geometry = find_named_node (node, "geometry")) != 0) {

		XMLProperty* prop;

		if ((prop = geometry->property("x_size")) == 0) {
			prop = geometry->property ("x-size");
		}
		if (prop) {
			g.base_width = atoi(prop->value());
		}
		if ((prop = geometry->property("y_size")) == 0) {
			prop = geometry->property ("y-size");
		}
		if (prop) {
			g.base_height = atoi(prop->value());
		}

		if ((prop = geometry->property ("x_pos")) == 0) {
			prop = geometry->property ("x-pos");
		}
		if (prop) {
			x = atoi (prop->value());

		}
		if ((prop = geometry->property ("y_pos")) == 0) {
			prop = geometry->property ("y-pos");
		}
		if (prop) {
			y = atoi (prop->value());
		}

		if ((prop = geometry->property ("x_off")) == 0) {
			prop = geometry->property ("x-off");
		}
		if (prop) {
			xoff = atoi (prop->value());
		}
		if ((prop = geometry->property ("y_off")) == 0) {
			prop = geometry->property ("y-off");
		}
		if (prop) {
			yoff = atoi (prop->value());
		}
	}

	set_default_size (g.base_width, g.base_height);
	move (x, y);

	if (_session && (prop = node.property ("playhead"))) {
		nframes64_t pos = atol (prop->value().c_str());
		playhead_cursor->set_position (pos);
	} else {
		playhead_cursor->set_position (0);
	}
	
	if ((prop = node.property ("mixer-width"))) {
		editor_mixer_strip_width = Width (string_2_enum (prop->value(), editor_mixer_strip_width));
	}

	if ((prop = node.property ("zoom-focus"))) {
		set_zoom_focus ((ZoomFocus) atoi (prop->value()));
	}

	if ((prop = node.property ("zoom"))) {
		reset_zoom (PBD::atof (prop->value()));
	}

	if ((prop = node.property ("snap-to"))) {
		set_snap_to ((SnapType) atoi (prop->value()));
	}

	if ((prop = node.property ("snap-mode"))) {
		set_snap_mode ((SnapMode) atoi (prop->value()));
	}

	if ((prop = node.property ("mouse-mode"))) {
		MouseMode m = str2mousemode(prop->value());
		mouse_mode = MouseMode ((int) m + 1); /* lie, force mode switch */
		set_mouse_mode (m, true);
	} else {
		mouse_mode = MouseGain; /* lie, to force the mode switch */
		set_mouse_mode (MouseObject, true);
	}

	if ((prop = node.property ("left-frame")) != 0){
		nframes64_t pos;
		if (sscanf (prop->value().c_str(), "%" PRId64, &pos) == 1) {
			reset_x_origin (pos);
			/* this hack prevents the initial call to update_current_screen() from doing re-centering on the playhead */
			last_update_frame = pos;
		}
	}

	if ((prop = node.property ("internal-edit"))) {
		bool yn = string_is_affirmative (prop->value());
		RefPtr<Action> act = ActionManager::get_action (X_("MouseMode"), X_("toggle-internal-edit"));
		if (act) {
			RefPtr<ToggleAction> tact = RefPtr<ToggleAction>::cast_dynamic(act);
			tact->set_active (!yn);
			tact->set_active (yn);
		}
	}

	if ((prop = node.property ("edit-point"))) {
		set_edit_point_preference ((EditPoint) string_2_enum (prop->value(), _edit_point), true);
	}

	if ((prop = node.property ("show-waveforms-recording"))) {
		bool yn = string_is_affirmative (prop->value());
		_show_waveforms_recording = !yn;
		RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleWaveformsWhileRecording"));
		if (act) {
			RefPtr<ToggleAction> tact = RefPtr<ToggleAction>::cast_dynamic(act);
			/* do it twice to force the change */
			tact->set_active (!yn);
			tact->set_active (yn);
		}
	}

	if ((prop = node.property ("show-measures"))) {
		bool yn = string_is_affirmative (prop->value());
		_show_measures = !yn;
		RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleMeasureVisibility"));
		if (act) {
			RefPtr<ToggleAction> tact = RefPtr<ToggleAction>::cast_dynamic(act);
			/* do it twice to force the change */
			tact->set_active (!yn);
			tact->set_active (yn);
		}
	}

	if ((prop = node.property ("follow-playhead"))) {
		bool yn = string_is_affirmative (prop->value());
		set_follow_playhead (yn);
		RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("toggle-follow-playhead"));
		if (act) {
			RefPtr<ToggleAction> tact = RefPtr<ToggleAction>::cast_dynamic(act);
			if (tact->get_active() != yn) {
				tact->set_active (yn);
			}
		}
	}

	if ((prop = node.property ("region-list-sort-type"))) {
		_regions->reset_sort_type (str2regionlistsorttype(prop->value()), true);
	}

	if ((prop = node.property ("xfades-visible"))) {
		bool yn = string_is_affirmative (prop->value());
		_xfade_visibility = !yn;
		// set_xfade_visibility (yn);
	}

	if ((prop = node.property ("show-editor-mixer"))) {

		Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("show-editor-mixer"));
		if (act) {

			Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
			bool yn = string_is_affirmative (prop->value());

			/* do it twice to force the change */

			tact->set_active (!yn);
			tact->set_active (yn);
		}
	}

	if ((prop = node.property ("show-editor-list"))) {

		Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("show-editor-list"));
		assert(act);
		if (act) {

			Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
			bool yn = string_is_affirmative (prop->value());

			/* do it twice to force the change */

			tact->set_active (!yn);
			tact->set_active (yn);
		}
	}


	return 0;
}

XMLNode&
Editor::get_state ()
{
	XMLNode* node = new XMLNode ("Editor");
	char buf[32];

	_id.print (buf, sizeof (buf));
	node->add_property ("id", buf);

	if (is_realized()) {
		Glib::RefPtr<Gdk::Window> win = get_window();

		int x, y, xoff, yoff, width, height;
		win->get_root_origin(x, y);
		win->get_position(xoff, yoff);
		win->get_size(width, height);

		XMLNode* geometry = new XMLNode ("geometry");

		snprintf(buf, sizeof(buf), "%d", width);
		geometry->add_property("x-size", string(buf));
		snprintf(buf, sizeof(buf), "%d", height);
		geometry->add_property("y-size", string(buf));
		snprintf(buf, sizeof(buf), "%d", x);
		geometry->add_property("x-pos", string(buf));
		snprintf(buf, sizeof(buf), "%d", y);
		geometry->add_property("y-pos", string(buf));
		snprintf(buf, sizeof(buf), "%d", xoff);
		geometry->add_property("x-off", string(buf));
		snprintf(buf, sizeof(buf), "%d", yoff);
		geometry->add_property("y-off", string(buf));
		snprintf(buf,sizeof(buf), "%d",gtk_paned_get_position (static_cast<Paned*>(&edit_pane)->gobj()));
		geometry->add_property("edit_pane_pos", string(buf));

		node->add_child_nocopy (*geometry);
	}

	maybe_add_mixer_strip_width (*node);

	snprintf (buf, sizeof(buf), "%d", (int) zoom_focus);
	node->add_property ("zoom-focus", buf);
	snprintf (buf, sizeof(buf), "%f", frames_per_unit);
	node->add_property ("zoom", buf);
	snprintf (buf, sizeof(buf), "%d", (int) _snap_type);
	node->add_property ("snap-to", buf);
	snprintf (buf, sizeof(buf), "%d", (int) _snap_mode);
	node->add_property ("snap-mode", buf);

	node->add_property ("edit-point", enum_2_string (_edit_point));

	snprintf (buf, sizeof (buf), "%" PRIi64, playhead_cursor->current_frame);
	node->add_property ("playhead", buf);
	snprintf (buf, sizeof (buf), "%" PRIi64, leftmost_frame);
	node->add_property ("left-frame", buf);

	node->add_property ("show-waveforms-recording", _show_waveforms_recording ? "yes" : "no");
	node->add_property ("show-measures", _show_measures ? "yes" : "no");
	node->add_property ("follow-playhead", _follow_playhead ? "yes" : "no");
	node->add_property ("xfades-visible", _xfade_visibility ? "yes" : "no");
	node->add_property ("region-list-sort-type", enum2str (_regions->sort_type ()));
	node->add_property ("mouse-mode", enum2str(mouse_mode));
	node->add_property ("internal-edit", _internal_editing ? "yes" : "no");

	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("show-editor-mixer"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		node->add_property (X_("show-editor-mixer"), tact->get_active() ? "yes" : "no");
	}

	act = ActionManager::get_action (X_("Editor"), X_("show-editor-list"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		node->add_property (X_("show-editor-list"), tact->get_active() ? "yes" : "no");
	}

	return *node;
}



/** @param y y offset from the top of all trackviews.
 *  @return pair: TimeAxisView that y is over, layer index.
 *  TimeAxisView may be 0.  Layer index is the layer number if the TimeAxisView is valid and is
 *  in stacked region display mode, otherwise 0.
 */
std::pair<TimeAxisView *, layer_t>
Editor::trackview_by_y_position (double y)
{
	for (TrackViewList::iterator iter = track_views.begin(); iter != track_views.end(); ++iter) {

		std::pair<TimeAxisView*, int> const r = (*iter)->covers_y_position (y);
		if (r.first) {
			return r;
		}
	}

	return std::make_pair ( (TimeAxisView *) 0, 0);
}

/** Snap a position to the grid, if appropriate, taking into account current
 *  grid settings and also the state of any snap modifier keys that may be pressed.
 *  @param start Position to snap.
 *  @param event Event to get current key modifier information from, or 0.
 */
void
Editor::snap_to_with_modifier (nframes64_t& start, GdkEvent const * event, int32_t direction, bool for_mark)
{
	if (!_session || !event) {
		return;
	}

	if (Keyboard::modifier_state_contains (event->button.state, Keyboard::snap_modifier())) {
		if (_snap_mode == SnapOff) {
			snap_to_internal (start, direction, for_mark);
		}
	} else {
		if (_snap_mode != SnapOff) {
			snap_to_internal (start, direction, for_mark);
		}
	}
}

void
Editor::snap_to (nframes64_t& start, int32_t direction, bool for_mark)
{
	if (!_session || _snap_mode == SnapOff) {
		return;
	}

	snap_to_internal (start, direction, for_mark);
}

void
Editor::timecode_snap_to_internal (nframes64_t& start, int32_t direction, bool /*for_mark*/)
{
	const nframes64_t one_timecode_second = (nframes64_t)(rint(_session->timecode_frames_per_second()) * _session->frames_per_timecode_frame());
	nframes64_t one_timecode_minute = (nframes64_t)(rint(_session->timecode_frames_per_second()) * _session->frames_per_timecode_frame() * 60);

	switch (_snap_type) {
	case SnapToTimecodeFrame:
		if (((direction == 0) && (fmod((double)start, (double)_session->frames_per_timecode_frame()) > (_session->frames_per_timecode_frame() / 2))) || (direction > 0)) {
			start = (nframes64_t) (ceil ((double) start / _session->frames_per_timecode_frame()) * _session->frames_per_timecode_frame());
		} else {
			start = (nframes64_t) (floor ((double) start / _session->frames_per_timecode_frame()) *  _session->frames_per_timecode_frame());
		}
		break;

	case SnapToTimecodeSeconds:
		if (_session->timecode_offset_negative())
		{
			start += _session->timecode_offset ();
		} else {
			start -= _session->timecode_offset ();
		}
		if (((direction == 0) && (start % one_timecode_second > one_timecode_second / 2)) || direction > 0) {
			start = (nframes64_t) ceil ((double) start / one_timecode_second) * one_timecode_second;
		} else {
			start = (nframes64_t) floor ((double) start / one_timecode_second) * one_timecode_second;
		}

		if (_session->timecode_offset_negative())
		{
			start -= _session->timecode_offset ();
		} else {
			start += _session->timecode_offset ();
		}
		break;

	case SnapToTimecodeMinutes:
		if (_session->timecode_offset_negative())
		{
			start += _session->timecode_offset ();
		} else {
			start -= _session->timecode_offset ();
		}
		if (((direction == 0) && (start % one_timecode_minute > one_timecode_minute / 2)) || direction > 0) {
			start = (nframes64_t) ceil ((double) start / one_timecode_minute) * one_timecode_minute;
		} else {
			start = (nframes64_t) floor ((double) start / one_timecode_minute) * one_timecode_minute;
		}
		if (_session->timecode_offset_negative())
		{
			start -= _session->timecode_offset ();
		} else {
			start += _session->timecode_offset ();
		}
		break;
	default:
		fatal << "Editor::smpte_snap_to_internal() called with non-timecode snap type!" << endmsg;
		/*NOTREACHED*/
	}
}

void
Editor::snap_to_internal (nframes64_t& start, int32_t direction, bool for_mark)
{
	const nframes64_t one_second = _session->frame_rate();
	const nframes64_t one_minute = _session->frame_rate() * 60;
	nframes64_t presnap = start;
	nframes64_t before;
	nframes64_t after;

	switch (_snap_type) {
	case SnapToTimecodeFrame:
	case SnapToTimecodeSeconds:
	case SnapToTimecodeMinutes:
		return timecode_snap_to_internal (start, direction, for_mark);

	case SnapToCDFrame:
		if (((direction == 0) && (start % (one_second/75) > (one_second/75) / 2)) || (direction > 0)) {
			start = (nframes64_t) ceil ((double) start / (one_second / 75)) * (one_second / 75);
		} else {
			start = (nframes64_t) floor ((double) start / (one_second / 75)) * (one_second / 75);
		}
		break;

	case SnapToSeconds:
		if (((direction == 0) && (start % one_second > one_second / 2)) || (direction > 0)) {
			start = (nframes64_t) ceil ((double) start / one_second) * one_second;
		} else {
			start = (nframes64_t) floor ((double) start / one_second) * one_second;
		}
		break;

	case SnapToMinutes:
		if (((direction == 0) && (start % one_minute > one_minute / 2)) || (direction > 0)) {
			start = (nframes64_t) ceil ((double) start / one_minute) * one_minute;
		} else {
			start = (nframes64_t) floor ((double) start / one_minute) * one_minute;
		}
		break;

	case SnapToBar:
		start = _session->tempo_map().round_to_bar (start, direction);
		break;

	case SnapToBeat:
		start = _session->tempo_map().round_to_beat (start, direction);
		break;

	case SnapToAThirtysecondBeat:
		start = _session->tempo_map().round_to_beat_subdivision (start, 32, direction);
		break;

	case SnapToASixteenthBeat:
		start = _session->tempo_map().round_to_beat_subdivision (start, 16, direction);
		break;

	case SnapToAEighthBeat:
		start = _session->tempo_map().round_to_beat_subdivision (start, 8, direction);
		break;

	case SnapToAQuarterBeat:
		start = _session->tempo_map().round_to_beat_subdivision (start, 4, direction);
		break;

	case SnapToAThirdBeat:
		start = _session->tempo_map().round_to_beat_subdivision (start, 3, direction);
		break;

	case SnapToMark:
		if (for_mark) {
			return;
		}

		_session->locations()->marks_either_side (start, before, after);

		if (before == max_frames) {
			start = after;
		} else if (after == max_frames) {
			start = before;
		} else if (before != max_frames && after != max_frames) {
			/* have before and after */
			if ((start - before) < (after - start)) {
				start = before;
			} else {
				start = after;
			}
		}

		break;

	case SnapToRegionStart:
	case SnapToRegionEnd:
	case SnapToRegionSync:
	case SnapToRegionBoundary:
		if (!region_boundary_cache.empty()) {

			vector<nframes64_t>::iterator prev = region_boundary_cache.end ();
			vector<nframes64_t>::iterator next = region_boundary_cache.end ();

			if (direction > 0) {
				next = std::upper_bound (region_boundary_cache.begin(), region_boundary_cache.end(), start);
			} else {
				next = std::lower_bound (region_boundary_cache.begin(), region_boundary_cache.end(), start);
			}

			if (next != region_boundary_cache.begin ()) {
				prev = next;
				prev--;
			}

			nframes64_t const p = (prev == region_boundary_cache.end()) ? region_boundary_cache.front () : *prev;
			nframes64_t const n = (next == region_boundary_cache.end()) ? region_boundary_cache.back () : *next;

			if (start > (p + n) / 2) {
				start = n;
			} else {
				start = p;
			}
		}
		break;
	}

	switch (_snap_mode) {
	case SnapNormal:
		return;

	case SnapMagnetic:

		if (presnap > start) {
			if (presnap > (start + unit_to_frame(snap_threshold))) {
				start = presnap;
			}

		} else if (presnap < start) {
			if (presnap < (start - unit_to_frame(snap_threshold))) {
				start = presnap;
			}
		}

	default:
		/* handled at entry */
		return;

	}
}


void
Editor::setup_toolbar ()
{
	string pixmap_path;

	/* Mode Buttons (tool selection) */

	mouse_move_button.set_relief(Gtk::RELIEF_NONE);
	mouse_select_button.set_relief(Gtk::RELIEF_NONE);
	mouse_gain_button.set_relief(Gtk::RELIEF_NONE);
	mouse_zoom_button.set_relief(Gtk::RELIEF_NONE);
	mouse_timefx_button.set_relief(Gtk::RELIEF_NONE);
	mouse_audition_button.set_relief(Gtk::RELIEF_NONE);
	// internal_edit_button.set_relief(Gtk::RELIEF_NONE);

	HBox* mode_box = manage(new HBox);
	mode_box->set_border_width (2);
	mode_box->set_spacing(4);
	mouse_mode_button_box.set_spacing(1);
	mouse_mode_button_box.pack_start(mouse_move_button, true, true);
	if (!Profile->get_sae()) {
		mouse_mode_button_box.pack_start(mouse_select_button, true, true);
	}
	mouse_mode_button_box.pack_start(mouse_zoom_button, true, true);
	if (!Profile->get_sae()) {
		mouse_mode_button_box.pack_start(mouse_gain_button, true, true);
	}
	mouse_mode_button_box.pack_start(mouse_timefx_button, true, true);
	mouse_mode_button_box.pack_start(mouse_audition_button, true, true);
	mouse_mode_button_box.pack_start(internal_edit_button, true, true);
	mouse_mode_button_box.set_homogeneous(true);

	vector<string> edit_mode_strings;
	edit_mode_strings.push_back (edit_mode_to_string (Slide));
	if (!Profile->get_sae()) {
		edit_mode_strings.push_back (edit_mode_to_string (Splice));
	}
	edit_mode_strings.push_back (edit_mode_to_string (Lock));

	edit_mode_selector.set_name ("EditModeSelector");
	set_popdown_strings (edit_mode_selector, edit_mode_strings, true);
	edit_mode_selector.signal_changed().connect (sigc::mem_fun(*this, &Editor::edit_mode_selection_done));

	mode_box->pack_start(edit_mode_selector);
	mode_box->pack_start(mouse_mode_button_box);

	mouse_mode_tearoff = manage (new TearOff (*mode_box));
	mouse_mode_tearoff->set_name ("MouseModeBase");
	mouse_mode_tearoff->tearoff_window().signal_key_press_event().connect (sigc::bind (sigc::ptr_fun (relay_key_press), &mouse_mode_tearoff->tearoff_window()), false);

	if (Profile->get_sae()) {
		mouse_mode_tearoff->set_can_be_torn_off (false);
	}

	mouse_mode_tearoff->Detach.connect (sigc::bind (sigc::mem_fun(*this, &Editor::detach_tearoff), static_cast<Box*>(&toolbar_hbox),
						  &mouse_mode_tearoff->tearoff_window()));
	mouse_mode_tearoff->Attach.connect (sigc::bind (sigc::mem_fun(*this, &Editor::reattach_tearoff), static_cast<Box*> (&toolbar_hbox),
						  &mouse_mode_tearoff->tearoff_window(), 1));
	mouse_mode_tearoff->Hidden.connect (sigc::bind (sigc::mem_fun(*this, &Editor::detach_tearoff), static_cast<Box*>(&toolbar_hbox),
						  &mouse_mode_tearoff->tearoff_window()));
	mouse_mode_tearoff->Visible.connect (sigc::bind (sigc::mem_fun(*this, &Editor::reattach_tearoff), static_cast<Box*> (&toolbar_hbox),
						   &mouse_mode_tearoff->tearoff_window(), 1));

	mouse_move_button.set_mode (false);
	mouse_select_button.set_mode (false);
	mouse_gain_button.set_mode (false);
	mouse_zoom_button.set_mode (false);
	mouse_timefx_button.set_mode (false);
	mouse_audition_button.set_mode (false);

	mouse_move_button.set_name ("MouseModeButton");
	mouse_select_button.set_name ("MouseModeButton");
	mouse_gain_button.set_name ("MouseModeButton");
	mouse_zoom_button.set_name ("MouseModeButton");
	mouse_timefx_button.set_name ("MouseModeButton");
	mouse_audition_button.set_name ("MouseModeButton");

	internal_edit_button.set_name ("MouseModeButton");

	mouse_move_button.unset_flags (CAN_FOCUS);
	mouse_select_button.unset_flags (CAN_FOCUS);
	mouse_gain_button.unset_flags (CAN_FOCUS);
	mouse_zoom_button.unset_flags (CAN_FOCUS);
	mouse_timefx_button.unset_flags (CAN_FOCUS);
	mouse_audition_button.unset_flags (CAN_FOCUS);
	internal_edit_button.unset_flags (CAN_FOCUS);

	/* Zoom */

	zoom_box.set_spacing (1);
	zoom_box.set_border_width (0);

	zoom_in_button.set_name ("EditorTimeButton");
	zoom_in_button.set_image (*(manage (new Image (Stock::ZOOM_IN, Gtk::ICON_SIZE_BUTTON))));
	zoom_in_button.signal_clicked().connect (sigc::bind (sigc::mem_fun(*this, &Editor::temporal_zoom_step), false));
	ARDOUR_UI::instance()->tooltips().set_tip (zoom_in_button, _("Zoom In"));

	zoom_out_button.set_name ("EditorTimeButton");
	zoom_out_button.set_image (*(manage (new Image (Stock::ZOOM_OUT, Gtk::ICON_SIZE_BUTTON))));
	zoom_out_button.signal_clicked().connect (sigc::bind (sigc::mem_fun(*this, &Editor::temporal_zoom_step), true));
	ARDOUR_UI::instance()->tooltips().set_tip (zoom_out_button, _("Zoom Out"));

	zoom_out_full_button.set_name ("EditorTimeButton");
	zoom_out_full_button.set_image (*(manage (new Image (Stock::ZOOM_100, Gtk::ICON_SIZE_BUTTON))));
	zoom_out_full_button.signal_clicked().connect (sigc::mem_fun(*this, &Editor::temporal_zoom_session));
	ARDOUR_UI::instance()->tooltips().set_tip (zoom_out_full_button, _("Zoom to Session"));

	zoom_focus_selector.set_name ("ZoomFocusSelector");
	set_popdown_strings (zoom_focus_selector, zoom_focus_strings, true);
	zoom_focus_selector.signal_changed().connect (sigc::mem_fun(*this, &Editor::zoom_focus_selection_done));
	ARDOUR_UI::instance()->tooltips().set_tip (zoom_focus_selector, _("Zoom focus"));

	zoom_box.pack_start (zoom_out_button, false, false);
	zoom_box.pack_start (zoom_in_button, false, false);
	zoom_box.pack_start (zoom_out_full_button, false, false);

	/* Track zoom buttons */
	tav_expand_button.set_name ("TrackHeightButton");
	tav_expand_button.set_size_request(-1,20);
	tav_expand_button.add (*(manage (new Image (::get_icon("tav_exp")))));
	tav_expand_button.signal_clicked().connect (sigc::bind (sigc::mem_fun(*this, &Editor::tav_zoom_step), true));
	ARDOUR_UI::instance()->tooltips().set_tip (tav_expand_button, _("Expand Tracks"));

	tav_shrink_button.set_name ("TrackHeightButton");
	tav_shrink_button.set_size_request(-1,20);
	tav_shrink_button.add (*(manage (new Image (::get_icon("tav_shrink")))));
	tav_shrink_button.signal_clicked().connect (sigc::bind (sigc::mem_fun(*this, &Editor::tav_zoom_step), false));
	ARDOUR_UI::instance()->tooltips().set_tip (tav_shrink_button, _("Shrink Tracks"));

	track_zoom_box.set_spacing (1);
	track_zoom_box.set_border_width (0);

	track_zoom_box.pack_start (tav_shrink_button, false, false);
	track_zoom_box.pack_start (tav_expand_button, false, false);

	HBox* zbc = manage (new HBox);
	zbc->pack_start (zoom_focus_selector, PACK_SHRINK);
	zoom_vbox.pack_start (*zbc, PACK_SHRINK);
	zoom_vbox.pack_start (zoom_box, PACK_SHRINK);
	zoom_vbox.pack_start (track_zoom_box, PACK_SHRINK);

	snap_box.set_spacing (1);
	snap_box.set_border_width (2);

	snap_type_selector.set_name ("SnapTypeSelector");
	set_popdown_strings (snap_type_selector, snap_type_strings, true);
	snap_type_selector.signal_changed().connect (sigc::mem_fun(*this, &Editor::snap_type_selection_done));
	ARDOUR_UI::instance()->tooltips().set_tip (snap_type_selector, _("Snap/Grid Units"));

	snap_mode_selector.set_name ("SnapModeSelector");
	set_popdown_strings (snap_mode_selector, snap_mode_strings, true);
	snap_mode_selector.signal_changed().connect (sigc::mem_fun(*this, &Editor::snap_mode_selection_done));
	ARDOUR_UI::instance()->tooltips().set_tip (snap_mode_selector, _("Snap/Grid Mode"));

	edit_point_selector.set_name ("EditPointSelector");
	set_popdown_strings (edit_point_selector, edit_point_strings, true);
	edit_point_selector.signal_changed().connect (sigc::mem_fun(*this, &Editor::edit_point_selection_done));
	ARDOUR_UI::instance()->tooltips().set_tip (edit_point_selector, _("Edit point"));

	snap_box.pack_start (snap_mode_selector, false, false);
	snap_box.pack_start (snap_type_selector, false, false);
	snap_box.pack_start (edit_point_selector, false, false);

	/* Nudge */

	HBox *nudge_box = manage (new HBox);
	nudge_box->set_spacing(1);
	nudge_box->set_border_width (2);

	nudge_forward_button.signal_button_release_event().connect (sigc::mem_fun(*this, &Editor::nudge_forward_release), false);
	nudge_backward_button.signal_button_release_event().connect (sigc::mem_fun(*this, &Editor::nudge_backward_release), false);

	nudge_box->pack_start (nudge_backward_button, false, false);
	nudge_box->pack_start (nudge_forward_button, false, false);
	nudge_box->pack_start (nudge_clock, false, false);


	/* Pack everything in... */

	HBox* hbox = manage (new HBox);
	hbox->set_spacing(10);

	tools_tearoff = manage (new TearOff (*hbox));
	tools_tearoff->set_name ("MouseModeBase");
	tools_tearoff->tearoff_window().signal_key_press_event().connect (sigc::bind (sigc::ptr_fun (relay_key_press), &tools_tearoff->tearoff_window()), false);

	if (Profile->get_sae()) {
		tools_tearoff->set_can_be_torn_off (false);
	}

	tools_tearoff->Detach.connect (sigc::bind (sigc::mem_fun(*this, &Editor::detach_tearoff), static_cast<Box*>(&toolbar_hbox),
					     &tools_tearoff->tearoff_window()));
	tools_tearoff->Attach.connect (sigc::bind (sigc::mem_fun(*this, &Editor::reattach_tearoff), static_cast<Box*> (&toolbar_hbox),
					     &tools_tearoff->tearoff_window(), 0));
	tools_tearoff->Hidden.connect (sigc::bind (sigc::mem_fun(*this, &Editor::detach_tearoff), static_cast<Box*>(&toolbar_hbox),
					     &tools_tearoff->tearoff_window()));
	tools_tearoff->Visible.connect (sigc::bind (sigc::mem_fun(*this, &Editor::reattach_tearoff), static_cast<Box*> (&toolbar_hbox),
					      &tools_tearoff->tearoff_window(), 0));

	toolbar_hbox.set_spacing (10);
	toolbar_hbox.set_border_width (1);

	toolbar_hbox.pack_start (*mouse_mode_tearoff, false, false);
	toolbar_hbox.pack_start (*tools_tearoff, false, false);

	hbox->pack_start (snap_box, false, false);
	hbox->pack_start (*nudge_box, false, false);
	hbox->pack_start (panic_box, false, false);

	hbox->show_all ();

	toolbar_base.set_name ("ToolBarBase");
	toolbar_base.add (toolbar_hbox);

	toolbar_frame.set_shadow_type (SHADOW_OUT);
	toolbar_frame.set_name ("BaseFrame");
	toolbar_frame.add (toolbar_base);
}

void
Editor::midi_panic ()
{
	cerr << "MIDI panic\n";

	if (_session) {
		_session->midi_panic();
	}
}

void
Editor::setup_midi_toolbar ()
{
	RefPtr<Action> act;

	/* Midi sound notes */
	midi_sound_notes.add (*(manage (new Image (::get_icon("midi_sound_notes")))));
	midi_sound_notes.set_relief(Gtk::RELIEF_NONE);
	ARDOUR_UI::instance()->tooltips().set_tip (midi_sound_notes, _("Sound Notes"));
	midi_sound_notes.unset_flags (CAN_FOCUS);

	/* Panic */

	act = ActionManager::get_action (X_("MIDI"), X_("panic"));
	midi_panic_button.set_name("MidiPanicButton");
	ARDOUR_UI::instance()->tooltips().set_tip (midi_panic_button, _("Send note off and reset controller messages on all MIDI channels"));
	act->connect_proxy (midi_panic_button);

	panic_box.pack_start (midi_sound_notes , true, true);
	panic_box.pack_start (midi_panic_button, true, true);
}

int
Editor::convert_drop_to_paths (
		vector<ustring>&                paths,
		const RefPtr<Gdk::DragContext>& /*context*/,
		gint                            /*x*/,
		gint                            /*y*/,
		const SelectionData&            data,
		guint                           /*info*/,
		guint                           /*time*/)
{
	if (_session == 0) {
		return -1;
	}

	vector<ustring> uris = data.get_uris();

	if (uris.empty()) {

		/* This is seriously fucked up. Nautilus doesn't say that its URI lists
		   are actually URI lists. So do it by hand.
		*/

		if (data.get_target() != "text/plain") {
			return -1;
		}

		/* Parse the "uri-list" format that Nautilus provides,
		   where each pathname is delimited by \r\n.

		   THERE MAY BE NO NULL TERMINATING CHAR!!!
		*/

		ustring txt = data.get_text();
		const char* p;
		const char* q;

		p = (const char *) malloc (txt.length() + 1);
		txt.copy ((char *) p, txt.length(), 0);
		((char*)p)[txt.length()] = '\0';

		while (p)
		{
			if (*p != '#')
			{
				while (g_ascii_isspace (*p))
					p++;

				q = p;
				while (*q && (*q != '\n') && (*q != '\r')) {
					q++;
				}

				if (q > p)
				{
					q--;
					while (q > p && g_ascii_isspace (*q))
						q--;

					if (q > p)
					{
						uris.push_back (ustring (p, q - p + 1));
					}
				}
			}
			p = strchr (p, '\n');
			if (p)
				p++;
		}

		free ((void*)p);

		if (uris.empty()) {
			return -1;
		}
	}

	for (vector<ustring>::iterator i = uris.begin(); i != uris.end(); ++i) {

		if ((*i).substr (0,7) == "file://") {

			ustring p = *i;
			PBD::url_decode (p);

			// scan forward past three slashes

			ustring::size_type slashcnt = 0;
			ustring::size_type n = 0;
			ustring::iterator x = p.begin();

			while (slashcnt < 3 && x != p.end()) {
				if ((*x) == '/') {
					slashcnt++;
				} else if (slashcnt == 3) {
					break;
				}
				++n;
				++x;
			}

			if (slashcnt != 3 || x == p.end()) {
				error << _("malformed URL passed to drag-n-drop code") << endmsg;
				continue;
			}

			paths.push_back (p.substr (n - 1));
		}
	}

	return 0;
}

void
Editor::new_tempo_section ()

{
}

void
Editor::map_transport_state ()
{
	ENSURE_GUI_THREAD (*this, &Editor::map_transport_state)

	if (_session->transport_stopped()) {
		have_pending_keyboard_selection = false;
	}

	update_loop_range_view (true);
}

/* UNDO/REDO */

Editor::State::State (PublicEditor const * e)
{
	selection = new Selection (e);
}

Editor::State::~State ()
{
	delete selection;
}

void
Editor::begin_reversible_command (string name)
{
	if (_session) {
		before = &get_state();
		_session->begin_reversible_command (name);
	}
}

void
Editor::commit_reversible_command ()
{
	if (_session) {
		_session->commit_reversible_command (new MementoCommand<Editor>(*this, before, &get_state()));
	}
}

void
Editor::set_route_group_solo (Route& route, bool yn)
{
	RouteGroup *route_group;

	if ((route_group = route.route_group()) != 0) {
		route_group->apply (&Route::set_solo, yn, this);
	} else {
		route.set_solo (yn, this);
	}
}

void
Editor::set_route_group_mute (Route& route, bool yn)
{
	RouteGroup *route_group = 0;

	if ((route_group == route.route_group()) != 0) {
		route_group->apply (&Route::set_mute, yn, this);
	} else {
		route.set_mute (yn, this);
	}
}

void
Editor::history_changed ()
{
	string label;

	if (undo_action && _session) {
		if (_session->undo_depth() == 0) {
			label = _("Undo");
		} else {
			label = string_compose(_("Undo (%1)"), _session->next_undo());
		}
		undo_action->property_label() = label;
	}

	if (redo_action && _session) {
		if (_session->redo_depth() == 0) {
			label = _("Redo");
		} else {
			label = string_compose(_("Redo (%1)"), _session->next_redo());
		}
		redo_action->property_label() = label;
	}
}

void
Editor::duplicate_dialog (bool with_dialog)
{
	float times = 1.0f;

	if (mouse_mode == MouseRange) {
		if (selection->time.length() == 0) {
			return;
		}
	}

	RegionSelection rs;
	get_regions_for_action (rs);

	if (mouse_mode != MouseRange) {

		if (rs.empty()) {
			return;
		}
	}

	if (with_dialog) {

		ArdourDialog win ("Duplicate");
		Label  label (_("Number of Duplications:"));
		Adjustment adjustment (1.0, 1.0, 1000000.0, 1.0, 5.0);
		SpinButton spinner (adjustment, 0.0, 1);
		HBox hbox;

		win.get_vbox()->set_spacing (12);
		win.get_vbox()->pack_start (hbox);
		hbox.set_border_width (6);
		hbox.pack_start (label, PACK_EXPAND_PADDING, 12);

		/* dialogs have ::add_action_widget() but that puts the spinner in the wrong
		   place, visually. so do this by hand.
		*/

		hbox.pack_start (spinner, PACK_EXPAND_PADDING, 12);
		spinner.signal_activate().connect (sigc::bind (sigc::mem_fun (win, &ArdourDialog::response), RESPONSE_ACCEPT));
		spinner.grab_focus();

		hbox.show ();
		label.show ();
		spinner.show ();

		win.add_button (Stock::CANCEL, RESPONSE_CANCEL);
		win.add_button (_("Duplicate"), RESPONSE_ACCEPT);
		win.set_default_response (RESPONSE_ACCEPT);

		win.set_position (WIN_POS_MOUSE);

		spinner.grab_focus ();

		switch (win.run ()) {
		case RESPONSE_ACCEPT:
			break;
		default:
			return;
		}

		times = adjustment.get_value();
	}

	if (mouse_mode == MouseRange) {
		duplicate_selection (times);
	} else {
		duplicate_some_regions (rs, times);
	}
}

void
Editor::show_verbose_canvas_cursor ()
{
	verbose_canvas_cursor->raise_to_top();
	verbose_canvas_cursor->show();
	verbose_cursor_visible = true;
}

void
Editor::hide_verbose_canvas_cursor ()
{
	verbose_canvas_cursor->hide();
	verbose_cursor_visible = false;
}

double
Editor::clamp_verbose_cursor_x (double x)
{
	if (x < 0) {
		x = 0;
	} else {
		x = min (_canvas_width - 200.0, x);
	}
	return x;
}

double
Editor::clamp_verbose_cursor_y (double y)
{
	if (y < canvas_timebars_vsize) {
		y = canvas_timebars_vsize;
	} else {
		y = min (_canvas_height - 50, y);
	}
	return y;
}

void
Editor::show_verbose_canvas_cursor_with (const string & txt)
{
	verbose_canvas_cursor->property_text() = txt.c_str();

	int x, y;
	double wx, wy;

	track_canvas->get_pointer (x, y);
	track_canvas->window_to_world (x, y, wx, wy);

	/* don't get too close to the edge */
	verbose_canvas_cursor->property_x() = clamp_verbose_cursor_x (wx);
	verbose_canvas_cursor->property_y() = clamp_verbose_cursor_y (wy);

	show_verbose_canvas_cursor ();
}

void
Editor::set_verbose_canvas_cursor (const string & txt, double x, double y)
{
	verbose_canvas_cursor->property_text() = txt.c_str();
	/* don't get too close to the edge */
	verbose_canvas_cursor->property_x() = clamp_verbose_cursor_x (x);
	verbose_canvas_cursor->property_y() = clamp_verbose_cursor_y (y);
}

void
Editor::set_verbose_canvas_cursor_text (const string & txt)
{
	verbose_canvas_cursor->property_text() = txt.c_str();
}

void
Editor::set_edit_mode (EditMode m)
{
	Config->set_edit_mode (m);
}

void
Editor::cycle_edit_mode ()
{
	switch (Config->get_edit_mode()) {
	case Slide:
		if (Profile->get_sae()) {
			Config->set_edit_mode (Lock);
		} else {
			Config->set_edit_mode (Splice);
		}
		break;
	case Splice:
		Config->set_edit_mode (Lock);
		break;
	case Lock:
		Config->set_edit_mode (Slide);
		break;
	}
}

void
Editor::edit_mode_selection_done ()
{
	if (_session == 0) {
		return;
	}

	string choice = edit_mode_selector.get_active_text();
	EditMode mode = Slide;

	if (choice == _("Splice Edit")) {
		mode = Splice;
	} else if (choice == _("Slide Edit")) {
		mode = Slide;
	} else if (choice == _("Lock Edit")) {
		mode = Lock;
	}

	Config->set_edit_mode (mode);
}

void
Editor::snap_type_selection_done ()
{
	string choice = snap_type_selector.get_active_text();
	SnapType snaptype = SnapToBeat;

	if (choice == _("Beats/3")) {
		snaptype = SnapToAThirdBeat;
	} else if (choice == _("Beats/4")) {
		snaptype = SnapToAQuarterBeat;
	} else if (choice == _("Beats/8")) {
		snaptype = SnapToAEighthBeat;
	} else if (choice == _("Beats/16")) {
		snaptype = SnapToASixteenthBeat;
	} else if (choice == _("Beats/32")) {
		snaptype = SnapToAThirtysecondBeat;
	} else if (choice == _("Beats")) {
		snaptype = SnapToBeat;
	} else if (choice == _("Bars")) {
		snaptype = SnapToBar;
	} else if (choice == _("Marks")) {
		snaptype = SnapToMark;
	} else if (choice == _("Region starts")) {
		snaptype = SnapToRegionStart;
	} else if (choice == _("Region ends")) {
		snaptype = SnapToRegionEnd;
	} else if (choice == _("Region bounds")) {
		snaptype = SnapToRegionBoundary;
	} else if (choice == _("Region syncs")) {
		snaptype = SnapToRegionSync;
	} else if (choice == _("CD Frames")) {
		snaptype = SnapToCDFrame;
	} else if (choice == _("Timecode Frames")) {
		snaptype = SnapToTimecodeFrame;
	} else if (choice == _("Timecode Seconds")) {
		snaptype = SnapToTimecodeSeconds;
	} else if (choice == _("Timecode Minutes")) {
		snaptype = SnapToTimecodeMinutes;
	} else if (choice == _("Seconds")) {
		snaptype = SnapToSeconds;
	} else if (choice == _("Minutes")) {
		snaptype = SnapToMinutes;
	}

	RefPtr<RadioAction> ract = snap_type_action (snaptype);
	if (ract) {
		ract->set_active ();
	}
}

void
Editor::snap_mode_selection_done ()
{
	string choice = snap_mode_selector.get_active_text();
	SnapMode mode = SnapNormal;

	if (choice == _("No Grid")) {
		mode = SnapOff;
	} else if (choice == _("Grid")) {
		mode = SnapNormal;
	} else if (choice == _("Magnetic")) {
		mode = SnapMagnetic;
	}

	RefPtr<RadioAction> ract = snap_mode_action (mode);

	if (ract) {
		ract->set_active (true);
	}
}

void
Editor::cycle_edit_point (bool with_marker)
{
	switch (_edit_point) {
	case EditAtMouse:
		set_edit_point_preference (EditAtPlayhead);
		break;
	case EditAtPlayhead:
		if (with_marker) {
			set_edit_point_preference (EditAtSelectedMarker);
		} else {
			set_edit_point_preference (EditAtMouse);
		}
		break;
	case EditAtSelectedMarker:
		set_edit_point_preference (EditAtMouse);
		break;
	}
}

void
Editor::edit_point_selection_done ()
{
	string choice = edit_point_selector.get_active_text();
	EditPoint ep = EditAtSelectedMarker;

	if (choice == _("Marker")) {
		set_edit_point_preference (EditAtSelectedMarker);
	} else if (choice == _("Playhead")) {
		set_edit_point_preference (EditAtPlayhead);
	} else {
		set_edit_point_preference (EditAtMouse);
	}

	RefPtr<RadioAction> ract = edit_point_action (ep);

	if (ract) {
		ract->set_active (true);
	}
}

void
Editor::zoom_focus_selection_done ()
{
	string choice = zoom_focus_selector.get_active_text();
	ZoomFocus focus_type = ZoomFocusLeft;

	if (choice == _("Left")) {
		focus_type = ZoomFocusLeft;
	} else if (choice == _("Right")) {
		focus_type = ZoomFocusRight;
	} else if (choice == _("Center")) {
		focus_type = ZoomFocusCenter;
	} else if (choice == _("Playhead")) {
		focus_type = ZoomFocusPlayhead;
	} else if (choice == _("Mouse")) {
		focus_type = ZoomFocusMouse;
	} else if (choice == _("Edit point")) {
		focus_type = ZoomFocusEdit;
	}

	RefPtr<RadioAction> ract = zoom_focus_action (focus_type);

	if (ract) {
		ract->set_active ();
	}
}

gint
Editor::edit_controls_button_release (GdkEventButton* ev)
{
	if (Keyboard::is_context_menu_event (ev)) {
		ARDOUR_UI::instance()->add_route (this);
	}
	return TRUE;
}

gint
Editor::mouse_select_button_release (GdkEventButton* ev)
{
	/* this handles just right-clicks */

	if (ev->button != 3) {
		return false;
	}

	return true;
}

void
Editor::set_zoom_focus (ZoomFocus f)
{
	string str = zoom_focus_strings[(int)f];

	if (str != zoom_focus_selector.get_active_text()) {
		zoom_focus_selector.set_active_text (str);
	}

	if (zoom_focus != f) {
		zoom_focus = f;

		ZoomFocusChanged (); /* EMIT_SIGNAL */

		instant_save ();
	}
}

void
Editor::ensure_float (Window& win)
{
	win.set_transient_for (*this);
}

void
Editor::pane_allocation_handler (Allocation &alloc, Paned* which)
{
	/* recover or initialize pane positions. do this here rather than earlier because
	   we don't want the positions to change the child allocations, which they seem to do.
	 */

	int pos;
	XMLProperty* prop;
	char buf[32];
	XMLNode* node = ARDOUR_UI::instance()->editor_settings();
	int width, height;
	static int32_t done;
	XMLNode* geometry;

	width = default_width;
	height = default_height;

	if ((geometry = find_named_node (*node, "geometry")) != 0) {

		if ((prop = geometry->property ("x_size")) == 0) {
			prop = geometry->property ("x-size");
		}
		if (prop) {
			width = atoi (prop->value());
		}
		if ((prop = geometry->property ("y_size")) == 0) {
			prop = geometry->property ("y-size");
		}
		if (prop) {
			height = atoi (prop->value());
		}
	}

	if (which == static_cast<Paned*> (&edit_pane)) {

		if (done) {
			return;
		}

		if (!geometry || (prop = geometry->property ("edit-pane-pos")) == 0) {
			/* initial allocation is 90% to canvas, 10% to notebook */
			pos = (int) floor (alloc.get_width() * 0.90f);
			snprintf (buf, sizeof(buf), "%d", pos);
		} else {
			pos = atoi (prop->value());
		}

		if ((done = GTK_WIDGET(edit_pane.gobj())->allocation.width > pos)) {
			edit_pane.set_position (pos);
			pre_maximal_pane_position = pos;
		}
	}
}

void
Editor::detach_tearoff (Box* /*b*/, Window* /*w*/)
{
	cerr << "remove tearoff\n";

	if (tools_tearoff->torn_off() &&
	    mouse_mode_tearoff->torn_off()) {
		top_hbox.remove (toolbar_frame);
	}
}

void
Editor::reattach_tearoff (Box* /*b*/, Window* /*w*/, int32_t /*n*/)
{
	cerr << "reattach tearoff\n";
	if (toolbar_frame.get_parent() == 0) {
		top_hbox.pack_end (toolbar_frame);
	}
}

void
Editor::set_show_measures (bool yn)
{
	if (_show_measures != yn) {
		hide_measures ();

		if ((_show_measures = yn) == true) {
			if (tempo_lines)
				tempo_lines->show();
			draw_measures ();
		}
		instant_save ();
	}
}

void
Editor::toggle_follow_playhead ()
{
	RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("toggle-follow-playhead"));
	if (act) {
		RefPtr<ToggleAction> tact = RefPtr<ToggleAction>::cast_dynamic(act);
		set_follow_playhead (tact->get_active());
	}
}

void
Editor::set_follow_playhead (bool yn)
{
	if (_follow_playhead != yn) {
		if ((_follow_playhead = yn) == true) {
			/* catch up */
			update_current_screen ();
		}
		instant_save ();
	}
}

void
Editor::toggle_xfade_active (boost::weak_ptr<Crossfade> wxfade)
{
	boost::shared_ptr<Crossfade> xfade (wxfade.lock());
	if (xfade) {
		xfade->set_active (!xfade->active());
	}
}

void
Editor::toggle_xfade_length (boost::weak_ptr<Crossfade> wxfade)
{
	boost::shared_ptr<Crossfade> xfade (wxfade.lock());
	if (xfade) {
		xfade->set_follow_overlap (!xfade->following_overlap());
	}
}

void
Editor::edit_xfade (boost::weak_ptr<Crossfade> wxfade)
{
	boost::shared_ptr<Crossfade> xfade (wxfade.lock());

	if (!xfade) {
		return;
	}

	CrossfadeEditor cew (_session, xfade, xfade->fade_in().get_min_y(), 1.0);

	ensure_float (cew);

	switch (cew.run ()) {
	case RESPONSE_ACCEPT:
		break;
	default:
		return;
	}

	cew.apply ();
	xfade->StateChanged (Change (~0));
}

PlaylistSelector&
Editor::playlist_selector () const
{
	return *_playlist_selector;
}

Evoral::MusicalTime
Editor::get_grid_type_as_beats (bool& success, nframes64_t position)
{
	success = true;

	switch (_snap_type) {
	case SnapToBeat:
		return 1.0;
		break;

	case SnapToAThirtysecondBeat:
		return 1.0/32.0;
		break;

	case SnapToASixteenthBeat:
		return 1.0/16.0;
		break;

	case SnapToAEighthBeat:
		return 1.0/8.0;
		break;

	case SnapToAQuarterBeat:
		return 1.0/4.0;
		break;

	case SnapToAThirdBeat:
		return 1.0/3.0;
		break;

	case SnapToBar:
		if (_session) {
			return _session->tempo_map().meter_at (position).beats_per_bar();
		}
		break;

	case SnapToCDFrame:
	case SnapToTimecodeFrame:
	case SnapToTimecodeSeconds:
	case SnapToTimecodeMinutes:
	case SnapToSeconds:
	case SnapToMinutes:
	case SnapToRegionStart:
	case SnapToRegionEnd:
	case SnapToRegionSync:
	case SnapToRegionBoundary:
	default:
		success = false;
		break;
	}

	return 0.0;
}

nframes64_t
Editor::get_nudge_distance (nframes64_t pos, nframes64_t& next)
{
	nframes64_t ret;

	ret = nudge_clock.current_duration (pos);
	next = ret + 1; /* XXXX fix me */

	return ret;
}

void
Editor::end_location_changed (Location* location)
{
	ENSURE_GUI_THREAD (*this, &Editor::end_location_changed, location)
	//reset_scrolling_region ();
	nframes64_t session_span = location->start() + (nframes64_t) floorf (current_page_frames() * 0.10f);
	horizontal_adjustment.set_upper (session_span / frames_per_unit);
}

int
Editor::playlist_deletion_dialog (boost::shared_ptr<Playlist> pl)
{
	ArdourDialog dialog ("playlist deletion dialog");
	Label  label (string_compose (_("Playlist %1 is currently unused.\n"
					"If left alone, no audio files used by it will be cleaned.\n"
					"If deleted, audio files used by it alone by will cleaned."),
				      pl->name()));

	dialog.set_position (WIN_POS_CENTER);
	dialog.get_vbox()->pack_start (label);

	label.show ();

	dialog.add_button (_("Delete playlist"), RESPONSE_ACCEPT);
	dialog.add_button (_("Keep playlist"), RESPONSE_REJECT);
	dialog.add_button (_("Cancel"), RESPONSE_CANCEL);

	switch (dialog.run ()) {
	case RESPONSE_ACCEPT:
		/* delete the playlist */
		return 0;
		break;

	case RESPONSE_REJECT:
		/* keep the playlist */
		return 1;
		break;

	default:
		break;
	}

	return -1;
}

bool
Editor::audio_region_selection_covers (nframes64_t where)
{
	for (RegionSelection::iterator a = selection->regions.begin(); a != selection->regions.end(); ++a) {
		if ((*a)->region()->covers (where)) {
			return true;
		}
	}

	return false;
}

void
Editor::prepare_for_cleanup ()
{
	cut_buffer->clear_regions ();
	cut_buffer->clear_playlists ();

	selection->clear_regions ();
	selection->clear_playlists ();

	_regions->suspend_redisplay ();
}

void
Editor::finish_cleanup ()
{
	_regions->resume_redisplay ();
}

Location*
Editor::transport_loop_location()
{
	if (_session) {
		return _session->locations()->auto_loop_location();
	} else {
		return 0;
	}
}

Location*
Editor::transport_punch_location()
{
	if (_session) {
		return _session->locations()->auto_punch_location();
	} else {
		return 0;
	}
}

bool
Editor::control_layout_scroll (GdkEventScroll* ev)
{
	if (Keyboard::some_magic_widget_has_focus()) {
		return false;
	}

	switch (ev->direction) {
	case GDK_SCROLL_UP:
		scroll_tracks_up_line ();
		return true;
		break;

	case GDK_SCROLL_DOWN:
		scroll_tracks_down_line ();
		return true;

	default:
		/* no left/right handling yet */
		break;
	}

	return false;
}

void
Editor::session_state_saved (string snap_name)
{
	ENSURE_GUI_THREAD (*this, &Editor::session_state_saved, snap_name);
	
	update_title ();	
	_snapshots->redisplay ();
}

void
Editor::maximise_editing_space ()
{
	mouse_mode_tearoff->set_visible (false);
	tools_tearoff->set_visible (false);

	pre_maximal_pane_position = edit_pane.get_position();
	pre_maximal_editor_width = this->get_width();

	if(post_maximal_pane_position == 0) {
		post_maximal_pane_position = edit_pane.get_width();
	}

	fullscreen();

	if(post_maximal_editor_width) {
		edit_pane.set_position (post_maximal_pane_position -
			abs(post_maximal_editor_width - pre_maximal_editor_width));
	} else {
		edit_pane.set_position (post_maximal_pane_position);
	}
}

void
Editor::restore_editing_space ()
{
	// user changed width of pane during fullscreen

	if(post_maximal_pane_position != edit_pane.get_position()) {
		post_maximal_pane_position = edit_pane.get_position();
	}

	unfullscreen();

	mouse_mode_tearoff->set_visible (true);
	tools_tearoff->set_visible (true);
	post_maximal_editor_width = this->get_width();

	edit_pane.set_position (pre_maximal_pane_position + abs(this->get_width() - pre_maximal_editor_width));
}

/**
 *  Make new playlists for a given track and also any others that belong
 *  to the same active route group with the `edit' property.
 *  @param v Track.
 */

void
Editor::new_playlists (TimeAxisView* v)
{
	begin_reversible_command (_("new playlists"));
	vector<boost::shared_ptr<ARDOUR::Playlist> > playlists;
	_session->playlists->get (playlists);
	mapover_tracks (sigc::bind (sigc::mem_fun (*this, &Editor::mapped_use_new_playlist), playlists), v, RouteGroup::Edit);
	commit_reversible_command ();
}

/**
 *  Use a copy of the current playlist for a given track and also any others that belong
 *  to the same active route group with the `edit' property.
 *  @param v Track.
 */

void
Editor::copy_playlists (TimeAxisView* v)
{
	begin_reversible_command (_("copy playlists"));
	vector<boost::shared_ptr<ARDOUR::Playlist> > playlists;
	_session->playlists->get (playlists);
	mapover_tracks (sigc::bind (sigc::mem_fun (*this, &Editor::mapped_use_copy_playlist), playlists), v, RouteGroup::Edit);
	commit_reversible_command ();
}

/** Clear the current playlist for a given track and also any others that belong
 *  to the same active route group with the `edit' property.
 *  @param v Track.
 */

void
Editor::clear_playlists (TimeAxisView* v)
{
	begin_reversible_command (_("clear playlists"));
	vector<boost::shared_ptr<ARDOUR::Playlist> > playlists;
	_session->playlists->get (playlists);
	mapover_tracks (sigc::mem_fun (*this, &Editor::mapped_clear_playlist), v, RouteGroup::Edit);
	commit_reversible_command ();
}

void
Editor::mapped_use_new_playlist (RouteTimeAxisView& atv, uint32_t sz, vector<boost::shared_ptr<ARDOUR::Playlist> > const & playlists)
{
	atv.use_new_playlist (sz > 1 ? false : true, playlists);
}

void
Editor::mapped_use_copy_playlist (RouteTimeAxisView& atv, uint32_t sz, vector<boost::shared_ptr<ARDOUR::Playlist> > const & playlists)
{
	atv.use_copy_playlist (sz > 1 ? false : true, playlists);
}

void
Editor::mapped_clear_playlist (RouteTimeAxisView& atv, uint32_t /*sz*/)
{
	atv.clear_playlist ();
}

bool
Editor::on_key_press_event (GdkEventKey* ev)
{
	return key_press_focus_accelerator_handler (*this, ev);
}

bool
Editor::on_key_release_event (GdkEventKey* ev)
{
	return Gtk::Window::on_key_release_event (ev);
	// return key_press_focus_accelerator_handler (*this, ev);
}

void
Editor::reset_x_origin (nframes64_t frame)
{
	//cerr << "resetting x origin" << endl;
	queue_visual_change (frame);
}

void
Editor::reset_y_origin (double y)
{
	queue_visual_change_y (y);
}

void
Editor::reset_zoom (double fpu)
{
	queue_visual_change (fpu);
}

void
Editor::reposition_and_zoom (nframes64_t frame, double fpu)
{
	//cerr << "Editor::reposition_and_zoom () called ha v:l:u:ps:fpu = " << horizontal_adjustment.get_value() << ":" << horizontal_adjustment.get_lower() << ":" << horizontal_adjustment.get_upper() << ":" << horizontal_adjustment.get_page_size() << ":" << frames_per_unit << endl;//DEBUG
	reset_x_origin (frame);
	reset_zoom (fpu);

	if (!no_save_visual) {
		undo_visual_stack.push_back (current_visual_state(false));
	}
}

Editor::VisualState*
Editor::current_visual_state (bool with_tracks)
{
	VisualState* vs = new VisualState;
	vs->y_position = vertical_adjustment.get_value();
	vs->frames_per_unit = frames_per_unit;
	vs->leftmost_frame = leftmost_frame;
	vs->zoom_focus = zoom_focus;

	if (with_tracks) {
		for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
			vs->track_states.push_back (TAVState ((*i), &(*i)->get_state()));
		}
	}

	return vs;
}

void
Editor::undo_visual_state ()
{
	if (undo_visual_stack.empty()) {
		return;
	}

	VisualState* vs = undo_visual_stack.back();
	undo_visual_stack.pop_back();
	use_visual_state (*vs);
	redo_visual_stack.push_back (vs);
}

void
Editor::redo_visual_state ()
{
	if (redo_visual_stack.empty()) {
		return;
	}

	VisualState* vs = redo_visual_stack.back();
	redo_visual_stack.pop_back();
	use_visual_state (*vs);
	undo_visual_stack.push_back (vs);
}

void
Editor::swap_visual_state ()
{
	if (undo_visual_stack.empty()) {
		redo_visual_state ();
	} else {
		undo_visual_state ();
	}
}

void
Editor::use_visual_state (VisualState& vs)
{
	no_save_visual = true;

	_routes->suspend_redisplay ();

	vertical_adjustment.set_value (vs.y_position);

	set_zoom_focus (vs.zoom_focus);
	reposition_and_zoom (vs.leftmost_frame, vs.frames_per_unit);

	for (list<TAVState>::iterator i = vs.track_states.begin(); i != vs.track_states.end(); ++i) {
		TrackViewList::iterator t;

		/* check if the track still exists - it could have been deleted */

		if ((t = find (track_views.begin(), track_views.end(), i->first)) != track_views.end()) {
			(*t)->set_state (*(i->second), Stateful::loading_state_version);
		}
	}


	if (!vs.track_states.empty()) {
		_routes->update_visibility ();
	}

	_routes->resume_redisplay ();

	no_save_visual = false;
}

void
Editor::set_frames_per_unit (double fpu)
{
	/* this is the core function that controls the zoom level of the canvas. it is called
	   whenever one or more calls are made to reset_zoom(). it executes in an idle handler.
	*/

	if (fpu == frames_per_unit) {
		return;
	}

	if (fpu < 2.0) {
		fpu = 2.0;
	}


	/* don't allow zooms that fit more than the maximum number
	   of frames into an 800 pixel wide space.
	*/

	if (max_frames / fpu < 800.0) {
		return;
	}

	if (tempo_lines)
		tempo_lines->tempo_map_changed();

	frames_per_unit = fpu;
	post_zoom ();
}

void
Editor::post_zoom ()
{
	nframes64_t cef = 0;

	// convert fpu to frame count

	nframes64_t frames = (nframes64_t) floor (frames_per_unit * _canvas_width);

	if (frames_per_unit != zoom_range_clock.current_duration()) {
		zoom_range_clock.set (frames);
	}

	if (mouse_mode == MouseRange && selection->time.start () != selection->time.end_frame ()) {
		for (TrackViewList::iterator i = selection->tracks.begin(); i != selection->tracks.end(); ++i) {
			(*i)->reshow_selection (selection->time);
		}
	}

	leftmost_frame = (nframes64_t) floor (horizontal_adjustment.get_value() * frames_per_unit);

	ZoomChanged (); /* EMIT_SIGNAL */

	if (_session) {
		cef = _session->current_end_frame() + (current_page_frames() / 10);// Add a little extra so we can see the end marker
	}
	horizontal_adjustment.set_upper (cef / frames_per_unit);

	//reset_scrolling_region ();

	if (playhead_cursor) {
		playhead_cursor->set_position (playhead_cursor->current_frame);
	}

	refresh_location_display();
	_summary->set_overlays_dirty ();

	instant_save ();
}

void
Editor::queue_visual_change (nframes64_t where)
{
	pending_visual_change.add (VisualChange::TimeOrigin);

	/* if we're moving beyond the end, make sure the upper limit of the horizontal adjustment
	   can reach.
	*/

	if (_session && (where > _session->current_end_frame())) {
		horizontal_adjustment.set_upper ((where + current_page_frames()) / frames_per_unit);
	}

	pending_visual_change.time_origin = where;

	ensure_visual_change_idle_handler ();
}

void
Editor::queue_visual_change (double fpu)
{
	pending_visual_change.add (VisualChange::ZoomLevel);
	pending_visual_change.frames_per_unit = fpu;

	ensure_visual_change_idle_handler ();
}

void
Editor::queue_visual_change_y (double y)
{
	pending_visual_change.add (VisualChange::YOrigin);
	pending_visual_change.y_origin = y;

	ensure_visual_change_idle_handler ();
}

void
Editor::ensure_visual_change_idle_handler ()
{
	if (pending_visual_change.idle_handler_id < 0) {
		pending_visual_change.idle_handler_id = g_idle_add (_idle_visual_changer, this);
	}
}

int
Editor::_idle_visual_changer (void* arg)
{
	return static_cast<Editor*>(arg)->idle_visual_changer ();
}

int
Editor::idle_visual_changer ()
{
	VisualChange::Type p = pending_visual_change.pending;
	pending_visual_change.pending = (VisualChange::Type) 0;

	double last_time_origin = horizontal_adjustment.get_value();

	if (p & VisualChange::ZoomLevel) {
		set_frames_per_unit (pending_visual_change.frames_per_unit);

		compute_fixed_ruler_scale ();
		compute_current_bbt_points(pending_visual_change.time_origin, pending_visual_change.time_origin + current_page_frames());
		compute_bbt_ruler_scale (pending_visual_change.time_origin, pending_visual_change.time_origin + current_page_frames());
		update_tempo_based_rulers ();
	}
	if (p & VisualChange::TimeOrigin) {
		horizontal_adjustment.set_value (pending_visual_change.time_origin / frames_per_unit);
	}
	if (p & VisualChange::YOrigin) {
		vertical_adjustment.set_value (pending_visual_change.y_origin);
	}

	nframes64_t csf=0, cef=0;
	nframes64_t current_time_origin = (nframes64_t) floor (horizontal_adjustment.get_value() * frames_per_unit);

	if (_session) {
		csf = _session->current_start_frame();
		cef = _session->current_end_frame();
	}

	/* if we seek beyond the current end of the canvas, move the end */


	if (last_time_origin == horizontal_adjustment.get_value() ) {
		/* changed signal not emitted */
		update_fixed_rulers ();
		redisplay_tempo (true);
	}

	if (current_time_origin != pending_visual_change.time_origin) {
		cef += current_page_frames() / 10; // Add a little extra so we can see the end marker
		horizontal_adjustment.set_upper (cef / frames_per_unit);
		horizontal_adjustment.set_value (pending_visual_change.time_origin / frames_per_unit);
	} else {
		update_fixed_rulers();
		redisplay_tempo (true);
	}

	_summary->set_overlays_dirty ();

	// cerr << "Editor::idle_visual_changer () called ha v:l:u:ps:fpu = " << horizontal_adjustment.get_value() << ":" << horizontal_adjustment.get_lower() << ":" << horizontal_adjustment.get_upper() << ":" << horizontal_adjustment.get_page_size() << ":" << frames_per_unit << endl;//DEBUG
	pending_visual_change.idle_handler_id = -1;
	return 0; /* this is always a one-shot call */
}

struct EditorOrderTimeAxisSorter {
    bool operator() (const TimeAxisView* a, const TimeAxisView* b) const {
	    return a->order () < b->order ();
    }
};

void
Editor::sort_track_selection (TrackViewList* sel)
{
	EditorOrderTimeAxisSorter cmp;

	if (sel) {
		sel->sort (cmp);
	} else {
		selection->tracks.sort (cmp);
	}
}

nframes64_t
Editor::get_preferred_edit_position (bool ignore_playhead)
{
	bool ignored;
	nframes64_t where = 0;
	EditPoint ep = _edit_point;

	if (entered_marker) {
		return entered_marker->position();
	}

	if (ignore_playhead && ep == EditAtPlayhead) {
		ep = EditAtSelectedMarker;
	}

	switch (ep) {
	case EditAtPlayhead:
		where = _session->audible_frame();
		break;

	case EditAtSelectedMarker:
		if (!selection->markers.empty()) {
			bool is_start;
			Location* loc = find_location_from_marker (selection->markers.front(), is_start);
			if (loc) {
				if (is_start) {
					where =  loc->start();
				} else {
					where = loc->end();
				}
				break;
			}
		}
		/* fallthru */

	default:
	case EditAtMouse:
		if (!mouse_frame (where, ignored)) {
			/* XXX not right but what can we do ? */
			return 0;
		}
		snap_to (where);
		break;
	}

	return where;
}

void
Editor::set_loop_range (nframes64_t start, nframes64_t end, string cmd)
{
	if (!_session) return;

	begin_reversible_command (cmd);

	Location* tll;

	if ((tll = transport_loop_location()) == 0) {
		Location* loc = new Location (start, end, _("Loop"),  Location::IsAutoLoop);
		XMLNode &before = _session->locations()->get_state();
		_session->locations()->add (loc, true);
		_session->set_auto_loop_location (loc);
		XMLNode &after = _session->locations()->get_state();
		_session->add_command (new MementoCommand<Locations>(*(_session->locations()), &before, &after));
	} else {
		XMLNode &before = tll->get_state();
		tll->set_hidden (false, this);
		tll->set (start, end);
		XMLNode &after = tll->get_state();
		_session->add_command (new MementoCommand<Location>(*tll, &before, &after));
	}

	commit_reversible_command ();
}

void
Editor::set_punch_range (nframes64_t start, nframes64_t end, string cmd)
{
	if (!_session) return;

	begin_reversible_command (cmd);

	Location* tpl;

	if ((tpl = transport_punch_location()) == 0) {
		Location* loc = new Location (start, end, _("Loop"),  Location::IsAutoPunch);
		XMLNode &before = _session->locations()->get_state();
		_session->locations()->add (loc, true);
		_session->set_auto_loop_location (loc);
		XMLNode &after = _session->locations()->get_state();
		_session->add_command (new MementoCommand<Locations>(*(_session->locations()), &before, &after));
	}
	else {
		XMLNode &before = tpl->get_state();
		tpl->set_hidden (false, this);
		tpl->set (start, end);
		XMLNode &after = tpl->get_state();
		_session->add_command (new MementoCommand<Location>(*tpl, &before, &after));
	}

	commit_reversible_command ();
}

/** Find regions which exist at a given time, and optionally on a given list of tracks.
 *  @param rs List to which found regions are added.
 *  @param where Time to look at.
 *  @param ts Tracks to look on; if this is empty, all tracks are examined.
 */
void
Editor::get_regions_at (RegionSelection& rs, nframes64_t where, const TrackViewList& ts) const
{
	const TrackViewList* tracks;

	if (ts.empty()) {
		tracks = &track_views;
	} else {
		tracks = &ts;
	}

	for (TrackViewList::const_iterator t = tracks->begin(); t != tracks->end(); ++t) {
		RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*>(*t);
		if (rtv) {
			boost::shared_ptr<Diskstream> ds;
			boost::shared_ptr<Playlist> pl;

			if ((ds = rtv->get_diskstream()) && ((pl = ds->playlist()))) {

				Playlist::RegionList* regions = pl->regions_at (
						(nframes64_t) floor ( (double)where * ds->speed()));

				for (Playlist::RegionList::iterator i = regions->begin(); i != regions->end(); ++i) {
					RegionView* rv = rtv->view()->find_view (*i);
					if (rv) {
						rs.add (rv);
					}
				}

				delete regions;
			}
		}
	}
}

void
Editor::get_regions_after (RegionSelection& rs, nframes64_t where, const TrackViewList& ts) const
{
	const TrackViewList* tracks;

	if (ts.empty()) {
		tracks = &track_views;
	} else {
		tracks = &ts;
	}

	for (TrackViewList::const_iterator t = tracks->begin(); t != tracks->end(); ++t) {
		RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*>(*t);
		if (rtv) {
			boost::shared_ptr<Diskstream> ds;
			boost::shared_ptr<Playlist> pl;

			if ((ds = rtv->get_diskstream()) && ((pl = ds->playlist()))) {

				Playlist::RegionList* regions = pl->regions_touched (
						(nframes64_t) floor ( (double)where * ds->speed()), max_frames);

				for (Playlist::RegionList::iterator i = regions->begin(); i != regions->end(); ++i) {

					RegionView* rv = rtv->view()->find_view (*i);

					if (rv) {
						rs.push_back (rv);
					}
				}

				delete regions;
			}
		}
	}
}

/** Find all regions which are either:
 *      - selected or
 *      - the entered_regionview (if allow_entered == true) or
 *      - under the preferred edit position AND on a selected track, or on a track
 *        which is in the same active edit-enable route group as a selected region (if allow_edit_position == true)
 *  @param rs Returned region list.
 *  @param allow_entered true to include the entered_regionview in the list.
 */
void
Editor::get_regions_for_action (RegionSelection& rs, bool allow_entered, bool allow_edit_position)
{
	/* Start with selected regions */
	rs = selection->regions;

	/* Add the entered_regionview, if requested */
	if (allow_entered && entered_regionview) {
		rs.add (entered_regionview);
	}

	if (allow_edit_position) {

		TrackViewList tracks = selection->tracks;

		/* tracks is currently the set of selected tracks; add any other tracks that
		 * have regions that are in the same edit-activated route group as one of
		 * our regions */
		for (RegionSelection::iterator i = rs.begin (); i != rs.end(); ++i) {

		 	RouteGroup* g = (*i)->get_time_axis_view().route_group ();
		 	if (g && g->active_property (RouteGroup::Edit)) {
		 		tracks.add (axis_views_from_routes (g->route_list()));
		 	}
			
		}

		if (!tracks.empty()) {
			/* now find regions that are at the edit position on those tracks */
			nframes64_t const where = get_preferred_edit_position ();
			get_regions_at (rs, where, tracks);
		}
	}
}

void
Editor::get_regions_corresponding_to (boost::shared_ptr<Region> region, vector<RegionView*>& regions)
{
	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {

		RouteTimeAxisView* tatv;

		if ((tatv = dynamic_cast<RouteTimeAxisView*> (*i)) != 0) {

			boost::shared_ptr<Playlist> pl;
			vector<boost::shared_ptr<Region> > results;
			RegionView* marv;
			boost::shared_ptr<Diskstream> ds;

			if ((ds = tatv->get_diskstream()) == 0) {
				/* bus */
				continue;
			}

			if ((pl = (ds->playlist())) != 0) {
				pl->get_region_list_equivalent_regions (region, results);
			}

			for (vector<boost::shared_ptr<Region> >::iterator ir = results.begin(); ir != results.end(); ++ir) {
				if ((marv = tatv->view()->find_view (*ir)) != 0) {
					regions.push_back (marv);
				}
			}

		}
	}
}

void
Editor::show_rhythm_ferret ()
{
	if (rhythm_ferret == 0) {
		rhythm_ferret = new RhythmFerret(*this);
	}

	rhythm_ferret->set_session (_session);
	rhythm_ferret->show ();
	rhythm_ferret->present ();
}

void
Editor::show_global_port_matrix (ARDOUR::DataType t)
{
	if (_global_port_matrix[t] == 0) {
		_global_port_matrix[t] = new GlobalPortMatrixWindow (_session, t);
	}

	_global_port_matrix[t]->show ();
}

void
Editor::first_idle ()
{
	MessageDialog* dialog = 0;

	if (track_views.size() > 1) {
		dialog = new MessageDialog (*this,
					    _("Please wait while Ardour loads visual data"),
					    true,
					    Gtk::MESSAGE_INFO,
					    Gtk::BUTTONS_NONE);
		dialog->present ();
		ARDOUR_UI::instance()->flush_pending ();
	}

	for (TrackViewList::iterator t = track_views.begin(); t != track_views.end(); ++t) {
		(*t)->first_idle();
	}

	// first idle adds route children (automation tracks), so we need to redisplay here
	_routes->redisplay ();

	delete dialog;

	_have_idled = true;
}

static gboolean
_idle_resizer (gpointer arg)
{
	return ((Editor*)arg)->idle_resize ();
}

void
Editor::add_to_idle_resize (TimeAxisView* view, int32_t h)
{
	if (resize_idle_id < 0) {
		resize_idle_id = g_idle_add (_idle_resizer, this);
		_pending_resize_amount = 0;
	}

	/* make a note of the smallest resulting height, so that we can clamp the
	   lower limit at TimeAxisView::hSmall */

	int32_t min_resulting = INT32_MAX;

	_pending_resize_amount += h;
	_pending_resize_view = view;

	min_resulting = min (min_resulting, int32_t (_pending_resize_view->current_height()) + _pending_resize_amount);

	if (selection->tracks.contains (_pending_resize_view)) {
		for (TrackViewList::iterator i = selection->tracks.begin(); i != selection->tracks.end(); ++i) {
			min_resulting = min (min_resulting, int32_t ((*i)->current_height()) + _pending_resize_amount);
		}
	}

	if (min_resulting < 0) {
		min_resulting = 0;
	}

	/* clamp */
	if (uint32_t (min_resulting) < TimeAxisView::hSmall) {
		_pending_resize_amount += TimeAxisView::hSmall - min_resulting;
	}
}

/** Handle pending resizing of tracks */
bool
Editor::idle_resize ()
{
	_pending_resize_view->idle_resize (_pending_resize_view->current_height() + _pending_resize_amount);

	if (dynamic_cast<AutomationTimeAxisView*> (_pending_resize_view) == 0 &&
	    selection->tracks.contains (_pending_resize_view)) {

		for (TrackViewList::iterator i = selection->tracks.begin(); i != selection->tracks.end(); ++i) {
			if (*i != _pending_resize_view) {
				(*i)->idle_resize ((*i)->current_height() + _pending_resize_amount);
			}
		}
	}

	flush_canvas ();
	_group_tabs->set_dirty ();
	resize_idle_id = -1;

	return false;
}

void
Editor::located ()
{
	ENSURE_GUI_THREAD (*this, &Editor::located)

	_pending_locate_request = false;
}

void
Editor::region_view_added (RegionView *)
{
	_summary->set_dirty ();
}

void
Editor::streamview_height_changed ()
{
	_summary->set_dirty ();
}

TimeAxisView*
Editor::axis_view_from_route (boost::shared_ptr<Route> r) const
{
	TrackViewList::const_iterator j = track_views.begin ();
	while (j != track_views.end()) {
		RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*> (*j);
		if (rtv && rtv->route() == r) {
			return rtv;
		}
		++j;
	}

	return 0;
}


TrackViewList
Editor::axis_views_from_routes (boost::shared_ptr<RouteList> r) const
{
	TrackViewList t;

	for (RouteList::const_iterator i = r->begin(); i != r->end(); ++i) {
		TimeAxisView* tv = axis_view_from_route (*i);
		if (tv) {
			t.push_back (tv);
		}
	}

	return t;
}


void
Editor::handle_new_route (RouteList& routes)
{
	ENSURE_GUI_THREAD (*this, &Editor::handle_new_route, routes)

	RouteTimeAxisView *rtv;
	list<RouteTimeAxisView*> new_views;

	for (RouteList::iterator x = routes.begin(); x != routes.end(); ++x) {
		boost::shared_ptr<Route> route = (*x);

		if (route->is_hidden()) {
			continue;
		}

		DataType dt = route->input()->default_type();

		if (dt == ARDOUR::DataType::AUDIO) {
			rtv = new AudioTimeAxisView (*this, _session, route, *track_canvas);
		} else if (dt == ARDOUR::DataType::MIDI) {
			rtv = new MidiTimeAxisView (*this, _session, route, *track_canvas);
		} else {
			throw unknown_type();
		}

		new_views.push_back (rtv);
		track_views.push_back (rtv);

		rtv->effective_gain_display ();

		rtv->view()->RegionViewAdded.connect (sigc::mem_fun (*this, &Editor::region_view_added));
		rtv->view()->HeightChanged.connect (sigc::mem_fun (*this, &Editor::streamview_height_changed));
	}

	_routes->routes_added (new_views);

	if (show_editor_mixer_when_tracks_arrive) {
		show_editor_mixer (true);
	}

	editor_list_button.set_sensitive (true);

	_summary->set_dirty ();
}

void
Editor::timeaxisview_deleted (TimeAxisView *tv)
{
	if (_session && _session->deletion_in_progress()) {
		/* the situation is under control */
		return;
	}

	ENSURE_GUI_THREAD (*this, &Editor::timeaxisview_deleted, tv);
		

	_routes->route_removed (tv);

	if (tv == entered_track) {
		entered_track = 0;
	}
	
	/* remove it from the list of track views */

	TrackViewList::iterator i;

	if ((i = find (track_views.begin(), track_views.end(), tv)) != track_views.end()) {
		i = track_views.erase (i);
	}

	/* update whatever the current mixer strip is displaying, if revelant */

	boost::shared_ptr<Route> route;
	RouteTimeAxisView* rtav = dynamic_cast<RouteTimeAxisView*> (tv);

	if (rtav) {
		route = rtav->route ();
	} 

	if (current_mixer_strip && current_mixer_strip->route() == route) {

		TimeAxisView* next_tv;

		if (track_views.empty()) {
			next_tv = 0;
		} else if (i == track_views.end()) {
			next_tv = track_views.front();
		} else {
			next_tv = (*i);
		}
		
		
		if (next_tv) {
			set_selected_mixer_strip (*next_tv);
		} else {
			/* make the editor mixer strip go away setting the
			 * button to inactive (which also unticks the menu option)
			 */
			
			ActionManager::uncheck_toggleaction ("<Actions>/Editor/show-editor-mixer");
		}
	} 
}

void
Editor::hide_track_in_display (TimeAxisView& tv, bool /*temponly*/)
{
	RouteTimeAxisView* rtv = dynamic_cast<RouteTimeAxisView*> (&tv);

	if (rtv && current_mixer_strip && (rtv->route() == current_mixer_strip->route())) {
		// this will hide the mixer strip
		set_selected_mixer_strip (tv);
	}

	_routes->hide_track_in_display (tv);
}

bool
Editor::sync_track_view_list_and_routes ()
{
	track_views = TrackViewList (_routes->views ());

	_summary->set_dirty ();
	_group_tabs->set_dirty ();

	return false; // do not call again (until needed)
}

void
Editor::foreach_time_axis_view (sigc::slot<void,TimeAxisView&> theslot)
{
	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		theslot (**i);
	}
}

RouteTimeAxisView*
Editor::get_route_view_by_id (PBD::ID& id)
{
	RouteTimeAxisView* v;

	for(TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		if((v = dynamic_cast<RouteTimeAxisView*>(*i)) != 0) {
			if(v->route()->id() == id) {
				return v;
			}
		}
	}

	return 0;
}

void
Editor::fit_route_group (RouteGroup *g)
{
	TrackViewList ts = axis_views_from_routes (g->route_list ());
	fit_tracks (ts);
}

void
Editor::consider_auditioning (boost::shared_ptr<Region> region)
{
	boost::shared_ptr<AudioRegion> r = boost::dynamic_pointer_cast<AudioRegion> (region);

	if (r == 0) {
		_session->cancel_audition ();
		return;
	}

	if (_session->is_auditioning()) {
		_session->cancel_audition ();
		if (r == last_audition_region) {
			return;
		}
	}

	_session->audition_region (r);
	last_audition_region = r;
}


void
Editor::hide_a_region (boost::shared_ptr<Region> r)
{
	r->set_hidden (true);
}

void
Editor::remove_a_region (boost::shared_ptr<Region> r)
{
	_session->remove_region_from_region_list (r);
}

void
Editor::audition_region_from_region_list ()
{
	_regions->selection_mapover (sigc::mem_fun (*this, &Editor::consider_auditioning));
}

void
Editor::hide_region_from_region_list ()
{
	_regions->selection_mapover (sigc::mem_fun (*this, &Editor::hide_a_region));
}

void
Editor::start_step_editing ()
{
	step_edit_connection = Glib::signal_timeout().connect (sigc::mem_fun (*this, &Editor::check_step_edit), 20);
}

void
Editor::stop_step_editing ()
{
	step_edit_connection.disconnect ();
}

bool
Editor::check_step_edit ()
{
	for (TrackViewList::iterator i = track_views.begin(); i != track_views.end(); ++i) {
		MidiTimeAxisView* mtv = dynamic_cast<MidiTimeAxisView*> (*i);
		if (mtv) {
			mtv->check_step_edit ();
		}
	}

	return true; // do it again, till we stop
}

void
Editor::horizontal_scroll_left ()
{
	double x = leftmost_position() - current_page_frames() / 5;
	if (x < 0) {
		x = 0;
	}
	
	reset_x_origin (x);
}

void
Editor::horizontal_scroll_right ()
{
	reset_x_origin (leftmost_position() + current_page_frames() / 5);
}
