#pragma once

#if defined(WITH_IMGUI)

#include "IDebugOverlay.h"

#include <memory>

#include <Containers/String.h>
#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class AppConfiguration;
	class IInputEventHandler;
	class Viewport;
	class SceneNode;

	/// Overlay debug ImGui interface
	class ImGuiDebugOverlay : public IDebugOverlay
	{
	public:
		explicit ImGuiDebugOverlay(float profileTextUpdateTime);

		ImGuiDebugOverlay(const ImGuiDebugOverlay&) = delete;
		ImGuiDebugOverlay& operator=(const ImGuiDebugOverlay&) = delete;

		void update() override;
		void updateFrameTimings() override;

#if defined(DEATH_TRACE)
		void log(TraceLevel level, StringView time, std::uint32_t threadId, StringView message) override;
#endif

	private:
		static constexpr float Margin = 10.0f;
		static constexpr float Transparency = 0.5f;

		struct ValuesType
		{
			enum
			{
				FrameTime = 0,
				BeginFrame,
				UpdateVisitDraw,
				Update,
				PostUpdate,
				Visit,
				Draw,
				ImGui,
				EndFrame,
				CulledNodes,
				VboUsed,
				IboUsed,
				UboUsed,
				SpriteVertices,
				MeshSpriteVertices,
				TileMapVertices,
				ParticleVertices,
				LightingVertices,
				TextVertices,
				ImGuiVertices,
				UnspecifiedVertices,
				TotalVertices,
#if defined(WITH_LUA)
				LuaUsed,
				LuaOperations,
#endif
				Count
			};
		};

		struct LogMessage
		{
			String Time;
			String Text;
			std::uint32_t ThreadId;
			TraceLevel Level;
		};

		bool lockOverlayPositions_;
		bool showTopLeftOverlay_;
		bool showTopRightOverlay_;
		bool showBottomLeftOverlay_;
		bool showBottomRightOverlay_;

		unsigned int numValues_;
		std::unique_ptr<float[]> plotValues_[ValuesType::Count];
		float maxFrameTime_;
		float maxUpdateVisitDraw_;
		unsigned int index_;
		bool plotAdditionalFrameValues_;
		bool plotOverlayValues_;
		String comboVideoModes_;

		SmallVector<LogMessage, 0> logBuffer_;

#if defined(WITH_RENDERDOC)
		static constexpr unsigned int MaxRenderDocPathLength = 128;
		static constexpr unsigned int MaxRenderDocCommentsLength = 512;

		String renderDocPathTemplate_;
		String renderDocFileComments_;
		String renderDocCapturePath_;
		unsigned int renderDocLastNumCaptures_;
#endif

		void guiWindow();
		void guiConfigureGui();
		void guiInitTimes();
		void guiLog();
		void guiGraphicsCapabilities();
		void guiApplicationConfiguration();
		void guiRenderingSettings();
		void guiWindowSettings();
		void guiAudioPlayers();
		void guiInputState();
		void guiRenderDoc();
		void guiAllocators();
		void guiViewports(Viewport* viewport, unsigned int viewportId);
		void guiRecursiveChildrenNodes(SceneNode* node, unsigned int childId);
		void guiNodeInspector();

#if defined(NCINE_PROFILING)
		void guiTopLeft();
#endif
		void guiTopRight();
		void guiBottomLeft();
		void guiBottomRight();
		void guiPlots();

		void initPlotValues();
#if defined(NCINE_PROFILING)
		void updateOverlayTimings();
#endif
	};
}

#endif
