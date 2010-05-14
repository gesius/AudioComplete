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

#ifndef __file_manager_h__
#define __file_manager_h__

#include <sys/types.h>
#include <string>
#include <map>
#include <sndfile.h>
#include <glibmm/thread.h>
#include "pbd/signals.h"

namespace ARDOUR {

class FileManager;

/** Parent class for FileDescriptors.
 *
 *  When a subclass is instantiated, the file it describes is added to a
 *  list.  The FileDescriptor can be `allocated', meaning that its
 *  file will be opened on the filesystem, and can then be `released'.
 *  FileDescriptors are reference counted as they are allocated and
 *  released.  When a descriptor's refcount is 0, the file on the
 *  filesystem is eligible to be closed if necessary to free up file
 *  handles for other files.
 *
 *  The upshot of all this is that Ardour can manage the number of
 *  open files to stay within limits imposed by the operating system.
 */
	
class FileDescriptor
{
public:
	FileDescriptor (std::string const &, bool);
	virtual ~FileDescriptor () {}

	void release ();

	/** Emitted when the file is closed */
	PBD::Signal0<void> Closed;

protected:

	friend class FileManager;

	/* These methods and variables must be called / accessed
	   with a lock held on the FileManager's mutex
	*/

	/** @return false on success, true on failure */
	virtual bool open () = 0;
	virtual void close () = 0;
	virtual bool is_open () const = 0;

	int refcount; ///< number of active users of this file
	double last_used; ///< monotonic time that this file was last allocated
	std::string name; ///< filename
	bool writeable; ///< true if it should be opened writeable, otherwise false

	FileManager* manager ();
	
private:
	
	static FileManager* _manager;
};

/** FileDescriptor for a file to be opened using libsndfile */	
class SndFileDescriptor : public FileDescriptor
{
public:
	SndFileDescriptor (std::string const &, bool, SF_INFO *);
	~SndFileDescriptor ();

	SNDFILE* allocate ();

private:	

	friend class FileManager;

	bool open ();
	void close ();
	bool is_open () const;

	SNDFILE* _sndfile; ///< SNDFILE* pointer, or 0 if the file is closed
	SF_INFO* _info; ///< libsndfile's info for this file
};

/** FileDescriptor for a file to be opened using POSIX open */	
class FdFileDescriptor : public FileDescriptor
{
public:
	FdFileDescriptor (std::string const &, bool, mode_t);
	~FdFileDescriptor ();

	int allocate ();

private:

	friend class FileManager;

	bool open ();
	void close ();
	bool is_open () const;

	int _fd; ///< file descriptor, or -1 if the file is closed
	mode_t _mode; ///< mode to use when creating files
};

}

#endif
