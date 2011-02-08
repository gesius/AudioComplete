/*
    Copyright (C) 1999 Paul Barton-Davis 

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

    $Id$
*/

#include <map>

#include <gtk/gtkpaned.h>
#include <gtk/gtk.h>

#include <gtkmm2ext/utils.h>
#include <gtkmm/widget.h>
#include <gtkmm/button.h>
#include <gtkmm/window.h>
#include <gtkmm/paned.h>
#include <gtkmm/comboboxtext.h>

#include "i18n.h"

using namespace std;

void
Gtkmm2ext::init ()
{
	// Necessary for gettext
	(void) bindtextdomain(PACKAGE, LOCALEDIR);
}

void
Gtkmm2ext::get_ink_pixel_size (Glib::RefPtr<Pango::Layout> layout,
			       int& width,
			       int& height)
{
	Pango::Rectangle ink_rect = layout->get_ink_extents ();
	
	width = (ink_rect.get_width() + PANGO_SCALE / 2) / PANGO_SCALE;
	height = (ink_rect.get_height() + PANGO_SCALE / 2) / PANGO_SCALE;
}

void
get_pixel_size (Glib::RefPtr<Pango::Layout> layout,
			       int& width,
			       int& height)
{
	layout->get_pixel_size (width, height);
}

void
Gtkmm2ext::set_size_request_to_display_given_text (Gtk::Widget &w, const gchar *text,
						   gint hpadding, gint vpadding)
	
{
	int width, height;
	w.ensure_style ();
	
	get_pixel_size (w.create_pango_layout (text), width, height);
	w.set_size_request(width + hpadding, height + vpadding);
}

void
Gtkmm2ext::set_size_request_to_display_given_text (Gtk::Widget &w, 
						   const std::vector<std::string>& strings,
						   gint hpadding, gint vpadding)
	
{
	int width, height;
	int width_max = 0;
	int height_max = 0;
	w.ensure_style ();
        vector<string> copy;
        const vector<string>* to_use;
        vector<string>::const_iterator i;

        for (i = strings.begin(); i != strings.end(); ++i) {
                if ((*i).find_first_of ("gy") != string::npos) {
                        /* contains a descender */
                        break;
                }
        }
	
        if (i == strings.end()) {
                /* make a copy of the strings then add one that has a descener */
                copy = strings;
                copy.push_back ("g");
                to_use = &copy;
        } else {
                to_use = &strings;
        }
	
	for (vector<string>::const_iterator i = to_use->begin(); i != to_use->end(); ++i) {
		get_pixel_size (w.create_pango_layout (*i), width, height);
		width_max = max(width_max,width);
		height_max = max(height_max, height);
	}

	w.set_size_request(width_max + hpadding, height_max + vpadding);
}

static inline guint8
demultiply_alpha (guint8 src,
                  guint8 alpha)
{
        /* cairo pixel buffer data contains RGB values with the alpha
           values premultiplied.

           GdkPixbuf pixel buffer data contains RGB values without the
           alpha value applied.

           this removes the alpha component from the cairo version and
           returns the GdkPixbuf version.
        */
	return alpha ? ((guint (src) << 8) - src) / alpha : 0;
}

static void
convert_bgra_to_rgba (guint8 const* src,
		      guint8*       dst,
		      int           width,
		      int           height)
{
	guint8 const* src_pixel = src;
	guint8*       dst_pixel = dst;
	
        /* cairo pixel data is endian-dependent ARGB with A in the most significant 8 bits,
           with premultipled alpha values (see preceding function)

           GdkPixbuf pixel data is non-endian-dependent RGBA with R in the lowest addressable
           8 bits, and non-premultiplied alpha values.

           convert from the cairo values to the GdkPixbuf ones.
        */

	for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                        /* Cairo [ B G R A ] is actually  [ B G R A ] in memory SOURCE
                                                            0 1 2 3
                           Pixbuf [ R G B A ] is actually [ R G B A ] in memory DEST
                        */
                        dst_pixel[0] = demultiply_alpha (src_pixel[2],
                                                         src_pixel[3]); // R [0] <= [ 2 ]
                        dst_pixel[1] = demultiply_alpha (src_pixel[1],
                                                         src_pixel[3]); // G [1] <= [ 1 ]
                        dst_pixel[2] = demultiply_alpha (src_pixel[0],  
                                                         src_pixel[3]); // B [2] <= [ 0 ]
                        dst_pixel[3] = src_pixel[3]; // alpha
                        
#elif G_BYTE_ORDER == G_BIG_ENDIAN
                        /* Cairo [ B G R A ] is actually  [ A R G B ] in memory SOURCE
                                                            0 1 2 3
                           Pixbuf [ R G B A ] is actually [ R G B A ] in memory DEST
                        */
                        dst_pixel[0] = demultiply_alpha (src_pixel[1],
                                                         src_pixel[0]); // R [0] <= [ 1 ]
                        dst_pixel[1] = demultiply_alpha (src_pixel[2],
                                                         src_pixel[0]); // G [1] <= [ 2 ]
                        dst_pixel[2] = demultiply_alpha (src_pixel[3],
                                                         src_pixel[0]); // B [2] <= [ 3 ]
                        dst_pixel[3] = src_pixel[0]; // alpha
                        
#else
#error ardour does not currently support PDP-endianess
#endif			
                        
                        dst_pixel += 4;
                        src_pixel += 4;
                }
	}
}

