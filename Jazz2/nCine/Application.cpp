#include "../Common.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	if defined(_M_X64)
#		ifdef WITH_GLEW
#			pragma comment(lib, "../Libs/x64/glew32.lib")
#		endif
#		ifdef WITH_GLFW
#			pragma comment(lib, "../Libs/x64/glfw3dll.lib")
#		endif
#		ifdef WITH_SDL
#			pragma comment(lib, "../Libs/x64/SDL2.lib")
#		endif
#		ifdef WITH_AUDIO
#			pragma comment(lib, "../Libs/x64/OpenAL32.lib")
#		endif
#	elif defined(_M_IX86)
#		ifdef WITH_GLEW
#			pragma comment(lib, "../Libs/x86/glew32.lib")
#		endif
#		ifdef WITH_GLFW
#			pragma comment(lib, "../Libs/x86/glfw3dll.lib")
#		endif
#		ifdef WITH_SDL
#			pragma comment(lib, "../Libs/x86/SDL2.lib")
#		endif
#		ifdef WITH_AUDIO
#			pragma comment(lib, "../Libs/x86/OpenAL32.lib")
#		endif
#	else
#		error Unsupported architecture
#	endif

extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
};

#endif

#include "Application.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
#include "IO/FileSystem.h"
#include "IO/IFileStream.h"
#include "ArrayIndexer.h"
#include "Graphics/GfxCapabilities.h"
#include "Graphics/RenderResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/GL/GLDebug.h"
#include "Base/Timer.h" // for `sleep()`
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#ifdef WITH_AUDIO
#	include "Audio/ALAudioDevice.h"
#endif

#ifdef WITH_THREADS
#	include "Threading/ThreadPool.h"
#endif

#ifdef WITH_LUA
#	include "LuaStatistics.h"
#endif

