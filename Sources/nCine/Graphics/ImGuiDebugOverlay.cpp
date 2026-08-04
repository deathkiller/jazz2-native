#if defined(WITH_IMGUI)

#include "ImGuiDebugOverlay.h"
#include "../Application.h"
#include "../ServiceLocator.h"
#include "RHI/IRhiCapabilities.h"
#include "../Input/IInputManager.h"
#include "../Input/InputEvents.h"
#include "../Primitives/Vector2.h"

#include "Viewport.h"
#include "Camera.h"
#include "DrawableNode.h"
#include "MeshSprite.h"
#include "ParticleSystem.h"

#include <Containers/StaticArray.h>

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

using namespace Death::Containers::Literals;

namespace ImGui
{
	void TextInRoundedRectangle(const char* text, const char* text_end = nullptr)
	{
		auto drawList = ImGui::GetWindowDrawList();
		ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
		ImVec2 textSize = ImGui::CalcTextSize(text, text_end, true);
		ImVec2 start = ImVec2(cursorScreenPos.x, cursorScreenPos.y + 2.0f);
		ImVec2 end = ImVec2(start.x + textSize.x + 7.0f, start.y + textSize.y - 1.0f);

		drawList->AddRectFilled(start, end, 0xFF46310B, 1.0f);
		drawList->AddRect(start, end, 0xFF736346, 1.0f);
		drawList->AddText(ImVec2(cursorScreenPos.x + 4.0f, cursorScreenPos.y), 0xFFFFFFFF, text, text_end);

		cursorScreenPos.x = end.x + 4.0f;
		ImGui::SetCursorScreenPos(cursorScreenPos);
	}
}

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

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		const char* openglApiName = "OpenGL ES";
#else
		const char* openglApiName = "OpenGL";
#endif
	}

	ImGuiDebugOverlay::ImGuiDebugOverlay(float profileTextUpdateTime)
		: IDebugOverlay(profileTextUpdateTime), _lockOverlayPositions(false), _showTopLeftOverlay(true), _showTopRightOverlay(true),
			_showBottomLeftOverlay(true), _showBottomRightOverlay(true), _numValues(80), _maxFrameTime(0.0f), _maxUpdateVisitDraw(0.0f),
			_index(0), _plotAdditionalFrameValues(false), _plotOverlayValues(true)
#if defined(WITH_RENDERDOC)
			, _renderDocPathTemplate(MaxRenderDocPathLength), _renderDocFileComments(MaxRenderDocCommentsLength),
				_renderDocCapturePath(MaxRenderDocPathLength), _renderDocLastNumCaptures(0)
#endif
	{
		InitPlotValues();
	}

	void ImGuiDebugOverlay::Update()
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
		//ImGui::ShowDemoWindow();

		if (_settings.showInfoText) {
			const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
			if (appCfg.withScenegraph) {
#if defined(NCINE_PROFILING)
				guiTopLeft();
#endif
				guiBottomRight();
			}
			guiTopRight();
			guiBottomLeft();
			guiLog();
		}

		if (_settings.showProfilerGraphs) {
			guiPlots();
		}
	}

	void ImGuiDebugOverlay::UpdateFrameTimings()
	{
		if (_lastUpdateTime.secondsSince() > _updateTime) {
			const AppConfiguration& appCfg = theApplication().GetAppConfiguration();

			_plotValues[ValuesType::FrameTime][_index] = theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f;

#if defined(NCINE_PROFILING)
			auto timings = theApplication().GetTimings();
			_plotValues[ValuesType::BeginFrame][_index] = timings[(std::int32_t)Application::Timings::BeginFrame] * 1000.0f;
			if (appCfg.withScenegraph) {
				_plotValues[ValuesType::PostUpdate][_index] = timings[(std::int32_t)Application::Timings::PostUpdate] * 1000.0f;
			}
			_plotValues[ValuesType::ImGui][_index] = timings[(std::int32_t)Application::Timings::ImGui] * 1000.0f;
			_plotValues[ValuesType::EndFrame][_index] = timings[(std::int32_t)Application::Timings::EndFrame] * 1000.0f;

			if (appCfg.withScenegraph) {
				_plotValues[ValuesType::UpdateVisitDraw][_index] = timings[(std::int32_t)Application::Timings::Update] * 1000.0f +
					timings[(std::int32_t)Application::Timings::Visit] * 1000.0f + timings[(std::int32_t)Application::Timings::Draw] * 1000.0f;
				_plotValues[ValuesType::Update][_index] = timings[(std::int32_t)Application::Timings::Update] * 1000.0f;
				_plotValues[ValuesType::Visit][_index] = timings[(std::int32_t)Application::Timings::Visit] * 1000.0f;
				_plotValues[ValuesType::Draw][_index] = timings[(std::int32_t)Application::Timings::Draw] * 1000.0f;
			}
#endif

			float maxFrameTime = 0.0f, avgFrameTime = 0.0f;
#if defined(NCINE_PROFILING)
			float maxUpdateVisitDraw = 0.0f, avgUpdateVisitDraw = 0.0f;
#endif
			for (std::uint32_t i = 0; i < _numValues; i++) {
				if (maxFrameTime < _plotValues[ValuesType::FrameTime][i]) {
					maxFrameTime = _plotValues[ValuesType::FrameTime][i];
				}
				avgFrameTime += _plotValues[ValuesType::FrameTime][i];

#if defined(NCINE_PROFILING)
				if (appCfg.withScenegraph) {
					if (maxUpdateVisitDraw < _plotValues[ValuesType::UpdateVisitDraw][i]) {
						maxUpdateVisitDraw = _plotValues[ValuesType::UpdateVisitDraw][i];
					}
					avgUpdateVisitDraw += _plotValues[ValuesType::UpdateVisitDraw][i];
				}
#endif
			}

			avgFrameTime = avgFrameTime * 2.0f / _numValues;
			if (maxFrameTime < avgFrameTime) {
				maxFrameTime = avgFrameTime;
			}
			if (_maxFrameTime < maxFrameTime) {
				_maxFrameTime = maxFrameTime;
			} else {
				_maxFrameTime = lerp(_maxFrameTime, maxFrameTime, 0.2f);
			}

#if defined(NCINE_PROFILING)
			avgUpdateVisitDraw = avgUpdateVisitDraw * 2.0f / _numValues;
			if (maxUpdateVisitDraw < avgUpdateVisitDraw) {
				maxUpdateVisitDraw = avgUpdateVisitDraw;
			}
			if (_maxUpdateVisitDraw < maxUpdateVisitDraw) {
				_maxUpdateVisitDraw = maxUpdateVisitDraw;
			} else {
				_maxUpdateVisitDraw = lerp(_maxUpdateVisitDraw, maxUpdateVisitDraw, 0.2f);
			}

			if (appCfg.withScenegraph) {
				UpdateOverlayTimings();
			}
#endif

			_index = (_index + 1) % _numValues;
			_lastUpdateTime = TimeStamp::now();
		}
	}

