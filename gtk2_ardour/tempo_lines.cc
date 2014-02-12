/*
    Copyright (C) 2002-2007 Paul Davis

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

#include "pbd/compose.h"

#include "canvas/line.h"
#include "canvas/canvas.h"
#include "canvas/debug.h"

#include "tempo_lines.h"
#include "ardour_ui.h"
#include "public_editor.h"

using namespace std;

TempoLines::TempoLines (ArdourCanvas::Canvas& canvas, ArdourCanvas::Group* group, double h)
	: _canvas (canvas)
	, _group (group)
	, _height (h)
{
}

void
TempoLines::tempo_map_changed()
{
	/* remove all lines from the group, put them in the cache (to avoid
	 * unnecessary object destruction+construction later), and clear _lines
	 */
	 
	_group->clear ();
	_cache.insert (_cache.end(), _lines.begin(), _lines.end());
	_lines.clear ();
}

void
TempoLines::show ()
{
	_group->show ();
}

void
TempoLines::hide ()
{
	_group->hide ();
}

void
TempoLines::draw (const ARDOUR::TempoMap::BBTPointList::const_iterator& begin, 
		  const ARDOUR::TempoMap::BBTPointList::const_iterator& end)
{
	ARDOUR::TempoMap::BBTPointList::const_iterator i;
	ArdourCanvas::Rect const visible = _canvas.visible_area ();
	double  beat_density;

	uint32_t beats = 0;
	uint32_t bars = 0;
	uint32_t color;

	/* get the first bar spacing */

	i = end;
	i--;
	bars = (*i).bar - (*begin).bar; 
	beats = distance (begin, end) - bars;

	beat_density = (beats * 10.0f) / visible.width ();

	if (beat_density > 4.0f) {
		/* if the lines are too close together, they become useless */
		tempo_map_changed();
		return;
	}

	tempo_map_changed ();

	for (i = begin; i != end; ++i) {

		if ((*i).is_bar()) {
			color = ARDOUR_UI::config()->get_canvasvar_MeasureLineBar();
		} else {
			if (beat_density > 2.0) {
				continue; /* only draw beat lines if the gaps between beats are large. */
			}
			color = ARDOUR_UI::config()->get_canvasvar_MeasureLineBeat();
		}

		ArdourCanvas::Coord xpos = PublicEditor::instance().sample_to_pixel_unrounded ((*i).frame);

		ArdourCanvas::Line* line;

		if (!_cache.empty()) {
			line = _cache.back ();
			_cache.pop_back ();
			line->reparent (_group);
		} else {
			line = new ArdourCanvas::Line (_group);
			CANVAS_DEBUG_NAME (line, string_compose ("tempo measure line @ %1", (*i).frame));
			line->set_ignore_events (true);
		}

		line->set_x0 (xpos);
		line->set_x1 (xpos);
		line->set_y0 (0.0);
		line->set_y1 (_height);
		line->set_outline_color (color);
		line->show ();
	}
}

