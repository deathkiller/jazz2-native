#include "AppConfiguration.h"
#include "IO/FileSystem.h"
#include "../Common.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	AppConfiguration::AppConfiguration()
		:
		frameTimerLogInterval(5.0f),
		resolution(1280, 720),
		inFullscreen(false),
		isResizable(true),
		frameLimit(0),
		useBufferMapping(false),
		deferShaderQueries(true),
		fixedBatchSize(10),
#if defined(WITH_IMGUI) || defined(WITH_NUKLEAR)
		vboSize(512 * 1024),
		iboSize(128 * 1024),
#else
		vboSize(64 * 1024),
		iboSize(8 * 1024),
#endif
		vaoPoolSize(16),
		renderCommandPoolSize(32),
		withDebugOverlay(false),
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
		profileTextUpdateTime_(0.2f),
		argc_(0),
		argv_(nullptr)
	{
#if defined(DEATH_TARGET_ANDROID)
		dataPath() = "asset::"_s;
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		dataPath() = fs::PathSeparator;
		// Always disable mapping on Emscripten as it is not supported by WebGL 2
		useBufferMapping = false;
#else
		dataPath() = "Content"_s + fs::PathSeparator;
#endif
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	const String& AppConfiguration::dataPath() const
	{
		return fs::dataPath_;
	}

	String& AppConfiguration::dataPath()
	{
		return fs::dataPath_;
	}

	const char* AppConfiguration::argv(int index) const
	{
		if (index < argc_)
			return argv_[index];
		return nullptr;
	}

}
