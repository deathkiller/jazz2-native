// Copyright 2007-2010 Baptiste Lepilleur and The JsonCpp Authors
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#ifndef JSON_ASSERTIONS_H_INCLUDED
#define JSON_ASSERTIONS_H_INCLUDED

#include <cstdlib>

#include "config.h"

/** It should not be possible for a maliciously designed file to
 *  cause an abort() or seg-fault, so these macros are used only
 *  for pre-condition violations and internal logic errors.
 *
 *  The message reaches Json::throwLogicError() as it is rather than being composed in an
 *  OStringStream first: every message here is a literal, and that one stream in a header the whole
 *  library includes instantiated the ostream and <locale> machinery in every translation unit that
 *  asserts - 350 KB of code in the final link, for a path that ends in abort(). Both forms of
 *  throwLogicError() do not return: one throws, the other prints the message and aborts.
 */
#if JSON_USE_EXCEPTION

 // @todo <= add detail about condition in exception
#define JSON_ASSERT(condition)										\
	do {															\
		if (!(condition)) {											\
			Json::throwLogicError("assert json failed");			\
		}															\
	} while (0)

#define JSON_FAIL_MESSAGE(message)									\
	do {															\
		Json::throwLogicError(message);								\
		abort();													\
	} while (0)

#else // JSON_USE_EXCEPTION

#define JSON_ASSERT(condition) assert(condition)

 // The call to assert() will show the failure message in debug builds. In
 // release builds we abort, for a core-dump or debugger.
#define JSON_FAIL_MESSAGE(message)									\
	{																\
		Json::throwLogicError(message);								\
		abort();													\
	}

#endif

#define JSON_ASSERT_MESSAGE(condition, message)						\
	do {															\
		if (!(condition)) {											\
			JSON_FAIL_MESSAGE(message);								\
		}															\
	} while (0)

#endif // JSON_ASSERTIONS_H_INCLUDED
