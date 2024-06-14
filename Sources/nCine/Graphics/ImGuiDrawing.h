#pragma once

#if defined(WITH_IMGUI)

#include "../../Common.h"
#include "../Primitives/Matrix4x4.h"

#include <memory>

// TODO: This include should be probably removed
#include <glew.h>

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



		GLuint          GlVersion = 0;               // Extracted at runtime using GL_MAJOR_VERSION, GL_MINOR_VERSION queries (e.g. 320 for GL 3.2)
		bool            GlProfileIsES2 = false;
		bool            GlProfileIsES3 = false;
		bool            GlProfileIsCompat = false;
		GLint           GlProfileMask = 0;
		GLint           AttribLocationTex = 0;       // Uniforms location
		GLint           AttribLocationProjMtx = 0;
		GLuint          AttribLocationVtxPos = 0;    // Vertex attributes location
		GLuint          AttribLocationVtxUV = 0;
		GLuint          AttribLocationVtxColor = 0;
		unsigned int    VboHandle = 0, ElementsHandle = 0;
		GLsizeiptr      VertexBufferSize = 0;
		GLsizeiptr      IndexBufferSize = 0;
		bool            HasPolygonMode = false;
		bool            HasClipOrigin = false;
		bool            UseBufferSubData = false;


	// TODO: Temporary solution
	//private:
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
