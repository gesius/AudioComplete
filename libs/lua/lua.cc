/* This is a C++ wrapper to compile the lua C code
 * with settings appropriate for including it with
 * Ardour.
 */

#include "lua/liblua_visibility.h"

#if _MSC_VER
# pragma push_macro("_CRT_SECURE_NO_WARNINGS")
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#elif defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcast-qual"
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-qual"
#endif

// forward ardour's defines to luaconf.h
#ifdef PLATFORM_WINDOWS
#  define LUA_USE_WINDOWS
#elif defined __APPLE__
#  define LUA_USE_MACOSX
#else
#  define LUA_USE_LINUX
#endif

// forward liblua visibility to luaconf.h
#ifdef LIBLUA_BUILD_AS_DLL
#define LUA_BUILD_AS_DLL
#endif

extern "C"
{

#define lobject_c
#define lvm_c
#define LUA_CORE
#define LUA_LIB
#include "lua-5.3.2/luaconf.h"
#undef lobject_c
#undef lvm_c
#undef LUA_CORE
#undef LUA_LIB

// override luaconf.h symbol export
#ifdef LIBLUA_STATIC // static lib (no DLL)
#  undef LUA_API
#  undef LUALIB_API
#  undef LUAMOD_API
#  define LUA_API     extern "C"
#  define LUALIB_API  LUA_API
#  define LUAMOD_API  LUALIB_API
#endif

// disable support for extenal libs
#undef LUA_DL_DLL
#undef LUA_USE_DLOPEN

// enable bit lib
#define LUA_COMPAT_BITLIB

#if _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4244) /* Possible loss of data */
#pragma warning (disable: 4702) /* Unreachable code */
#endif

#include "lua-5.3.2/ltable.c"

#include "lua-5.3.2/lauxlib.c"
#include "lua-5.3.2/lbaselib.c"

#include "lua-5.3.2/lbitlib.c"
#include "lua-5.3.2/lcorolib.c"
#include "lua-5.3.2/ldblib.c"
#include "lua-5.3.2/linit.c"
#include "lua-5.3.2/liolib.c"
#include "lua-5.3.2/lmathlib.c"
#include "lua-5.3.2/loslib.c"
#include "lua-5.3.2/lstrlib.c"
#include "lua-5.3.2/ltablib.c"

#include "lua-5.3.2/lapi.c"
#include "lua-5.3.2/lcode.c"
#include "lua-5.3.2/lctype.c"
#include "lua-5.3.2/ldebug.c"
#include "lua-5.3.2/ldo.c"
#include "lua-5.3.2/ldump.c"
#include "lua-5.3.2/lfunc.c"
#include "lua-5.3.2/lgc.c"
#include "lua-5.3.2/llex.c"
#include "lua-5.3.2/lmem.c"
#include "lua-5.3.2/lobject.c"
#include "lua-5.3.2/lopcodes.c"
#include "lua-5.3.2/lparser.c"
#include "lua-5.3.2/lstate.c"
#include "lua-5.3.2/lstring.c"
#include "lua-5.3.2/ltm.c"
#include "lua-5.3.2/lundump.c"
#include "lua-5.3.2/lutf8lib.c"
#include "lua-5.3.2/lvm.c"
#include "lua-5.3.2/lzio.c"

#include "lua-5.3.2/loadlib.c"

#if _MSC_VER
#pragma warning (pop)
#endif

} // end extern "C"

#if _MSC_VER
#  pragma pop_macro("_CRT_SECURE_NO_WARNINGS")
#elif defined(__clang__)
#  pragma clang diagnostic pop
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#  pragma GCC diagnostic pop
#endif
