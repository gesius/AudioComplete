/*
    Copyright (C) 2013 Paul Davis

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

#ifndef __libevoral_libevoral_visibility_h__
#define __libevoral_libevoral_visibility_h__

/* _WIN32 is defined by most compilers targetting Windows, but within the
 * evoral source tree, we also define COMPILER_MSVC or COMPILER_MINGW depending
 * on how a Windows build is built.
 */

#if defined _WIN32 || defined __CYGWIN__ || defined(COMPILER_MSVC) || defined(COMPILER_MINGW)
  #define LIBEVORAL_DLL_IMPORT __declspec(dllimport)
  #define LIBEVORAL_DLL_EXPORT __declspec(dllexport)
  #define LIBEVORAL_DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define LIBEVORAL_DLL_IMPORT __attribute__ ((visibility ("default")))
    #define LIBEVORAL_DLL_EXPORT __attribute__ ((visibility ("default")))
    #define LIBEVORAL_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define LIBEVORAL_DLL_IMPORT
    #define LIBEVORAL_DLL_EXPORT
    #define LIBEVORAL_DLL_LOCAL
  #endif
#endif

#ifdef LIBEVORAL_DLL // libevoral is a DLL
#ifdef LIBEVORAL_DLL_EXPORTS // defined if we are building the libevoral DLL (instead of using it)
    #define LIBEVORAL_API LIBEVORAL_DLL_EXPORT
#else
    #define LIBEVORAL_API LIBEVORAL_DLL_IMPORT
#endif 
#define     LIBEVORAL_LOCAL LIBEVORAL_DLL_LOCAL
#else /* static lib, not DLL */
#define LIBEVORAL_API
#define LIBEVORAL_LOCAL
#endif

#endif /* __libevoral_libevoral_visibility_h__ */
