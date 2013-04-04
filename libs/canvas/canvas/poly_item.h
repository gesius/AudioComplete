#ifndef __CANVAS_POLY_ITEM_H__
#define __CANVAS_POLY_ITEM_H__

#include "canvas/item.h"
#include "canvas/outline.h"

namespace ArdourCanvas {

class PolyItem : virtual public Item, public Outline
{
public:
	PolyItem (Group *);

	void compute_bounding_box () const;

	void add_poly_item_state (XMLNode *) const;
	void set_poly_item_state (XMLNode const *);
	
	void set (Points const &);
	Points const & get () const;

protected:
	void render_path (Rect const &, Cairo::RefPtr<Cairo::Context>) const;

	Points _points;
};
	
}

#endif
