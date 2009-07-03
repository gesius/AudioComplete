/*
    Copyright (C) 2000-2007 Paul Davis 

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

#include "ardour/ardour.h"
#include "ardour/profile.h"

#include "actions.h"
#include "ardour_ui.h"
#include "editing.h"
#include "editor.h"
#include "gui_thread.h"
#include "time_axis_view.h"
#include "utils.h"
#include "i18n.h"
#include "audio_time_axis.h"
#include "editor_group_tabs.h"
#include "editor_routes.h"

using namespace Gtk;
using namespace Glib;
using namespace std;
using namespace sigc;
using namespace ARDOUR;
using namespace PBD;
using namespace Editing;

void
Editor::register_actions ()
{
	RefPtr<Action> act;

	editor_actions = ActionGroup::create (X_("Editor"));
	
	/* non-operative menu items for menu bar */

	ActionManager::register_action (editor_actions, X_("AlignMenu"), _("Align"));
	ActionManager::register_action (editor_actions, X_("Autoconnect"), _("Autoconnect"));
	ActionManager::register_action (editor_actions, X_("Crossfades"), _("Crossfades"));
	ActionManager::register_action (editor_actions, X_("Edit"), _("Edit"));
	ActionManager::register_action (editor_actions, X_("EditCursorMovementOptions"), _("Move Selected Marker"));
	ActionManager::register_action (editor_actions, X_("EditSelectRangeOptions"), _("Select Range Operations"));
	ActionManager::register_action (editor_actions, X_("EditSelectRegionOptions"), _("Select Regions"));
	ActionManager::register_action (editor_actions, X_("EditPointMenu"), _("Edit Point"));
	ActionManager::register_action (editor_actions, X_("FadeMenu"), _("Fade"));
	ActionManager::register_action (editor_actions, X_("LatchMenu"), _("Latch"));
	ActionManager::register_action (editor_actions, X_("Layering"), _("Layering"));
	ActionManager::register_action (editor_actions, X_("Link"), _("Link"));
	ActionManager::register_action (editor_actions, X_("ZoomFocusMenu"), _("Zoom Focus"));
	ActionManager::register_action (editor_actions, X_("KeyMouseActions"), _("Key Mouse"));
	ActionManager::register_action (editor_actions, X_("LocateToMarker"), _("Locate to Markers"));
	ActionManager::register_action (editor_actions, X_("MarkerMenu"), _("Markers"));
	ActionManager::register_action (editor_actions, X_("MeterFalloff"), _("Meter falloff"));
	ActionManager::register_action (editor_actions, X_("MeterHold"), _("Meter hold"));
	ActionManager::register_action (editor_actions, X_("MiscOptions"), _("Misc Options"));
	ActionManager::register_action (editor_actions, X_("Monitoring"), _("Monitoring"));
	ActionManager::register_action (editor_actions, X_("MoveActiveMarkMenu"), _("Active Mark"));
	ActionManager::register_action (editor_actions, X_("MovePlayHeadMenu"), _("Playhead"));
	ActionManager::register_action (editor_actions, X_("NudgeRegionMenu"), _("Nudge"));
	ActionManager::register_action (editor_actions, X_("PlayMenu"), _("Play"));
	ActionManager::register_action (editor_actions, X_("PrimaryClockMenu"), _("Primary Clock"));
	ActionManager::register_action (editor_actions, X_("Pullup"), _("Pullup / Pulldown"));
	ActionManager::register_action (editor_actions, X_("RegionMenu"), _("Region"));
	ActionManager::register_action (editor_actions, X_("RegionEditOps"), _("Region operations"));
	ActionManager::register_action (editor_actions, X_("RegionGainMenu"), _("Gain"));
	ActionManager::register_action (editor_actions, X_("RulerMenu"), _("Rulers"));
	ActionManager::register_action (editor_actions, X_("SavedViewMenu"), _("Views"));
	ActionManager::register_action (editor_actions, X_("ScrollMenu"), _("Scroll"));
	ActionManager::register_action (editor_actions, X_("SecondaryClockMenu"), _("Secondary Clock"));
	ActionManager::register_action (editor_actions, X_("Select"), _("Select"));
	ActionManager::register_action (editor_actions, X_("SelectMenu"), _("Select"));
	ActionManager::register_action (editor_actions, X_("SeparateMenu"), _("Separate"));
	ActionManager::register_action (editor_actions, X_("SetLoopMenu"), _("Loop"));
	ActionManager::register_action (editor_actions, X_("SetPunchMenu"), _("Punch"));
	ActionManager::register_action (editor_actions, X_("Solo"), _("Solo"));
	ActionManager::register_action (editor_actions, X_("Subframes"), _("Subframes"));
	ActionManager::register_action (editor_actions, X_("SyncMenu"), _("Sync"));
	ActionManager::register_action (editor_actions, X_("TempoMenu"), _("Tempo"));
	ActionManager::register_action (editor_actions, X_("Timecode"), _("Timecode fps"));
	ActionManager::register_action (editor_actions, X_("TrackHeightMenu"), _("Height"));
	ActionManager::register_action (editor_actions, X_("TrackMenu"), _("Track"));
	ActionManager::register_action (editor_actions, X_("Tools"), _("Tools"));
	ActionManager::register_action (editor_actions, X_("TrimMenu"), _("Trim"));
	ActionManager::register_action (editor_actions, X_("View"), _("View"));
	ActionManager::register_action (editor_actions, X_("ZoomFocus"), _("Zoom Focus"));
	ActionManager::register_action (editor_actions, X_("ZoomMenu"), _("Zoom"));

	/* add named actions for the editor */

	ActionManager::register_action (editor_actions, "break-drag", _("Break drag"), mem_fun (*this, &Editor::break_drag));

	act = ActionManager::register_toggle_action (editor_actions, "show-editor-mixer", _("Show Editor Mixer"), mem_fun (*this, &Editor::editor_mixer_button_toggled));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_toggle_action (editor_actions, "show-editor-list", _("Show Editor List"), mem_fun (*this, &Editor::editor_list_button_toggled));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "toggle-selected-region-fade-in", _("Toggle Region Fade In"), bind (mem_fun(*this, &Editor::toggle_selected_region_fades), 1));;
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-selected-region-fade-out", _("Toggle Region Fade Out"), bind (mem_fun(*this, &Editor::toggle_selected_region_fades), -1));;
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-selected-region-fades", _("Toggle Region Fades"), bind (mem_fun(*this, &Editor::toggle_selected_region_fades), 0));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "playhead-to-next-region-boundary", _("Playhead to Next Region Boundary"), bind (mem_fun(*this, &Editor::cursor_to_next_region_boundary), true ));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-next-region-boundary-noselection", _("Playhead to Next Region Boundary (No Track Selection)"), bind (mem_fun(*this, &Editor::cursor_to_next_region_boundary), false ));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-previous-region-boundary", _("Playhead to Previous Region Boundary"), bind (mem_fun(*this, &Editor::cursor_to_previous_region_boundary), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-previous-region-boundary-noselection", _("Playhead to Previous Region Boundary (No Track Selection"), bind (mem_fun(*this, &Editor::cursor_to_previous_region_boundary), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "playhead-to-next-region-start", _("Playhead to Next Region Start"), bind (mem_fun(*this, &Editor::cursor_to_next_region_point), playhead_cursor, RegionPoint (Start)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-next-region-end", _("Playhead to Next Region End"), bind (mem_fun(*this, &Editor::cursor_to_next_region_point), playhead_cursor, RegionPoint (End)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-next-region-sync", _("Playhead to Next Region Sync"), bind (mem_fun(*this, &Editor::cursor_to_next_region_point), playhead_cursor, RegionPoint (SyncPoint)));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "playhead-to-previous-region-start", _("Playhead to Previous Region Start"), bind (mem_fun(*this, &Editor::cursor_to_previous_region_point), playhead_cursor, RegionPoint (Start)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-previous-region-end", _("Playhead to Previous Region End"), bind (mem_fun(*this, &Editor::cursor_to_previous_region_point), playhead_cursor, RegionPoint (End)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-previous-region-sync", _("Playhead to Previous Region Sync"), bind (mem_fun(*this, &Editor::cursor_to_previous_region_point), playhead_cursor, RegionPoint (SyncPoint)));
	ActionManager::session_sensitive_actions.push_back (act);

 	act = ActionManager::register_action (editor_actions, "selected-marker-to-next-region-boundary", _("to Next Region Boundary"), bind (mem_fun(*this, &Editor::selected_marker_to_next_region_boundary), true));
  	ActionManager::session_sensitive_actions.push_back (act);
 	act = ActionManager::register_action (editor_actions, "selected-marker-to-next-region-boundary-noselection", _("to Next Region Boundary (No Track Selection)"), bind (mem_fun(*this, &Editor::selected_marker_to_next_region_boundary), false));
 	ActionManager::session_sensitive_actions.push_back (act);
 	act = ActionManager::register_action (editor_actions, "selected-marker-to-previous-region-boundary", _("to Previous Region Boundary"), bind (mem_fun(*this, &Editor::selected_marker_to_previous_region_boundary), true));
 	ActionManager::session_sensitive_actions.push_back (act);
 	act = ActionManager::register_action (editor_actions, "selected-marker-to-previous-region-boundary-noselection", _("to Previous Region Boundary (No Track Selection)"), bind (mem_fun(*this, &Editor::selected_marker_to_previous_region_boundary), false));
 	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "edit-cursor-to-next-region-start", _("To Next Region Start"), bind (mem_fun(*this, &Editor::selected_marker_to_next_region_point), RegionPoint (Start)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-cursor-to-next-region-end", _("To Next Region End"), bind (mem_fun(*this, &Editor::selected_marker_to_next_region_point), RegionPoint (End)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-cursor-to-next-region-sync", _("To Next Region Sync"), bind (mem_fun(*this, &Editor::selected_marker_to_next_region_point), RegionPoint (SyncPoint)));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "edit-cursor-to-previous-region-start", _("To Previous Region Start"), bind (mem_fun(*this, &Editor::selected_marker_to_previous_region_point), RegionPoint (Start)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-cursor-to-previous-region-end", _("To Previous Region End"), bind (mem_fun(*this, &Editor::selected_marker_to_previous_region_point), RegionPoint (End)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-cursor-to-previous-region-sync", _("To Previous Region Sync"), bind (mem_fun(*this, &Editor::selected_marker_to_previous_region_point), RegionPoint (SyncPoint)));
	ActionManager::session_sensitive_actions.push_back (act);

        act = ActionManager::register_action (editor_actions, "edit-cursor-to-range-start", _("To Range Start"), mem_fun(*this, &Editor::selected_marker_to_selection_start));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-cursor-to-range-end", _("To Range End"), mem_fun(*this, &Editor::selected_marker_to_selection_end));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "playhead-to-range-start", _("Playhead to Range Start"), bind (mem_fun(*this, &Editor::cursor_to_selection_start), playhead_cursor));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-to-range-end", _("Playhead to Range End"), bind (mem_fun(*this, &Editor::cursor_to_selection_end), playhead_cursor));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "select-all", _("Select All"), bind (mem_fun(*this, &Editor::select_all), Selection::Set));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "deselect-all", _("Deselect All"), mem_fun(*this, &Editor::deselect_all));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "invert-selection", _("Invert Selection"), mem_fun(*this, &Editor::invert_selection));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "select-all-after-edit-cursor", _("Select All After Edit Point"), bind (mem_fun(*this, &Editor::select_all_selectables_using_edit), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "select-all-before-edit-cursor", _("Select All Before Edit Point"), bind (mem_fun(*this, &Editor::select_all_selectables_using_edit), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "select-all-between-cursors", _("Select All Overlapping Edit Range"), bind (mem_fun(*this, &Editor::select_all_selectables_between), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "select-all-within-cursors", _("Select All Inside Edit Range"), bind (mem_fun(*this, &Editor::select_all_selectables_between), true));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "select-range-between-cursors", _("Select Edit Range"), mem_fun(*this, &Editor::select_range_between));
	ActionManager::session_sensitive_actions.push_back (act);

       	act = ActionManager::register_action (editor_actions, "select-all-in-punch-range", _("Select All in Punch Range"), mem_fun(*this, &Editor::select_all_selectables_using_punch));
	ActionManager::session_sensitive_actions.push_back (act);
       	act = ActionManager::register_action (editor_actions, "select-all-in-loop-range", _("Select All in Loop Range"), mem_fun(*this, &Editor::select_all_selectables_using_loop));
	ActionManager::session_sensitive_actions.push_back (act);
	
       	act = ActionManager::register_action (editor_actions, "select-next-route", _("Select Next Track/Bus"), mem_fun(*this, &Editor::select_next_route));
	ActionManager::session_sensitive_actions.push_back (act);
       	act = ActionManager::register_action (editor_actions, "select-prev-route", _("Select Previous Track/Bus"), mem_fun(*this, &Editor::select_prev_route));
	ActionManager::session_sensitive_actions.push_back (act);
	
       	act = ActionManager::register_action (editor_actions, "track-record-enable-toggle", _("Toggle Record Enable"), mem_fun(*this, &Editor::toggle_record_enable));
	ActionManager::session_sensitive_actions.push_back (act);


	act = ActionManager::register_action (editor_actions, "save-visual-state-1", _("Save View 1"), bind (mem_fun (*this, &Editor::start_visual_state_op), 0));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-1", _("Goto View 1"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 0));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-2", _("Save View 2"), bind (mem_fun (*this, &Editor::start_visual_state_op), 1));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-2", _("Goto View 2"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 1));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-3", _("Save View 3"), bind (mem_fun (*this, &Editor::start_visual_state_op), 2));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-3", _("Goto View 3"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 2));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-4", _("Save View 4"), bind (mem_fun (*this, &Editor::start_visual_state_op), 3));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-4", _("Goto View 4"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 3));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-5", _("Save View 5"), bind (mem_fun (*this, &Editor::start_visual_state_op), 4));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-5", _("Goto View 5"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 4));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-6", _("Save View 6"), bind (mem_fun (*this, &Editor::start_visual_state_op), 5));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-6", _("Goto View 6"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 5));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-7", _("Save View 7"), bind (mem_fun (*this, &Editor::start_visual_state_op), 6));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-7", _("Goto View 7"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 6));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-8", _("Save View 8"), bind (mem_fun (*this, &Editor::start_visual_state_op), 7));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-8", _("Goto View 8"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 7));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-9", _("Save View 9"), bind (mem_fun (*this, &Editor::start_visual_state_op), 8));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-9", _("Goto View 9"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 8));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-10", _("Save View 10"), bind (mem_fun (*this, &Editor::start_visual_state_op), 9));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-10", _("Goto View 10"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 9));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-11", _("Save View 11"), bind (mem_fun (*this, &Editor::start_visual_state_op), 10));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-11", _("Goto View 11"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 10));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "save-visual-state-12", _("Save View 12"), bind (mem_fun (*this, &Editor::start_visual_state_op), 11));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-visual-state-12", _("Goto View 12"), bind (mem_fun (*this, &Editor::cancel_visual_state_op), 11));
	ActionManager::session_sensitive_actions.push_back (act);


	act = ActionManager::register_action (editor_actions, "goto-mark-1", _("Locate to Mark 1"), bind (mem_fun (*this, &Editor::goto_nth_marker), 0));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-2", _("Locate to Mark 2"), bind (mem_fun (*this, &Editor::goto_nth_marker), 1));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-3", _("Locate to Mark 3"), bind (mem_fun (*this, &Editor::goto_nth_marker), 2));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-4", _("Locate to Mark 4"), bind (mem_fun (*this, &Editor::goto_nth_marker), 3));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-5", _("Locate to Mark 5"), bind (mem_fun (*this, &Editor::goto_nth_marker), 4));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-6", _("Locate to Mark 6"), bind (mem_fun (*this, &Editor::goto_nth_marker), 5));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-7", _("Locate to Mark 7"), bind (mem_fun (*this, &Editor::goto_nth_marker), 6));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-8", _("Locate to Mark 8"), bind (mem_fun (*this, &Editor::goto_nth_marker), 7));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto-mark-9", _("Locate to Mark 9"), bind (mem_fun (*this, &Editor::goto_nth_marker), 8));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "jump-forward-to-mark", _("Jump Forward to Mark"), mem_fun(*this, &Editor::jump_forward_to_mark));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "jump-backward-to-mark", _("Jump Backward to Mark"), mem_fun(*this, &Editor::jump_backward_to_mark));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "add-location-from-playhead", _("Add Mark from Playhead"), mem_fun(*this, &Editor::add_location_from_playhead_cursor));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "nudge-forward", _("Nudge Forward"), bind (mem_fun(*this, &Editor::nudge_forward), false, false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "nudge-next-forward", _("Nudge Next Forward"), bind (mem_fun(*this, &Editor::nudge_forward), true, false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "nudge-backward", _("Nudge Backward"), bind (mem_fun(*this, &Editor::nudge_backward), false, false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "nudge-next-backward", _("Nudge Next Backward"), bind (mem_fun(*this, &Editor::nudge_backward), true, false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "nudge-playhead-forward", _("Nudge Playhead Forward"), bind (mem_fun(*this, &Editor::nudge_forward), false, true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "nudge-playhead-backward", _("Nudge Playhead Backward"), bind (mem_fun(*this, &Editor::nudge_backward), false, true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-forward-to-grid", _("Forward to Grid"), mem_fun(*this, &Editor::playhead_forward_to_grid));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "playhead-backward-to-grid", _("Backward to Grid"), mem_fun(*this, &Editor::playhead_backward_to_grid));
	ActionManager::session_sensitive_actions.push_back (act);


	act = ActionManager::register_action (editor_actions, "temporal-zoom-out", _("Zoom Out"), bind (mem_fun(*this, &Editor::temporal_zoom_step), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "temporal-zoom-in", _("Zoom In"), bind (mem_fun(*this, &Editor::temporal_zoom_step), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "zoom-to-session", _("Zoom to Session"), mem_fun(*this, &Editor::temporal_zoom_session));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "zoom-to-region", _("Zoom to Region"), bind (mem_fun(*this, &Editor::toggle_zoom_region), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "zoom-to-region-both-axes", _("Zoom to Region (W&H)"), bind (mem_fun(*this, &Editor::toggle_zoom_region), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-zoom", _("Toggle Zoom State"), mem_fun(*this, &Editor::swap_visual_state));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "move-selected-tracks-up", _("Move Selected Tracks Up"), bind (mem_fun(*_routes, &EditorRoutes::move_selected_tracks), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "move-selected-tracks-down", _("Move Selected Tracks Down"), bind (mem_fun(*_routes, &EditorRoutes::move_selected_tracks), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "scroll-tracks-up", _("Scroll Tracks Up"), mem_fun(*this, &Editor::scroll_tracks_up));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "scroll-tracks-down", _("Scroll Tracks Down"), mem_fun(*this, &Editor::scroll_tracks_down));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "step-tracks-up", _("Step Tracks Up"), mem_fun(*this, &Editor::scroll_tracks_up_line));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "step-tracks-down", _("Step Tracks Down"), mem_fun(*this, &Editor::scroll_tracks_down_line));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "scroll-backward", _("Scroll Backward"), bind (mem_fun(*this, &Editor::scroll_backward), 0.8f));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "scroll-forward", _("Scroll Forward"), bind (mem_fun(*this, &Editor::scroll_forward), 0.8f));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "goto", _("goto"), mem_fun(*this, &Editor::goto_frame));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "center-playhead", _("Center Playhead"), mem_fun(*this, &Editor::center_playhead));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "center-edit-cursor", _("Center Active Marker"), mem_fun(*this, &Editor::center_edit_point));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "scroll-playhead-forward", _("Playhead Forward"), bind (mem_fun(*this, &Editor::scroll_playhead), true));;
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "scroll-playhead-backward", _("Playhead Backward"), bind (mem_fun(*this, &Editor::scroll_playhead), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "playhead-to-edit", _("Playhead to Active Mark"), bind (mem_fun(*this, &Editor::cursor_align), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "edit-to-playhead", _("Active Mark to Playhead"), bind (mem_fun(*this, &Editor::cursor_align), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "trim-front", _("Trim Start at Edit Point"), mem_fun(*this, &Editor::trim_region_front));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "trim-back", _("Trim End at Edit Point"), mem_fun(*this, &Editor::trim_region_back));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "trim-from-start", _("Start to Edit Point"), mem_fun(*this, &Editor::trim_region_from_edit_point));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "trim-to-end", _("Edit Point to End"), mem_fun(*this, &Editor::trim_region_to_edit_point));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "trim-region-to-loop", _("Trim to Loop"), mem_fun(*this, &Editor::trim_region_to_loop));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "trim-region-to-punch", _("Trim to Punch"), mem_fun(*this, &Editor::trim_region_to_punch));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "trim-to-previous-region", _("Trim to Previous"), mem_fun(*this, &Editor::trim_region_to_previous_region_end));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "trim-to-next-region", _("Trim to Next"), mem_fun(*this, &Editor::trim_region_to_next_region_start));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "set-loop-from-edit-range", _("Set Loop from Edit Range"), bind (mem_fun(*this, &Editor::set_loop_from_edit_range), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-loop-from-region", _("Set Loop from Region"), bind (mem_fun(*this, &Editor::set_loop_from_region), false));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "loop-region", _("Loop Region"), bind (mem_fun(*this, &Editor::set_loop_from_region), true));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-punch-from-edit-range", _("Set Punch from Edit Range"), mem_fun(*this, &Editor::set_punch_from_edit_range));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-punch-from-region", _("Set Punch From Region"), mem_fun(*this, &Editor::set_punch_from_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "pitch-shift-region", _("Transpose"), mem_fun(*this, &Editor::pitch_shift_regions));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-opaque-region", _("Toggle Opaque"), mem_fun(*this, &Editor::toggle_region_opaque));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "add-range-marker-from-region", _("Add 1 Range Marker"), mem_fun(*this, &Editor::add_location_from_audio_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "add-range-markers-from-region", _("Add Range Marker(s)"), mem_fun(*this, &Editor::add_locations_from_audio_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	
	act = ActionManager::register_action (editor_actions, "set-fade-in-length", _("Set Fade In Length"), bind (mem_fun(*this, &Editor::set_fade_length), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-fade-in-active", _("Toggle Fade In Active"), bind (mem_fun(*this, &Editor::toggle_fade_active), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-fade-out-length", _("Set Fade Out Length"), bind (mem_fun(*this, &Editor::set_fade_length), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-fade-out-active", _("Toggle Fade Out Active"), bind (mem_fun(*this, &Editor::toggle_fade_active), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "align-regions-start", _("Align Regions Start"), bind (mem_fun(*this, &Editor::align), ARDOUR::Start));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "align-regions-start-relative", _("Align Regions Start Relative"), bind (mem_fun(*this, &Editor::align_relative), ARDOUR::Start));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "align-regions-end", _("Align Regions End"), bind (mem_fun(*this, &Editor::align), ARDOUR::End));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "align-regions-end-relative", _("Align Regions End Relative"), bind (mem_fun(*this, &Editor::align_relative), ARDOUR::End));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "align-regions-sync", _("Align Regions Sync"), bind (mem_fun(*this, &Editor::align), ARDOUR::SyncPoint));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "align-regions-sync-relative", _("Align Regions Sync Relative"), bind (mem_fun(*this, &Editor::align_relative), ARDOUR::SyncPoint));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

        act = ActionManager::register_action (editor_actions, "play-from-edit-point", _("Play From Edit Point"), mem_fun(*this, &Editor::play_from_edit_point));
        ActionManager::session_sensitive_actions.push_back (act);
        act = ActionManager::register_action (editor_actions, "play-from-edit-point-and-return", _("Play from Edit Point & Return"), mem_fun(*this, &Editor::play_from_edit_point_and_return));
        ActionManager::session_sensitive_actions.push_back (act);

        act = ActionManager::register_action (editor_actions, "play-edit-range", _("Play Edit Range"), mem_fun(*this, &Editor::play_edit_range));
        act = ActionManager::register_action (editor_actions, "play-selected-regions", _("Play Selected Region(s)"), mem_fun(*this, &Editor::play_selected_region));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
        act = ActionManager::register_action (editor_actions, "brush-at-mouse", _("Brush at Mouse"), mem_fun(*this, &Editor::kbd_brush));
        ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "set-playhead", _("Playhead to Mouse"), mem_fun(*this, &Editor::set_playhead_cursor));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-edit-point", _("Active Marker to Mouse"), mem_fun(*this, &Editor::set_edit_point));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "duplicate-region", _("Duplicate Region"), bind (mem_fun(*this, &Editor::duplicate_dialog), false));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "multi-duplicate-region", _("Multi-Duplicate Region"), bind (mem_fun(*this, &Editor::duplicate_dialog), true));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "duplicate-range", _("Duplicate Range"), bind (mem_fun(*this, &Editor::duplicate_dialog), false));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "insert-region", _("Insert Region"), mem_fun(*this, &Editor::keyboard_insert_region_list_selection));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "normalize-region", _("Normalize Region"), mem_fun(*this, &Editor::normalize_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "rename-region", _("Rename"), mem_fun(*this, &Editor::rename_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "auto-rename-region", _("Auto-Rename"), mem_fun(*this, &Editor::rename_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "boost-region-gain", _("Boost Region Gain"), bind (mem_fun(*this, &Editor::adjust_region_scale_amplitude), true));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "cut-region-gain", _("Cut Region Gain"), bind (mem_fun(*this, &Editor::adjust_region_scale_amplitude), false));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "split-region", _("Split Region"), mem_fun(*this, &Editor::split));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-region-sync-position", _("Set Region Sync Position"), mem_fun(*this, &Editor::set_region_sync_from_edit_point));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "remove-region-sync", _("Remove Region Sync"), mem_fun(*this, &Editor::remove_region_sync));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "raise-region", _("Raise Region"), mem_fun(*this, &Editor::raise_region));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "lower-region", _("Lower Region"), mem_fun(*this, &Editor::lower_region));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "export-region", _("Export Region"), mem_fun(*this, &Editor::export_region));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_toggle_action (editor_actions, "lock-region", _("Lock Region"), mem_fun(*this, &Editor::toggle_region_lock));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_toggle_action (editor_actions, "glue-region", _("Glue Region to Bars & Beats"), bind (mem_fun (*this, &Editor::set_region_lock_style), Region::MusicTime));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "naturalize-region", _("Move to Original Position"), mem_fun (*this, &Editor::naturalize));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "reverse-region", _("Reverse"), mem_fun (*this, &Editor::reverse_region));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "monoize-region", _("Make mono regions"), (mem_fun(*this, &Editor::split_multichannel_region)));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "region-fill-track", _("Fill Track"), (mem_fun(*this, &Editor::region_fill_track)));
        ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "mute-unmute-region", _("Mute/Unmute Region"), mem_fun(*this, &Editor::kbd_mute_unmute_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);

	undo_action = act = ActionManager::register_action (editor_actions, "undo", _("Undo"), bind (mem_fun(*this, &Editor::undo), 1U));
	ActionManager::session_sensitive_actions.push_back (act);
	redo_action = act = ActionManager::register_action (editor_actions, "redo", _("Redo"), bind (mem_fun(*this, &Editor::redo), 1U));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "export-audio", _("Export Audio"), mem_fun(*this, &Editor::export_audio));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "export-range", _("Export Range"), mem_fun(*this, &Editor::export_range));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "editor-separate", _("Separate"), mem_fun(*this, &Editor::separate_region_from_selection));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::mouse_edit_point_requires_canvas_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "separate-from-punch", _("Separate Using Punch Range"), mem_fun(*this, &Editor::separate_region_from_punch));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::mouse_edit_point_requires_canvas_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "separate-from-loop", _("Separate Using Loop Range"), mem_fun(*this, &Editor::separate_region_from_loop));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::mouse_edit_point_requires_canvas_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "editor-crop", _("Crop"), mem_fun(*this, &Editor::crop_region_to_selection));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::mouse_edit_point_requires_canvas_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "editor-cut", _("Cut"), mem_fun(*this, &Editor::cut));
	ActionManager::session_sensitive_actions.push_back (act);
	/* Note: for now, editor-delete does the exact same thing as editor-cut */
	act = ActionManager::register_action (editor_actions, "editor-delete", _("Delete"), mem_fun(*this, &Editor::cut));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "editor-copy", _("Copy"), mem_fun(*this, &Editor::copy));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "editor-paste", _("Paste"), mem_fun(*this, &Editor::keyboard_paste));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "quantize-region", _("Quantize Region"), mem_fun(*this, &Editor::quantize_region));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	
	act = ActionManager::register_action (editor_actions, "set-tempo-from-region", _("Set Tempo from Region=Bar"), mem_fun(*this, &Editor::use_region_as_bar));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "set-tempo-from-edit-range", _("Set Tempo from Edit Range=Bar"), mem_fun(*this, &Editor::use_range_as_bar));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "split-region-at-transients", _("Split Regions At Percussion Onsets"), mem_fun(*this, &Editor::split_region_at_transients));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::region_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-rhythm-ferret", _("Rhythm Ferret"), mem_fun(*this, &Editor::show_rhythm_ferret));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-bundle-manager", _("Bundle Manager"), mem_fun (*this, &Editor::show_bundle_manager));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-audio-connection-manager", _("Audio Connection Manager"), bind (mem_fun (*this, &Editor::show_global_port_matrix), ARDOUR::DataType::AUDIO));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "toggle-midi-connection-manager", _("MIDI Connection Manager"), bind (mem_fun (*this, &Editor::show_global_port_matrix), ARDOUR::DataType::MIDI));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "tab-to-transient-forwards", _("Move Forward to Transient"), bind (mem_fun(*this, &Editor::tab_to_transient), true));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "tab-to-transient-backwards", _("Move Backwards to Transient"), bind (mem_fun(*this, &Editor::tab_to_transient), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "crop", _("Crop"), mem_fun(*this, &Editor::crop_region_to_selection));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "insert-chunk", _("Insert Chunk"), bind (mem_fun(*this, &Editor::paste_named_selection), 1.0f));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "split-at-edit-cursor", _("Split At Edit Point"), mem_fun(*this, &Editor::split_region));
	ActionManager::edit_point_in_region_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "start-range", _("Start Range"), mem_fun(*this, &Editor::keyboard_selection_begin));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "finish-range", _("Finish Range"), bind (mem_fun(*this, &Editor::keyboard_selection_finish), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "finish-add-range", _("Finish add Range"), bind (mem_fun(*this, &Editor::keyboard_selection_finish), true));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "extend-range-to-end-of-region", _("Extend Range to End of Region"), bind (mem_fun(*this, &Editor::extend_selection_to_end_of_region), false));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "extend-range-to-start-of-region", _("Extend Range to Start of Region"), bind (mem_fun(*this, &Editor::extend_selection_to_start_of_region), false));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_toggle_action (editor_actions, "toggle-follow-playhead", _("Follow Playhead"), (mem_fun(*this, &Editor::toggle_follow_playhead)));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "remove-last-capture", _("Remove Last Capture"), (mem_fun(*this, &Editor::remove_last_capture)));
	ActionManager::session_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "insert-time", _("Insert Time"), (mem_fun(*this, &Editor::do_insert_time)));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "toggle-track-active", _("Toggle Active"), (mem_fun(*this, &Editor::toggle_tracks_active)));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	if (Profile->get_sae()) {
		act = ActionManager::register_action (editor_actions, "remove-track", _("Delete"), (mem_fun(*this, &Editor::remove_tracks)));
	} else {
		act = ActionManager::register_action (editor_actions, "remove-track", _("Remove"), (mem_fun(*this, &Editor::remove_tracks)));
	}
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);

	act = ActionManager::register_action (editor_actions, "fit-tracks", _("Fit Selected Tracks"), mem_fun(*this, &Editor::fit_selected_tracks));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-largest", _("Largest"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hLargest));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-larger", _("Larger"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hLarger));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-large", _("Large"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hLarge));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-normal", _("Normal"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hNormal));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-small", _("Small"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hSmall));
	ActionManager::track_selection_sensitive_actions.push_back (act);
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, "track-height-smaller", _("Smaller"), bind (
			mem_fun(*this, &Editor::set_track_height), TimeAxisView::hSmaller));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::track_selection_sensitive_actions.push_back (act);

	Glib::RefPtr<ActionGroup> zoom_actions = ActionGroup::create (X_("Zoom"));
	RadioAction::Group zoom_group;

	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-left", _("Zoom Focus Left"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusLeft));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-right", _("Zoom Focus Right"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusRight));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-center", _("Zoom Focus Center"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusCenter));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-playhead", _("Zoom Focus Playhead"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusPlayhead));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-mouse", _("Zoom Focus Mouse"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusMouse));
	ActionManager::session_sensitive_actions.push_back (act);
	ActionManager::register_radio_action (zoom_actions, zoom_group, "zoom-focus-edit", _("Zoom Focus Edit"), bind (mem_fun(*this, &Editor::zoom_focus_chosen), Editing::ZoomFocusEdit));
	ActionManager::session_sensitive_actions.push_back (act);

	Glib::RefPtr<ActionGroup> mouse_mode_actions = ActionGroup::create (X_("MouseMode"));
	RadioAction::Group mouse_mode_group;

	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-object", _("Object Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseObject, false));
	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-range", _("Range Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseRange, false));
	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-gain", _("Gain Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseGain, false));
	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-zoom", _("Zoom Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseZoom, false));
	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-timefx", _("Timefx Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseTimeFX, false));
	ActionManager::register_radio_action (mouse_mode_actions, mouse_mode_group, "set-mouse-mode-note", _("Note Tool"), bind (mem_fun(*this, &Editor::set_mouse_mode), Editing::MouseNote, false));

	ActionManager::register_action (editor_actions, "step-mouse-mode", _("Step Mouse Mode"), bind (mem_fun(*this, &Editor::step_mouse_mode), true));
	
	RadioAction::Group edit_point_group;
	ActionManager::register_radio_action (editor_actions, edit_point_group, X_("edit-at-playhead"), _("Playhead"), (bind (mem_fun(*this, &Editor::edit_point_chosen), Editing::EditAtPlayhead)));
	ActionManager::register_radio_action (editor_actions, edit_point_group, X_("edit-at-mouse"), _("Mouse"), (bind (mem_fun(*this, &Editor::edit_point_chosen), Editing::EditAtPlayhead)));
	ActionManager::register_radio_action (editor_actions, edit_point_group, X_("edit-at-selected-marker"), _("Marker"), (bind (mem_fun(*this, &Editor::edit_point_chosen), Editing::EditAtPlayhead)));

	ActionManager::register_action (editor_actions, "cycle-edit-point", _("Change edit point"), bind (mem_fun (*this, &Editor::cycle_edit_point), false));
	ActionManager::register_action (editor_actions, "cycle-edit-point-with-marker", _("Change edit point (w/Marker)"), bind (mem_fun (*this, &Editor::cycle_edit_point), true));
	if (!Profile->get_sae()) {
		ActionManager::register_action (editor_actions, "set-edit-splice", _("Splice"), bind (mem_fun (*this, &Editor::set_edit_mode), Splice));
	}
	ActionManager::register_action (editor_actions, "set-edit-slide", _("Slide"), bind (mem_fun (*this, &Editor::set_edit_mode), Slide));
	ActionManager::register_action (editor_actions, "set-edit-lock", _("Lock"), bind (mem_fun (*this, &Editor::set_edit_mode), Lock));
	ActionManager::register_action (editor_actions, "toggle-edit-mode", _("Toggle Edit Mode"), mem_fun (*this, &Editor::cycle_edit_mode));

	ActionManager::register_action (editor_actions, X_("SnapTo"), _("Snap to"));
	ActionManager::register_action (editor_actions, X_("SnapMode"), _("Snap Mode"));

	RadioAction::Group snap_mode_group;
	ActionManager::register_radio_action (editor_actions, snap_mode_group, X_("snap-off"), _("No Grid"), (bind (mem_fun(*this, &Editor::snap_mode_chosen), Editing::SnapOff)));
	ActionManager::register_radio_action (editor_actions, snap_mode_group, X_("snap-normal"), _("Grid"), (bind (mem_fun(*this, &Editor::snap_mode_chosen), Editing::SnapNormal)));
	ActionManager::register_radio_action (editor_actions, snap_mode_group, X_("snap-magnetic"), _("Magnetic"), (bind (mem_fun(*this, &Editor::snap_mode_chosen), Editing::SnapMagnetic)));

	ActionManager::register_action (editor_actions, X_("cycle-snap-mode"), _("Next Snap Mode"), mem_fun (*this, &Editor::cycle_snap_mode));
	ActionManager::register_action (editor_actions, X_("cycle-snap-choice"), _("Next Snap Choice"), mem_fun (*this, &Editor::cycle_snap_choice));

	Glib::RefPtr<ActionGroup> snap_actions = ActionGroup::create (X_("Snap"));
	RadioAction::Group snap_choice_group;

	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-cd-frame"), _("Snap to CD Frame"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToCDFrame)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-smpte-frame"), _("Snap to SMPTE frame"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToSMPTEFrame)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-smpte-seconds"), _("Snap to SMPTE seconds"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToSMPTESeconds)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-smpte-minutes"), _("Snap to SMPTE minutes"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToSMPTEMinutes)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-seconds"), _("Snap to Seconds"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToSeconds)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-minutes"), _("Snap to Minutes"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToMinutes)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-thirtyseconds"), _("Snap to Thirtyseconds"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToAThirtysecondBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-asixteenthbeat"), _("Snap to Asixteenthbeat"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToASixteenthBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-eighths"), _("Snap to Eighths"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToAEighthBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-quarters"), _("Snap to Quarters"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToAQuarterBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-thirds"), _("Snap to Thirds"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToAThirdBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-beat"), _("Snap to Beat"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToBeat)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-bar"), _("Snap to Bar"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToBar)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-mark"), _("Snap to Mark"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToMark)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-region-start"), _("Snap to Region start"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToRegionStart)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-region-end"), _("Snap to Region End"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToRegionEnd)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-region-sync"), _("Snap to Region Sync"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToRegionSync)));
	ActionManager::register_radio_action (snap_actions, snap_choice_group, X_("snap-to-region-boundary"), _("Snap to Region Boundary"), (bind (mem_fun(*this, &Editor::snap_type_chosen), Editing::SnapToRegionBoundary)));

	/* RULERS */
	
	Glib::RefPtr<ActionGroup> ruler_actions = ActionGroup::create (X_("Rulers"));
	ruler_tempo_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-tempo-ruler"), _("Tempo"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_tempo)));
	ruler_meter_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-meter-ruler"), _("Meter"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_meter)));
	ruler_range_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-range-ruler"), _("Ranges"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_range_marker)));
	ruler_marker_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-marker-ruler"), _("Markers"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_marker)));
	ruler_cd_marker_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-cd-marker-ruler"), _("CD Markers"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_cd_marker)));
	ruler_loop_punch_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-loop-punch-ruler"), _("Loop/Punch"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_time_transport_marker)));
	ruler_bbt_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-bbt-ruler"), _("Bars & Beats"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_metric_frames)));
	ruler_samples_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-samples-ruler"), _("Samples"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_metric_bbt)));
	ruler_timecode_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-timecode-ruler"), _("Timecode"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_metric_smpte)));
	ruler_minsec_action = Glib::RefPtr<ToggleAction>::cast_static (ActionManager::register_toggle_action (ruler_actions, X_("toggle-minsec-ruler"), _("Min:Sec"), bind (mem_fun(*this, &Editor::toggle_ruler_visibility), ruler_metric_minsec)));

	/* set defaults here */

	no_ruler_shown_update = true;
	ruler_meter_action->set_active (true);
	ruler_tempo_action->set_active (true);
	ruler_marker_action->set_active (true);
	ruler_range_action->set_active (false);
	ruler_loop_punch_action->set_active (true);
	ruler_loop_punch_action->set_active (true);
	if (Profile->get_sae()) {
		ruler_bbt_action->set_active (true);
		ruler_cd_marker_action->set_active (false);
		ruler_timecode_action->set_active (false);
		ruler_minsec_action->set_active (true);
	} else {
		ruler_bbt_action->set_active (false);
		ruler_cd_marker_action->set_active (true);
		ruler_timecode_action->set_active (true);
		ruler_minsec_action->set_active (false);
	}
	ruler_samples_action->set_active (false);
	no_ruler_shown_update = false;
	
	/* REGION LIST */

	Glib::RefPtr<ActionGroup> rl_actions = ActionGroup::create (X_("RegionList"));
	RadioAction::Group sort_type_group;
	RadioAction::Group sort_order_group;

	/* the region list popup menu */
	ActionManager::register_action (rl_actions, X_("RegionListSort"), _("Sort"));

	act = ActionManager::register_action (rl_actions, X_("rlAudition"), _("Audition"), mem_fun(*this, &Editor::audition_region_from_region_list));
	ActionManager::region_list_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (rl_actions, X_("rlHide"), _("Hide"), mem_fun(*this, &Editor::hide_region_from_region_list));
	ActionManager::region_list_selection_sensitive_actions.push_back (act);
	act = ActionManager::register_action (rl_actions, X_("rlRemove"), _("Remove"), mem_fun (*this, &Editor::remove_region_from_region_list));
	ActionManager::region_list_selection_sensitive_actions.push_back (act);
	ActionManager::register_toggle_action (rl_actions, X_("rlShowAll"), _("Show All"), mem_fun(*this, &Editor::toggle_full_region_list));
	ActionManager::register_toggle_action (rl_actions, X_("rlShowAuto"), _("Show Automatic Regions"), mem_fun(*this, &Editor::toggle_show_auto_regions));

	ActionManager::register_radio_action (rl_actions, sort_order_group, X_("SortAscending"),  _("Ascending"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_direction), true));
	ActionManager::register_radio_action (rl_actions, sort_order_group, X_("SortDescending"),   _("Descending"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_direction), false));
	
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionName"),  _("By Region Name"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByName));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionLength"),  _("By Region Length"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByLength));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionPosition"),  _("By Region Position"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByPosition));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionTimestamp"),  _("By Region Timestamp"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByTimestamp));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionStartinFile"),  _("By Region Start in File"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByStartInFile));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortByRegionEndinFile"),  _("By Region End in File"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), ByEndInFile));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortBySourceFileName"),  _("By Source File Name"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), BySourceFileName));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortBySourceFileLength"),  _("By Source File Length"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), BySourceFileLength));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortBySourceFileCreationDate"),  _("By Source File Creation Date"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), BySourceFileCreationDate));
	ActionManager::register_radio_action (rl_actions, sort_type_group, X_("SortBySourceFilesystem"),  _("By Source Filesystem"),
			       bind (mem_fun(*this, &Editor::reset_region_list_sort_type), BySourceFileFS));


	/* the next two are duplicate items with different names for use in two different contexts */

	act = ActionManager::register_action (editor_actions, X_("addExistingAudioFiles"), _("Import"), mem_fun (*this, &Editor::external_audio_dialog));
	ActionManager::session_sensitive_actions.push_back (act);
	act = ActionManager::register_action (editor_actions, X_("addExternalAudioToRegionList"), _("Import to Region List"), bind (mem_fun(*this, &Editor::add_external_audio_action), ImportAsRegion));
	ActionManager::session_sensitive_actions.push_back (act);
	
	ActionManager::register_action (editor_actions, X_("importFromSession"), _("Import From Session"), mem_fun(*this, &Editor::session_import_dialog));

	ActionManager::register_toggle_action (editor_actions, X_("ToggleWaveformsWhileRecording"), _("Show Waveforms While Recording"), mem_fun (*this, &Editor::toggle_waveforms_while_recording));

	ActionManager::register_toggle_action (editor_actions, X_("ToggleSummary"), _("Show Summary"), mem_fun (*this, &Editor::set_summary));

	ActionManager::register_toggle_action (editor_actions, X_("ToggleGroupTabs"), _("Show Group Tabs"), mem_fun (*this, &Editor::set_group_tabs));
	
	ActionManager::register_toggle_action (editor_actions, X_("ToggleMeasureVisibility"), _("Show Measures"), mem_fun (*this, &Editor::toggle_measure_visibility));
	
	/* if there is a logo in the editor canvas, its always visible at startup */

	act = ActionManager::register_toggle_action (editor_actions, X_("ToggleLogoVisibility"), _("Show Logo"), mem_fun (*this, &Editor::toggle_logo_visibility));
	Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
	tact->set_active (true);

	ActionManager::add_action_group (rl_actions);
	ActionManager::add_action_group (ruler_actions);
	ActionManager::add_action_group (zoom_actions);
	ActionManager::add_action_group (mouse_mode_actions);
	ActionManager::add_action_group (snap_actions);
	ActionManager::add_action_group (editor_actions);
}

