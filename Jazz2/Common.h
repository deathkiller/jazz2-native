#pragma once

#if !defined(_MSC_VER) && defined(__has_include)
#   if __has_include("../Shared/Common.h")
#       define __HAS_LOCAL_COMMON
#   endif
#endif
#ifdef __HAS_LOCAL_COMMON
#   include "../Shared/Common.h"
#else
#   include <Common.h>
#endif

// These attributes are not defined outside of MSVC
#if !defined(__in)
#   define __in
#endif
#if !defined(__in_opt)
#   define __in_opt
#endif
#if !defined(__out)
#   define __out
#endif
#if !defined(__out_opt)
#   define __out_opt
#endif
#if !defined(__success)
#   define __success(expr)
#endif