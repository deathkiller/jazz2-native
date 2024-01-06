#if defined(WITH_IMGUI)

#include "ImGuiDebugOverlay.h"
#include "../Application.h"
#include "../ServiceLocator.h"
#include "../Graphics/IGfxCapabilities.h"
#include "../Input/IInputManager.h"
#include "../Input/InputEvents.h"
#include "../Primitives/Vector2.h"

#include "Viewport.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "MeshSprite.h"
#include "ParticleSystem.h"

#include <imgui.h>

#if defined(WITH_AUDIO)
#	include "../Audio/IAudioPlayer.h"
#endif

#include "RenderStatistics.h"
#if defined(WITH_LUA)
#	include "LuaStatistics.h"
#endif

#if defined(WITH_RENDERDOC)
#	include "RenderDocCapture.h"
#endif

#if defined(WITH_ALLOCATORS)
#	include "allocators_config.h"
#endif

namespace nCine
{
	namespace
	{
		/*int inputTextCallback(ImGuiInputTextCallbackData* data)
		{
			String* string = static_cast<String*>(data->UserData);
			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
				// Resize string callback
				ASSERT(data->Buf == string->data());
				string->setLength(data->BufTextLen);
				data->Buf = string->data();
			}
			return 0;
		}*/

		const char* nodeTypeToString(Object::ObjectType type)
		{
			switch (type) {
				case Object::ObjectType::SceneNode: return "SceneNode";
				case Object::ObjectType::Sprite: return "Sprite";
				case Object::ObjectType::MeshSprite: return "MeshSprite";
				case Object::ObjectType::AnimatedSprite: return "AnimatedSprite";
				case Object::ObjectType::Particle: return "Particle";
				case Object::ObjectType::ParticleSystem: return "ParticleSystem";
				default: return "N/A";
			}
		}

#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		const char* openglApiName = "OpenGL ES";
#else
		const char* openglApiName = "OpenGL";
#endif
	}

	ImGuiDebugOverlay::ImGuiDebugOverlay(float profileTextUpdateTime)
		: IDebugOverlay(profileTextUpdateTime), lockOverlayPositions_(false), showTopLeftOverlay_(true), showTopRightOverlay_(true),
			showBottomLeftOverlay_(true), showBottomRightOverlay_(true), numValues_(80), maxFrameTime_(0.0f), maxUpdateVisitDraw_(0.0f),
			index_(0), plotAdditionalFrameValues_(false), plotOverlayValues_(true)
#if defined(WITH_RENDERDOC)
			, renderDocPathTemplate_(MaxRenderDocPathLength), renderDocFileComments_(MaxRenderDocCommentsLength),
				renderDocCapturePath_(MaxRenderDocPathLength), renderDocLastNumCaptures_(0)
#endif
	{
		initPlotValues();
	}

	void ImGuiDebugOverlay::update()
	{
		// TODO: MenuBar
		/*if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Create")) {
				}
				if (ImGui::MenuItem("Open", "Ctrl+O")) {
				}
				if (ImGui::MenuItem("Save", "Ctrl+S")) {
				}
				if (ImGui::MenuItem("Save as..")) {
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}*/

		guiWindow();

		//ImGui::ShowMetricsWindow();

		if (settings_.showInfoText) {
			const AppConfiguration& appCfg = theApplication().appConfiguration();
			if (appCfg.withScenegraph) {
#if defined(NCINE_PROFILING)
				guiTopLeft();
#endif
				guiBottomRight();
			}
			guiTopRight();
			guiBottomLeft();
		}

		if (settings_.showProfilerGraphs) {
			guiPlots();
		}
	}

	void ImGuiDebugOverlay::updateFrameTimings()
	{
		if (lastUpdateTime_.secondsSince() > updateTime_) {
			const AppConfiguration& appCfg = theApplication().appConfiguration();

			plotValues_[ValuesType::FrameTime][index_] = theApplication().frameTimer().lastFrameDuration() * 1000.0f;

#if defined(NCINE_PROFILING)
			const float* timings = theApplication().timings();
			plotValues_[ValuesType::FrameStart][index_] = timings[(int)Application::Timings::FrameStart] * 1000.0f;
			if (appCfg.withScenegraph) {
				plotValues_[ValuesType::PostUpdate][index_] = timings[(int)Application::Timings::PostUpdate] * 1000.0f;
			}
			plotValues_[ValuesType::ImGui][index_] = timings[(int)Application::Timings::ImGui] * 1000.0f;
			plotValues_[ValuesType::FrameEnd][index_] = timings[(int)Application::Timings::FrameEnd] * 1000.0f;

			if (appCfg.withScenegraph) {
				plotValues_[ValuesType::UpdateVisitDraw][index_] = timings[(int)Application::Timings::Update] * 1000.0f +
					timings[(int)Application::Timings::Visit] * 1000.0f + timings[(int)Application::Timings::Draw] * 1000.0f;
				plotValues_[ValuesType::Update][index_] = timings[(int)Application::Timings::Update] * 1000.0f;
				plotValues_[ValuesType::Visit][index_] = timings[(int)Application::Timings::Visit] * 1000.0f;
				plotValues_[ValuesType::Draw][index_] = timings[(int)Application::Timings::Draw] * 1000.0f;
			}
#endif

			float maxFrameTime = 0.0f, avgFrameTime = 0.0f;
#if defined(NCINE_PROFILING)
			float maxUpdateVisitDraw = 0.0f, avgUpdateVisitDraw = 0.0f;
#endif
			for (unsigned int i = 0; i < numValues_; i++) {
				if (maxFrameTime < plotValues_[ValuesType::FrameTime][i]) {
					maxFrameTime = plotValues_[ValuesType::FrameTime][i];
				}
				avgFrameTime += plotValues_[ValuesType::FrameTime][i];

#if defined(NCINE_PROFILING)
				if (appCfg.withScenegraph) {
					if (maxUpdateVisitDraw < plotValues_[ValuesType::UpdateVisitDraw][i]) {
						maxUpdateVisitDraw = plotValues_[ValuesType::UpdateVisitDraw][i];
					}
					avgUpdateVisitDraw += plotValues_[ValuesType::UpdateVisitDraw][i];
				}
#endif
			}

			avgFrameTime = avgFrameTime * 2.0f / numValues_;
			if (maxFrameTime < avgFrameTime) {
				maxFrameTime = avgFrameTime;
			}
			if (maxFrameTime_ < maxFrameTime) {
				maxFrameTime_ = maxFrameTime;
			} else {
				maxFrameTime_ = lerp(maxFrameTime_, maxFrameTime, 0.2f);
			}

#if defined(NCINE_PROFILING)
			avgUpdateVisitDraw = avgUpdateVisitDraw * 2.0f / numValues_;
			if (maxUpdateVisitDraw < avgUpdateVisitDraw) {
				maxUpdateVisitDraw = avgUpdateVisitDraw;
			}
			if (maxUpdateVisitDraw_ < maxUpdateVisitDraw) {
				maxUpdateVisitDraw_ = maxUpdateVisitDraw;
			} else {
				maxUpdateVisitDraw_ = lerp(maxUpdateVisitDraw_, maxUpdateVisitDraw, 0.2f);
			}

			if (appCfg.withScenegraph) {
				updateOverlayTimings();
			}
#endif

			index_ = (index_ + 1) % numValues_;
			lastUpdateTime_ = TimeStamp::now();
		}
	}

	namespace
	{
#if defined(WITH_AUDIO)
		const char* audioPlayerStateToString(IAudioPlayer::PlayerState state)
		{
			switch (state) {
				case IAudioPlayer::PlayerState::Initial: return "Initial";
				case IAudioPlayer::PlayerState::Playing: return "Playing";
				case IAudioPlayer::PlayerState::Paused: return "Paused";
				case IAudioPlayer::PlayerState::Stopped: return "Stopped";
				default: return "Unknown";
			}
		}
#endif

		const char* mouseCursorModeToString(IInputManager::Cursor mode)
		{
			switch (mode) {
				case IInputManager::Cursor::Arrow: return "Normal";
				case IInputManager::Cursor::Hidden: return "Hidden";
				case IInputManager::Cursor::HiddenLocked: return "HiddenLocked";
				default: return "Unknown";
			}
		}