void
Editor::toggle_ruler_visibility (RulerType rt)
{
	const char* action = 0;

	if (no_ruler_shown_update) {
		return;
	}

	switch (rt) {
	case ruler_metric_smpte:
		action = "toggle-timecode-ruler";
		break;
	case ruler_metric_bbt:
		action = "toggle-bbt-ruler";
		break;
	case ruler_metric_frames:
		action = "toggle-samples-ruler";
		break;
	case ruler_metric_minsec:
		action = "toggle-minsec-ruler";
		break;
	case ruler_time_tempo:
		action = "toggle-tempo-ruler";
		break;
	case ruler_time_meter:
		action = "toggle-meter-ruler";
		break;
	case ruler_time_marker:
		action = "toggle-marker-ruler";
		break;
	case ruler_time_range_marker:
		action = "toggle-range-ruler";
		break;
	case ruler_time_transport_marker:
		action = "toggle-loop-punch-ruler";
		break;
	case ruler_time_cd_marker:
		action = "toggle-cd-marker-ruler";
		break;
	}

	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Rulers"), action);
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		update_ruler_visibility ();
		store_ruler_visibility ();
	}
}

void
Editor::toggle_waveforms_while_recording ()
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleWaveformsWhileRecording"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		set_show_waveforms_recording (tact->get_active());
	}
}

