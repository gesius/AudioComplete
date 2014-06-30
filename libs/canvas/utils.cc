/*
    Copyright (C) 2011-2013 Paul Davis
    Author: Carl Hetherington <cth@carlh.net>

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

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <cairomm/context.h>
#include "canvas/utils.h"

using std::max;
using std::min;

void
ArdourCanvas::color_to_hsv (Color color, double& h, double& s, double& v)
{
	double r, g, b, a;
	double cmax;
	double cmin;
	double delta;
	
	color_to_rgba (color, r, g, b, a);
	
	if (r > g) {
		cmax = max (r, b);
	} else {
		cmax = max (g, b);
	}

	if (r < g) {
		cmin = min (r, b);
	} else {
		cmin = min (g, b);
	}

	v = cmax;

	delta = cmax - cmin;

	if (cmax == 0) {
		// r = g = b == 0 ... v is undefined, s = 0
		s = 0.0;  
		h = -1.0;
	}

	if (delta != 0.0) {	
		if (cmax == r) {
			h = fmod ((g - b)/delta, 6.0);
		} else if (cmax == g) {
			h = ((b - r)/delta) + 2;
		} else {
			h = ((r - g)/delta) + 4;
		}
		
		h *= 60.0;
	}

	if (delta == 0 || cmax == 0) {
		s = 0;
	} else {
		s = delta / cmax;
	}

}

ArdourCanvas::Color
ArdourCanvas::hsv_to_color (double h, double s, double v, double a)
{
	s = min (1.0, max (0.0, s));
	v = min (1.0, max (0.0, v));

	if (s == 0) {
		// achromatic (grey)
		return rgba_to_color (v, v, v, a);
	}

	h = min (360.0, max (0.0, h));

	double c = v * s;
        double x = c * (1.0 - fabs(fmod(h / 60.0, 2) - 1.0));
        double m = v - c;

        if (h >= 0.0 && h < 60.0) {
		return rgba_to_color (c + m, x + m, m, a);
        } else if (h >= 60.0 && h < 120.0) {
		return rgba_to_color (x + m, c + m, m, a);
        } else if (h >= 120.0 && h < 180.0) {
		return rgba_to_color (m, c + m, x + m, a);
        } else if (h >= 180.0 && h < 240.0) {
		return rgba_to_color (m, x + m, c + m, a);
        } else if (h >= 240.0 && h < 300.0) {
		return rgba_to_color (x + m, m, c + m, a);
        } else if (h >= 300.0 && h < 360.0) {
		return rgba_to_color (c + m, m, x + m, a);
        } 
	return rgba_to_color (m, m, m, a);
}

void
ArdourCanvas::color_to_rgba (Color color, double& r, double& g, double& b, double& a)
{
	r = ((color >> 24) & 0xff) / 255.0;
	g = ((color >> 16) & 0xff) / 255.0;
	b = ((color >>  8) & 0xff) / 255.0;
	a = ((color >>  0) & 0xff) / 255.0;
}

ArdourCanvas::Color
ArdourCanvas::rgba_to_color (double r, double g, double b, double a)
{
	/* clamp to [0 .. 1] range */

	r = min (1.0, max (0.0, r));
	g = min (1.0, max (0.0, g));
	b = min (1.0, max (0.0, b));
	a = min (1.0, max (0.0, a));

	/* convert to [0..255] range */

	unsigned int rc, gc, bc, ac;
	rc = rint (r * 255.0);
	gc = rint (g * 255.0);
	bc = rint (b * 255.0);
	ac = rint (a * 255.0);

	/* build-an-integer */

	return (rc << 24) | (gc << 16) | (bc << 8) | ac;
}

void
ArdourCanvas::set_source_rgba (Cairo::RefPtr<Cairo::Context> context, Color color)
{
	context->set_source_rgba (
		((color >> 24) & 0xff) / 255.0,
		((color >> 16) & 0xff) / 255.0,
		((color >>  8) & 0xff) / 255.0,
		((color >>  0) & 0xff) / 255.0
		);
}

