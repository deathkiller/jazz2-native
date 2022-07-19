#if defined(_WIN32) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/x64/glew32.lib")
#		pragma comment(lib, "../Libs/x64/glfw3dll.lib")
#		ifdef WITH_AUDIO
#			pragma comment(lib, "../Libs/x64/OpenAL32.lib")
#		endif
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/x86/glew32.lib")
#		pragma comment(lib, "../Libs/x86/glfw3dll.lib")
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

#ifdef WITH_AUDIO
#include "Audio/ALAudioDevice.h"
#endif

#ifdef WITH_THREADS
#include "Threading/ThreadPool.h"
#endif

#ifdef WITH_LUA
#include "LuaStatistics.h"
#endif

#ifdef WITH_IMGUI
#include "ImGuiDrawing.h"
#include "ImGuiDebugOverlay.h"
#endif

#ifdef WITH_NUKLEAR
#include "NuklearDrawing.h"
#endif

#ifdef WITH_RENDERDOC
#include "RenderDocCapture.h"
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

	/*Application::GuiSettings::GuiSettings()
		: imguiLayer(0xffff - 1024),
		nuklearLayer(0xffff - 512),
		imguiViewport(nullptr), nuklearViewport(nullptr)
	{
	}*/

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

	float Application::interval() const
	{
		return frameTimer_->lastFrameInterval();
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
				appEventHandler_->onRootViewportResized(width, height);
			}
		}
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	void Application::initCommon()
	{
		profileStartTime_ = TimeStamp::now();

		//#ifdef WITH_GIT_VERSION
		//	appInfoString.format("nCine %s (%s) compiled on %s at %s", VersionStrings::Version, VersionStrings::GitBranch,
		//	                     VersionStrings::CompilationDate, VersionStrings::CompilationTime);
		//#else
			//sprintf_s(appInfoString, "nCine compiled on %s at %s", VersionStrings::CompilationDate, VersionStrings::CompilationTime);
		//#endif
		//	LOGI_X("%s", appInfoString.data());
		//#ifdef WITH_TRACY
		//	TracyAppInfo(appInfoString.data(), appInfoString.length());
		//#endif

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

		LOGI_X("Data path: \"%s\"", fs::dataPath().data());
		LOGI_X("Save path: \"%s\"", fs::savePath().data());

#ifdef WITH_RENDERDOC
		RenderDocCapture::init();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, appCfg_.profileTextUpdateTime());

#ifdef WITH_IMGUI
		imguiDrawing_ = std::make_unique<ImGuiDrawing>(appCfg_.withScenegraph);
#endif
#ifdef WITH_NUKLEAR
		nuklearDrawing_ = std::make_unique<NuklearDrawing>(appCfg_.withScenegraph);
#endif

		if (appCfg_.withScenegraph) {
			gfxDevice_->setupGL();
			RenderResources::create();
			rootNode_ = std::make_unique<SceneNode>();
			screenViewport_ = std::make_unique<ScreenViewport>();
			screenViewport_->setRootNode(rootNode_.get());
		} else
			RenderResources::createMinimal(); // some resources are still required for rendering

#ifdef WITH_IMGUI
	// Debug overlay is available even when scenegraph is not
		if (appCfg_.withDebugOverlay)
			debugOverlay_ = std::make_unique<ImGuiDebugOverlay>(appCfg_.profileTextUpdateTime());
#endif

		// Initialization of the static random generator seeds
		Random().Initialize(static_cast<uint64_t>(TimeStamp::now().ticks()), static_cast<uint64_t>(profileStartTime_.ticks()));

		LOGI("Application initialized");

		timings_[Timings::INIT_COMMON] = profileStartTime_.secondsSince();

		{
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onInit();
			timings_[Timings::APP_INIT] = profileStartTime_.secondsSince();
			LOGI("IAppEventHandler::onInit() invoked");
		}

		// Give user code a chance to add custom GUI fonts
#ifdef WITH_IMGUI
		imguiDrawing_->buildFonts();
