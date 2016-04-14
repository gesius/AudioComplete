/*
    Copyright (C) 1998-2006 Paul Davis

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
#ifndef __libpbd_error_h__
#define __libpbd_error_h__

#include "pbd/libpbd_visibility.h"
#include "transmitter.h"

namespace PBD {
	LIBPBD_API extern Transmitter error;
	LIBPBD_API extern Transmitter info;
	LIBPBD_API extern Transmitter warning;
	LIBPBD_API extern Transmitter fatal;
}

#endif  // __libpbd_error_h__
