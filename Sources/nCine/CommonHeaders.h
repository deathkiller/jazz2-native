#include <CommonInternal.h>

#if defined(NCINE_INCLUDE_OPENGL)
#	if defined(WITH_OPENGLES)
#		define GL_GLEXT_PROTOTYPES
#		include <GLES3/gl3.h>
#		include <GLES2/gl2ext.h>
#	elif defined(WITH_GLEW)
#		define GLEW_NO_GLU
#		if defined(_MSC_VER) && defined(__has_include)
#			if __has_include("../../Libs/GL/glew.h")
#				define __HAS_LOCAL_OPENGL
#			endif
#		endif
#		ifdef __HAS_LOCAL_OPENGL
#			include "../../Libs/GL/glew.h"
#		else
#			include <GL/glew.h>
#		endif
#	elif defined(__APPLE__)
#		define GL_SILENCE_DEPRECATION
#		include <OpenGL/gl3.h>
#		include <OpenGL/gl3ext.h>
#	else
#		if defined(_MSC_VER) && defined(__has_include)
#			if __has_include("../../Libs/GL/glew.h")
#				define __HAS_LOCAL_OPENGL
#			endif
#		endif
#		ifdef __HAS_LOCAL_OPENGL
#			include "../../Libs/GL/glew.h"
#		else
#			define GL_GLEXT_PROTOTYPES
#			include <GL/gl.h>
#			include <GL/glext.h>
#		endif
#	endif
#endif

#if defined(NCINE_INCLUDE_OPENAL)
#	define AL_ALEXT_PROTOTYPES
#	ifdef __APPLE__
#		include <OpenAL/al.h>
#	else
#		if defined(_MSC_VER) && defined(__has_include)
#			if __has_include("../../Libs/AL/al.h")
#				define __HAS_LOCAL_OPENAL
#			endif
#		endif
#		ifdef __HAS_LOCAL_OPENAL
#			include "../../Libs/AL/al.h"
#			include "../../Libs/AL/alext.h"
#		else
#			include <AL/al.h>
#			if !defined(__EMSCRIPTEN__)
#				include <AL/alext.h>
#			endif
#		endif
#		if !defined(__EMSCRIPTEN__)
#			define OPENAL_FILTERS_SUPPORTED
#		endif
#	endif
#endif

#if defined(NCINE_INCLUDE_OPENALC)
#	ifdef __APPLE__
#		include <OpenAL/alc.h>
#		include <OpenAL/al.h>
#	else
#		if defined(_MSC_VER) && defined(__has_include)
#			if __has_include("../../Libs/AL/al.h")
#				define __HAS_LOCAL_OPENALC
#			endif
#		endif
#		ifdef __HAS_LOCAL_OPENALC
#			include "../../Libs/AL/alc.h"
#			include "../../Libs/AL/al.h"
#			include "../../Libs/AL/alext.h"
#			if defined(__has_include)
#				if __has_include("../../Libs/AL/inprogext.h")
#					include "../../Libs/AL/inprogext.h"
#				endif
#			endif
#		else
#			include <AL/alc.h>
#			include <AL/al.h>
#			if !defined(__EMSCRIPTEN__)
#				include <AL/alext.h>
#			endif
#			if defined(__has_include)
#				if __has_include(<AL/inprogext.h>)
#					include <AL/inprogext.h>
#				endif
#			endif
#		endif
#	endif
#endif