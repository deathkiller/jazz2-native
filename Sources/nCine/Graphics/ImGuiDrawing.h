#pragma once

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"
#include "RHI/RhiFwd.h"
#include "../Base/HashMap.h"
#include "../Primitives/Matrix4x4.h"

#include <memory>

#include <Containers/SmallVector.h>

#include <imgui.h>

using namespace Death::Containers;

struct ImTextureData;

namespace nCine
{
	class RenderCommand;
	class RenderQueue;
	class IInputEventHandler;

	/**
		@brief Handles ImGui drawing
		
		Bridges the ImGui debug overlay to the engine renderer. Owns the ImGui shader program, vertex and
		index buffers and the GPU textures backing the ImGui font and user atlases. Depending on the mode
		selected at construction, it either submits @ref RenderCommand instances through the scene-graph
		@ref RenderQueue or issues the OpenGL draw calls directly.
	*/
	class ImGuiDrawing
	{
	public:
		explicit ImGuiDrawing(bool withSceneGraph);
		~ImGuiDrawing();

		/**
		 * @brief Begins a new ImGui frame
		 *
		 * Forwards input from the active backend and updates the projection matrix when the display size changes.
		 */
		void NewFrame();
		/**
		 * @brief Ends the frame and submits ImGui draw data as render commands
		 *
		 * @param renderQueue  Queue that receives the generated render commands
		 */
		void EndFrame(RenderQueue& renderQueue);
		/**
		 * @brief Ends the frame and draws ImGui directly with OpenGL
		 */
		void EndFrame();

	private:
		bool _withSceneGraph;
		HashMap<RHI::Texture*, std::unique_ptr<RHI::Texture>> _textures;
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN) || !defined(WITH_RHI_GL)
		// Sub-rect updates are repacked into a contiguous buffer where GL_UNPACK_ROW_LENGTH is unavailable
		// (OpenGL ES / WebGL) or the backend is not OpenGL at all
		SmallVector<char, 0> _tempTexBuffer;
#endif
		std::unique_ptr<RHI::ShaderProgram> _imguiShaderProgram;
		std::unique_ptr<RHI::Buffer> _vbo;
		std::unique_ptr<RHI::Buffer> _ibo;

		static const std::int32_t UniformsBufferSize = 65;
		std::uint8_t _uniformsBuffer[UniformsBufferSize];
		std::unique_ptr<RHI::ShaderUniforms> _imguiShaderUniforms;
		IInputEventHandler* _appInputHandler;

		std::int32_t _lastFrameWidth;
		std::int32_t _lastFrameHeight;
		Matrix4x4f _projectionMatrix;
		std::uint16_t _lastLayerValue;

#if defined(IMGUI_HAS_VIEWPORT) && defined(WITH_RHI_GL)
		std::int32_t _attribLocationTex;
		std::int32_t _attribLocationProjMtx;
		std::uint32_t _attribLocationVtxPos;
		std::uint32_t _attribLocationVtxUV;
		std::uint32_t _attribLocationVtxColor;
		std::uint32_t _vboHandle;
		std::uint32_t _elementsHandle;
#endif

		void DestroyTexture(ImTextureData* tex);
		void UpdateTexture(ImTextureData* tex);
		RenderCommand* RetrieveCommandFromPool();
		void SetupRenderCommand(RenderCommand& cmd);
		void Draw(RenderQueue& renderQueue);

		void SetupBuffersAndShader();
		void Draw();
		/**
		 * @brief Draws one ImGui draw list directly (the main window's, or a multi-viewport platform window's)
		 *
		 * The geometry buffers are a parameter because each platform window must own a pair: the draws are only
		 * recorded here, and a backend that executes them later would find a shared buffer holding whichever
		 * window's geometry was uploaded last.
		 */
		void DrawData(ImDrawData* drawData, RHI::Buffer* vbo, RHI::Buffer* ibo);

		// Multi-viewport platform windows need a renderer that can draw into a window of its own, which the
		// OpenGL and Direct3D 11 backends provide in fundamentally different ways: GL renders with raw GL calls
		// into per-window GL contexts (the RHI wrappers must not touch those - their bind caches describe the
		// main context), D3D11 redirects the one device into a per-window swap chain and reuses the RHI path.
#if defined(IMGUI_HAS_VIEWPORT) && defined(WITH_RHI_GL)
		void PrepareForViewports();
		static void OnRenderPlatformWindow(ImGuiViewport* viewport, void*);
		void DrawPlatformWindow(ImGuiViewport* viewport);
		void SetupRenderStateForPlatformWindow(ImDrawData* drawData, std::int32_t fbWidth, std::int32_t fbHeight, std::uint32_t vertexArrayObject);
#elif defined(IMGUI_HAS_VIEWPORT) && defined(WITH_RHI_D3D11)
		void PrepareForViewports();
		static void OnCreatePlatformWindow(ImGuiViewport* viewport);
		static void OnDestroyPlatformWindow(ImGuiViewport* viewport);
		static void OnResizePlatformWindow(ImGuiViewport* viewport, ImVec2 size);
		static void OnRenderPlatformWindow(ImGuiViewport* viewport, void*);
		static void OnSwapPlatformWindowBuffers(ImGuiViewport* viewport, void*);
		void DrawPlatformWindow(ImGuiViewport* viewport);
#elif defined(IMGUI_HAS_VIEWPORT) && defined(WITH_RHI_VULKAN) && (defined(WITH_SDL2) || defined(WITH_SDL3))
		// No swap-buffers hook here: every window is presented from the device's PresentFrame(), together with the
		// main one, because they all share the frame's single command buffer and submit
		void PrepareForViewports();
		static void OnCreatePlatformWindow(ImGuiViewport* viewport);
		static void OnDestroyPlatformWindow(ImGuiViewport* viewport);
		static void OnResizePlatformWindow(ImGuiViewport* viewport, ImVec2 size);
		static void OnRenderPlatformWindow(ImGuiViewport* viewport, void*);
		void DrawPlatformWindow(ImGuiViewport* viewport);
#endif
	};
}

#endif