#endif
#ifdef WITH_NUKLEAR
		nuklearDrawing_->bakeFonts();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
	}

	void Application::step()
	{

		frameTimer_->addFrame();

#ifdef WITH_IMGUI
		{
			ZoneScopedN("ImGui newFrame");
			profileStartTime_ = TimeStamp::now();
			imguiDrawing_->newFrame();
			timings_[Timings::IMGUI] = profileStartTime_.secondsSince();
		}
#endif

#ifdef WITH_NUKLEAR
		{
			ZoneScopedN("Nuklear newFrame");
			profileStartTime_ = TimeStamp::now();
			nuklearDrawing_->newFrame();
			timings_[Timings::NUKLEAR] = profileStartTime_.secondsSince();
		}
#endif

#ifdef WITH_LUA
		LuaStatistics::update();
#endif

		{
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onFrameStart();
			timings_[Timings::FRAME_START] = profileStartTime_.secondsSince();
		}

		//if (debugOverlay_)
		//	debugOverlay_->update();

		if (appCfg_.withScenegraph) {
			{
				profileStartTime_ = TimeStamp::now();
				screenViewport_->update();
				timings_[Timings::UPDATE] = profileStartTime_.secondsSince();
			}

			{
				profileStartTime_ = TimeStamp::now();
				appEventHandler_->onPostUpdate();
				timings_[Timings::POST_UPDATE] = profileStartTime_.secondsSince();
			}

			{
				profileStartTime_ = TimeStamp::now();
				screenViewport_->visit();
				timings_[Timings::VISIT] = profileStartTime_.secondsSince();
			}

#ifdef WITH_IMGUI
			{
				ZoneScopedN("ImGui endFrame");
				profileStartTime_ = TimeStamp::now();
				RenderQueue* imguiRenderQueue = (guiSettings_.imguiViewport) ?
					guiSettings_.imguiViewport->renderQueue_.get() :
					screenViewport_->renderQueue_.get();
				imguiDrawing_->endFrame(*imguiRenderQueue);
				timings_[Timings::IMGUI] += profileStartTime_.secondsSince();
			}
#endif

#ifdef WITH_NUKLEAR
			{
				ZoneScopedN("Nuklear endFrame");
				profileStartTime_ = TimeStamp::now();
				RenderQueue* nuklearRenderQueue = (guiSettings_.nuklearViewport) ?
					guiSettings_.nuklearViewport->renderQueue_.get() :
					screenViewport_->renderQueue_.get();
				nuklearDrawing_->endFrame(*nuklearRenderQueue);
				timings_[Timings::NUKLEAR] += profileStartTime_.secondsSince();
			}
#endif

			{
				profileStartTime_ = TimeStamp::now();
				screenViewport_->sortAndCommitQueue();
				screenViewport_->draw();
				timings_[Timings::DRAW] = profileStartTime_.secondsSince();
			}
		} else {
#ifdef WITH_IMGUI
			{
				ZoneScopedN("ImGui endFrame");
				profileStartTime_ = TimeStamp::now();
				imguiDrawing_->endFrame();
				timings_[Timings::IMGUI] += profileStartTime_.secondsSince();
			}
#endif

#ifdef WITH_NUKLEAR
			{
				ZoneScopedN("Nuklear endFrame");
				profileStartTime_ = TimeStamp::now();
				nuklearDrawing_->endFrame();
				timings_[Timings::NUKLEAR] += profileStartTime_.secondsSince();
			}
#endif
		}

		{
			theServiceLocator().audioDevice().updatePlayers();
		}

		{
			profileStartTime_ = TimeStamp::now();
			appEventHandler_->onFrameEnd();
			timings_[Timings::FRAME_END] = profileStartTime_.secondsSince();
		}

		//if (debugOverlay_)
		//	debugOverlay_->updateFrameTimings();

		gfxDevice_->update();

		if (appCfg_.frameLimit > 0) {
			const float frameTimeDuration = 1.0f / static_cast<float>(appCfg_.frameLimit);
			while (frameTimer_->frameInterval() < frameTimeDuration) {
				Timer::sleep(0.0f);
			}
		}
	}

	void Application::shutdownCommon()
	{
		appEventHandler_->onShutdown();
		LOGI("IAppEventHandler::onShutdown() invoked");
		appEventHandler_.reset(nullptr);

#ifdef WITH_NUKLEAR
		nuklearDrawing_.reset(nullptr);
#endif
#ifdef WITH_IMGUI
		imguiDrawing_.reset(nullptr);
#endif

#ifdef WITH_RENDERDOC
		RenderDocCapture::removeHooks();
#endif

		//debugOverlay_.reset(nullptr);
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
#if defined(WITH_TRACY) && !defined(__ANDROID__)
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
