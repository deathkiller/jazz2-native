#if defined(WITH_IMGUI)

#include "ImGuiDrawing.h"
#include "RenderQueue.h"
#include "RenderCommandPool.h"
#include "RenderResources.h"
#include "GL/GLTexture.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLScissorTest.h"
#include "GL/GLBlending.h"
#include "GL/GLDepthTest.h"
#include "GL/GLCullFace.h"
#include "../Application.h"
#include "../Input/IInputManager.h"

#include <imgui.h>

#include <IO/FileSystem.h>

#if defined(WITH_GLFW)
#	include "../Backends/ImGuiGlfwInput.h"
#elif defined(WITH_SDL)
#	include "../Backends/ImGuiSdlInput.h"
#elif defined(WITH_QT5)
#	include "../Backends/ImGuiQt5Input.h"
#elif defined(DEATH_TARGET_ANDROID)
#	include "../Backends/Android/ImGuiAndroidInput.h"
#endif

#if defined(WITH_EMBEDDED_SHADERS)
#	include "shader_strings.h"
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	ImGuiDrawing::ImGuiDrawing(bool withSceneGraph)
		: withSceneGraph_(withSceneGraph), appInputHandler_(nullptr), lastFrameWidth_(0), lastFrameHeight_(0), lastLayerValue_(0)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		io.BackendRendererName = "nCine_OpenGL_ES";
#else
		io.BackendRendererName = "nCine_OpenGL";
#endif

/*#if defined(DEATH_TARGET_ANDROID)
		static char iniFilename[512];
		nctl::strncpy(iniFilename, fs::joinPath(fs::savePath(), "imgui.ini").data(), 512);
		io.IniFilename = iniFilename;
#endif*/

#if !(defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) && !defined(DEATH_TARGET_EMSCRIPTEN)
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
#endif

		imguiShaderProgram_ = std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate);