		const char* mappedButtonNameToString(ButtonName name)
		{
			switch (name) {
				case ButtonName::UNKNOWN: return "Unknown";
				case ButtonName::A: return "A";
				case ButtonName::B: return "B";
				case ButtonName::X: return "X";
				case ButtonName::Y: return "Y";
				case ButtonName::BACK: return "Back";
				case ButtonName::GUIDE: return "Guide";
				case ButtonName::START: return "Start";
				case ButtonName::LSTICK: return "LStick";
				case ButtonName::RSTICK: return "RStick";
				case ButtonName::LBUMPER: return "LBumper";
				case ButtonName::RBUMPER: return "RBumper";
				case ButtonName::DPAD_UP: return "DPad_Up";
				case ButtonName::DPAD_DOWN: return "DPad_Down";
				case ButtonName::DPAD_LEFT: return "DPad_Left";
				case ButtonName::DPAD_RIGHT: return "DPad_Right";
				case ButtonName::MISC1: return "Misc1";
				case ButtonName::PADDLE1: return "Paddle1";
				case ButtonName::PADDLE2: return "Paddle2";
				case ButtonName::PADDLE3: return "Paddle3";
				case ButtonName::PADDLE4: return "Paddle4";
				default: return "Unknown";
			}
		}

		const char* mappedAxisNameToString(AxisName name)
		{
			switch (name) {
				case AxisName::UNKNOWN: return "Unknown";
				case AxisName::LX: return "LX";
				case AxisName::LY: return "LY";
				case AxisName::RX: return "RX";
				case AxisName::RY: return "RY";
				case AxisName::LTRIGGER: return "LTrigger";
				case AxisName::RTRIGGER: return "RTrigger";
				default: return "Unknown";
			}
		}
	}

	void ImGuiDebugOverlay::guiWindow()
	{
		if (!settings_.showInterface) {
			return;
		}

		const ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y - 0.5f);
		const ImVec2 windowPosPivot = ImVec2(0.5f, 0.5f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PushStyleColor(ImGuiCol_WindowShadow, ImVec4(0, 0, 0, 1));
#endif
		ImGui::Begin("Debug Overlay", &settings_.showInterface);
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PopStyleColor();
#endif

		const AppConfiguration& appCfg = theApplication().appConfiguration();

		bool disableAutoSuspension = !theApplication().autoSuspension();
		ImGui::Checkbox("Disable auto-suspension", &disableAutoSuspension);
		theApplication().setAutoSuspension(!disableAutoSuspension);
		/*ImGui::SameLine();
		if (ImGui::Button("Quit")) {
			theApplication().quit();
		}*/

		guiConfigureGui();
		guiInitTimes();
		guiLog();
		guiGraphicsCapabilities();
		guiApplicationConfiguration();
		if (appCfg.withScenegraph) {
			guiRenderingSettings();
		}
		//guiWindowSettings();
		guiAudioPlayers();
		//guiInputState();
		guiRenderDoc();
		guiAllocators();
		if (appCfg.withScenegraph) {
			guiNodeInspector();
		}

		ImGui::End();
	}

	void ImGuiDebugOverlay::guiConfigureGui()
	{
		static int numValues = 0;

		if (ImGui::CollapsingHeader("Configure GUI")) {
			const AppConfiguration& appCfg = theApplication().appConfiguration();

			ImGui::Checkbox("Show interface", &settings_.showInterface);
			if (ImGui::TreeNodeEx("Overlays", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Show overlays", &settings_.showInfoText);
				ImGui::Checkbox("Lock overlay positions", &lockOverlayPositions_);
				if (appCfg.withScenegraph) {
					ImGui::Checkbox("Show Top-Left", &showTopLeftOverlay_);
					ImGui::SameLine();
				}
				ImGui::Checkbox("Show Top-Right", &showTopRightOverlay_);
				ImGui::Checkbox("Show Bottom-Left", &showBottomLeftOverlay_);
#if defined(WITH_LUA)
				if (appCfg.withScenegraph) {
					ImGui::SameLine();
					ImGui::Checkbox("Show Bottom-Right", &showBottomRightOverlay_);
				}
#endif
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Profiling Graphs", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Show profiling graphs", &settings_.showProfilerGraphs);
				ImGui::Checkbox("Plot additional frame values", &plotAdditionalFrameValues_);
				if (appCfg.withScenegraph)
					ImGui::Checkbox("Plot overlay values", &plotOverlayValues_);
				ImGui::SliderFloat("Graphs update time", &updateTime_, 0.0f, 1.0f, "%.3f s");
				numValues = (numValues == 0) ? static_cast<int>(numValues_) : numValues;
				ImGui::SliderInt("Number of values", &numValues, 16, 512);
				ImGui::SameLine();
				if (ImGui::Button("Apply") && numValues_ != static_cast<unsigned int>(numValues)) {
					numValues_ = static_cast<unsigned int>(numValues);
					initPlotValues();
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("GUI Style")) {
				static int styleIndex = 0;
				ImGui::Combo("Theme", &styleIndex, "Dark\0Light\0Classic\0");

				if (styleIndex < 0)
					styleIndex = 0;
				else if (styleIndex > 2)
					styleIndex = 2;

				switch (styleIndex) {
					case 0: ImGui::StyleColorsDark(); break;
					case 1: ImGui::StyleColorsLight(); break;
					case 2: ImGui::StyleColorsClassic(); break;
				}

				const float MinFrameRounding = 0.0f;
				const float MaxFrameRounding = 12.0f;
				ImGuiStyle& style = ImGui::GetStyle();
				ImGui::SliderFloat("Window Rounding", &style.WindowRounding, MinFrameRounding, MaxFrameRounding, "%.0f");
				ImGui::SliderFloat("Child Rounding", &style.ChildRounding, MinFrameRounding, MaxFrameRounding, "%.0f");
				ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, MinFrameRounding, MaxFrameRounding, "%.0f");

				if (style.FrameRounding < MinFrameRounding)
					style.FrameRounding = MinFrameRounding;
				else if (style.FrameRounding > MaxFrameRounding)
					style.FrameRounding = MaxFrameRounding;
				// Make `GrabRounding` always the same value as `FrameRounding`
				style.GrabRounding = style.FrameRounding;

				static bool windowBorder = true;
				windowBorder = (style.WindowBorderSize > 0.0f);
				ImGui::Checkbox("Window Border", &windowBorder);
				style.WindowBorderSize = windowBorder ? 1.0f : 0.0f;
				ImGui::SameLine();
				static bool frameBorder = true;
				frameBorder = (style.FrameBorderSize > 0.0f);
				ImGui::Checkbox("Frame Border", &frameBorder);
				style.FrameBorderSize = frameBorder ? 1.0f : 0.0f;
				ImGui::SameLine();
				static bool popupBorder = true;
				popupBorder = (style.PopupBorderSize > 0.0f);
				ImGui::Checkbox("Popup Border", &popupBorder);
				style.PopupBorderSize = popupBorder ? 1.0f : 0.0f;

				const float MinScaling = 0.5f;
				const float MaxScaling = 2.0f;
				static float scaling = ImGui::GetIO().FontGlobalScale;
				ImGui::SliderFloat("Scaling", &scaling, MinScaling, MaxScaling, "%.1f");
				ImGui::SameLine();
				if (ImGui::Button("Reset"))
					scaling = 1.0f;

				if (scaling < MinScaling)
					scaling = MinScaling;
				if (scaling > MaxScaling)
					scaling = MaxScaling;
				ImGui::GetIO().FontGlobalScale = scaling;

				ImGui::TreePop();
			}
		} else
			numValues = 0;
	}

	void ImGuiDebugOverlay::guiInitTimes()
	{
#if defined(NCINE_PROFILING)
		if (ImGui::CollapsingHeader("Init Times")) {
			const float* timings = theApplication().timings();

			float initTimes[3];
			initTimes[0] = timings[(int)Application::Timings::PreInit] * 1000.0f;
			initTimes[1] = initTimes[0] + timings[(int)Application::Timings::InitCommon] * 1000.0f;
			initTimes[2] = initTimes[1] + timings[(int)Application::Timings::AppInit] * 1000.0f;
			ImGui::PlotHistogram("Init Times", initTimes, 3, 0, nullptr, 0.0f, initTimes[2], ImVec2(0.0f, 100.0f));

			ImGui::Text("Pre-Init Time: %.2f ms", timings[(int)Application::Timings::PreInit] * 1000.0f);
			ImGui::Text("Init Time: %.2f ms", timings[(int)Application::Timings::InitCommon] * 1000.0f);
			ImGui::Text("Application Init Time: %.2f ms", timings[(int)Application::Timings::AppInit] * 1000.0f);
		}
#endif
	}

	void ImGuiDebugOverlay::guiLog()
	{
		/*if (ImGui::CollapsingHeader("Log")) {
			ILogger& logger = theServiceLocator().logger();

			ImGui::BeginChild("scrolling", ImVec2(0.0f, -1.2f * ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextUnformatted(logger.logString());
			ImGui::EndChild();
			ImGui::Separator();
			if (ImGui::Button("Clear"))
				logger.clearLogString();
			ImGui::SameLine();
			ImGui::Text("Length: %u / %u", logger.logStringLength(), logger.logStringCapacity());
		}*/
	}

