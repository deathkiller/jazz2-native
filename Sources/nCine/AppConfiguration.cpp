#include "AppConfiguration.h"
#include "../Main.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	AppConfiguration::AppConfiguration()
		:
		resolution(0, 0),
		windowPosition(WindowPositionIgnore, WindowPositionIgnore),
		frameLimit(0),
		frameTimerLogInterval(5.0f),
		fullscreen(false),
		resizable(true),
		windowScaling(true),
		useBufferMapping(false),
		useBufferStorage(true),
#if defined(WITH_FIXED_BATCH_SIZE) && WITH_FIXED_BATCH_SIZE > 0
		fixedBatchSize(WITH_FIXED_BATCH_SIZE),
#elif defined(DEATH_TARGET_WINDOWS_RT)
		fixedBatchSize(24),
#elif defined(DEATH_TARGET_EMSCRIPTEN) || defined(WITH_ANGLE)
		fixedBatchSize(10),
#else
		fixedBatchSize(0),
#endif
#if defined(WITH_IMGUI)
		vboSize(512 * 1024),
		iboSize(128 * 1024),
#else
		vboSize(64 * 1024),
		iboSize(8 * 1024),
#endif
		vaoPoolSize(16),
		renderCommandPoolSize(32),
#if defined(WITH_IMGUI)
		withDebugOverlay(false),
#endif
		withAudio(true),
		withGraphics(true),
		withScenegraph(true),
		withThreads(false),
		withVSync(true),
		withGlDebugContext(false),

		// Compile-time variables
		glCoreProfile_(true),
		glForwardCompatible_(true),
#if defined(RHI_GL_PROFILE_ES2)
		// Real OpenGL|ES 2.0 profile (PS Vita target): ESSL 100, no UBOs, no gl_VertexID
		glMajorVersion_(2),
		glMinorVersion_(0),
#elif defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		glMajorVersion_(3),
		glMinorVersion_(0),
#else
		glMajorVersion_(3),
		glMinorVersion_(3),
#endif
		argv_(nullptr)
	{
#if defined(DEATH_TARGET_ANDROID)
		dataPath() = "assets:/"_s;
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		dataPath() = fs::PathSeparator;
		// Always disable mapping on Emscripten as it is not supported by WebGL 2
		useBufferMapping = false;
#else
		dataPath() = "Content"_s + fs::PathSeparator;
#endif

#if defined(RHI_GL_PROFILE_ES2)
		// glMapBufferRange()/glUnmapBuffer() are ES 3.0 (OES_mapbuffer is write-only and not assumed), so the
		// streaming buffers must use the glBufferSubData() host-copy path on the ES2 profile
		useBufferMapping = false;
#endif

#if defined(DEATH_TARGET_UNIX) && (defined(WITH_SDL2) || defined(WITH_SDL3))
		// DPI queries do not seem to work reliably on X11 with SDL2
		windowScaling = false;
#endif
	}

	const String& AppConfiguration::dataPath() const
	{
		return dataPath_;
	}

	String& AppConfiguration::dataPath()
	{
		return dataPath_;
	}

	const StringView AppConfiguration::argv(std::size_t index) const
	{
		return argv_[index];
	}
}