void
Editor::set_summary ()
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleSummary"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
		session->config.set_show_summary (tact->get_active ());
	}
}

void
Editor::set_group_tabs ()
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleGroupTabs"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
		session->config.set_show_group_tabs (tact->get_active ());
	}
}

void
Editor::toggle_measure_visibility ()
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleMeasureVisibility"));
	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		set_show_measures (tact->get_active());
	}
}

void
Editor::toggle_logo_visibility ()
{
	Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleLogoVisibility"));

	if (act) {
		Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic(act);
		if (logo_item) {
			if (tact->get_active()) {
				logo_item->show ();
			} else {
				logo_item->hide ();
			}
		}
	}
}

RefPtr<RadioAction>
Editor::snap_type_action (SnapType type)
{

	const char* action = 0;
	RefPtr<Action> act;
	
	switch (type) {
	case Editing::SnapToCDFrame:
		action = "snap-to-cd-frame";
		break;
	case Editing::SnapToSMPTEFrame:
		action = "snap-to-smpte-frame";
		break;
	case Editing::SnapToSMPTESeconds:
		action = "snap-to-smpte-seconds";
		break;
	case Editing::SnapToSMPTEMinutes:
		action = "snap-to-smpte-minutes";
		break;
	case Editing::SnapToSeconds:
		action = "snap-to-seconds";
		break;
	case Editing::SnapToMinutes:
		action = "snap-to-minutes";
		break;
	case Editing::SnapToAThirtysecondBeat:
		action = "snap-to-thirtyseconds";
		break;
	case Editing::SnapToASixteenthBeat:
		action = "snap-to-asixteenthbeat";
		break;
	case Editing::SnapToAEighthBeat:
		action = "snap-to-eighths";
		break;
	case Editing::SnapToAQuarterBeat:
		action = "snap-to-quarters";
		break;
	case Editing::SnapToAThirdBeat:
		action = "snap-to-thirds";
		break;
	case Editing::SnapToBeat:
		action = "snap-to-beat";
		break;
	case Editing::SnapToBar:
		action = "snap-to-bar";
		break;
	case Editing::SnapToMark:
		action = "snap-to-mark";
		break;
	case Editing::SnapToRegionStart:
		action = "snap-to-region-start";
		break;
	case Editing::SnapToRegionEnd:
		action = "snap-to-region-end";
		break;
	case Editing::SnapToRegionSync:
		action = "snap-to-region-sync";
		break;
	case Editing::SnapToRegionBoundary:
		action = "snap-to-region-boundary";
		break;
	default:
		fatal << string_compose (_("programming error: %1: %2"), "Editor: impossible snap-to type", (int) type) << endmsg;
		/*NOTREACHED*/
	}

	act = ActionManager::get_action (X_("Snap"), action);

	if (act) {
		RefPtr<RadioAction> ract = RefPtr<RadioAction>::cast_dynamic(act);
		return ract;

	} else  {
		error << string_compose (_("programming error: %1"), "Editor::snap_type_chosen could not find action to match type.") << endmsg;
		return RefPtr<RadioAction>();
	}
}

