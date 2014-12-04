/*
    Copyright (C) 2000-2007 Paul Davis 

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

#ifndef __pbd__crossthread_h__
#define __pbd__crossthread_h__

#ifdef check
#undef check
#endif

#include <glibmm/main.h>

#include "pbd/libpbd_visibility.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif // PLATFORM_WINDOWS


/** A simple abstraction of a mechanism of signalling one thread from another.
 * The signaller calls ::wakeup() to tell the signalled thread to check for
 * work to be done. 
 *
 * This implementation provides both ::selectable() for use in direct
 * poll/select-based event loops, and a Glib::IOSource via ::ios() for use
 * in Glib main loop based situations. 
 */

class LIBPBD_API CrossThreadChannel { 
  public:
	/** if @a non_blocking is true, the channel will not cause blocking
	 * when used in an event loop based on poll/select or the glib main
	 * loop.
	 */
	CrossThreadChannel(bool non_blocking);
	~CrossThreadChannel();
	
	/** Tell the listening thread that is has work to do.
	 */
	void wakeup();
	
	/* if the listening thread cares about the precise message
	 * it is being sent, then ::deliver() can be used to send
	 * a single byte message rather than a simple wakeup. These
	 * two mechanisms should not be used on the same CrossThreadChannel
	 * because there is no way to know which byte value will be used
	 * for ::wakeup()
	 */
     int deliver (char msg);

	/** if using ::deliver() to wakeup the listening thread, then
	 * the listener should call ::receive() to fetch the message
	 * type from the channel.
	 */
     int receive (char& msg);

	/** empty the channel of all requests.
	 * Typically this is done as soon as input 
	 * is noticed on the channel, because the
	 * handler will look at a separately managed work
	 * queue. The actual number of queued "wakeups"
	 * in the channel will not be important.
	 */
	void drain ();

    void set_receive_handler (sigc::slot<bool,Glib::IOCondition> s);
    void attach (Glib::RefPtr<Glib::MainContext>);

private:
	friend gboolean cross_thread_channel_call_receive_slot (GIOChannel*, GIOCondition condition, void *data);

	GIOChannel* receive_channel;
    GSource*    receive_source;
    sigc::slot<bool,Glib::IOCondition> receive_slot;

#ifndef PLATFORM_WINDOWS
	int fds[2]; // current implementation uses a pipe/fifo
#else

	SOCKET send_socket;
	SOCKET receive_socket;
	struct sockaddr_in recv_address;
#endif

};

#endif /* __pbd__crossthread_h__ */