ArdourCanvas::Distance
ArdourCanvas::distance_to_segment_squared (Duple const & p, Duple const & p1, Duple const & p2, double& t, Duple& at)
{
	static const double kMinSegmentLenSquared = 0.00000001;  // adjust to suit.  If you use float, you'll probably want something like 0.000001f
	static const double kEpsilon = 1.0E-14;  // adjust to suit.  If you use floats, you'll probably want something like 1E-7f
	double dx = p2.x - p1.x;
	double dy = p2.y - p1.y;
	double dp1x = p.x - p1.x;
	double dp1y = p.y - p1.y;
	const double segLenSquared = (dx * dx) + (dy * dy);

	if (segLenSquared >= -kMinSegmentLenSquared && segLenSquared <= kMinSegmentLenSquared) {
		// segment is a point.
		at = p1;
		t = 0.0;
		return ((dp1x * dp1x) + (dp1y * dp1y));
	} 


	// Project a line from p to the segment [p1,p2].  By considering the line
	// extending the segment, parameterized as p1 + (t * (p2 - p1)),
	// we find projection of point p onto the line. 
	// It falls where t = [(p - p1) . (p2 - p1)] / |p2 - p1|^2
		
	t = ((dp1x * dx) + (dp1y * dy)) / segLenSquared;

	if (t < kEpsilon) {
		// intersects at or to the "left" of first segment vertex (p1.x, p1.y).  If t is approximately 0.0, then
		// intersection is at p1.  If t is less than that, then there is no intersection (i.e. p is not within
		// the 'bounds' of the segment)
		if (t > -kEpsilon) {
			// intersects at 1st segment vertex
			t = 0.0;
		}
		// set our 'intersection' point to p1.
		at = p1;
		// Note: If you wanted the ACTUAL intersection point of where the projected lines would intersect if
		// we were doing PointLineDistanceSquared, then qx would be (p1.x + (t * dx)) and qy would be (p1.y + (t * dy)).

	} else if (t > (1.0 - kEpsilon)) {
		// intersects at or to the "right" of second segment vertex (p2.x, p2.y).  If t is approximately 1.0, then
		// intersection is at p2.  If t is greater than that, then there is no intersection (i.e. p is not within
		// the 'bounds' of the segment)
		if (t < (1.0 + kEpsilon)) {
			// intersects at 2nd segment vertex
			t = 1.0;
		}
		// set our 'intersection' point to p2.
		at = p2;
		// Note: If you wanted the ACTUAL intersection point of where the projected lines would intersect if
		// we were doing PointLineDistanceSquared, then qx would be (p1.x + (t * dx)) and qy would be (p1.y + (t * dy)).
	} else {
		// The projection of the point to the point on the segment that is perpendicular succeeded and the point
		// is 'within' the bounds of the segment.  Set the intersection point as that projected point.
		at = Duple (p1.x + (t * dx), p1.y + (t * dy));
	}

	// return the squared distance from p to the intersection point.  Note that we return the squared distance
	// as an optimization because many times you just need to compare relative distances and the squared values
	// works fine for that.  If you want the ACTUAL distance, just take the square root of this value.
	double dpqx = p.x - at.x;
	double dpqy = p.y - at.y;

	return ((dpqx * dpqx) + (dpqy * dpqy));
}

uint32_t
ArdourCanvas::contrasting_text_color (uint32_t c)
{
	double r, g, b, a;
	ArdourCanvas::color_to_rgba (c, r, g, b, a);

	const double black_r = 0.0;
	const double black_g = 0.0;
	const double black_b = 0.0;

	const double white_r = 1.0;
	const double white_g = 1.0;
	const double white_b = 1.0;

	/* Use W3C contrast guideline calculation */

	double white_contrast = (max (r, white_r) - min (r, white_r)) +
		(max (g, white_g) - min (g, white_g)) + 
		(max (b, white_b) - min (b, white_b));

	double black_contrast = (max (r, black_r) - min (r, black_r)) +
		(max (g, black_g) - min (g, black_g)) + 
		(max (b, black_b) - min (b, black_b));

	if (white_contrast > black_contrast) {		
		/* use white */
		return ArdourCanvas::rgba_to_color (1.0, 1.0, 1.0, 1.0);
	} else {
		/* use black */
		return ArdourCanvas::rgba_to_color (0.0, 0.0, 0.0, 1.0);
	}
}