void
Editor::cycle_snap_choice()
{
	switch (snap_type) {
	case Editing::SnapToCDFrame:
		set_snap_to (Editing::SnapToSMPTEFrame);
		break;
	case Editing::SnapToSMPTEFrame:
		set_snap_to (Editing::SnapToSMPTESeconds);
		break;
	case Editing::SnapToSMPTESeconds:
		set_snap_to (Editing::SnapToSMPTEMinutes);
		break;
	case Editing::SnapToSMPTEMinutes:
		set_snap_to (Editing::SnapToSeconds);
		break;
	case Editing::SnapToSeconds:
		set_snap_to (Editing::SnapToMinutes);
		break;
	case Editing::SnapToMinutes:
		set_snap_to (Editing::SnapToAThirtysecondBeat);
		break;
	case Editing::SnapToAThirtysecondBeat:
		set_snap_to (Editing::SnapToASixteenthBeat);
		break;
	case Editing::SnapToASixteenthBeat:
		set_snap_to (Editing::SnapToAEighthBeat);
		break;
	case Editing::SnapToAEighthBeat:
		set_snap_to (Editing::SnapToAQuarterBeat);
		break;
	case Editing::SnapToAQuarterBeat:
		set_snap_to (Editing::SnapToAThirdBeat);
		break;
	case Editing::SnapToAThirdBeat:
		set_snap_to (Editing::SnapToBeat);
		break;
	case Editing::SnapToBeat:
		set_snap_to (Editing::SnapToBar);
		break;
	case Editing::SnapToBar:
		set_snap_to (Editing::SnapToMark);
		break;
	case Editing::SnapToMark:
		set_snap_to (Editing::SnapToRegionStart);
		break;
	case Editing::SnapToRegionStart:
		set_snap_to (Editing::SnapToRegionEnd);
		break;
	case Editing::SnapToRegionEnd:
		set_snap_to (Editing::SnapToRegionSync);
		break;
	case Editing::SnapToRegionSync:
		set_snap_to (Editing::SnapToRegionBoundary);
		break;
	case Editing::SnapToRegionBoundary:
		set_snap_to (Editing::SnapToCDFrame);
		break;
	}
}

