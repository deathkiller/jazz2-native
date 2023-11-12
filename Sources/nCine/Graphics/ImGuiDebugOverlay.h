#pragma once

#if defined(WITH_IMGUI)

#include "IDebugOverlay.h"

#include <memory>

#include <Containers/String.h>

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

		void update() override;
		void updateFrameTimings() override;

	private:
		const float Margin = 10.0f;
		const float Transparency = 0.5f;

		struct ValuesType
		{
			enum
			{
				FRAME_TIME = 0,
				FRAME_START,
				UPDATE_VISIT_DRAW,
				UPDATE,
				POST_UPDATE,
				VISIT,
				DRAW,
				IMGUI,
				FRAME_END,
				CULLED_NODES,
				VBO_USED,
				IBO_USED,
				UBO_USED,
				SPRITE_VERTICES,
				MESHSPRITE_VERTICES,
				PARTICLE_VERTICES,
				TEXT_VERTICES,
				IMGUI_VERTICES,
				TOTAL_VERTICES,
#if defined(WITH_LUA)
				LUA_USED,
				LUA_OPERATIONS,
#endif
				COUNT
			};
		};

		bool disableAppInputEvents_;
		IInputEventHandler* appInputHandler_;
		bool lockOverlayPositions_;
		bool showTopLeftOverlay_;
		bool showTopRightOverlay_;
		bool showBottomLeftOverlay_;
		bool showBottomRightOverlay_;

		unsigned int numValues_;
		std::unique_ptr<float[]> plotValues_[ValuesType::COUNT];
		float maxFrameTime_;
		float maxUpdateVisitDraw_;
		unsigned int index_;
		bool plotAdditionalFrameValues_;
		bool plotOverlayValues_;
		String comboVideoModes_;

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

		void guiTopLeft();
		void guiTopRight();
		void guiBottomLeft();
		void guiBottomRight();
		void guiPlots();

		void initPlotValues();
		void updateOverlayTimings();

		/// Deleted copy constructor
		ImGuiDebugOverlay(const ImGuiDebugOverlay&) = delete;
		/// Deleted assignment operator
		ImGuiDebugOverlay& operator=(const ImGuiDebugOverlay&) = delete;
	};
}

#endif
