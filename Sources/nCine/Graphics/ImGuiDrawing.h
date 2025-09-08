#pragma once

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"
#include "../Base/HashMap.h"
#include "../Primitives/Matrix4x4.h"

#include <memory>

#include <imgui.h>

struct ImTextureData;

namespace nCine
{
	class GLTexture;
	class GLShaderProgram;
	class GLShaderUniforms;
	class GLBufferObject;
	class RenderCommand;
	class RenderQueue;
	class IInputEventHandler;

	/// Handles ImGui drawing
	class ImGuiDrawing
	{
	public:
		explicit ImGuiDrawing(bool withSceneGraph);
		~ImGuiDrawing();

		void NewFrame();
		/// Renders ImGui with render commands
		void EndFrame(RenderQueue& renderQueue);
		/// Renders ImGui directly with OpenGL
		void EndFrame();

	private:
		bool withSceneGraph_;
		HashMap<GLTexture*, std::unique_ptr<GLTexture>> textures_;
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		SmallVector<char, 0> tempTexBuffer_;
#endif
		std::unique_ptr<GLShaderProgram> imguiShaderProgram_;
		std::unique_ptr<GLBufferObject> vbo_;
		std::unique_ptr<GLBufferObject> ibo_;

		static const std::int32_t UniformsBufferSize = 65;
		std::uint8_t uniformsBuffer_[UniformsBufferSize];
		std::unique_ptr<GLShaderUniforms> imguiShaderUniforms_;
		IInputEventHandler* appInputHandler_;

		std::int32_t lastFrameWidth_;
		std::int32_t lastFrameHeight_;
		Matrix4x4f projectionMatrix_;
		std::uint16_t lastLayerValue_;

#if defined(IMGUI_HAS_VIEWPORT)
		std::int32_t attribLocationTex_;
		std::int32_t attribLocationProjMtx_;
		std::uint32_t attribLocationVtxPos_;
		std::uint32_t attribLocationVtxUV_;
		std::uint32_t attribLocationVtxColor_;
		std::uint32_t vboHandle_;
		std::uint32_t elementsHandle_;
#endif

		void DestroyTexture(ImTextureData* tex);
		void UpdateTexture(ImTextureData* tex);
		RenderCommand* RetrieveCommandFromPool();
		void SetupRenderCommand(RenderCommand& cmd);
		void Draw(RenderQueue& renderQueue);

		void SetupBuffersAndShader();
		void Draw();

#if defined(IMGUI_HAS_VIEWPORT)
		void PrepareForViewports();
		static void OnRenderPlatformWindow(ImGuiViewport* viewport, void*);
		void DrawPlatformWindow(ImGuiViewport* viewport);
		void SetupRenderStateForPlatformWindow(ImDrawData* drawData, std::int32_t fbWidth, std::int32_t fbHeight, std::uint32_t vertexArrayObject);
#endif
	};
}

#endif