void
Editor::snap_type_chosen (SnapType type)
{
	/* this is driven by a toggle on a radio group, and so is invoked twice,
	   once for the item that became inactive and once for the one that became
	   active.
	*/

	RefPtr<RadioAction> ract = snap_type_action (type);

	if (ract && ract->get_active()) {
		set_snap_to (type);
	}
}

RefPtr<RadioAction>
Editor::snap_mode_action (SnapMode mode)
{
	const char* action = 0;
	RefPtr<Action> act;
	
	switch (mode) {
	case Editing::SnapOff:
		action = X_("snap-off");
		break;
	case Editing::SnapNormal:
		action = X_("snap-normal");
		break;
	case Editing::SnapMagnetic:
		action = X_("snap-magnetic");
		break;
	default:
		fatal << string_compose (_("programming error: %1: %2"), "Editor: impossible snap mode type", (int) mode) << endmsg;
		/*NOTREACHED*/
	}
	
	act = ActionManager::get_action (X_("Editor"), action);
	
	if (act) {
		RefPtr<RadioAction> ract = RefPtr<RadioAction>::cast_dynamic(act);
		return ract;
		
	} else  {
		error << string_compose (_("programming error: %1: %2"), "Editor::snap_mode_chosen could not find action to match mode.", action) << endmsg;
		return RefPtr<RadioAction> ();
	}
}

