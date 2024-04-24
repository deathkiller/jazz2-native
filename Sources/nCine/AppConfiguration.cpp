#include "AppConfiguration.h"
#include "../Common.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	AppConfiguration::AppConfiguration()
		:
		frameTimerLogInterval(5.0f),
		resolution(0, 0),
		fullscreen(false),
		//windowPosition(WindowPositionIgnore, WindowPositionIgnore),
		resizable(true),
		windowScaling(true),
		frameLimit(0),
		useBufferMapping(false),
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
		withThreads(false),
		withScenegraph(true),
		withVSync(true),
		withGlDebugContext(false),

		// Compile-time variables
		glCoreProfile_(true),
		glForwardCompatible_(true),
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		glMajorVersion_(3),
		glMinorVersion_(0),
#else
		glMajorVersion_(3),
		glMinorVersion_(3),
#endif
		argv_(nullptr)
	{
#if defined(DEATH_TARGET_ANDROID)
		dataPath() = "asset:/"_s;
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		dataPath() = fs::PathSeparator;
		// Always disable mapping on Emscripten as it is not supported by WebGL 2
		useBufferMapping = false;
#else
		dataPath() = "Content"_s + fs::PathSeparator;
#endif

#if defined(DEATH_TARGET_UNIX) && defined(WITH_SDL)
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

	const StringView AppConfiguration::argv(int index) const
	{
		return argv_[index];
	}
}