Glib::RefPtr<Gdk::Pixbuf>
Gtkmm2ext::pixbuf_from_string(const string& name, Pango::FontDescription* font, int clip_width, int clip_height, Gdk::Color fg)
{
	static Glib::RefPtr<Gdk::Pixbuf>* empty_pixbuf = 0;

	if (name.empty()) {
		if (empty_pixbuf == 0) {
			empty_pixbuf = new Glib::RefPtr<Gdk::Pixbuf>;
			*empty_pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, clip_width, clip_height);
		}
		return *empty_pixbuf;
	}

	Glib::RefPtr<Gdk::Pixbuf> buf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, clip_width, clip_height);

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, clip_width, clip_height);
	cairo_t* cr = cairo_create (surface);
	cairo_text_extents_t te;
	
	cairo_set_source_rgba (cr, fg.get_red_p(), fg.get_green_p(), fg.get_blue_p(), 1.0);
	cairo_select_font_face (cr, font->get_family().c_str(),
				CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr,  font->get_size() / Pango::SCALE);
	cairo_text_extents (cr, name.c_str(), &te);
	
	cairo_move_to (cr, 0.5, int (0.5 - te.height / 2 - te.y_bearing + clip_height / 2));
	cairo_show_text (cr, name.c_str());
	
	convert_bgra_to_rgba(cairo_image_surface_get_data (surface), buf->get_pixels(), clip_width, clip_height);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return buf;
}

void
Gtkmm2ext::set_popdown_strings (Gtk::ComboBoxText& cr, const vector<string>& strings, bool set_size, gint hpadding, gint vpadding)
{
	vector<string>::const_iterator i;

	cr.clear ();

	if (set_size) {
                set_size_request_to_display_given_text (cr, strings, COMBO_FUDGE+10+hpadding, 15+vpadding); 
	}

	for (i = strings.begin(); i != strings.end(); ++i) {
		cr.append_text (*i);
	}
}

GdkWindow*
Gtkmm2ext::get_paned_handle (Gtk::Paned& paned)
{
	return GTK_PANED(paned.gobj())->handle;
}

void
Gtkmm2ext::set_decoration (Gtk::Window* win, Gdk::WMDecoration decor)
{
	win->get_window()->set_decorations (decor);
}

void Gtkmm2ext::set_treeview_header_as_default_label(Gtk::TreeViewColumn* c)
{
	gtk_tree_view_column_set_widget( c->gobj(), GTK_WIDGET(0) );
}

void
Gtkmm2ext::detach_menu (Gtk::Menu& menu)
{
	/* its possible for a Gtk::Menu to have no gobj() because it has
	   not yet been instantiated. Catch this and provide a safe
	   detach method.
	*/
	if (menu.gobj()) {
		if (menu.get_attach_widget()) {
			menu.detach ();
		}
	}
}

bool
Gtkmm2ext::possibly_translate_keyval_to_make_legal_accelerator (uint32_t& keyval)
{
	int fakekey = GDK_VoidSymbol;

	switch (keyval) {
	case GDK_Tab:
	case GDK_ISO_Left_Tab:
		fakekey = GDK_nabla;
		break;

	case GDK_Up:
		fakekey = GDK_uparrow;
		break;

	case GDK_Down:
		fakekey = GDK_downarrow;
		break;

	case GDK_Right:
		fakekey = GDK_rightarrow;
		break;

	case GDK_Left:
		fakekey = GDK_leftarrow;
		break;

	case GDK_Return:
		fakekey = GDK_3270_Enter;
		break;

	case GDK_KP_Enter:
		fakekey = GDK_F35;
		break;

	default:
		break;
	}

	if (fakekey != GDK_VoidSymbol) {
		keyval = fakekey;
		return true;
	}

	return false;
}

uint32_t
Gtkmm2ext::possibly_translate_legal_accelerator_to_real_key (uint32_t keyval)
{
	switch (keyval) {
	case GDK_nabla:
		return GDK_Tab;
		break;

	case GDK_uparrow:
		return GDK_Up;
		break;

	case GDK_downarrow:
		return GDK_Down;
		break;

	case GDK_rightarrow:
		return GDK_Right;
		break;

	case GDK_leftarrow:
		return GDK_Left;
		break;

	case GDK_3270_Enter:
		return GDK_Return;

	case GDK_F35:
		return GDK_KP_Enter;
		break;
	}

	return keyval;
}