#ifdef WITH_RENDERDOC
#	include "Graphics/RenderDocCapture.h"
#endif

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Application::Application()
		: isSuspended_(false), autoSuspension_(true), hasFocus_(true), shouldQuit_(false)
	{
	}

	Application::~Application() = default;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	Viewport& Application::screenViewport()
	{
		return *screenViewport_;
	}

	unsigned long int Application::numFrames() const
	{
		return frameTimer_->totalNumberFrames();
	}

	float Application::averageFps() const
	{
		return frameTimer_->averageFps();
	}

	float Application::timeMult() const
	{
		return frameTimer_->timeMult();
	}

	void Application::resizeScreenViewport(int width, int height)
	{
		if (screenViewport_ != nullptr) {
			bool sizeChanged = (width != screenViewport_->width_ || height != screenViewport_->height_);
			screenViewport_->resize(width, height);
			if (sizeChanged && width > 0 && height > 0) {
				appEventHandler_->onResizeWindow(width, height);
			}
		}
	}

	void Application::handleFullscreenChanged(bool isFullscreen)
	{
		appEventHandler_->onFullscreenChanged(isFullscreen);
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	void Application::initCommon()
	{
		TracyGpuContext;
		ZoneScoped;
		profileStartTime_ = TimeStamp::now();

		//char appInfoString[128];
		//#ifdef WITH_GIT_VERSION
		//	formatString(appInfoString, sizeof(appInfoString), "nCine %s (%s) compiled on %s at %s", VersionStrings::Version, VersionStrings::GitBranch,
		//	                     VersionStrings::CompilationDate, VersionStrings::CompilationTime);
		//#else
		//	formatString(appInfoString, sizeof(appInfoString), "nCine compiled on %s at %s", VersionStrings::CompilationDate, VersionStrings::CompilationTime);
		//#endif
		//	LOGI_X("%s", appInfoString);
#ifdef WITH_TRACY
		TracyAppInfo("nCine", 5);
		LOGI("Tracy integration is enabled");
#endif

		theServiceLocator().registerIndexer(std::make_unique<ArrayIndexer>());
#ifdef WITH_AUDIO
		if (appCfg_.withAudio)
			theServiceLocator().registerAudioDevice(std::make_unique<ALAudioDevice>());
#endif
#ifdef WITH_THREADS
		if (appCfg_.withThreads)
			theServiceLocator().registerThreadPool(std::make_unique<ThreadPool>());
#endif
		theServiceLocator().registerGfxCapabilities(std::make_unique<GfxCapabilities>());
		GLDebug::init(theServiceLocator().gfxCapabilities());

		LOGI_X("Data path: \"%s\"", fs::GetDataPath().data());

#ifdef WITH_RENDERDOC
		RenderDocCapture::init();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, appCfg_.profileTextUpdateTime());

		if (appCfg_.withScenegraph) {
			gfxDevice_->setupGL();
			RenderResources::create();
			rootNode_ = std::make_unique<SceneNode>();
			screenViewport_ = std::make_unique<ScreenViewport>();
			screenViewport_->setRootNode(rootNode_.get());
		} else {
			RenderResources::createMinimal(); // some resources are still required for rendering
		}

		// Initialization of the static random generator seeds
		Random().Initialize(static_cast<uint64_t>(TimeStamp::now().ticks()), static_cast<uint64_t>(profileStartTime_.ticks()));

		LOGI("Application initialized");

		timings_[Timings::InitCommon] = profileStartTime_.secondsSince();

		{
			ZoneScopedN("onInit");
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onInit();
			timings_[Timings::AppInit] = profileStartTime_.secondsSince();
			LOGI("IAppEventHandler::onInit() invoked");
		}

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;
	}

	void Application::step()
	{

		frameTimer_->addFrame();

#ifdef WITH_LUA
		LuaStatistics::update();
#endif

		{
			ZoneScopedN("onFrameStart");
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onFrameStart();
			timings_[Timings::FrameStart] = profileStartTime_.secondsSince();
		}

		if (appCfg_.withScenegraph) {
			ZoneScopedN("SceneGraph");
			{
				ZoneScopedN("Update");
				profileStartTime_ = TimeStamp::now();
				screenViewport_->update();
				timings_[Timings::Update] = profileStartTime_.secondsSince();
			}

			{
				ZoneScopedN("onPostUpdate");
				profileStartTime_ = TimeStamp::now();
				appEventHandler_->onPostUpdate();
				timings_[Timings::PostUpdate] = profileStartTime_.secondsSince();
			}

			{
				ZoneScopedN("Visit");
				profileStartTime_ = TimeStamp::now();
				screenViewport_->visit();
				timings_[Timings::Visit] = profileStartTime_.secondsSince();
			}

			{
				ZoneScopedN("Draw");
				profileStartTime_ = TimeStamp::now();
				screenViewport_->sortAndCommitQueue();
				screenViewport_->draw();
				timings_[Timings::Draw] = profileStartTime_.secondsSince();
			}
		}

		{
			theServiceLocator().audioDevice().updatePlayers();
		}

		{
			ZoneScopedN("onFrameEnd");
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onFrameEnd();
			timings_[Timings::FrameEnd] = profileStartTime_.secondsSince();
		}

		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;

		if (appCfg_.frameLimit > 0) {
			const float frameTimeDuration = 1.0f / static_cast<float>(appCfg_.frameLimit);
			while (frameTimer_->frameInterval() < frameTimeDuration) {
				Timer::sleep(0.0f);
			}
		}
	}

	void Application::shutdownCommon()
	{
		ZoneScoped;
		appEventHandler_->onShutdown();
		LOGI("IAppEventHandler::onShutdown() invoked");
		appEventHandler_.reset(nullptr);

#ifdef WITH_RENDERDOC
		RenderDocCapture::removeHooks();
#endif

		rootNode_.reset(nullptr);
		RenderResources::dispose();
		frameTimer_.reset(nullptr);
		inputManager_.reset(nullptr);
		gfxDevice_.reset(nullptr);

		if (!theServiceLocator().indexer().empty()) {
			LOGW_X("The object indexer is not empty, %u object(s) left", theServiceLocator().indexer().size());
			//theServiceLocator().indexer().logReport();
		}

		LOGI("Application shut down");

		theServiceLocator().unregisterAll();
	}

	void Application::setFocus(bool hasFocus)
	{
#if defined(WITH_TRACY) && !defined(DEATH_TARGET_ANDROID)
		hasFocus = true;
#endif

		hasFocus_ = hasFocus;
	}

	void Application::suspend()
	{
		frameTimer_->suspend();
		if (appEventHandler_) {
			appEventHandler_->onSuspend();
		}
		LOGI("IAppEventHandler::onSuspend() invoked");
	}

	void Application::resume()
	{
		if (appEventHandler_) {
			appEventHandler_->onResume();
		}
		const TimeStamp suspensionDuration = frameTimer_->resume();
		LOGV_X("Suspended for %.3f seconds", suspensionDuration.seconds());
		profileStartTime_ += suspensionDuration;
		LOGI("IAppEventHandler::onResume() invoked");
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	bool Application::shouldSuspend()
	{
		return ((!hasFocus_ && autoSuspension_) || isSuspended_);
	}
}