#if !defined(WITH_EMBEDDED_SHADERS)
		imguiShaderProgram_->attachShaderFromFile(GL_VERTEX_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_vs.glsl"_s }));
		imguiShaderProgram_->attachShaderFromFile(GL_FRAGMENT_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_fs.glsl"_s }));
#else
		imguiShaderProgram_->attachShaderFromString(GL_VERTEX_SHADER, ShaderStrings::imgui_vs);
		imguiShaderProgram_->attachShaderFromString(GL_FRAGMENT_SHADER, ShaderStrings::imgui_fs);
#endif
		imguiShaderProgram_->link(GLShaderProgram::Introspection::Enabled);
		FATAL_ASSERT(imguiShaderProgram_->status() != GLShaderProgram::Status::LinkingFailed);

		if (!withSceneGraph) {
			setupBuffersAndShader();
		}

		// Set custom style
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.FrameBorderSize = 1.0f;

		// Try to use system font
#if defined(DEATH_TARGET_WINDOWS)
		String systemFont = fs::CombinePath(fs::GetWindowsDirectory(), "Fonts\\SegoeUI.ttf");
		if (fs::FileExists(systemFont)) {
			// Include the most of european latin characters
			static const ImWchar ranges[] = { 0x0020, 0x017E, 0 };
			io.Fonts->AddFontFromFileTTF(systemFont.data(), 17.0f, nullptr, ranges);
		}
#endif

		// TODO: Add these lines to "ImGui::NavUpdateCancelRequest()"
		/*
			if (g.NavWindow && ((g.NavWindow->Flags & ImGuiWindowFlags_Popup) || !(g.NavWindow->Flags & ImGuiWindowFlags_ChildWindow))) {
				if (g.NavWindow->NavLastIds[0] != 0) {
					g.NavWindow->NavLastIds[0] = 0;
				} else {
					FocusWindow(NULL);
				}
			}
		*/
	}

	ImGuiDrawing::~ImGuiDrawing()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->SetTexID(nullptr);
		io.BackendRendererName = nullptr;
		io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
	}

	bool ImGuiDrawing::buildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels = nullptr;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		texture_ = std::make_unique<GLTexture>(GL_TEXTURE_2D);
		texture_->texParameteri(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		texture_->texParameteri(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		texture_->texImage2D(0, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		io.Fonts->TexID = reinterpret_cast<ImTextureID>(texture_.get());

		return (pixels != nullptr);
	}

	void ImGuiDrawing::newFrame()
	{
#if defined(WITH_GLFW)
		ImGuiGlfwInput::newFrame();
#elif defined(WITH_SDL)
		ImGuiSdlInput::newFrame();
#elif defined(WITH_QT5)
		ImGuiQt5Input::newFrame();
#elif defined(DEATH_TARGET_ANDROID)
		ImGuiAndroidInput::newFrame();
#endif

		ImGuiIO& io = ImGui::GetIO();

		if (lastFrameWidth_ != io.DisplaySize.x || lastFrameHeight_ != io.DisplaySize.y) {
			projectionMatrix_ = Matrix4x4f::Ortho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);

			if (!withSceneGraph_) {
				imguiShaderUniforms_->uniform(Material::GuiProjectionMatrixUniformName)->setFloatVector(projectionMatrix_.Data());
				imguiShaderUniforms_->uniform(Material::DepthUniformName)->setFloatValue(0.0f);
				imguiShaderUniforms_->commitUniforms();
			}
		}

		ImGui::NewFrame();
	}

	void ImGuiDrawing::endFrame(RenderQueue& renderQueue)
	{
		ImGui::EndFrame();
		ImGui::Render();

		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureKeyboard) {
			if (appInputHandler_ == nullptr) {
				appInputHandler_ = IInputManager::handler();
				IInputManager::setHandler(nullptr);
			}
		} else {
			if (appInputHandler_ != nullptr) {
				IInputManager::setHandler(appInputHandler_);
				appInputHandler_ = nullptr;
			}
		}

		draw(renderQueue);
	}

	void ImGuiDrawing::endFrame()
	{
		ImGui::EndFrame();
		ImGui::Render();

		draw();
	}

	RenderCommand* ImGuiDrawing::retrieveCommandFromPool()
	{
		bool commandAdded = false;
		RenderCommand* retrievedCommand = RenderResources::renderCommandPool().retrieveOrAdd(imguiShaderProgram_.get(), commandAdded);
		if (commandAdded) {
			setupRenderCmd(*retrievedCommand);
		}
		return retrievedCommand;
	}

	void ImGuiDrawing::setupRenderCmd(RenderCommand& cmd)
	{
		cmd.setType(RenderCommand::CommandTypes::ImGui);

		Material& material = cmd.material();
		material.setShaderProgram(imguiShaderProgram_.get());
		material.reserveUniformsDataMemory();
		material.uniform(Material::TextureUniformName)->setIntValue(0); // GL_TEXTURE0
		imguiShaderProgram_->attribute(Material::PositionAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->attribute(Material::TexCoordsAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setNormalized(true);
		material.setBlendingEnabled(true);
		material.setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		cmd.geometry().setNumElementsPerVertex(sizeof(ImDrawVert) / sizeof(GLfloat));
		cmd.geometry().setDrawParameters(GL_TRIANGLES, 0, 0);
	}

	void ImGuiDrawing::draw(RenderQueue& renderQueue)
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const unsigned int numElements = sizeof(ImDrawVert) / sizeof(GLfloat);

		ImGuiIO& io = ImGui::GetIO();
		const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}
		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		unsigned int numCmd = 0;
		for (int n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];

			RenderCommand& firstCmd = *retrieveCommandFromPool();
			if (lastFrameWidth_ != static_cast<int>(io.DisplaySize.x) ||
				lastFrameHeight_ != static_cast<int>(io.DisplaySize.y)) {
				firstCmd.material().uniform(Material::GuiProjectionMatrixUniformName)->setFloatVector(projectionMatrix_.Data());
				lastFrameWidth_ = static_cast<int>(io.DisplaySize.x);
				lastFrameHeight_ = static_cast<int>(io.DisplaySize.y);
			}

			firstCmd.geometry().shareVbo(nullptr);
			GLfloat* vertices = firstCmd.geometry().acquireVertexPointer(imCmdList->VtxBuffer.Size * numElements, numElements);
			memcpy(vertices, imCmdList->VtxBuffer.Data, imCmdList->VtxBuffer.Size * numElements * sizeof(GLfloat));
			firstCmd.geometry().releaseVertexPointer();

			firstCmd.geometry().shareIbo(nullptr);
			GLushort* indices = firstCmd.geometry().acquireIndexPointer(imCmdList->IdxBuffer.Size);
			memcpy(indices, imCmdList->IdxBuffer.Data, imCmdList->IdxBuffer.Size * sizeof(GLushort));
			firstCmd.geometry().releaseIndexPointer();

			if (lastLayerValue_ != theApplication().guiSettings().imguiLayer) {
				// It is enough to set the uniform value once as every ImGui command share the same shader
				const float depth = RenderCommand::calculateDepth(theApplication().guiSettings().imguiLayer, -1.0f, 1.0f);
				firstCmd.material().uniform(Material::DepthUniformName)->setFloatValue(depth);
				lastLayerValue_ = theApplication().guiSettings().imguiLayer;
			}

			for (int cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];
				RenderCommand& currCmd = (cmdIdx == 0) ? firstCmd : *retrieveCommandFromPool();

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					continue;

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				currCmd.setScissor(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
								   static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				if (cmdIdx > 0) {
					currCmd.geometry().shareVbo(&firstCmd.geometry());
					currCmd.geometry().shareIbo(&firstCmd.geometry());
				}

				currCmd.geometry().setNumIndices(imCmd->ElemCount);
				currCmd.geometry().setFirstIndex(imCmd->IdxOffset);
				currCmd.geometry().setFirstVertex(imCmd->VtxOffset);
				currCmd.setLayer(theApplication().guiSettings().imguiLayer);
				currCmd.setVisitOrder(numCmd);
				currCmd.material().setTexture(reinterpret_cast<GLTexture*>(imCmd->GetTexID()));

				renderQueue.addCommand(&currCmd);
				numCmd++;
			}
		}
	}

	void ImGuiDrawing::setupBuffersAndShader()
	{
		vbo_ = std::make_unique<GLBufferObject>(GL_ARRAY_BUFFER);
		ibo_ = std::make_unique<GLBufferObject>(GL_ELEMENT_ARRAY_BUFFER);

		imguiShaderUniforms_ = std::make_unique<GLShaderUniforms>(imguiShaderProgram_.get());
		imguiShaderUniforms_->setUniformsDataPointer(uniformsBuffer_);
		imguiShaderUniforms_->uniform(Material::TextureUniformName)->setIntValue(0); // GL_TEXTURE0

		imguiShaderProgram_->attribute(Material::PositionAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->attribute(Material::TexCoordsAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setNormalized(true);
	}

	void ImGuiDrawing::draw()
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}

		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		GLBlending::State blendingState = GLBlending::state();
		GLBlending::enable();
		GLBlending::setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLCullFace::State cullFaceState = GLCullFace::state();
		GLCullFace::disable();
		GLDepthTest::State depthTestState = GLDepthTest::state();
		GLDepthTest::disable();

		for (int n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];
			const ImDrawIdx* firstIndex = nullptr;

			// Always define vertex format (and bind VAO) before uploading data to buffers
			imguiShaderProgram_->defineVertexFormat(vbo_.get(), ibo_.get());
			vbo_->bufferData(static_cast<GLsizeiptr>(imCmdList->VtxBuffer.Size) * sizeof(ImDrawVert), static_cast<const GLvoid*>(imCmdList->VtxBuffer.Data), GL_STREAM_DRAW);
			ibo_->bufferData(static_cast<GLsizeiptr>(imCmdList->IdxBuffer.Size) * sizeof(ImDrawIdx), static_cast<const GLvoid*>(imCmdList->IdxBuffer.Data), GL_STREAM_DRAW);
			imguiShaderProgram_->use();

			for (int cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
					continue;
				}

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				GLScissorTest::enable(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
									  static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				// Bind texture, Draw
				GLTexture::bindHandle(GL_TEXTURE_2D, reinterpret_cast<GLTexture*>(imCmd->GetTexID())->glHandle());
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, firstIndex);
#else
				glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
										 reinterpret_cast<void*>(static_cast<intptr_t>(imCmd->IdxOffset * sizeof(ImDrawIdx))), static_cast<GLint>(imCmd->VtxOffset));
#endif
				firstIndex += imCmd->ElemCount;
			}
		}

		GLScissorTest::disable();
		GLDepthTest::setState(depthTestState);
		GLCullFace::setState(cullFaceState);
		GLBlending::setState(blendingState);
	}
}

#endif