void
Editor::cycle_snap_mode ()
{
	switch (snap_mode) {
	case SnapOff:
		set_snap_mode (SnapNormal);
		break;
	case SnapNormal:
		set_snap_mode (SnapMagnetic);
		break;
	case SnapMagnetic:
		set_snap_mode (SnapOff);
		break;
	}
}

void
Editor::snap_mode_chosen (SnapMode mode)
{
	/* this is driven by a toggle on a radio group, and so is invoked twice,
	   once for the item that became inactive and once for the one that became
	   active.
	*/

	RefPtr<RadioAction> ract = snap_mode_action (mode);

	if (ract && ract->get_active()) {
		set_snap_mode (mode);
	}
}

RefPtr<RadioAction>
Editor::edit_point_action (EditPoint ep)
{
	const char* action = 0;
	RefPtr<Action> act;
	
	switch (ep) {
	case Editing::EditAtPlayhead:
		action = X_("edit-at-playhead");
		break;
	case Editing::EditAtSelectedMarker:
		action = X_("edit-at-selected-marker");
		break;
	case Editing::EditAtMouse:
		action = X_("edit-at-mouse");
		break;
	default:
		fatal << string_compose (_("programming error: %1: %2"), "Editor: impossible edit point type", (int) ep) << endmsg;
		/*NOTREACHED*/
	}
	
	act = ActionManager::get_action (X_("Editor"), action);
	
	if (act) {
		RefPtr<RadioAction> ract = RefPtr<RadioAction>::cast_dynamic(act);
		return ract;
		
	} else  {
		error << string_compose (_("programming error: %1: %2"), "Editor::edit_point_action could not find action to match edit point.", action) << endmsg;
		return RefPtr<RadioAction> ();
	}
}