int
Gtkmm2ext::physical_screen_height (Glib::RefPtr<Gdk::Window> win)
{
        GdkScreen* scr = gdk_screen_get_default();

        if (win) {
                GdkRectangle r;
                gint monitor = gdk_screen_get_monitor_at_window (scr, win->gobj());
                gdk_screen_get_monitor_geometry (scr, monitor, &r);
                return r.height;
        } else {
                return gdk_screen_get_height (scr);
        }
}

int
Gtkmm2ext::physical_screen_width (Glib::RefPtr<Gdk::Window> win)
{
        GdkScreen* scr = gdk_screen_get_default();
        
        if (win) {
                GdkRectangle r;
                gint monitor = gdk_screen_get_monitor_at_window (scr, win->gobj());
                gdk_screen_get_monitor_geometry (scr, monitor, &r);
                return r.width;
        } else {
                return gdk_screen_get_width (scr);
        }
}

void
Gtkmm2ext::container_clear (Gtk::Container& c)
{
        list<Gtk::Widget*> children = c.get_children();
        for (list<Gtk::Widget*>::iterator child = children.begin(); child != children.end(); ++child) {
                c.remove (**child);
        }
}

#if 1
void
Gtkmm2ext::rounded_rectangle (Cairo::RefPtr<Cairo::Context> context, double x, double y, double w, double h, double r)
{
        /* renders small shapes better than most others */

/*    A****BQ
      H    C
      *    *
      G    D
      F****E
*/
        context->move_to(x+r,y); // Move to A
        context->line_to(x+w-r,y); // Straight line to B
        context->curve_to(x+w,y,x+w,y,x+w,y+r); // Curve to C, Control points are both at Q
        context->line_to(x+w,y+h-r); // Move to D
        context->curve_to(x+w,y+h,x+w,y+h,x+w-r,y+h); // Curve to E
        context->line_to(x+r,y+h); // Line to F
        context->curve_to(x,y+h,x,y+h,x,y+h-r); // Curve to G
        context->line_to(x,y+r); // Line to H
        context->curve_to(x,y,x,y,x+r,y); // Curve to A
}

#else

void
Gtkmm2ext::rounded_rectangle (Cairo::RefPtr<Cairo::Context> context, double x, double y, double width, double height, double radius)
{
        /* doesn't render small shapes well at all, and does not absolutely honor width & height */

        double x0          = x+radius/2.0;
        double y0          = y+radius/2.0;
        double rect_width  = width - radius;
        double rect_height = height - radius;

        context->save();

        double x1=x0+rect_width;
        double y1=y0+rect_height;

        if (rect_width/2<radius) {
                if (rect_height/2<radius) {
                        context->move_to  (x0, (y0 + y1)/2);
                        context->curve_to (x0 ,y0, x0, y0, (x0 + x1)/2, y0);
                        context->curve_to (x1, y0, x1, y0, x1, (y0 + y1)/2);
                        context->curve_to (x1, y1, x1, y1, (x1 + x0)/2, y1);
                        context->curve_to (x0, y1, x0, y1, x0, (y0 + y1)/2);
                } else {
                        context->move_to  (x0, y0 + radius);
                        context->curve_to (x0 ,y0, x0, y0, (x0 + x1)/2, y0);
                        context->curve_to (x1, y0, x1, y0, x1, y0 + radius);
                        context->line_to (x1 , y1 - radius);
                        context->curve_to (x1, y1, x1, y1, (x1 + x0)/2, y1);
                        context->curve_to (x0, y1, x0, y1, x0, y1- radius);
                }
        } else {
                if (rect_height/2<radius) {
                        context->move_to  (x0, (y0 + y1)/2);
                        context->curve_to (x0 , y0, x0 , y0, x0 + radius, y0);
                        context->line_to (x1 - radius, y0);
                        context->curve_to (x1, y0, x1, y0, x1, (y0 + y1)/2);
                        context->curve_to (x1, y1, x1, y1, x1 - radius, y1);
                        context->line_to (x0 + radius, y1);
                        context->curve_to (x0, y1, x0, y1, x0, (y0 + y1)/2);
                } else {
                        context->move_to  (x0, y0 + radius);
                        context->curve_to (x0 , y0, x0 , y0, x0 + radius, y0);
                        context->line_to (x1 - radius, y0);
                        context->curve_to (x1, y0, x1, y0, x1, y0 + radius);
                        context->line_to (x1 , y1 - radius);
                        context->curve_to (x1, y1, x1, y1, x1 - radius, y1);
                        context->line_to (x0 + radius, y1);
                        context->curve_to (x0, y1, x0, y1, x0, y1- radius);
                }
        }

        context->close_path ();
        context->restore();
}

#endif