	void ImGuiDebugOverlay::guiGraphicsCapabilities()
	{
		if (ImGui::CollapsingHeader("Graphics Capabilities")) {
			const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();

			const IGfxCapabilities::GlInfoStrings& glInfoStrings = gfxCaps.glInfoStrings();
			ImGui::Text("%s Vendor: %s", openglApiName, glInfoStrings.vendor);
			ImGui::Text("%s Renderer: %s", openglApiName, glInfoStrings.renderer);
			ImGui::Text("%s Version: %s", openglApiName, glInfoStrings.glVersion);
			ImGui::Text("GLSL Version: %s", glInfoStrings.glslVersion);

			ImGui::Text("%s parsed version: %d.%d.%d", openglApiName,
						gfxCaps.glVersion(IGfxCapabilities::GLVersion::Major),
						gfxCaps.glVersion(IGfxCapabilities::GLVersion::Minor),
						gfxCaps.glVersion(IGfxCapabilities::GLVersion::Release));

			ImGui::Separator();
			ImGui::Text("GL_MAX_TEXTURE_SIZE: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_TEXTURE_SIZE));
			ImGui::Text("GL_MAX_TEXTURE_IMAGE_UNITS: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_TEXTURE_IMAGE_UNITS));
			ImGui::Text("GL_MAX_UNIFORM_BLOCK_SIZE: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE));
			ImGui::Text("GL_MAX_UNIFORM_BUFFER_BINDINGS: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BUFFER_BINDINGS));
			ImGui::Text("GL_MAX_VERTEX_UNIFORM_BLOCKS: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_VERTEX_UNIFORM_BLOCKS));
			ImGui::Text("GL_MAX_FRAGMENT_UNIFORM_BLOCKS: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_FRAGMENT_UNIFORM_BLOCKS));
			ImGui::Text("GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT));
#if !defined(DEATH_TARGET_EMSCRIPTEN) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
			ImGui::Text("GL_MAX_VERTEX_ATTRIB_STRIDE: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_VERTEX_ATTRIB_STRIDE));
#endif
			ImGui::Text("GL_MAX_COLOR_ATTACHMENTS: %d", gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_COLOR_ATTACHMENTS));

			ImGui::Separator();
			ImGui::Text("GL_KHR_debug: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::KHR_DEBUG));
			ImGui::Text("GL_ARB_texture_storage: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_TEXTURE_STORAGE));
			ImGui::Text("GL_ARB_get_program_binary: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY));
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			ImGui::Text("GL_OES_get_program_binary: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY));
#endif
			ImGui::Text("GL_EXT_texture_compression_s3tc: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::EXT_TEXTURE_COMPRESSION_S3TC));
			ImGui::Text("GL_AMD_compressed_ATC_texture: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::AMD_COMPRESSED_ATC_TEXTURE));
			ImGui::Text("GL_IMG_texture_compression_pvrtc: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::IMG_TEXTURE_COMPRESSION_PVRTC));
			ImGui::Text("GL_KHR_texture_compression_astc_ldr: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR));
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
			ImGui::Text("GL_OES_compressed_ETC1_RGB8_texture: %d", gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE));
#endif
		}
	}

	void ImGuiDebugOverlay::guiApplicationConfiguration()
	{
		if (ImGui::CollapsingHeader("Application Configuration")) {
			const AppConfiguration& appCfg = theApplication().appConfiguration();
#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
			ImGui::Text("OpenGL Core: %s", appCfg.glCoreProfile() ? "true" : "false");
			ImGui::Text("OpenGL Forward: %s", appCfg.glForwardCompatible() ? "true" : "false");
#endif
			ImGui::Text("%s Major: %d", openglApiName, appCfg.glMajorVersion());
			ImGui::Text("%s Minor: %d", openglApiName, appCfg.glMinorVersion());

			ImGui::Separator();
			ImGui::Text("Data path: \"%s\"", appCfg.dataPath().data());
			//ImGui::Text("Log file: \"%s\"", appCfg.logFile.data());
			//ImGui::Text("Console log level: %d", static_cast<int>(appCfg.consoleLogLevel));
			//ImGui::Text("File log level: %d", static_cast<int>(appCfg.fileLogLevel));
			ImGui::Text("Frametimer Log interval: %f", appCfg.frameTimerLogInterval);
			//ImGui::Text("Profile text update time: %f", appCfg.profileTextUpdateTime());
			ImGui::Text("Resolution: %d x %d", appCfg.resolution.X, appCfg.resolution.Y);
			//ImGui::Text("Refresh Rate: %f", appCfg.refreshRate);
			/*widgetName_.assign("Window Position: ");
			if (appCfg.windowPosition.x == AppConfiguration::WindowPositionIgnore)
				widgetName_.append("Ignore x ");
			else
				widgetName_.formatAppend("%d x ", appCfg.windowPosition.x);
			if (appCfg.windowPosition.y == AppConfiguration::WindowPositionIgnore)
				widgetName_.append("Ignore");
			else
				widgetName_.formatAppend("%d", appCfg.windowPosition.y);
			ImGui::TextUnformatted(widgetName_.data());*/
			//ImGui::Text("Full Screen: %s", appCfg.fullScreen ? "true" : "false");
			ImGui::Text("Resizable: %s", appCfg.resizable ? "true" : "false");
			ImGui::Text("Window Scaling: %s", appCfg.windowScaling ? "true" : "false");
			ImGui::Text("Frame Limit: %u", appCfg.frameLimit);

			ImGui::Separator();
			ImGui::Text("Window title: \"%s\"", appCfg.windowTitle.data());
			ImGui::Text("Window icon: \"%s\"", appCfg.windowIconFilename.data());

			ImGui::Separator();
			ImGui::Text("Buffer mapping: %s", appCfg.useBufferMapping ? "true" : "false");
			//ImGui::Text("Defer shader queries: %s", appCfg.deferShaderQueries ? "true" : "false");
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(WITH_ANGLE)
			ImGui::Text("Fixed batch size: %u", appCfg.fixedBatchSize);
#endif
			ImGui::Text("VBO size: %lu", appCfg.vboSize);
			ImGui::Text("IBO size: %lu", appCfg.iboSize);
			ImGui::Text("Vao pool size: %u", appCfg.vaoPoolSize);
			ImGui::Text("RenderCommand pool size: %u", appCfg.renderCommandPoolSize);

			ImGui::Separator();
			ImGui::Text("Debug Overlay: %s", appCfg.withDebugOverlay ? "true" : "false");
			ImGui::Text("Audio: %s", appCfg.withAudio ? "true" : "false");
			ImGui::Text("Threads: %s", appCfg.withThreads ? "true" : "false");
			ImGui::Text("Scenegraph: %s", appCfg.withScenegraph ? "true" : "false");
			ImGui::Text("VSync: %s", appCfg.withVSync ? "true" : "false");
			ImGui::Text("%s Debug Context: %s", openglApiName, appCfg.withGlDebugContext ? "true" : "false");
			//ImGui::Text("Console Colors: %s", appCfg.withConsoleColors ? "true" : "false");
		}
	}

	void ImGuiDebugOverlay::guiRenderingSettings()
	{
		if (ImGui::CollapsingHeader("Rendering Settings")) {
			Application::RenderingSettings& settings = theApplication().renderingSettings();
			int minBatchSize = settings.minBatchSize;
			int maxBatchSize = settings.maxBatchSize;

			ImGui::Checkbox("Batching", &settings.batchingEnabled);
			ImGui::SameLine();
			ImGui::Checkbox("Batching with indices", &settings.batchingWithIndices);
			ImGui::SameLine();
			ImGui::Checkbox("Culling", &settings.cullingEnabled);
			ImGui::DragIntRange2("Batch size", &minBatchSize, &maxBatchSize, 1.0f, 0, 512);

			settings.minBatchSize = minBatchSize;
			settings.maxBatchSize = maxBatchSize;
		}
	}

	void ImGuiDebugOverlay::guiWindowSettings()
	{
		if (ImGui::CollapsingHeader("Window Settings")) {
			/*IGfxDevice& gfxDevice = theApplication().gfxDevice();
			const unsigned int numMonitors = gfxDevice.numMonitors();
			for (unsigned int i = 0; i < numMonitors; i++) {
				const IGfxDevice::Monitor& monitor = gfxDevice.monitor(i);
				widgetName_.format("Monitor #%u: \"%s\"", i, monitor.name);
				if (i == gfxDevice.primaryMonitorIndex())
					widgetName_.append(" [Primary]");
				if (i == gfxDevice.windowMonitorIndex())
					widgetName_.formatAppend(" [%s]", gfxDevice.isFullScreen() ? "Full Screen" : "Window");

				if (ImGui::TreeNode(widgetName_.data())) {
					ImGui::Text("Position: <%d, %d>", monitor.position.x, monitor.position.y);
					ImGui::Text("DPI: <%d, %d>", monitor.dpi.x, monitor.dpi.y);
					ImGui::Text("Scale: <%.2f, %.2f>", monitor.scale.x, monitor.scale.y);

					const unsigned int numVideoModes = monitor.numVideoModes;
					widgetName_.format("%u Video Modes", numVideoModes);
					if (ImGui::TreeNode(widgetName_.data())) {
						for (unsigned int j = 0; j < numVideoModes; j++) {
							const IGfxDevice::VideoMode& videoMode = monitor.videoModes[j];
							widgetName_.format("#%u: %u x %u, %.2f Hz", j, videoMode.width, videoMode.height, videoMode.refreshRate);
							if (videoMode.redBits != 8 || videoMode.greenBits != 8 || videoMode.blueBits != 8)
								widgetName_.formatAppend(" (R%uG%uB%u)", videoMode.redBits, videoMode.greenBits, videoMode.blueBits);
							ImGui::TextUnformatted(widgetName_.data());
						}
						ImGui::TreePop();
					}
					ImGui::TreePop();
				}
			}

#if !defined(DEATH_TARGET_ANDROID) || (defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 30)
			static Vector2i resolution = theApplication().resolutionInt();
			static Vector2i winPosition = gfxDevice.windowPosition();
			static bool fullScreen = gfxDevice.isFullScreen();

			static int selectedVideoMode = -1;
			const IGfxDevice::VideoMode currentVideoMode = gfxDevice.currentVideoMode();
			if (fullScreen == false) {
				ImGui::InputInt2("Resolution", resolution.data());
				ImGui::InputInt2("Position", winPosition.data());
				selectedVideoMode = -1;
			} else {
				const int monitorIndex = gfxDevice.windowMonitorIndex();
				const IGfxDevice::Monitor& monitor = gfxDevice.monitor(monitorIndex);

				unsigned int currentVideoModeIndex = 0;
				const unsigned int numVideoModes = monitor.numVideoModes;
				comboVideoModes_.clear();
				for (unsigned int i = 0; i < numVideoModes; i++) {
					const IGfxDevice::VideoMode& mode = monitor.videoModes[i];
					comboVideoModes_.formatAppend("%u: %u x %u, %.2f Hz", i, mode.width, mode.height, mode.refreshRate);
					comboVideoModes_.setLength(comboVideoModes_.length() + 1);

					if (mode == currentVideoMode)
						currentVideoModeIndex = i;
				}
				comboVideoModes_.setLength(comboVideoModes_.length() + 1);
				// Append a second '\0' to signal the end of the combo item list
				comboVideoModes_[comboVideoModes_.length() - 1] = '\0';

				if (selectedVideoMode < 0)
					selectedVideoMode = currentVideoModeIndex;

				ImGui::Combo("Video Mode", &selectedVideoMode, comboVideoModes_.data());
				resolution.x = monitor.videoModes[selectedVideoMode].width;
				resolution.y = monitor.videoModes[selectedVideoMode].height;
			}

#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_SWITCH)
			ImGui::TextUnformatted("Full Screen: true");
#else
			ImGui::Checkbox("Full Screen", &fullScreen);
#endif
			ImGui::SameLine();
			if (ImGui::Button("Apply")) {
				if (fullScreen == false) {
					// Disable full screen, then change window size and position
					gfxDevice.setFullScreen(fullScreen);
					gfxDevice.setWindowSize(resolution);
					gfxDevice.setWindowPosition(winPosition);
				} else {
					// Set the video mode, then enable full screen
					winPosition = gfxDevice.windowPosition();
					gfxDevice.setVideoMode(selectedVideoMode);
					gfxDevice.setFullScreen(fullScreen);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset")) {
#if !defined(DEATH_TARGET_ANDROID)
				resolution = theApplication().appConfiguration().resolution;
				fullScreen = theApplication().appConfiguration().fullScreen;
#endif
				gfxDevice.setFullScreen(fullScreen);
				gfxDevice.setWindowSize(resolution[0], resolution[1]);
				gfxDevice.setWindowPosition(winPosition[0], winPosition[1]);
			}
			ImGui::SameLine();
			if (ImGui::Button("Current")) {
				resolution = theApplication().resolutionInt();
				winPosition = gfxDevice.windowPosition();
				fullScreen = gfxDevice.isFullScreen();
				selectedVideoMode = -1;
			}

			ImGui::Text("Resizable: %s", gfxDevice.isResizable() ? "true" : "false");
#endif
*/
		}
	}

	void ImGuiDebugOverlay::guiAudioPlayers()
	{
#if defined(WITH_AUDIO)
		if (ImGui::CollapsingHeader("Audio Players")) {
			ImGui::Text("Device Name: %s", theServiceLocator().audioDevice().name());
			ImGui::Text("Listener Gain: %f", theServiceLocator().audioDevice().gain());

			unsigned int numPlayers = theServiceLocator().audioDevice().numPlayers();
			ImGui::Text("Active Players: %d", numPlayers);

			if (numPlayers > 0) {
				if (ImGui::Button("Stop"))
					theServiceLocator().audioDevice().stopPlayers();
				ImGui::SameLine();
				if (ImGui::Button("Pause"))
					theServiceLocator().audioDevice().pausePlayers();
			}

			// Stopping or pausing players change the number of active ones
			numPlayers = theServiceLocator().audioDevice().numPlayers();
			for (unsigned int i = 0; i < numPlayers; i++) {
				const IAudioPlayer* player = theServiceLocator().audioDevice().player(i);
				char widgetName[32];
				formatString(widgetName, sizeof(widgetName), "Player %d", i);
				if (ImGui::TreeNode(widgetName)) {
					ImGui::Text("Source Id: %u", player->sourceId());
					ImGui::Text("Buffer Id: %u", player->bufferId());
					ImGui::Text("Channels: %d", player->numChannels());
					ImGui::Text("Frequency: %d Hz", player->frequency());
					auto bufferSize = player->bufferSize();
					if (bufferSize == UINT32_MAX) {
						ImGui::Text("Buffer Size: Streaming");
					} else {
						ImGui::Text("Buffer Size: %lu bytes", bufferSize);
					}
					ImGui::NewLine();

					ImGui::Text("State: %s", audioPlayerStateToString(player->state()));
					ImGui::Text("Looping: %s", player->isLooping() ? "true" : "false");
					ImGui::Text("Gain: %f", player->gain());
					ImGui::Text("Pitch: %f", player->pitch());
					const Vector3f& pos = player->position();
					ImGui::Text("Position: <%f, %f, %f>", pos.X, pos.Y, pos.Z);

					ImGui::TreePop();
				}
			}
		}
#endif
	}

	void ImGuiDebugOverlay::guiInputState()
	{
		if (ImGui::CollapsingHeader("Input State")) {
			const IInputManager& input = theApplication().inputManager();

			/*if (ImGui::TreeNode("Keyboard")) {
				nctl::String pressedKeys;
				const KeyboardState& keyState = input.keyboardState();
				for (unsigned int i = 0; i < static_cast<int>(KeySym::COUNT); i++) {
					if (keyState.isKeyDown(static_cast<KeySym>(i)))
						pressedKeys.formatAppend("%d ", i);
				}
				ImGui::Text("Keys pressed: %s", pressedKeys.data());
				ImGui::TreePop();
			}*/

			if (ImGui::TreeNode("Mouse")) {
				ImGui::Text("Cursor Mode: %s", mouseCursorModeToString(input.cursor()));

				const MouseState& mouseState = input.mouseState();
				ImGui::Text("Position: %d, %d", mouseState.x, mouseState.y);

				/*nctl::String pressedMouseButtons(32);
				if (mouseState.isLeftButtonDown())
					pressedMouseButtons.append("left ");
				if (mouseState.isRightButtonDown())
					pressedMouseButtons.append("right ");
				if (mouseState.isMiddleButtonDown())
					pressedMouseButtons.append("middle ");
				if (mouseState.isFourthButtonDown())
					pressedMouseButtons.append("fourth ");
				if (mouseState.isFifthButtonDown())
					pressedMouseButtons.append("fifth");
				ImGui::Text("Pressed buttons: %s", pressedMouseButtons.data());*/
				ImGui::TreePop();
			}

			unsigned int numConnectedJoysticks = 0;
			for (int joyId = 0; joyId < IInputManager::MaxNumJoysticks; joyId++) {
				if (input.isJoyPresent(joyId))
					numConnectedJoysticks++;
			}
			if (numConnectedJoysticks > 0) {
				char widgetName[32];
				formatString(widgetName, sizeof(widgetName), "%d Joystick(s)", numConnectedJoysticks);
				if (ImGui::TreeNode(widgetName)) {
					ImGui::Text("Joystick mappings: %u", input.numJoyMappings());

					for (int joyId = 0; joyId < IInputManager::MaxNumJoysticks; joyId++) {
						if (input.isJoyPresent(joyId) == false)
							continue;

						formatString(widgetName, sizeof(widgetName), "Joystick %d", joyId);
						if (ImGui::TreeNode(widgetName)) {
							ImGui::Text("Name: %s", input.joyName(joyId));
							ImGui::Text("GUID: %s", input.joyGuid(joyId));
							ImGui::Text("Num Buttons: %d", input.joyNumButtons(joyId));
							ImGui::Text("Num Hats: %d", input.joyNumHats(joyId));
							ImGui::Text("Num Axes: %d", input.joyNumAxes(joyId));
							ImGui::Separator();

							const JoystickState& joyState = input.joystickState(joyId);
							/*nctl::String pressedButtons;
							for (int buttonId = 0; buttonId < input.joyNumButtons(joyId); buttonId++) {
								if (joyState.isButtonPressed(buttonId))
									pressedButtons.formatAppend("%d ", buttonId);
							}
							ImGui::Text("Pressed buttons: %s", pressedButtons.data());*/

							for (int hatId = 0; hatId < input.joyNumHats(joyId); hatId++) {
								unsigned char hatState = joyState.hatState(hatId);
								ImGui::Text("Hat %d: %u", hatId, hatState);
							}

							for (int axisId = 0; axisId < input.joyNumAxes(joyId); axisId++) {
								const float axisValue = joyState.axisValue(axisId);
								ImGui::Text("Axis %d:", axisId);
								ImGui::SameLine();
								ImGui::ProgressBar((axisValue + 1.0f) / 2.0f);
								ImGui::SameLine();
								ImGui::Text("%.2f", axisValue);
							}

							if (input.isJoyMapped(joyId)) {
								ImGui::Separator();
								const JoyMappedState& joyMappedState = input.joyMappedState(joyId);
								/*nctl::String pressedMappedButtons(64);
								for (unsigned int buttonId = 0; buttonId < JoyMappedState::NumButtons; buttonId++) {
									const ButtonName buttonName = static_cast<ButtonName>(buttonId);
									if (joyMappedState.isButtonPressed(buttonName))
										pressedMappedButtons.formatAppend("%s ", mappedButtonNameToString(buttonName));
								}
								ImGui::Text("Pressed buttons: %s", pressedMappedButtons.data());*/

								for (unsigned int axisId = 0; axisId < JoyMappedState::NumAxes; axisId++) {
									const AxisName axisName = static_cast<AxisName>(axisId);
									const float axisValue = joyMappedState.axisValue(axisName);
									ImGui::Text("Axis %s:", mappedAxisNameToString(axisName));
									ImGui::SameLine();
									ImGui::ProgressBar((axisValue + 1.0f) / 2.0f);
									ImGui::SameLine();
									ImGui::Text("%.2f", axisValue);
								}
							}
							ImGui::TreePop();
						}
					}
					ImGui::TreePop();
				}
			} else
				ImGui::TextUnformatted("No joysticks connected");
		}
	}

	void ImGuiDebugOverlay::guiRenderDoc()
	{
#if defined(WITH_RENDERDOC)
		if (!RenderDocCapture::isAvailable())
			return;

		if (RenderDocCapture::numCaptures() > renderDocLastNumCaptures_) {
			unsigned int pathLength = 0;
			uint64_t timestamp = 0;
			RenderDocCapture::captureInfo(RenderDocCapture::numCaptures() - 1, renderDocCapturePath_.data(), &pathLength, &timestamp);
			renderDocCapturePath_.setLength(pathLength);
			RenderDocCapture::setCaptureFileComments(renderDocCapturePath_.data(), renderDocFileComments_.data());
			renderDocLastNumCaptures_ = RenderDocCapture::numCaptures();
			LOGI_X("RenderDoc capture %d: %s (%lu)", RenderDocCapture::numCaptures() - 1, renderDocCapturePath_.data(), timestamp);
		}

		if (ImGui::CollapsingHeader("RenderDoc")) {
			int major, minor, patch;
			RenderDocCapture::apiVersion(&major, &minor, &patch);
			ImGui::Text("RenderDoc API: %d.%d.%d", major, minor, patch);
			ImGui::Text("Target control connected: %s", RenderDocCapture::isTargetControlConnected() ? "true" : "false");
			ImGui::Text("Number of captures: %u", RenderDocCapture::numCaptures());
			if (renderDocCapturePath_.isEmpty())
				ImGui::Text("Last capture path: (no capture has been made yet)");
			else
				ImGui::Text("Last capture path: %s", renderDocCapturePath_.data());
			ImGui::Separator();

			renderDocPathTemplate_ = RenderDocCapture::captureFilePathTemplate();
			if (ImGui::InputText("File path template", renderDocPathTemplate_.data(), MaxRenderDocPathLength,
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
				inputTextCallback, &renderDocPathTemplate_)) {
				RenderDocCapture::setCaptureFilePathTemplate(renderDocPathTemplate_.data());
			}

			ImGui::InputTextMultiline("File comments", renderDocFileComments_.data(), MaxRenderDocCommentsLength,
									  ImVec2(0, 0), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
									  inputTextCallback, &renderDocFileComments_);

			static bool overlayEnabled = RenderDocCapture::isOverlayEnabled();
			ImGui::Checkbox("Enable overlay", &overlayEnabled);
			RenderDocCapture::enableOverlay(overlayEnabled);

			if (RenderDocCapture::isFrameCapturing())
				ImGui::TextUnformatted("Capturing a frame...");
			else {
				static int numFrames = 1;
				ImGui::SetNextItemWidth(80.0f);
				ImGui::InputInt("Frames", &numFrames);
				if (numFrames < 1)
					numFrames = 1;
				ImGui::SameLine();
				if (ImGui::Button("Capture"))
					RenderDocCapture::triggerMultiFrameCapture(numFrames);
			}

			if (RenderDocCapture::isTargetControlConnected())
				ImGui::TextUnformatted("Replay UI is connected");
			else {
				if (ImGui::Button("Launch Replay UI"))
					RenderDocCapture::launchReplayUI(1, nullptr);
			}

			static bool crashHandlerLoaded = true;
			if (crashHandlerLoaded) {
				if (ImGui::Button("Unload crash handler")) {
					RenderDocCapture::unloadCrashHandler();
					crashHandlerLoaded = false;
				}
			} else
				ImGui::TextUnformatted("Crash handler not loaded");
		}
#endif
	}

#if defined(RECORD_ALLOCATIONS)
	void guiAllocator(nctl::IAllocator& alloc)
	{
		ImGui::Text("Allocations - Recorded: %lu, Active: %lu, Used Memory: %lu",
					alloc.numEntries(), alloc.numAllocations(), alloc.usedMemory());
		ImGui::NewLine();

		const int tableNumRows = alloc.numEntries() > 32 ? 32 : alloc.numEntries();
		if (ImGui::BeginTable("allocatorEntries", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * tableNumRows))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
			ImGui::TableSetupColumn("Time");
			ImGui::TableSetupColumn("Pointer");
			ImGui::TableSetupColumn("Bytes");
			ImGui::TableSetupColumn("Alignment");
			ImGui::TableSetupColumn("State");
			ImGui::TableHeadersRow();

			for (unsigned int i = 0; i < alloc.numEntries(); i++) {
				const nctl::IAllocator::Entry& e = alloc.entry(i);
				const unsigned int deallocationIndex = alloc.findDeallocation(i);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("#%u", i);
				ImGui::TableNextColumn();
				ImGui::Text("%f s", e.timestamp.seconds());
				ImGui::TableNextColumn();
				ImGui::Text("0x%lx", uintptr_t(e.ptr));
				ImGui::TableNextColumn();
				ImGui::Text("%lu", e.bytes);
				ImGui::TableNextColumn();
				ImGui::Text("%u", e.alignment);
				ImGui::TableNextColumn();
				if (deallocationIndex > 0) {
					const TimeStamp diffStamp = alloc.entry(deallocationIndex).timestamp - e.timestamp;
					ImGui::Text("Freed by #%u after %f s", deallocationIndex, diffStamp.seconds());
				} else
					ImGui::TextUnformatted("Active");
			}

			ImGui::EndTable();
		}
	}
#endif

	void ImGuiDebugOverlay::guiAllocators()
	{
#if defined(WITH_ALLOCATORS)
		if (ImGui::CollapsingHeader("Memory Allocators")) {
			widgetName_.format("Default Allocator \"%s\" (%d allocations, %lu bytes)",
							   nctl::theDefaultAllocator().name(), nctl::theDefaultAllocator().numAllocations(), nctl::theDefaultAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
			ImGui::BulletText("%s", widgetName_.data());
#else
			widgetName_.append("###DefaultAllocator");
			if (ImGui::TreeNode(widgetName_.data())) {
				guiAllocator(nctl::theDefaultAllocator());
				ImGui::TreePop();
			}
#endif

			if (&nctl::theStringAllocator() != &nctl::theDefaultAllocator()) {
				widgetName_.format("String Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theStringAllocator().name(), nctl::theStringAllocator().numAllocations(), nctl::theStringAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", widgetName_.data());
#else
				widgetName_.append("###StringAllocator");
				if (ImGui::TreeNode(widgetName_.data())) {
					guiAllocator(nctl::theStringAllocator());
					ImGui::TreePop();
				}
#endif
			} else
				ImGui::TextUnformatted("The string allocator is the default one");

			if (&nctl::theImGuiAllocator() != &nctl::theDefaultAllocator()) {
				widgetName_.format("ImGui Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theImGuiAllocator().name(), nctl::theImGuiAllocator().numAllocations(), nctl::theImGuiAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", widgetName_.data());
#else
				widgetName_.append("###ImGuiAllocator");
				if (ImGui::TreeNode(widgetName_.data())) {
					guiAllocator(nctl::theImGuiAllocator());
					ImGui::TreePop();
				}
#endif
			} else
				ImGui::TextUnformatted("The ImGui allocator is the default one");

#if defined(WITH_LUA)
			if (&nctl::theLuaAllocator() != &nctl::theDefaultAllocator()) {
				widgetName_.format("Lua Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theLuaAllocator().name(), nctl::theLuaAllocator().numAllocations(), nctl::theLuaAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", widgetName_.data());
#else
				widgetName_.append("###LuaAllocator");
				if (ImGui::TreeNode(widgetName_.data())) {
					guiAllocator(nctl::theLuaAllocator());
					ImGui::TreePop();
				}
#endif
			} else
				ImGui::TextUnformatted("The Lua allocator is the default one");
#endif
		}

#endif
	}

	void ImGuiDebugOverlay::guiViewports(Viewport* viewport, unsigned int viewportId)
	{
		char widgetName[64];
		formatString(widgetName, sizeof(widgetName), "#%u Viewport", viewportId);
		/*if (viewport->type() != Viewport::Type::NoTexture)
			widgetName_.formatAppend(" - size: %d x %d", viewport->width(), viewport->height());*/
		/*const Recti viewportRect = viewport->viewportRect();
		widgetName_.formatAppend(" - rect: pos <%d, %d>, size %d x %d", viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
		const Rectf cullingRect = viewport->cullingRect();
		widgetName_.formatAppend(" - culling: pos <%.2f, %.2f>, size %.2f x %.2f", cullingRect.x, cullingRect.y, cullingRect.w, cullingRect.h);
		widgetName_.formatAppend("###0x%x", uintptr_t(viewport));*/

		SceneNode* rootNode = viewport->rootNode();
		if (rootNode != nullptr && ImGui::TreeNode(widgetName)) {
			if (viewport->camera() != nullptr) {
				const Camera::ViewValues& viewValues = viewport->camera()->viewValues();
				ImGui::Text("Camera view - position: <%.2f, %.2f>, rotation: %.2f, scale: %.2f", viewValues.position.X, viewValues.position.Y, viewValues.rotation, viewValues.scale);
				const Camera::ProjectionValues& projValues = viewport->camera()->projectionValues();
				ImGui::Text("Camera projection - left: %.2f, right: %.2f, top: %.2f, bottom: %.2f", projValues.left, projValues.right, projValues.top, projValues.bottom);
			}

			guiRecursiveChildrenNodes(rootNode, 0);
			ImGui::TreePop();
		}
	}

	void ImGuiDebugOverlay::guiRecursiveChildrenNodes(SceneNode* node, unsigned int childId)
	{
		/*DrawableNode* drawable = nullptr;
		if (node->type() != Object::ObjectType::SceneNode &&
			node->type() != Object::ObjectType::ParticleSystem) {
			drawable = reinterpret_cast<DrawableNode*>(node);
		}

		BaseSprite* baseSprite = nullptr;
		if (node->type() == Object::ObjectType::Sprite ||
			node->type() == Object::ObjectType::MeshSprite ||
			node->type() == Object::ObjectType::AnimatedSprite) {
			baseSprite = reinterpret_cast<BaseSprite*>(node);
		}

		MeshSprite* meshSprite = nullptr;
		if (node->type() == Object::ObjectType::MeshSprite)
			meshSprite = reinterpret_cast<MeshSprite*>(node);

		ParticleSystem* particleSys = nullptr;
		if (node->type() == Object::ObjectType::ParticleSystem)
			particleSys = reinterpret_cast<ParticleSystem*>(node);

		widgetName_.format("#%u ", childId);
		if (node->name() != nullptr)
			widgetName_.formatAppend("\"%s\" ", node->name());
		widgetName_.formatAppend("%s", nodeTypeToString(node->type()));
		const unsigned int numChildren = node->children().size();
		if (numChildren > 0)
			widgetName_.formatAppend(" (%u children)", node->children().size());
		widgetName_.formatAppend(" - position: %.1f x %.1f", node->position().x, node->position().y);
		if (drawable) {
			widgetName_.formatAppend(" - size: %.1f x %.1f", drawable->width(), drawable->height());
			if (drawable->isDrawEnabled() && drawable->lastFrameRendered() < theApplication().numFrames() - 1)
				widgetName_.append(" - culled");
		}
		widgetName_.formatAppend("###0x%x", uintptr_t(node));

		if (ImGui::TreeNode(widgetName_.data())) {
			ImGui::PushID(reinterpret_cast<void*>(node));
			Colorf nodeColor(node->color());
			ImGui::SliderFloat2("Position", node->position().data(), 0.0f, theApplication().width());
			if (drawable) {
				Vector2f nodeAnchorPoint = drawable->anchorPoint();
				ImGui::SliderFloat2("Anchor", nodeAnchorPoint.data(), 0.0f, 1.0f);
				ImGui::SameLine();
				if (ImGui::Button("Reset##Anchor"))
					nodeAnchorPoint = DrawableNode::AnchorCenter;
				drawable->setAnchorPoint(nodeAnchorPoint);
			}
			Vector2f nodeScale = node->scale();
			ImGui::SliderFloat2("Scale", nodeScale.data(), 0.01f, 3.0f);
			ImGui::SameLine();
			if (ImGui::Button("Reset##Scale"))
				nodeScale.set(1.0f, 1.0f);
			node->setScale(nodeScale);
			float nodeRotation = node->rotation();
			ImGui::SliderFloat("Rotation", &nodeRotation, 0.0f, 360.0f);
			ImGui::SameLine();
			if (ImGui::Button("Reset##Rotation"))
				nodeRotation = 0.0f;
			node->setRotation(nodeRotation);
			ImGui::ColorEdit4("Color", nodeColor.data());
			ImGui::SameLine();
			if (ImGui::Button("Reset##Color"))
				nodeColor.set(1.0f, 1.0f, 1.0f, 1.0f);
			node->setColor(nodeColor);

			if (drawable) {
				int layer = drawable->layer();
				ImGui::PushItemWidth(100.0f);
				ImGui::InputInt("Layer", &layer);
				ImGui::PopItemWidth();
				if (layer < 0)
					layer = 0;
				else if (layer > 0xffff)
					layer = 0xffff;
				drawable->setLayer(static_cast<uint16_t>(layer));

				ImGui::SameLine();
				ASSERT(childId == node->childOrderIndex());
				ImGui::Text("Visit order: %u", node->visitOrderIndex());

				ImGui::SameLine();
				bool isBlendingEnabled = drawable->isBlendingEnabled();
				ImGui::Checkbox("Blending", &isBlendingEnabled);
				drawable->setBlendingEnabled(isBlendingEnabled);
			}

			if (baseSprite) {
				ImGui::Text("Texture: \"%s\" (%d x %d)", baseSprite->texture()->name(),
							baseSprite->texture()->width(), baseSprite->texture()->height());

				bool isFlippedX = baseSprite->isFlippedX();
				ImGui::Checkbox("Flipped X", &isFlippedX);
				baseSprite->setFlippedX(isFlippedX);
				ImGui::SameLine();
				bool isFlippedY = baseSprite->isFlippedY();
				ImGui::Checkbox("Flipped Y", &isFlippedY);
				baseSprite->setFlippedY(isFlippedY);

				const Texture* tex = baseSprite->texture();
				Recti texRect = baseSprite->texRect();
				int minX = texRect.x;
				int maxX = minX + texRect.w;
				ImGui::DragIntRange2("Rect X", &minX, &maxX, 1.0f, 0, tex->width());

				int minY = texRect.y;
				int maxY = minY + texRect.h;
				ImGui::DragIntRange2("Rect Y", &minY, &maxY, 1.0f, 0, tex->height());

				texRect.x = minX;
				texRect.w = maxX - minX;
				texRect.y = minY;
				texRect.h = maxY - minY;
				const Recti oldRect = baseSprite->texRect();
				if (oldRect.x != texRect.x || oldRect.y != texRect.y ||
					oldRect.w != texRect.w || oldRect.h != texRect.h) {
					baseSprite->setTexRect(texRect);
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset##TexRect"))
					texRect = Recti(0, 0, tex->width(), tex->height());
			}

			if (meshSprite)
				ImGui::Text("Vertices: %u, Indices: %u", meshSprite->numVertices(), meshSprite->numIndices());
			else if (particleSys) {
				const float aliveFraction = particleSys->numAliveParticles() / static_cast<float>(particleSys->numParticles());
				widgetName_.format("%u / %u", particleSys->numAliveParticles(), particleSys->numParticles());
				ImGui::ProgressBar(aliveFraction, ImVec2(0.0f, 0.0f), widgetName_.data());
				ImGui::SameLine();
				if (ImGui::Button("Kill All##Particles"))
					particleSys->killParticles();
			}
			if (textnode) {
				nctl::String textnodeString(textnode->string());
				if (ImGui::InputTextMultiline("String", textnodeString.data(), textnodeString.capacity(),
					ImVec2(0.0f, 3.0f * ImGui::GetTextLineHeightWithSpacing()),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
					inputTextCallback, &textnodeString)) {
					textnode->setString(textnodeString);
				}
			}

			bool updateEnabled = node->isUpdateEnabled();
			ImGui::Checkbox("Update", &updateEnabled);
			node->setUpdateEnabled(updateEnabled);
			ImGui::SameLine();
			bool drawEnabled = node->isDrawEnabled();
			ImGui::Checkbox("Draw", &drawEnabled);
			node->setDrawEnabled(drawEnabled);
			ImGui::SameLine();
			bool deleteChildrenOnDestruction = node->deleteChildrenOnDestruction();
			ImGui::Checkbox("Delete Children on Destruction", &deleteChildrenOnDestruction);
			node->setDeleteChildrenOnDestruction(deleteChildrenOnDestruction);

			if (ImGui::TreeNode("Absolute Measures")) {
				if (drawable)
					ImGui::Text("Absolute Size: %.1f x %.1f", drawable->absWidth(), drawable->absHeight());
				ImGui::Text("Absolute Position: %.1f, %.1f", node->absPosition().x, node->absPosition().y);
				ImGui::Text("Absolute Anchor Points: %.1f, %.1f", node->absAnchorPoint().x, node->absAnchorPoint().y);
				ImGui::Text("Absolute Scale Factors: %.1f, %.1f", node->absScale().x, node->absScale().y);
				ImGui::Text("Absolute Rotation: %.1f", node->absRotation());

				ImGui::TreePop();
			}

			if (numChildren > 0) {
				if (ImGui::TreeNode("Child Nodes")) {
					const nctl::Array<SceneNode*>& children = node->children();
					for (unsigned int i = 0; i < children.size(); i++)
						guiRecursiveChildrenNodes(children[i], i);
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
			ImGui::TreePop();
		}*/
	}

	void ImGuiDebugOverlay::guiNodeInspector()
	{
		if (ImGui::CollapsingHeader("Node Inspector")) {
			guiViewports(&theApplication().screenViewport(), 0);
			for (unsigned int i = 0; i < Viewport::chain().size(); i++)
				guiViewports(Viewport::chain()[i], i + 1);
		}
	}

#if defined(NCINE_PROFILING)
	void ImGuiDebugOverlay::guiTopLeft()
	{
		const RenderStatistics::VaoPool& vaoPool = RenderStatistics::vaoPool();
		const RenderStatistics::CommandPool& commandPool = RenderStatistics::commandPool();
		const RenderStatistics::Textures& textures = RenderStatistics::textures();
		const RenderStatistics::CustomBuffers& customVbos = RenderStatistics::customVBOs();
		const RenderStatistics::CustomBuffers& customIbos = RenderStatistics::customIBOs();
		const RenderStatistics::Buffers& vboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::Array);
		const RenderStatistics::Buffers& iboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::ElementArray);
		const RenderStatistics::Buffers& uboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::Uniform);

		const ImVec2 windowPos = ImVec2(Margin, Margin);
		const ImVec2 windowPosPivot = ImVec2(0.0f, 0.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (lockOverlayPositions_)
			windowFlags |= ImGuiWindowFlags_NoMove;
		if (showTopLeftOverlay_) {
			ImGui::Begin("###Top-Left", nullptr, windowFlags);

			ImGui::Text("Culled nodes: %u", RenderStatistics::culled());
			if (plotOverlayValues_) {
				ImGui::SameLine(180.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::CulledNodes].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("%u/%u VAOs (%u reuses, %u bindings)", vaoPool.size, vaoPool.capacity, vaoPool.reuses, vaoPool.bindings);
			ImGui::Text("%u/%u RenderCommands in the pool (%u retrievals)", commandPool.usedSize, commandPool.usedSize + commandPool.freeSize, commandPool.retrievals);
			ImGui::Text("%.2f Kb in %u Texture(s)", textures.dataSize / 1024.0f, textures.count);
			ImGui::Text("%.2f Kb in %u custom VBO(s)", customVbos.dataSize / 1024.0f, customVbos.count);
			ImGui::Text("%.2f Kb in %u custom IBO(s)", customIbos.dataSize / 1024.0f, customIbos.count);
			ImGui::Text("%.2f/%lu Kb in %u VBO(s)", vboBuffers.usedSpace / 1024.0f, vboBuffers.size / 1024, vboBuffers.count);
			if (plotOverlayValues_) {
				ImGui::SameLine(180.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::VboUsed].get(), numValues_, index_, nullptr, 0.0f, vboBuffers.size / 1024.0f);
			}

			ImGui::Text("%.2f/%lu Kb in %u IBO(s)", iboBuffers.usedSpace / 1024.0f, iboBuffers.size / 1024, iboBuffers.count);
			if (plotOverlayValues_) {
				ImGui::SameLine(180.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::IboUsed].get(), numValues_, index_, nullptr, 0.0f, iboBuffers.size / 1024.0f);
			}

			ImGui::Text("%.2f/%lu Kb in %u UBO(s)", uboBuffers.usedSpace / 1024.0f, uboBuffers.size / 1024, uboBuffers.count);
			if (plotOverlayValues_) {
				ImGui::SameLine(180.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::UboUsed].get(), numValues_, index_, nullptr, 0.0f, uboBuffers.size / 1024.0f);
			}

			ImGui::Text("Viewport chain length: %u", Viewport::chain().size());

			ImGui::End();
		}
	}
#endif

	void ImGuiDebugOverlay::guiTopRight()
	{
		if (!showTopRightOverlay_) {
			return;
		}

		const ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x - Margin, Margin);
		const ImVec2 windowPosPivot = ImVec2(1.0f, 0.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (lockOverlayPositions_)
			windowFlags |= ImGuiWindowFlags_NoMove;

		ImGui::Begin("###Top-Right", nullptr, windowFlags);

		ImGui::Text("FPS: %.0f (%.2f ms - %.2fx)", theApplication().frameTimer().averageFps(), theApplication().frameTimer().lastFrameDuration() * 1000.0f, theApplication().timeMult());
		ImGui::Text("Num Frames: %lu", theApplication().numFrames());

#if defined(NCINE_PROFILING)
		const AppConfiguration& appCfg = theApplication().appConfiguration();
		if (appCfg.withScenegraph) {
			const RenderStatistics::Commands& spriteCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Sprite);
			const RenderStatistics::Commands& meshspriteCommands = RenderStatistics::commands(RenderCommand::CommandTypes::MeshSprite);
			const RenderStatistics::Commands& tileMapCommands = RenderStatistics::commands(RenderCommand::CommandTypes::TileMap);
			const RenderStatistics::Commands& particleCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Particle);
			const RenderStatistics::Commands& textCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Text);
			const RenderStatistics::Commands& imguiCommands = RenderStatistics::commands(RenderCommand::CommandTypes::ImGui);
			const RenderStatistics::Commands& unspecifiedCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Unspecified);
			const RenderStatistics::Commands& allCommands = RenderStatistics::allCommands();

			ImGui::Separator();
			ImGui::Text("Sprites: %uV, %uDC (%u Tr), %uI/%uB", spriteCommands.vertices, spriteCommands.commands, spriteCommands.transparents, spriteCommands.instances, spriteCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::SpriteVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Mesh Sprites: %uV, %uDC (%u Tr), %uI/%uB", meshspriteCommands.vertices, meshspriteCommands.commands, meshspriteCommands.transparents, meshspriteCommands.instances, meshspriteCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::MeshSpriteVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Tile Map: %uV, %uDC (%u Tr), %uI/%uB\n", tileMapCommands.vertices, tileMapCommands.commands, tileMapCommands.transparents, tileMapCommands.instances, tileMapCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::TileMapVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Particles: %uV, %uDC (%u Tr), %uI/%uB\n", particleCommands.vertices, particleCommands.commands, particleCommands.transparents, particleCommands.instances, particleCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::ParticleVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Text: %uV, %uDC (%u Tr), %uI/%uB", textCommands.vertices, textCommands.commands, textCommands.transparents, textCommands.instances, textCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::TextVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("ImGui: %uV, %uDC (%u Tr), %uI/%u", imguiCommands.vertices, imguiCommands.commands, imguiCommands.transparents, imguiCommands.instances, imguiCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::ImGuiVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Unspecified: %uV, %uDC (%u Tr), %uI/%u", unspecifiedCommands.vertices, unspecifiedCommands.commands, unspecifiedCommands.transparents, unspecifiedCommands.instances, unspecifiedCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::UnspecifiedVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Separator();
			ImGui::Text("Total: %uV, %uDC (%u Tr), %uI/%uB", allCommands.vertices, allCommands.commands, allCommands.transparents, allCommands.instances, allCommands.batchSize);
			if (plotOverlayValues_) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("", plotValues_[ValuesType::TotalVertices].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}
		}
#endif

		ImGui::End();
	}

	void ImGuiDebugOverlay::guiBottomLeft()
	{
		/*const ImVec2 windowPos = ImVec2(Margin, ImGui::GetIO().DisplaySize.y - Margin);
		const ImVec2 windowPosPivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (lockOverlayPositions_)
			windowFlags |= ImGuiWindowFlags_NoMove;
		if (showBottomLeftOverlay_) {
			ImGui::Begin("###Bottom-Left", nullptr, windowFlags);
#ifdef WITH_GIT_VERSION
			ImGui::Text("%s (%s)", VersionStrings::Version, VersionStrings::GitBranch);
#else
			ImGui::Text("%s at %s", VersionStrings::CompilationDate, VersionStrings::CompilationTime);
#endif
			ImGui::End();
		}*/
	}

	void ImGuiDebugOverlay::guiBottomRight()
	{
#if defined(WITH_LUA)
		// Do not show statistics if there are no registered state managers
		if (LuaStatistics::numRegistered() == 0)
			return;

		const ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x - Margin, ImGui::GetIO().DisplaySize.y - Margin);
		const ImVec2 windowPosPivot = ImVec2(1.0f, 1.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (lockOverlayPositions_)
			windowFlags |= ImGuiWindowFlags_NoMove;
		if (showBottomRightOverlay_) {
			ImGui::Begin("###Bottom-Right", nullptr, windowFlags);

			ImGui::Text("%u Lua state(s) with %u tracked userdata", LuaStatistics::numRegistered(), LuaStatistics::numTrackedUserDatas());
			ImGui::Text("Used memory: %zu Kb", LuaStatistics::usedMemory() / 1024);
			if (plotOverlayValues_) {
				ImGui::SameLine();
				ImGui::PlotLines("", plotValues_[ValuesType::LuaUsed].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Operations: %d ops/s", LuaStatistics::operations());
			if (plotOverlayValues_) {
				ImGui::SameLine();
				ImGui::PlotLines("", plotValues_[ValuesType::LuaOperations].get(), numValues_, index_, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Textures: %u, Sprites: %u, Mesh sprites: %u",
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::TEXTURE),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::SPRITE),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::MESH_SPRITE));
			ImGui::Text("Animated sprites: %u, Fonts: %u, Textnodes: %u",
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::ANIMATED_SPRITE),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::FONT),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::TEXTNODE));
			ImGui::Text("Audio buffers: %u, Buffer players: %u\n",
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::AUDIOBUFFER),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::AUDIOBUFFER_PLAYER));
			ImGui::Text("Stream players: %u, Particle systems: %u",
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::AUDIOSTREAM_PLAYER),
						LuaStatistics::numTypedUserDatas(LuaTypes::UserDataType::PARTICLE_SYSTEM));

			ImGui::End();
		}
#endif
	}

	void ImGuiDebugOverlay::guiPlots()
	{
		const float appWidth = theApplication().width();

		const ImVec2 windowPos = ImVec2(appWidth * 0.5f, ImGui::GetIO().DisplaySize.y - Margin);
		const ImVec2 windowPosPivot = ImVec2(0.5f, 1.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (lockOverlayPositions_)
			windowFlags |= ImGuiWindowFlags_NoMove;
		ImGui::Begin("###Plots", nullptr, windowFlags);

		ImGui::PlotLines("Frame time", plotValues_[ValuesType::FrameTime].get(), numValues_, index_, nullptr, 0.0f, maxFrameTime_, ImVec2(appWidth * 0.2f, 0.0f));

#if defined(NCINE_PROFILING)
		const AppConfiguration& appCfg = theApplication().appConfiguration();
		if (appCfg.withScenegraph) {
			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::PlotLines("Update", plotValues_[ValuesType::Update].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.6f, 1.0f, 0.2f, 1.0f));
			ImGui::PlotLines("Visit", plotValues_[ValuesType::Visit].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
			ImGui::PlotLines("Draw", plotValues_[ValuesType::Draw].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			ImGui::PlotLines("Aggregated", plotValues_[ValuesType::UpdateVisitDraw].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
		}

		if (plotAdditionalFrameValues_) {
			ImGui::Separator();
			ImGui::PlotLines("OnFrameStart", plotValues_[ValuesType::FrameStart].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			if (appCfg.withScenegraph)
				ImGui::PlotLines("OnPostUpdate", plotValues_[ValuesType::PostUpdate].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PlotLines("OnFrameEnd", plotValues_[ValuesType::FrameEnd].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PlotLines("ImGui", plotValues_[ValuesType::ImGui].get(), numValues_, index_, nullptr, 0.0f, maxUpdateVisitDraw_, ImVec2(appWidth * 0.2f, 0.0f));
		}
#endif

		ImGui::End();
	}

	void ImGuiDebugOverlay::initPlotValues()
	{
		for (unsigned int type = 0; type < ValuesType::Count; type++) {
			plotValues_[type] = std::make_unique<float[]>(numValues_);

			for (unsigned int i = index_; i < numValues_; i++) {
				plotValues_[type][i] = 0.0f;
			}
		}
	}

#if defined(NCINE_PROFILING)
	void ImGuiDebugOverlay::updateOverlayTimings()
	{
		const RenderStatistics::Buffers& vboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::Array);
		const RenderStatistics::Buffers& iboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::ElementArray);
		const RenderStatistics::Buffers& uboBuffers = RenderStatistics::buffers(RenderBuffersManager::BufferTypes::Uniform);

		const RenderStatistics::Commands& spriteCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Sprite);
		const RenderStatistics::Commands& meshspriteCommands = RenderStatistics::commands(RenderCommand::CommandTypes::MeshSprite);
		const RenderStatistics::Commands& tileMapCommands = RenderStatistics::commands(RenderCommand::CommandTypes::TileMap);
		const RenderStatistics::Commands& particleCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Particle);
		const RenderStatistics::Commands& textCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Text);
		const RenderStatistics::Commands& imguiCommands = RenderStatistics::commands(RenderCommand::CommandTypes::ImGui);
		const RenderStatistics::Commands& unspecifiedCommands = RenderStatistics::commands(RenderCommand::CommandTypes::Unspecified);
		const RenderStatistics::Commands& allCommands = RenderStatistics::allCommands();

		plotValues_[ValuesType::CulledNodes][index_] = static_cast<float>(RenderStatistics::culled());
		plotValues_[ValuesType::VboUsed][index_] = vboBuffers.usedSpace / 1024.0f;
		plotValues_[ValuesType::IboUsed][index_] = iboBuffers.usedSpace / 1024.0f;
		plotValues_[ValuesType::UboUsed][index_] = uboBuffers.usedSpace / 1024.0f;

		plotValues_[ValuesType::SpriteVertices][index_] = static_cast<float>(spriteCommands.vertices);
		plotValues_[ValuesType::MeshSpriteVertices][index_] = static_cast<float>(meshspriteCommands.vertices);
		plotValues_[ValuesType::TileMapVertices][index_] = static_cast<float>(tileMapCommands.vertices);
		plotValues_[ValuesType::ParticleVertices][index_] = static_cast<float>(particleCommands.vertices);
		plotValues_[ValuesType::TextVertices][index_] = static_cast<float>(textCommands.vertices);
		plotValues_[ValuesType::ImGuiVertices][index_] = static_cast<float>(imguiCommands.vertices);
		plotValues_[ValuesType::UnspecifiedVertices][index_] = static_cast<float>(unspecifiedCommands.vertices);
		plotValues_[ValuesType::TotalVertices][index_] = static_cast<float>(allCommands.vertices);

#	if defined(WITH_LUA)
		plotValues_[ValuesType::LuaUsed][index_] = LuaStatistics::usedMemory() / 1024.0f;
		plotValues_[ValuesType::LuaOperations][index_] = static_cast<float>(LuaStatistics::operations());
#	endif
	}
#endif
}

#endif