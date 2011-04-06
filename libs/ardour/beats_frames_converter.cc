/*
    Copyright (C) 2009 Paul Davis
    Author: David Robillard

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

    $Id: midiregion.h 733 2006-08-01 17:19:38Z drobilla $
*/

#include "ardour/beats_frames_converter.h"
#include "ardour/tempo.h"

namespace ARDOUR {

framecnt_t
BeatsFramesConverter::to (double beats) const
{
	return _tempo_map.framepos_plus_bbt (_origin_b, Timecode::BBT_Time(beats)) - _origin_b;
}

double
BeatsFramesConverter::from (framecnt_t frames) const
{
	return _tempo_map.framewalk_to_beats (_origin_b, frames);
}

} /* namespace ARDOUR */

