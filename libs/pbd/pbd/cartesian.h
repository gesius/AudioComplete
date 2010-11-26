/*
    Copyright (C) 2010 Paul Davis

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

#ifndef __libpbd_cartesian_h__
#define __libpbd_cartesian_h__

#include <cfloat>
#include <cmath>

namespace PBD {

void azi_ele_to_cart (double azi, double ele, double& x, double& y, double& z);
void cart_to_azi_ele (double x, double y, double z, double& azi, double& ele);
        
struct AngularVector;

struct CartesianVector {
	double x;
	double y;
	double z;

	CartesianVector () : x(0.0), y(0.0), z(0.0) {}
	CartesianVector (double xp, double yp, double zp = 0.0) : x(xp), y(yp), z(zp) {}

	CartesianVector& translate (CartesianVector& other, double xtranslate, double ytranslate, double ztranslate = 0.0) {
		other.x += xtranslate;
		other.y += ytranslate;
		other.z += ztranslate;
		return other;
	}

	CartesianVector& scale (CartesianVector& other, double xscale, double yscale, double zscale = 1.0) {
		other.x *= xscale;
		other.y *= yscale;
		other.z *= zscale;
		return other;
	}

	void angular (AngularVector&) const;
};

struct AngularVector {
	double azi;
	double ele;
	double length;

	AngularVector () : azi(0.0), ele(0.0), length (0.0) {}
	AngularVector (double a, double e, double l = 1.0) : azi(a), ele(e), length (l) {}

	AngularVector operator- (const AngularVector& other) const {
		AngularVector r;
		r.azi = azi - other.azi;
		r.ele = ele - other.ele;
		r.length = length - other.length;
		return r;
	}

	AngularVector operator+ (const AngularVector& other) const {
		AngularVector r;
		r.azi = azi + other.azi;
		r.ele = ele + other.ele;
		r.length = length + other.length;
		return r;
	}

	bool operator== (const AngularVector& other) const {
		return fabs (azi - other.azi) <= FLT_EPSILON &&
		fabs (ele - other.ele) <= FLT_EPSILON &&
		fabs (length - other.length) <= FLT_EPSILON;
	}

	bool operator!= (const AngularVector& other) const {
		return fabs (azi - other.azi) > FLT_EPSILON ||
			fabs (ele - other.ele) > FLT_EPSILON ||
			fabs (length - other.length) > FLT_EPSILON;
	}

	void cartesian (CartesianVector& c) const {
		azi_ele_to_cart (azi, ele, c.x, c.y, c.z);
	}
};

inline void CartesianVector::angular (AngularVector& a) const {
	cart_to_azi_ele (x, y, z, a.azi, a.ele);
}

}

#endif /* __libpbd_cartesian_h__ */