void
Editor::edit_point_chosen (EditPoint ep)
{
	/* this is driven by a toggle on a radio group, and so is invoked twice,
	   once for the item that became inactive and once for the one that became
	   active.
	*/

	RefPtr<RadioAction> ract = edit_point_action (ep);

	if (ract && ract->get_active()) {
		set_edit_point_preference (ep);
	}
}


RefPtr<RadioAction>
Editor::zoom_focus_action (ZoomFocus focus)
{
	const char* action = 0;
	RefPtr<Action> act;
	
	switch (focus) {
	case ZoomFocusLeft:
		action = X_("zoom-focus-left");
		break;
	case ZoomFocusRight:
		action = X_("zoom-focus-right");
		break;
	case ZoomFocusCenter:
		action = X_("zoom-focus-center");
		break;
	case ZoomFocusPlayhead:
		action = X_("zoom-focus-playhead");
		break;
	case ZoomFocusMouse:
		action = X_("zoom-focus-mouse");
		break;
	case ZoomFocusEdit:
		action = X_("zoom-focus-edit");
		break;
	default:
		fatal << string_compose (_("programming error: %1: %2"), "Editor: impossible focus type", (int) focus) << endmsg;
		/*NOTREACHED*/
	}
	
	act = ActionManager::get_action (X_("Zoom"), action);
	
	if (act) {
		RefPtr<RadioAction> ract = RefPtr<RadioAction>::cast_dynamic(act);
		return ract;
	} else {
		error << string_compose (_("programming error: %1: %2"), "Editor::zoom_focus_action could not find action to match focus.", action) << endmsg;
	}

	return RefPtr<RadioAction> ();
}

