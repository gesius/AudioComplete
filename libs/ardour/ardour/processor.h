/*
    Copyright (C) 2000 Paul Davis 

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

#ifndef __ardour_processor_h__
#define __ardour_processor_h__

#include <vector>
#include <string>
#include <exception>

#include "pbd/statefuldestructible.h" 

#include <sigc++/signal.h>

#include "ardour/ardour.h"
#include "ardour/automatable_controls.h"
#include "ardour/buffer_set.h"
#include "ardour/latent.h"
#include "ardour/session_object.h"
#include "ardour/types.h"

class XMLNode;

namespace ARDOUR {

class Session;
class Route;

/* A mixer strip element - plugin, send, meter, etc.
 */
class Processor : public SessionObject, public AutomatableControls, public Latent
{
  public:
	static const std::string state_node_name;

	Processor(Session&, const std::string& name);
	
	virtual ~Processor() { }
	
	/** Configuration of a processor on a bus
	 * (i.e. how to apply to a BufferSet)
	 */
	struct Mapping {
	    ChanCount in;
	    ChanCount out;
	};

	virtual bool visible() const { return true; }
	
	bool active () const { return _active; }
	
	bool get_next_ab_is_active () const { return _next_ab_is_active; }
	void set_next_ab_is_active (bool yn) { _next_ab_is_active = yn; }
	
	virtual nframes_t signal_latency() const { return 0; }
	
	virtual void transport_stopped (nframes_t frame) {}
	
	virtual void set_block_size (nframes_t nframes) {}

	virtual void run_in_place (BufferSet& bufs,
				   sframes_t start_frame, sframes_t end_frame,
				   nframes_t nframes) { assert(is_in_place()); }
	
	virtual void run_out_of_place (BufferSet& input, BufferSet& output,
				       sframes_t start_frame, sframes_t end_frame,
				       nframes_t nframes) { assert(is_out_of_place()); }
	
	virtual void silence (nframes_t nframes) {}
	
	void activate ()   { _active = true; ActiveChanged(); }
	void deactivate () { _active = false; ActiveChanged(); }
	
	virtual bool configure_io (ChanCount in, ChanCount out);

	/* Derived classes should override these, or processor appears as an in-place pass-through */

	/** In-place processors implement run_in_place and modify thee input buffer parameter */
	virtual bool is_in_place () const { return true; }

	/* Out-Of-Place processors implement run_out_of_place, don't modify the input parameter
	 * and write to their output parameter */
	virtual bool is_out_of_place () const { return false; }

	virtual bool can_support_io_configuration (const ChanCount& in, ChanCount& out) const = 0;
	virtual ChanCount input_streams () const { return _configured_input; }
	virtual ChanCount output_streams() const { return _configured_output; }

	/* note: derived classes should implement state(), NOT get_state(), to allow
	   us to merge C++ inheritance and XML lack-of-inheritance reasonably
	   smoothly.
	 */

	virtual XMLNode& state (bool full);
	XMLNode& get_state (void);
	int set_state (const XMLNode&);
	
	void *get_gui () const { return _gui; }
	void  set_gui (void *p) { _gui = p; }

	static sigc::signal<void,Processor*> ProcessorCreated;

	sigc::signal<void>                     ActiveChanged;
	sigc::signal<void,ChanCount,ChanCount> ConfigurationChanged;

protected:
	int       _pending_active;
	bool      _active;
	bool      _next_ab_is_active;
	bool      _configured;
	ChanCount _configured_input;
	ChanCount _configured_output;
	void*     _gui;  /* generic, we don't know or care what this is */
	Mapping   _mapping;
};

} // namespace ARDOUR

#endif /* __ardour_processor_h__ */