#if defined(DEATH_TRACE)
	void ImGuiDebugOverlay::Log(TraceLevel level, StringView time, StringView threadId, StringView functionName, StringView message)
	{
		_logBuffer.emplace_back(LogMessage{time, message, threadId, functionName, level});
	}
#endif

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
				case ButtonName::Unknown: return "Unknown";
				case ButtonName::A: return "A";
				case ButtonName::B: return "B";
				case ButtonName::X: return "X";
				case ButtonName::Y: return "Y";
				case ButtonName::Back: return "Back";
				case ButtonName::Guide: return "Guide";
				case ButtonName::Start: return "Start";
				case ButtonName::LeftStick: return "LStick";
				case ButtonName::RightStick: return "RStick";
				case ButtonName::LeftBumper: return "LBumper";
				case ButtonName::RightBumper: return "RBumper";
				case ButtonName::Up: return "DPad_Up";
				case ButtonName::Down: return "DPad_Down";
				case ButtonName::Left: return "DPad_Left";
				case ButtonName::Right: return "DPad_Right";
				case ButtonName::Misc1: return "Misc1";
				case ButtonName::Paddle1: return "Paddle1";
				case ButtonName::Paddle2: return "Paddle2";
				case ButtonName::Paddle3: return "Paddle3";
				case ButtonName::Paddle4: return "Paddle4";
				default: return "Unknown";
			}
		}

		const char* mappedAxisNameToString(AxisName name)
		{
			switch (name) {
				case AxisName::Unknown: return "Unknown";
				case AxisName::LeftX: return "LX";
				case AxisName::LeftY: return "LY";
				case AxisName::RightX: return "RX";
				case AxisName::RightY: return "RY";
				case AxisName::LeftTrigger: return "LTrigger";
				case AxisName::RightTrigger: return "RTrigger";
				default: return "Unknown";
			}
		}
	}

	void ImGuiDebugOverlay::guiWindow()
	{
		if (!_settings.showInterface) {
			return;
		}

		const ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y - 0.5f);
		const ImVec2 windowPosPivot = ImVec2(0.5f, 0.5f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PushStyleColor(ImGuiCol_WindowShadow, ImVec4(0, 0, 0, 1));
#endif
		bool isVisible = ImGui::Begin("Debug Overlay", &_settings.showInterface);
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PopStyleColor();
#endif

		if (isVisible) {
			const AppConfiguration& appCfg = theApplication().GetAppConfiguration();

			bool disableAutoSuspension = !theApplication().GetAutoSuspension();
			ImGui::Checkbox("Disable auto-suspension", &disableAutoSuspension);
			theApplication().SetAutoSuspension(!disableAutoSuspension);
			/*ImGui::SameLine();
			if (ImGui::Button("Quit")) {
				theApplication().Quit();
			}*/

			guiConfigureGui();
			guiInitTimes();
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
		}

		ImGui::End();
	}

	void ImGuiDebugOverlay::guiConfigureGui()
	{
		static std::int32_t numValues = 0;

		if (ImGui::CollapsingHeader("Configure GUI")) {
			const AppConfiguration& appCfg = theApplication().GetAppConfiguration();

			ImGui::Checkbox("Show interface", &_settings.showInterface);
			if (ImGui::TreeNodeEx("Overlays", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Show overlays", &_settings.showInfoText);
				ImGui::Checkbox("Lock overlay positions", &_lockOverlayPositions);
				if (appCfg.withScenegraph) {
					ImGui::Checkbox("Show Top-Left", &_showTopLeftOverlay);
					ImGui::SameLine();
				}
				ImGui::Checkbox("Show Top-Right", &_showTopRightOverlay);
				ImGui::Checkbox("Show Bottom-Left", &_showBottomLeftOverlay);
#if defined(WITH_LUA)
				if (appCfg.withScenegraph) {
					ImGui::SameLine();
					ImGui::Checkbox("Show Bottom-Right", &_showBottomRightOverlay);
				}
#endif
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Profiling Graphs", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Show profiling graphs", &_settings.showProfilerGraphs);
				ImGui::Checkbox("Plot additional frame values", &_plotAdditionalFrameValues);
				if (appCfg.withScenegraph)
					ImGui::Checkbox("Plot overlay values", &_plotOverlayValues);
				ImGui::SliderFloat("Graphs update time", &_updateTime, 0.0f, 1.0f, "%.3f s");
				numValues = (numValues == 0) ? static_cast<int>(_numValues) : numValues;
				ImGui::SliderInt("Number of values", &numValues, 16, 512);
				ImGui::SameLine();
				if (ImGui::Button("Apply") && _numValues != static_cast<std::uint32_t>(numValues)) {
					_numValues = static_cast<std::uint32_t>(numValues);
					InitPlotValues();
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("GUI Style")) {
				static std::int32_t styleIndex = 0;
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
				static float scaling = style.FontScaleMain;
				ImGui::SliderFloat("Scaling", &scaling, MinScaling, MaxScaling, "%.1f");
				ImGui::SameLine();
				if (ImGui::Button("Reset"))
					scaling = 1.0f;

				if (scaling < MinScaling)
					scaling = MinScaling;
				if (scaling > MaxScaling)
					scaling = MaxScaling;
				style.FontScaleMain = scaling;

				ImGui::TreePop();
			}
		} else
			numValues = 0;
	}

	void ImGuiDebugOverlay::guiInitTimes()
	{
#if defined(NCINE_PROFILING)
		if (ImGui::CollapsingHeader("Init Times")) {
			auto timings = theApplication().GetTimings();

			float initTimes[3];
			initTimes[0] = timings[(std::int32_t)Application::Timings::PreInit] * 1000.0f;
			initTimes[1] = initTimes[0] + timings[(std::int32_t)Application::Timings::InitCommon] * 1000.0f;
			initTimes[2] = initTimes[1] + timings[(std::int32_t)Application::Timings::AppInit] * 1000.0f;
			ImGui::PlotHistogram("##1", initTimes, 3, 0, nullptr, 0.0f, initTimes[2], ImVec2(0.0f, 100.0f));

			ImGui::Text("Pre-Init Time: %.2f ms", timings[(std::int32_t)Application::Timings::PreInit] * 1000.0f);
			ImGui::Text("Init Time: %.2f ms", timings[(std::int32_t)Application::Timings::InitCommon] * 1000.0f);
			ImGui::Text("Application Init Time: %.2f ms", timings[(std::int32_t)Application::Timings::AppInit] * 1000.0f);
		}
#endif
	}

	void ImGuiDebugOverlay::guiLog()
	{
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PushStyleColor(ImGuiCol_WindowShadow, ImVec4(0, 0, 0, 1));
#endif
		bool isVisible = ImGui::Begin("Log", &_settings.showInterface);
#if defined(IMGUI_HAS_SHADOWS)
		ImGui::PopStyleColor();
#endif
		if (isVisible) {
			ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoPadOuterX;
			if (ImGui::BeginTable("log", 3, flags, ImVec2(0.0f, 0.0f))) {
				ImGuiListClipper clipper;
				clipper.Begin(_logBuffer.size());
				while (clipper.Step()) {
					for (std::int32_t row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
						const auto& message = _logBuffer[row];

						ImGui::TableNextRow();

						std::uint32_t color;
						switch (message.Level) {
							case TraceLevel::Fatal:		color = 0xFF403EEC; break;
							case TraceLevel::Assert:	color = 0xFFad00cc; break;
							case TraceLevel::Error:		color = 0xFF5050D8; break;
							case TraceLevel::Warning:	color = 0xFF7AC7EB; break;
							case TraceLevel::Info:		color = 0xFFEEEEEE; break;
							default:					color = 0xFF969696; break;
						}
						ImGui::PushStyleColor(ImGuiCol_Text, color);

						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(message.Time.begin(), message.Time.end());

						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(message.ThreadId.begin(), message.ThreadId.end());

						ImGui::TableSetColumnIndex(2);
						if (!message.FunctionName.empty()) {
							ImGui::TextInRoundedRectangle(message.FunctionName.begin(), message.FunctionName.end());
						}
						ImGui::TextUnformatted(message.Text.begin(), message.Text.end());

						ImGui::PopStyleColor();
					}
				}
				ImGui::EndTable();
			}
		}

		ImGui::End();
	}

	void ImGuiDebugOverlay::guiGraphicsCapabilities()
	{
		if (ImGui::CollapsingHeader("Graphics Capabilities")) {
			const RHI::IRhiCapabilities& caps = theServiceLocator().GetRhiCapabilities();

			const RHI::IRhiCapabilities::InfoStrings& glInfoStrings = caps.GetInfoStrings();
			const auto hasInfoString = [](const char* value) { return (value != nullptr && value[0] != '\0'); };
			if (hasInfoString(glInfoStrings.vendor)) {
				ImGui::Text("%s Vendor: %s", openglApiName, glInfoStrings.vendor);
			}
			if (hasInfoString(glInfoStrings.renderer)) {
				ImGui::Text("%s Renderer: %s", openglApiName, glInfoStrings.renderer);
			}
			if (hasInfoString(glInfoStrings.apiVersion)) {
				ImGui::Text("%s Version: %s", openglApiName, glInfoStrings.apiVersion);
			}
			if (hasInfoString(glInfoStrings.shadingLanguageVersion)) {
				ImGui::Text("GLSL Version: %s", glInfoStrings.shadingLanguageVersion);
			}

			ImGui::Text("%s parsed version: %d.%d.%d", openglApiName,
						caps.GetApiVersion(RHI::IRhiCapabilities::ApiVersion::Major),
						caps.GetApiVersion(RHI::IRhiCapabilities::ApiVersion::Minor),
						caps.GetApiVersion(RHI::IRhiCapabilities::ApiVersion::Release));

			ImGui::Separator();
			ImGui::Text("GL_MAX_TEXTURE_SIZE: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_TEXTURE_SIZE));
			ImGui::Text("GL_MAX_TEXTURE_IMAGE_UNITS: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_TEXTURE_IMAGE_UNITS));
			ImGui::Text("GL_MAX_UNIFORM_BLOCK_SIZE: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_UNIFORM_BLOCK_SIZE));
			ImGui::Text("GL_MAX_UNIFORM_BUFFER_BINDINGS: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_UNIFORM_BUFFER_BINDINGS));
			ImGui::Text("GL_MAX_VERTEX_UNIFORM_BLOCKS: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_VERTEX_UNIFORM_BLOCKS));
			ImGui::Text("GL_MAX_FRAGMENT_UNIFORM_BLOCKS: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS));
			ImGui::Text("GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT));
#if !defined(DEATH_TARGET_EMSCRIPTEN) && (!defined(RHI_GL_PROFILE_ES) || (defined(RHI_GL_PROFILE_ES) && GL_ES_VERSION_3_1))
			ImGui::Text("GL_MAX_VERTEX_ATTRIB_STRIDE: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_VERTEX_ATTRIB_STRIDE));
#endif
			ImGui::Text("GL_MAX_COLOR_ATTACHMENTS: %d", caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_COLOR_ATTACHMENTS));

			ImGui::Separator();
			ImGui::Text("GL_KHR_debug: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::KHR_DEBUG));
			ImGui::Text("GL_ARB_texture_storage: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::ARB_TEXTURE_STORAGE));
			ImGui::Text("GL_ARB_get_program_binary: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::ARB_GET_PROGRAM_BINARY));
#if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			ImGui::Text("GL_OES_get_program_binary: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::OES_GET_PROGRAM_BINARY));
#endif
			ImGui::Text("GL_EXT_texture_compression_s3tc: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::EXT_TEXTURE_COMPRESSION_S3TC));
			ImGui::Text("GL_AMD_compressed_ATC_texture: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::AMD_COMPRESSED_ATC_TEXTURE));
			ImGui::Text("GL_IMG_texture_compression_pvrtc: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::IMG_TEXTURE_COMPRESSION_PVRTC));
			ImGui::Text("GL_KHR_texture_compression_astc_ldr: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR));
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			ImGui::Text("GL_OES_compressed_ETC1_RGB8_texture: %d", caps.HasExtension(RHI::IRhiCapabilities::Extensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE));
#endif
		}
	}

	void ImGuiDebugOverlay::guiApplicationConfiguration()
	{
		if (ImGui::CollapsingHeader("Application Configuration")) {
			const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
#if !defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN)
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
			/*_widgetName.assign("Window Position: ");
			if (appCfg.windowPosition.x == AppConfiguration::WindowPositionIgnore)
				_widgetName.append("Ignore x ");
			else
				_widgetName.formatAppend("%d x ", appCfg.windowPosition.x);
			if (appCfg.windowPosition.y == AppConfiguration::WindowPositionIgnore)
				_widgetName.append("Ignore");
			else
				_widgetName.formatAppend("%d", appCfg.windowPosition.y);
			ImGui::TextUnformatted(_widgetName.data());*/
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
			Application::RenderingSettings& settings = theApplication().GetRenderingSettings();
			std::int32_t minBatchSize = settings.minBatchSize;
			std::int32_t maxBatchSize = settings.maxBatchSize;

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
			const std::uint32_t numMonitors = gfxDevice.numMonitors();
			for (std::uint32_t i = 0; i < numMonitors; i++) {
				const IGfxDevice::Monitor& monitor = gfxDevice.monitor(i);
				_widgetName.format("Monitor #%u: \"%s\"", i, monitor.name);
				if (i == gfxDevice.primaryMonitorIndex())
					_widgetName.append(" [Primary]");
				if (i == gfxDevice.windowMonitorIndex())
					_widgetName.formatAppend(" [%s]", gfxDevice.isFullScreen() ? "Full Screen" : "Window");

				if (ImGui::TreeNode(_widgetName.data())) {
					ImGui::Text("Position: <%d, %d>", monitor.position.x, monitor.position.y);
					ImGui::Text("DPI: <%d, %d>", monitor.dpi.x, monitor.dpi.y);
					ImGui::Text("Scale: <%.2f, %.2f>", monitor.scale.x, monitor.scale.y);

					const std::uint32_t numVideoModes = monitor.numVideoModes;
					_widgetName.format("%u Video Modes", numVideoModes);
					if (ImGui::TreeNode(_widgetName.data())) {
						for (std::uint32_t j = 0; j < numVideoModes; j++) {
							const IGfxDevice::VideoMode& videoMode = monitor.videoModes[j];
							_widgetName.format("#%u: %u x %u, %.2f Hz", j, videoMode.width, videoMode.height, videoMode.refreshRate);
							if (videoMode.redBits != 8 || videoMode.greenBits != 8 || videoMode.blueBits != 8)
								_widgetName.formatAppend(" (R%uG%uB%u)", videoMode.redBits, videoMode.greenBits, videoMode.blueBits);
							ImGui::TextUnformatted(_widgetName.data());
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

			static std::int32_t selectedVideoMode = -1;
			const IGfxDevice::VideoMode currentVideoMode = gfxDevice.currentVideoMode();
			if (fullScreen == false) {
				ImGui::InputInt2("Resolution", resolution.data());
				ImGui::InputInt2("Position", winPosition.data());
				selectedVideoMode = -1;
			} else {
				const std::int32_t monitorIndex = gfxDevice.windowMonitorIndex();
				const IGfxDevice::Monitor& monitor = gfxDevice.monitor(monitorIndex);

				std::uint32_t currentVideoModeIndex = 0;
				const std::uint32_t numVideoModes = monitor.numVideoModes;
				_comboVideoModes.clear();
				for (std::uint32_t i = 0; i < numVideoModes; i++) {
					const IGfxDevice::VideoMode& mode = monitor.videoModes[i];
					_comboVideoModes.formatAppend("%u: %u x %u, %.2f Hz", i, mode.width, mode.height, mode.refreshRate);
					_comboVideoModes.setLength(_comboVideoModes.length() + 1);

					if (mode == currentVideoMode)
						currentVideoModeIndex = i;
				}
				_comboVideoModes.setLength(_comboVideoModes.length() + 1);
				// Append a second '\0' to signal the end of the combo item list
				_comboVideoModes[_comboVideoModes.length() - 1] = '\0';

				if (selectedVideoMode < 0)
					selectedVideoMode = currentVideoModeIndex;

				ImGui::Combo("Video Mode", &selectedVideoMode, _comboVideoModes.data());
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
			ImGui::Text("Device Name: %s", theServiceLocator().GetAudioDevice().name());
			ImGui::Text("Listener Gain: %f", theServiceLocator().GetAudioDevice().gain());

			std::uint32_t numPlayers = theServiceLocator().GetAudioDevice().numPlayers();
			ImGui::Text("Active Players: %d", numPlayers);

			if (numPlayers > 0) {
				if (ImGui::Button("Stop"))
					theServiceLocator().GetAudioDevice().stopPlayers();
				ImGui::SameLine();
				if (ImGui::Button("Pause"))
					theServiceLocator().GetAudioDevice().pausePlayers();
			}

			// Stopping or pausing players change the number of active ones
			numPlayers = theServiceLocator().GetAudioDevice().numPlayers();
			for (std::uint32_t i = 0; i < numPlayers; i++) {
				const IAudioPlayer* player = theServiceLocator().GetAudioDevice().player(i);
				char widgetName[32];
				std::size_t length = formatInto(widgetName, "Player {}", i);
				widgetName[length] = '\0';
				if (ImGui::TreeNode(widgetName)) {
					ImGui::Text("Source Id: %u", player->sourceId());
					ImGui::Text("Buffer Id: %u", player->bufferId());
					ImGui::Text("Channels: %d", player->numChannels());
					ImGui::Text("Frequency: %d Hz", player->frequency());
					auto bufferSize = player->bufferSize();
					if (bufferSize < 0) {
						ImGui::Text("Buffer Size: Streaming");
					} else {
						ImGui::Text("Buffer Size: %lu bytes", bufferSize);
					}
					ImGui::Text("State: %s", audioPlayerStateToString(player->state()));
					ImGui::Text("Looping: %s", player->isLooping() ? "true" : "false");
					ImGui::Text("Gain: %f", player->gain());
					ImGui::Text("Pitch: %f", player->pitch());
					ImGui::Text("Low-pass: %f", player->lowPass());
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
			const IInputManager& input = theApplication().GetInputManager();

			/*if (ImGui::TreeNode("Keyboard")) {
				nctl::String pressedKeys;
				const KeyboardState& keyState = input.keyboardState();
				for (std::uint32_t i = 0; i < static_cast<int>(Keys::COUNT); i++) {
					if (keyState.isKeyDown(static_cast<Keys>(i)))
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

			std::uint32_t numConnectedJoysticks = 0;
			for (std::int32_t joyId = 0; joyId < IInputManager::MaxNumJoysticks; joyId++) {
				if (input.isJoyPresent(joyId))
					numConnectedJoysticks++;
			}
			if (numConnectedJoysticks > 0) {
				char widgetName[32];
				std::size_t length = formatInto(widgetName, "{} Joystick(s)", numConnectedJoysticks);
				widgetName[length] = '\0';
				if (ImGui::TreeNode(widgetName)) {
					ImGui::Text("Joystick mappings: %u", input.numJoyMappings());

					for (std::int32_t joyId = 0; joyId < IInputManager::MaxNumJoysticks; joyId++) {
						if (input.isJoyPresent(joyId) == false)
							continue;

						length = formatInto(widgetName, "Joystick {}", joyId);
						widgetName[length] = '\0';
						if (ImGui::TreeNode(widgetName)) {
							ImGui::Text("Name: %s", input.joyName(joyId));
							ImGui::Text("GUID: %s", input.joyGuid(joyId));
							ImGui::Text("Num Buttons: %d", input.joyNumButtons(joyId));
							ImGui::Text("Num Hats: %d", input.joyNumHats(joyId));
							ImGui::Text("Num Axes: %d", input.joyNumAxes(joyId));
							ImGui::Separator();

							const JoystickState& joyState = input.joystickState(joyId);
							/*nctl::String pressedButtons;
							for (std::int32_t buttonId = 0; buttonId < input.joyNumButtons(joyId); buttonId++) {
								if (joyState.isButtonPressed(buttonId))
									pressedButtons.formatAppend("%d ", buttonId);
							}
							ImGui::Text("Pressed buttons: %s", pressedButtons.data());*/

							for (std::int32_t hatId = 0; hatId < input.joyNumHats(joyId); hatId++) {
								unsigned char hatState = joyState.hatState(hatId);
								ImGui::Text("Hat %d: %u", hatId, hatState);
							}

							for (std::int32_t axisId = 0; axisId < input.joyNumAxes(joyId); axisId++) {
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
								for (std::uint32_t buttonId = 0; buttonId < JoyMappedState::NumButtons; buttonId++) {
									const ButtonName buttonName = static_cast<ButtonName>(buttonId);
									if (joyMappedState.isButtonPressed(buttonName))
										pressedMappedButtons.formatAppend("%s ", mappedButtonNameToString(buttonName));
								}
								ImGui::Text("Pressed buttons: %s", pressedMappedButtons.data());*/

								for (std::uint32_t axisId = 0; axisId < JoyMappedState::NumAxes; axisId++) {
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

		if (RenderDocCapture::numCaptures() > _renderDocLastNumCaptures) {
			std::uint32_t pathLength = 0;
			uint64_t timestamp = 0;
			RenderDocCapture::captureInfo(RenderDocCapture::numCaptures() - 1, _renderDocCapturePath.data(), &pathLength, &timestamp);
			_renderDocCapturePath.setLength(pathLength);
			RenderDocCapture::setCaptureFileComments(_renderDocCapturePath.data(), _renderDocFileComments.data());
			_renderDocLastNumCaptures = RenderDocCapture::numCaptures();
			LOGI("RenderDoc capture {}: {} ({})", RenderDocCapture::numCaptures() - 1, _renderDocCapturePath, timestamp);
		}

		if (ImGui::CollapsingHeader("RenderDoc")) {
			std::int32_t major, minor, patch;
			RenderDocCapture::apiVersion(&major, &minor, &patch);
			ImGui::Text("RenderDoc API: %d.%d.%d", major, minor, patch);
			ImGui::Text("Target control connected: %s", RenderDocCapture::isTargetControlConnected() ? "true" : "false");
			ImGui::Text("Number of captures: %u", RenderDocCapture::numCaptures());
			if (_renderDocCapturePath.isEmpty())
				ImGui::Text("Last capture path: (no capture has been made yet)");
			else
				ImGui::Text("Last capture path: %s", _renderDocCapturePath.data());
			ImGui::Separator();

			_renderDocPathTemplate = RenderDocCapture::captureFilePathTemplate();
			if (ImGui::InputText("File path template", _renderDocPathTemplate.data(), MaxRenderDocPathLength,
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
				inputTextCallback, &_renderDocPathTemplate)) {
				RenderDocCapture::setCaptureFilePathTemplate(_renderDocPathTemplate.data());
			}

			ImGui::InputTextMultiline("File comments", _renderDocFileComments.data(), MaxRenderDocCommentsLength,
									  ImVec2(0, 0), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
									  inputTextCallback, &_renderDocFileComments);

			static bool overlayEnabled = RenderDocCapture::isOverlayEnabled();
			ImGui::Checkbox("Enable overlay", &overlayEnabled);
			RenderDocCapture::enableOverlay(overlayEnabled);

			if (RenderDocCapture::isFrameCapturing())
				ImGui::TextUnformatted("Capturing a frame...");
			else {
				static std::int32_t numFrames = 1;
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

		const std::int32_t tableNumRows = alloc.numEntries() > 32 ? 32 : alloc.numEntries();
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

			for (std::uint32_t i = 0; i < alloc.numEntries(); i++) {
				const nctl::IAllocator::Entry& e = alloc.entry(i);
				const std::uint32_t deallocationIndex = alloc.findDeallocation(i);

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
			_widgetName.format("Default Allocator \"%s\" (%d allocations, %lu bytes)",
							   nctl::theDefaultAllocator().name(), nctl::theDefaultAllocator().numAllocations(), nctl::theDefaultAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
			ImGui::BulletText("%s", _widgetName.data());
#else
			_widgetName.append("###DefaultAllocator");
			if (ImGui::TreeNode(_widgetName.data())) {
				guiAllocator(nctl::theDefaultAllocator());
				ImGui::TreePop();
			}
#endif

			if (&nctl::theStringAllocator() != &nctl::theDefaultAllocator()) {
				_widgetName.format("String Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theStringAllocator().name(), nctl::theStringAllocator().numAllocations(), nctl::theStringAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", _widgetName.data());
#else
				_widgetName.append("###StringAllocator");
				if (ImGui::TreeNode(_widgetName.data())) {
					guiAllocator(nctl::theStringAllocator());
					ImGui::TreePop();
				}
#endif
			} else
				ImGui::TextUnformatted("The string allocator is the default one");

			if (&nctl::theImGuiAllocator() != &nctl::theDefaultAllocator()) {
				_widgetName.format("ImGui Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theImGuiAllocator().name(), nctl::theImGuiAllocator().numAllocations(), nctl::theImGuiAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", _widgetName.data());
#else
				_widgetName.append("###ImGuiAllocator");
				if (ImGui::TreeNode(_widgetName.data())) {
					guiAllocator(nctl::theImGuiAllocator());
					ImGui::TreePop();
				}
#endif
			} else
				ImGui::TextUnformatted("The ImGui allocator is the default one");

#if defined(WITH_LUA)
			if (&nctl::theLuaAllocator() != &nctl::theDefaultAllocator()) {
				_widgetName.format("Lua Allocator \"%s\" (%d allocations, %lu bytes)",
								   nctl::theLuaAllocator().name(), nctl::theLuaAllocator().numAllocations(), nctl::theLuaAllocator().usedMemory());
#if !defined(RECORD_ALLOCATIONS)
				ImGui::BulletText("%s", _widgetName.data());
#else
				_widgetName.append("###LuaAllocator");
				if (ImGui::TreeNode(_widgetName.data())) {
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

	void ImGuiDebugOverlay::guiViewports(Viewport* viewport, std::uint32_t viewportId)
	{
		char widgetName[64];
		std::size_t length = formatInto(widgetName, "#{} Viewport", viewportId);
		/*if (viewport->type() != Viewport::Type::NoTexture)
			_widgetName.formatAppend(" - size: %d x %d", viewport->width(), viewport->height());*/
		/*const Recti viewportRect = viewport->viewportRect();
		_widgetName.formatAppend(" - rect: pos <%d, %d>, size %d x %d", viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
		const Rectf cullingRect = viewport->cullingRect();
		_widgetName.formatAppend(" - culling: pos <%.2f, %.2f>, size %.2f x %.2f", cullingRect.x, cullingRect.y, cullingRect.w, cullingRect.h);
		_widgetName.formatAppend("###0x%x", uintptr_t(viewport));*/
		widgetName[length] = '\0';

		SceneNode* rootNode = viewport->GetRootNode();
		if (rootNode != nullptr && ImGui::TreeNode(widgetName)) {
			if (viewport->GetCamera() != nullptr) {
				const Camera::ViewValues& viewValues = viewport->GetCamera()->GetViewValues();
				ImGui::Text("Camera view - position: <%.2f, %.2f>, rotation: %.2f, scale: %.2f", viewValues.position.X, viewValues.position.Y, viewValues.rotation, viewValues.scale);
				const Camera::ProjectionValues& projValues = viewport->GetCamera()->GetProjectionValues();
				ImGui::Text("Camera projection - left: %.2f, right: %.2f, top: %.2f, bottom: %.2f", projValues.left, projValues.right, projValues.top, projValues.bottom);
			}

			guiRecursiveChildrenNodes(rootNode, 0);
			ImGui::TreePop();
		}
	}

	void ImGuiDebugOverlay::guiRecursiveChildrenNodes(SceneNode* node, std::uint32_t childId)
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

		_widgetName.format("#%u ", childId);
		if (node->name() != nullptr)
			_widgetName.formatAppend("\"%s\" ", node->name());
		_widgetName.formatAppend("%s", nodeTypeToString(node->type()));
		const std::uint32_t numChildren = node->children().size();
		if (numChildren > 0)
			_widgetName.formatAppend(" (%u children)", node->children().size());
		_widgetName.formatAppend(" - position: %.1f x %.1f", node->position().x, node->position().y);
		if (drawable) {
			_widgetName.formatAppend(" - size: %.1f x %.1f", drawable->width(), drawable->height());
			if (drawable->isDrawEnabled() && drawable->lastFrameRendered() < theApplication().numFrames() - 1)
				_widgetName.append(" - culled");
		}
		_widgetName.formatAppend("###0x%x", uintptr_t(node));

		if (ImGui::TreeNode(_widgetName.data())) {
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
				std::int32_t layer = drawable->layer();
				ImGui::PushItemWidth(100.0f);
				ImGui::InputInt("Layer", &layer);
				ImGui::PopItemWidth();
				if (layer < 0)
					layer = 0;
				else if (layer > 0xffff)
					layer = 0xffff;
				drawable->SetLayer(static_cast<uint16_t>(layer));

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
				std::int32_t minX = texRect.x;
				std::int32_t maxX = minX + texRect.w;
				ImGui::DragIntRange2("Rect X", &minX, &maxX, 1.0f, 0, tex->width());

				std::int32_t minY = texRect.y;
				std::int32_t maxY = minY + texRect.h;
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
				_widgetName.format("%u / %u", particleSys->numAliveParticles(), particleSys->numParticles());
				ImGui::ProgressBar(aliveFraction, ImVec2(0.0f, 0.0f), _widgetName.data());
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
					for (std::uint32_t i = 0; i < children.size(); i++)
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
			guiViewports(&theApplication().GetScreenViewport(), 0);
			for (std::uint32_t i = 0; i < Viewport::GetChain().size(); i++)
				guiViewports(Viewport::GetChain()[i], i + 1);
		}
	}

#if defined(NCINE_PROFILING)
	void ImGuiDebugOverlay::guiTopLeft()
	{
		if (!_showTopLeftOverlay) {
			return;
		}

		const RenderStatistics::VaoPool& vaoPool = RenderStatistics::GetVaoPool();
		const RenderStatistics::CommandPool& commandPool = RenderStatistics::GetCommandPool();
		const RenderStatistics::Textures& textures = RenderStatistics::GetTextures();
		const RenderStatistics::CustomBuffers& customVbos = RenderStatistics::GetCustomVBOs();
		const RenderStatistics::CustomBuffers& customIbos = RenderStatistics::GetCustomIBOs();
		const RenderStatistics::Buffers& vboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::Array);
		const RenderStatistics::Buffers& iboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::ElementArray);
		const RenderStatistics::Buffers& uboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::Uniform);

		const ImVec2 windowPos = ImVec2(Margin, Margin);
		const ImVec2 windowPosPivot = ImVec2(0.0f, 0.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;	
#if defined(IMGUI_HAS_DOCK)
		windowFlags |= ImGuiWindowFlags_NoDocking;
#endif
		if (_lockOverlayPositions)
			windowFlags |= ImGuiWindowFlags_NoMove;

		ImGui::Begin("Top-Left Panel", nullptr, windowFlags);

		ImGui::Text("Culled nodes: %u", RenderStatistics::GetCulled());
		if (_plotOverlayValues) {
			ImGui::SameLine(180.0f);
			ImGui::PlotLines("##1", _plotValues[ValuesType::CulledNodes].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
		}

		ImGui::Text("%u/%u VAOs (%u reuses, %u bindings)", vaoPool.size, vaoPool.capacity, vaoPool.reuses, vaoPool.bindings);
		ImGui::Text("%u/%u RenderCommands in the pool (%u retrievals)", commandPool.usedSize, commandPool.usedSize + commandPool.freeSize, commandPool.retrievals);
		if (textures.dataSize > 2 * 1024 * 1024) {
			ImGui::Text("%.2f MB in %u Texture(s)", textures.dataSize / (1024.0f * 1024.0f), textures.count);
		} else {
			ImGui::Text("%.2f kB in %u Texture(s)", textures.dataSize / 1024.0f, textures.count);
		}
		ImGui::Text("%.2f kB in %u custom VBO(s)", customVbos.dataSize / 1024.0f, customVbos.count);
		ImGui::Text("%.2f kB in %u custom IBO(s)", customIbos.dataSize / 1024.0f, customIbos.count);
		ImGui::Text("%.2f/%lu kB in %u VBO(s)", vboBuffers.usedSpace / 1024.0f, vboBuffers.size / 1024, vboBuffers.count);
		if (_plotOverlayValues) {
			ImGui::SameLine(180.0f);
			ImGui::PlotLines("##2", _plotValues[ValuesType::VboUsed].get(), _numValues, _index, nullptr, 0.0f, vboBuffers.size / 1024.0f);
		}

		ImGui::Text("%.2f/%lu kB in %u IBO(s)", iboBuffers.usedSpace / 1024.0f, iboBuffers.size / 1024, iboBuffers.count);
		if (_plotOverlayValues) {
			ImGui::SameLine(180.0f);
			ImGui::PlotLines("##3", _plotValues[ValuesType::IboUsed].get(), _numValues, _index, nullptr, 0.0f, iboBuffers.size / 1024.0f);
		}

		ImGui::Text("%.2f/%lu kB in %u UBO(s)", uboBuffers.usedSpace / 1024.0f, uboBuffers.size / 1024, uboBuffers.count);
		if (_plotOverlayValues) {
			ImGui::SameLine(180.0f);
			ImGui::PlotLines("##4", _plotValues[ValuesType::UboUsed].get(), _numValues, _index, nullptr, 0.0f, uboBuffers.size / 1024.0f);
		}

		ImGui::Text("Viewport chain length: %u", Viewport::GetChain().size());

		ImGui::End();
	}
#endif

	void ImGuiDebugOverlay::guiTopRight()
	{
		if (!_showTopRightOverlay) {
			return;
		}

		const ImVec2 windowPos = ImVec2(ImGui::GetIO().DisplaySize.x - Margin, Margin);
		const ImVec2 windowPosPivot = ImVec2(1.0f, 0.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
#if defined(IMGUI_HAS_DOCK)
		windowFlags |= ImGuiWindowFlags_NoDocking;
#endif
		if (_lockOverlayPositions)
			windowFlags |= ImGuiWindowFlags_NoMove;

		ImGui::Begin("Top-Right Panel", nullptr, windowFlags);

		ImGui::Text("FPS: %.0f (%.2f ms - %.2fx)", theApplication().GetFrameTimer().GetAverageFps(), theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f, theApplication().GetTimeMult());
		ImGui::Text("Frame Count: %lu", theApplication().GetFrameCount());

#if defined(NCINE_PROFILING)
		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		if (appCfg.withScenegraph) {
			const RenderStatistics::Commands& spriteCommands = RenderStatistics::GetCommands(RenderCommand::Type::Sprite);
			const RenderStatistics::Commands& meshspriteCommands = RenderStatistics::GetCommands(RenderCommand::Type::MeshSprite);
			const RenderStatistics::Commands& tileMapCommands = RenderStatistics::GetCommands(RenderCommand::Type::TileMap);
			const RenderStatistics::Commands& particleCommands = RenderStatistics::GetCommands(RenderCommand::Type::Particle);
			const RenderStatistics::Commands& lightingCommands = RenderStatistics::GetCommands(RenderCommand::Type::Lighting);
			const RenderStatistics::Commands& textCommands = RenderStatistics::GetCommands(RenderCommand::Type::Text);
			const RenderStatistics::Commands& imguiCommands = RenderStatistics::GetCommands(RenderCommand::Type::ImGui);
			const RenderStatistics::Commands& unspecifiedCommands = RenderStatistics::GetCommands(RenderCommand::Type::Unspecified);
			const RenderStatistics::Commands& allCommands = RenderStatistics::GetAllCommands();

			ImGui::Separator();
			ImGui::Text("Sprites: %uV, %uDC (%u Tr), %uI/%uB", spriteCommands.vertices, spriteCommands.commands, spriteCommands.transparents, spriteCommands.instances, spriteCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##1", _plotValues[ValuesType::SpriteVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Mesh Sprites: %uV, %uDC (%u Tr), %uI/%uB", meshspriteCommands.vertices, meshspriteCommands.commands, meshspriteCommands.transparents, meshspriteCommands.instances, meshspriteCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##2", _plotValues[ValuesType::MeshSpriteVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Tile Map: %uV, %uDC (%u Tr), %uI/%uB\n", tileMapCommands.vertices, tileMapCommands.commands, tileMapCommands.transparents, tileMapCommands.instances, tileMapCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##3", _plotValues[ValuesType::TileMapVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Particles: %uV, %uDC (%u Tr), %uI/%uB\n", particleCommands.vertices, particleCommands.commands, particleCommands.transparents, particleCommands.instances, particleCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##4", _plotValues[ValuesType::ParticleVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Lighting: %uV, %uDC (%u Tr), %uI/%uB\n", lightingCommands.vertices, lightingCommands.commands, lightingCommands.transparents, lightingCommands.instances, lightingCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##5", _plotValues[ValuesType::LightingVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Text: %uV, %uDC (%u Tr), %uI/%uB", textCommands.vertices, textCommands.commands, textCommands.transparents, textCommands.instances, textCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##6", _plotValues[ValuesType::TextVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("ImGui: %uV, %uDC (%u Tr), %uI/%u", imguiCommands.vertices, imguiCommands.commands, imguiCommands.transparents, imguiCommands.instances, imguiCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##7", _plotValues[ValuesType::ImGuiVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Unspecified: %uV, %uDC (%u Tr), %uI/%u", unspecifiedCommands.vertices, unspecifiedCommands.commands, unspecifiedCommands.transparents, unspecifiedCommands.instances, unspecifiedCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##8", _plotValues[ValuesType::UnspecifiedVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Separator();
			ImGui::Text("Total: %uV, %uDC (%u Tr), %uI/%uB", allCommands.vertices, allCommands.commands, allCommands.transparents, allCommands.instances, allCommands.batchSize);
			if (_plotOverlayValues) {
				ImGui::SameLine(230.0f);
				ImGui::PlotLines("##9", _plotValues[ValuesType::TotalVertices].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
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
		if (_lockOverlayPositions)
			windowFlags |= ImGuiWindowFlags_NoMove;
		if (_showBottomLeftOverlay) {
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
		if (_lockOverlayPositions)
			windowFlags |= ImGuiWindowFlags_NoMove;
		if (_showBottomRightOverlay) {
			ImGui::Begin("###Bottom-Right", nullptr, windowFlags);

			ImGui::Text("%u Lua state(s) with %u tracked userdata", LuaStatistics::numRegistered(), LuaStatistics::numTrackedUserDatas());
			ImGui::Text("Used memory: %zu Kb", LuaStatistics::usedMemory() / 1024);
			if (_plotOverlayValues) {
				ImGui::SameLine();
				ImGui::PlotLines("", _plotValues[ValuesType::LuaUsed].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
			}

			ImGui::Text("Operations: %d ops/s", LuaStatistics::operations());
			if (_plotOverlayValues) {
				ImGui::SameLine();
				ImGui::PlotLines("", _plotValues[ValuesType::LuaOperations].get(), _numValues, _index, nullptr, 0.0f, FLT_MAX);
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
		const float appWidth = theApplication().GetWidth();

		const ImVec2 windowPos = ImVec2(appWidth * 0.5f, ImGui::GetIO().DisplaySize.y - Margin);
		const ImVec2 windowPosPivot = ImVec2(0.5f, 1.0f);
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::SetNextWindowBgAlpha(Transparency);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
#if defined(IMGUI_HAS_DOCK)
		windowFlags |= ImGuiWindowFlags_NoDocking;
#endif
		if (_lockOverlayPositions)
			windowFlags |= ImGuiWindowFlags_NoMove;
		ImGui::Begin("Plots", nullptr, windowFlags);

		ImGui::PlotLines("Frame time", _plotValues[ValuesType::FrameTime].get(), _numValues, _index, nullptr, 0.0f, _maxFrameTime, ImVec2(appWidth * 0.2f, 0.0f));

#if defined(NCINE_PROFILING)
		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		if (appCfg.withScenegraph) {
			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::PlotLines("Update", _plotValues[ValuesType::Update].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.6f, 1.0f, 0.2f, 1.0f));
			ImGui::PlotLines("Visit", _plotValues[ValuesType::Visit].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
			ImGui::PlotLines("Draw", _plotValues[ValuesType::Draw].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			ImGui::PlotLines("Aggregated", _plotValues[ValuesType::UpdateVisitDraw].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PopStyleColor();
		}

		if (_plotAdditionalFrameValues) {
			ImGui::Separator();
			ImGui::PlotLines("OnFrameStart", _plotValues[ValuesType::BeginFrame].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			if (appCfg.withScenegraph)
				ImGui::PlotLines("OnPostUpdate", _plotValues[ValuesType::PostUpdate].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PlotLines("OnFrameEnd", _plotValues[ValuesType::EndFrame].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
			ImGui::PlotLines("ImGui", _plotValues[ValuesType::ImGui].get(), _numValues, _index, nullptr, 0.0f, _maxUpdateVisitDraw, ImVec2(appWidth * 0.2f, 0.0f));
		}
#endif

		ImGui::End();
	}

	void ImGuiDebugOverlay::InitPlotValues()
	{
		for (std::uint32_t type = 0; type < ValuesType::Count; type++) {
			_plotValues[type] = std::make_unique<float[]>(_numValues);

			for (std::uint32_t i = _index; i < _numValues; i++) {
				_plotValues[type][i] = 0.0f;
			}
		}
	}

#if defined(NCINE_PROFILING)
	void ImGuiDebugOverlay::UpdateOverlayTimings()
	{
		const RenderStatistics::Buffers& vboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::Array);
		const RenderStatistics::Buffers& iboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::ElementArray);
		const RenderStatistics::Buffers& uboBuffers = RenderStatistics::GetBuffers(RenderBuffersManager::BufferTypes::Uniform);

		const RenderStatistics::Commands& spriteCommands = RenderStatistics::GetCommands(RenderCommand::Type::Sprite);
		const RenderStatistics::Commands& meshspriteCommands = RenderStatistics::GetCommands(RenderCommand::Type::MeshSprite);
		const RenderStatistics::Commands& tileMapCommands = RenderStatistics::GetCommands(RenderCommand::Type::TileMap);
		const RenderStatistics::Commands& particleCommands = RenderStatistics::GetCommands(RenderCommand::Type::Particle);
		const RenderStatistics::Commands& lightingCommands = RenderStatistics::GetCommands(RenderCommand::Type::Lighting);
		const RenderStatistics::Commands& textCommands = RenderStatistics::GetCommands(RenderCommand::Type::Text);
		const RenderStatistics::Commands& imguiCommands = RenderStatistics::GetCommands(RenderCommand::Type::ImGui);
		const RenderStatistics::Commands& unspecifiedCommands = RenderStatistics::GetCommands(RenderCommand::Type::Unspecified);
		const RenderStatistics::Commands& allCommands = RenderStatistics::GetAllCommands();

		_plotValues[ValuesType::CulledNodes][_index] = static_cast<float>(RenderStatistics::GetCulled());
		_plotValues[ValuesType::VboUsed][_index] = vboBuffers.usedSpace / 1024.0f;
		_plotValues[ValuesType::IboUsed][_index] = iboBuffers.usedSpace / 1024.0f;
		_plotValues[ValuesType::UboUsed][_index] = uboBuffers.usedSpace / 1024.0f;

		_plotValues[ValuesType::SpriteVertices][_index] = static_cast<float>(spriteCommands.vertices);
		_plotValues[ValuesType::MeshSpriteVertices][_index] = static_cast<float>(meshspriteCommands.vertices);
		_plotValues[ValuesType::TileMapVertices][_index] = static_cast<float>(tileMapCommands.vertices);
		_plotValues[ValuesType::ParticleVertices][_index] = static_cast<float>(particleCommands.vertices);
		_plotValues[ValuesType::LightingVertices][_index] = static_cast<float>(lightingCommands.vertices);
		_plotValues[ValuesType::TextVertices][_index] = static_cast<float>(textCommands.vertices);
		_plotValues[ValuesType::ImGuiVertices][_index] = static_cast<float>(imguiCommands.vertices);
		_plotValues[ValuesType::UnspecifiedVertices][_index] = static_cast<float>(unspecifiedCommands.vertices);
		_plotValues[ValuesType::TotalVertices][_index] = static_cast<float>(allCommands.vertices);

#	if defined(WITH_LUA)
		_plotValues[ValuesType::LuaUsed][_index] = LuaStatistics::usedMemory() / 1024.0f;
		_plotValues[ValuesType::LuaOperations][_index] = static_cast<float>(LuaStatistics::operations());
#	endif
	}
#endif
}

#endif