void
Editor::zoom_focus_chosen (ZoomFocus focus)
{
	/* this is driven by a toggle on a radio group, and so is invoked twice,
	   once for the item that became inactive and once for the one that became
	   active.
	*/

	RefPtr<RadioAction> ract = zoom_focus_action (focus);

	if (ract && ract->get_active()) {
		set_zoom_focus (focus);
	}
}

/** A Configuration parameter has changed.
 * @param parameter_name Name of the changed parameter.
 */
void
Editor::parameter_changed (std::string p)
{
	ENSURE_GUI_THREAD (bind (mem_fun (*this, &Editor::parameter_changed), p));

	if (p == "auto-loop") {
		update_loop_range_view (true);
	} else if (p == "punch-in") {
		update_punch_range_view (true);
	} else if (p == "punch-out") {
		update_punch_range_view (true);
	} else if (p == "smpte-format") {
		update_just_smpte ();
	} else if (p == "xfades-visible") {
		update_xfade_visibility ();
	} else if (p == "show-region-fades") {
		update_region_fade_visibility ();
	} else if (p == "edit-mode") {
		edit_mode_selector.set_active_text (edit_mode_to_string (Config->get_edit_mode()));
	} else if (p == "subframes-per-frame") {
		update_just_smpte ();
	} else if (p == "show-track-meters") {
		toggle_meter_updating();
	} else if (p == "show-summary") {
		
		bool const s = session->config.get_show_summary ();
 		if (s) {
 			_summary->show ();
 		} else {
 			_summary->hide ();
 		}

		Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleSummary"));
		if (act) {
			Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
			if (tact->get_active () != s) {
				tact->set_active (s);
			}
		}
	} else if (p == "show-group-tabs") {

		bool const s = session->config.get_show_group_tabs ();
		if (s) {
			_group_tabs->show ();
		} else {
			_group_tabs->hide ();
		}

		Glib::RefPtr<Action> act = ActionManager::get_action (X_("Editor"), X_("ToggleGroupTabs"));
		if (act) {
			Glib::RefPtr<ToggleAction> tact = Glib::RefPtr<ToggleAction>::cast_dynamic (act);
			if (tact->get_active () != s) {
				tact->set_active (s);
			}
		}
	}
}

void
Editor::reset_focus ()
{
	track_canvas->grab_focus();
}

void
Editor::reset_canvas_action_sensitivity (bool onoff)
{
	if (_edit_point != EditAtMouse) {
		onoff = true;
	}

	for (vector<Glib::RefPtr<Action> >::iterator x = ActionManager::mouse_edit_point_requires_canvas_actions.begin();  
	     x != ActionManager::mouse_edit_point_requires_canvas_actions.end(); ++x) {
		(*x)->set_sensitive (onoff);
	}
}

