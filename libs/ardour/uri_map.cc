/*
    Copyright (C) 2008-2010 Paul Davis
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

*/

#include <cassert>
#include <iostream>
#include <stdint.h>
#include "ardour/uri_map.h"

using namespace std;

namespace ARDOUR {


URIMap::URIMap()
	: next_uri_id(1)
{
	uri_map_feature_data.uri_to_id = &URIMap::uri_map_uri_to_id;
	uri_map_feature_data.callback_data = this;
	uri_map_feature.URI = LV2_URI_MAP_URI;
	uri_map_feature.data = &uri_map_feature_data;
}


uint32_t
URIMap::uri_to_id(const char* map,
                  const char* uri)
{
	return uri_map_uri_to_id(this, map, uri);
}


uint32_t
URIMap::uri_map_uri_to_id(LV2_URI_Map_Callback_Data callback_data,
                          const char*               /*map*/,
                          const char*               uri)
{
	// TODO: map ignored, < UINT16_MAX assumed

	URIMap* me = (URIMap*)callback_data;
	uint32_t ret = 0;

	Map::iterator i = me->uri_map.find(uri);
	if (i != me->uri_map.end()) {
		ret = i->second;
	} else {
		ret = me->next_uri_id++;
		me->uri_map.insert(make_pair(string(uri), ret));
	}

	/*cout << "URI MAP (" << (map ? (void*)map : NULL)
		<< "): " << uri << " -> " << ret << endl;*/

	assert(ret <= UINT16_MAX);
	return ret;
}


} // namespace ARDOUR

