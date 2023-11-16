#pragma once

#if defined(WITH_IMGUI)

#include "../../Common.h"
#include "../Primitives/Matrix4x4.h"

#include <memory>

namespace nCine
{
	class GLTexture;
	class GLShaderProgram;
	class GLShaderUniforms;
	class GLBufferObject;
	class RenderCommand;
	class RenderQueue;
	class IInputEventHandler;

	/// The class the handles ImGui drawing
	class ImGuiDrawing
	{
	public:
		explicit ImGuiDrawing(bool withSceneGraph);
		~ImGuiDrawing();

		/// Builds the ImGui font atlas and uploads it to a texture
		bool buildFonts();

		void newFrame();
		/// Renders ImGui with render commands
		void endFrame(RenderQueue& renderQueue);
		/// Renders ImGui directly with OpenGL
		void endFrame();

	private:
		bool withSceneGraph_;
		std::unique_ptr<GLTexture> texture_;
		std::unique_ptr<GLShaderProgram> imguiShaderProgram_;

		std::unique_ptr<GLBufferObject> vbo_;
		std::unique_ptr<GLBufferObject> ibo_;

		static const int UniformsBufferSize = 65;
		unsigned char uniformsBuffer_[UniformsBufferSize];
		std::unique_ptr<GLShaderUniforms> imguiShaderUniforms_;
		IInputEventHandler* appInputHandler_;

		int lastFrameWidth_;
		int lastFrameHeight_;
		Matrix4x4f projectionMatrix_;
		uint16_t lastLayerValue_;

		RenderCommand* retrieveCommandFromPool();
		void setupRenderCmd(RenderCommand& cmd);
		void draw(RenderQueue& renderQueue);

		void setupBuffersAndShader();
		void draw();
	};
}

#endif
