/*
    Copyright (C) 2001 Paul Davis 

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

#ifndef __ardour_redirect_h__
#define __ardour_redirect_h__

#include <string>
#include <boost/shared_ptr.hpp>
#include <sigc++/signal.h>

#include <glibmm/thread.h>

#include "pbd/undo.h"

#include "ardour/ardour.h"
#include "ardour/processor.h"

class XMLNode;

namespace ARDOUR {

class Session;
class IO;

/** A mixer strip element (Processor) with Jack ports (IO).
 */
class IOProcessor : public Processor
{
  public:
	IOProcessor (Session&, const std::string& proc_name, const std::string io_name="",
		     ARDOUR::DataType default_type = DataType::AUDIO);
	IOProcessor (Session&, IO* io, const std::string& proc_name,
		     ARDOUR::DataType default_type = DataType::AUDIO);
	virtual ~IOProcessor ();
	
	bool set_name (const std::string& str);

	virtual ChanCount output_streams() const;
	virtual ChanCount input_streams () const;
	virtual ChanCount natural_output_streams() const;
	virtual ChanCount natural_input_streams () const;

	boost::shared_ptr<IO>       io()       { return _io; }
	boost::shared_ptr<const IO> io() const { return _io; }
	void set_io (boost::shared_ptr<IO>);
	
	virtual void automation_snapshot (nframes_t now, bool force);

	virtual void run_in_place (BufferSet& in, sframes_t start, sframes_t end, nframes_t nframes) = 0;
	void silence (nframes_t nframes);

	sigc::signal<void,IOProcessor*,bool>     AutomationPlaybackChanged;
	sigc::signal<void,IOProcessor*,uint32_t> AutomationChanged;
	
	XMLNode& state (bool full_state);
	int set_state (const XMLNode&);
	
  protected:
	boost::shared_ptr<IO> _io;

  private:
	/* disallow copy construction */
	IOProcessor (const IOProcessor&);
	bool _own_io;

};

} // namespace ARDOUR

#endif /* __ardour_redirect_h__ */
