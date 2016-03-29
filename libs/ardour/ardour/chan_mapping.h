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
*/

#ifndef __ardour_chan_mapping_h__
#define __ardour_chan_mapping_h__

#include <map>
#include <cassert>
#include <ostream>
#include <utility>

#include "ardour/data_type.h"
#include "ardour/chan_count.h"

namespace ARDOUR {


/** A mapping from one set of channels to another
 * (e.g. how to 'connect' two BufferSets).
 *
 * for plugins the form is  "pin" -> "buffer"
 */
class LIBARDOUR_API ChanMapping {
public:
	ChanMapping() {}
	ChanMapping(ARDOUR::ChanCount identity);
	ChanMapping(const ChanMapping&);

	uint32_t get(DataType t, uint32_t from, bool* valid) const;

	/** get buffer mapping for given data type and pin
	 * @param type data type
	 * @param from pin
	 * @returns mapped buffer number (or ChanMapping::Invalid)
	 */
	uint32_t get(DataType t, uint32_t from) const { return get (t, from, NULL); }
	/** set buffer mapping for given data type
	 * @param type data type
	 * @param from pin
	 * @param to buffer
	 */
	void     set(DataType t, uint32_t from, uint32_t to);
	void     offset_from(DataType t, int32_t delta);
	void     offset_to(DataType t, int32_t delta);

	/** remove mapping
	 * @param type data type
	 * @param from source to remove from mapping
	 */
	void     unset(DataType t, uint32_t from);

	/** Test mapping matrix for identity
	 * @param offset per data-type offset to take into account
	 * @returns true if the mapping is a channel identity map
	 */
	bool     is_identity (ARDOUR::ChanCount offset = ARDOUR::ChanCount()) const;

	/** Test if this mapping is monotonic (useful to see if inplace processing is feasible)
	 * @returns true if the map is a strict monotonic set
	 */
	bool     is_monotonic () const;


	/** Test if this mapping is a subset
	 * @param superset to test against
	 * @returns true if all mapping are also present in the superset
	 */
	bool     is_subset (const ChanMapping& superset) const;

	typedef std::map<uint32_t, uint32_t>    TypeMapping;
	typedef std::map<DataType, TypeMapping> Mappings;

	Mappings       mappings()       { return _mappings; }
	const Mappings mappings() const { return _mappings; }

	bool operator==(const ChanMapping& other) const {
		return (_mappings == other._mappings);
	}

	bool operator!=(const ChanMapping& other) const {
		return ! (*this == other);
	}

private:
	Mappings _mappings;
};

} // namespace ARDOUR

std::ostream& operator<<(std::ostream& o, const ARDOUR::ChanMapping& m);

#endif // __ardour_chan_mapping_h__

