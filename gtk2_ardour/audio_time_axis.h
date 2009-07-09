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

#ifndef __ardour_audio_time_axis_h__
#define __ardour_audio_time_axis_h__

#include <gtkmm/table.h>
#include <gtkmm/button.h>
#include <gtkmm/box.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/radiomenuitem.h>
#include <gtkmm/checkmenuitem.h>

#include <gtkmm2ext/selector.h>
#include <list>

#include "ardour/types.h"

#include "ardour_dialog.h"
#include "route_ui.h"
#include "enums.h"
#include "editing.h"
#include "route_time_axis.h"
#include "canvas.h"
#include "shared_ptrs.h"

namespace ARDOUR {
	class Session;
	class AudioDiskstream;
	class RouteGroup;
	class IOProcessor;
	class Processor;
	class Location;
	class AudioPlaylist;
}

class PublicEditor;
class AudioThing;
class AudioStreamView;
class Selection;
class Selectable;
class RegionView;
class AudioRegionView;
class AutomationLine;
class AutomationGainLine;
class AutomationPanLine;
class TimeSelection;
class AutomationTimeAxisView;

class AudioTimeAxisView : public RouteTimeAxisView
{
  public:
	static AudioTimeAxisViewPtr
 	create (PublicEditor&, ARDOUR::Session&, boost::shared_ptr<ARDOUR::Route>, ArdourCanvas::Canvas& canvas);
	
 	virtual ~AudioTimeAxisView ();
	
	AudioStreamView* audio_view();

	void set_show_waveforms_recording (bool yn);
	void show_all_xfades ();
	void hide_all_xfades ();
	void hide_dependent_views (TimeAxisViewItem&);
	void reveal_dependent_views (TimeAxisViewItem&);
		
	/* Overridden from parent to store display state */
	guint32 show_at (double y, int& nth, Gtk::VBox *parent);
	void hide ();
	
	void create_automation_child (const Evoral::Parameter& param, bool show);
	
	void first_idle ();

	XMLNode* get_child_xml_node (const std::string & childname);

  private:
	friend class AudioStreamView;
	friend class AudioRegionView;

 	AudioTimeAxisView (PublicEditor&, ARDOUR::Session&, boost::shared_ptr<ARDOUR::Route>, ArdourCanvas::Canvas& canvas);
	void init (PublicEditor&, ARDOUR::Session&, boost::shared_ptr<ARDOUR::Route>, ArdourCanvas::Canvas& canvas);
	
	void route_active_changed ();

	void append_extra_display_menu_items ();
	Gtk::Menu* build_mode_menu();
	
	void show_all_automation ();
	void show_existing_automation ();
	void hide_all_automation ();

	void gain_hidden ();
	void pan_hidden ();

	void ensure_pan_views (bool show = true);
	void update_control_names ();
};

#endif /* __ardour_audio_time_axis_h__ */

