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
using namespace nCine::Backends;

#if defined(DEATH_TRACE_VERBOSE_GL)
#	define GL_CALL(op)													\
		do {															\
			op;															\
			GLenum glErr_ = glGetError();								\
			if (glErr_ != 0) {											\
				LOGE("GL error 0x{:x} returned from {}", glErr_, #op);	\
			}															\
		} while (0)
#else
#	define GL_CALL(op) op
#endif

namespace nCine
{
	ImGuiDrawing::ImGuiDrawing(bool withSceneGraph)
		: withSceneGraph_(withSceneGraph), appInputHandler_(nullptr), lastFrameWidth_(0), lastFrameHeight_(0), lastLayerValue_(0)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

#if defined(IMGUI_HAS_DOCK)
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;		// Enable Docking
#endif
#if defined(IMGUI_HAS_VIEWPORT)
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;		// Enable Multi-Viewport / Platform Windows
#endif

		io.BackendRendererUserData = this;
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
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;	// We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
#endif
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;	// We can honor ImGuiPlatformIO::Textures[] requests during render.

		imguiShaderProgram_ = std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate);
#if !defined(WITH_EMBEDDED_SHADERS)
		imguiShaderProgram_->AttachShaderFromFile(GL_VERTEX_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_vs.glsl"_s }));
		imguiShaderProgram_->AttachShaderFromFile(GL_FRAGMENT_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_fs.glsl"_s }));
#else
		imguiShaderProgram_->AttachShaderFromString(GL_VERTEX_SHADER, ShaderStrings::imgui_vs);
		imguiShaderProgram_->AttachShaderFromString(GL_FRAGMENT_SHADER, ShaderStrings::imgui_fs);
#endif
		imguiShaderProgram_->Link(GLShaderProgram::Introspection::Enabled);
		FATAL_ASSERT(imguiShaderProgram_->GetStatus() != GLShaderProgram::Status::LinkingFailed);

		if (!withSceneGraph) {
			SetupBuffersAndShader();
		}

#if defined(IMGUI_HAS_VIEWPORT)
		PrepareForViewports();
#endif

		// Set custom style
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.FrameBorderSize = 1.0f;

		// TODO: Set custom purple colors
		ImVec4* colors = style.Colors;
		/*colors[ImGuiCol_FrameBg] = ImVec4(0.24f, 0.13f, 0.33f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.37f, 0.21f, 0.49f, 0.54f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.24f, 0.59f, 0.54f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.09f, 0.21f, 0.54f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.24f, 0.13f, 0.33f, 0.85f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.58f, 0.26f, 0.98f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.44f, 0.20f, 0.74f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.52f, 0.22f, 0.89f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.59f, 0.26f, 0.98f, 0.40f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.24f, 0.68f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.53f, 0.29f, 0.72f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.61f, 0.26f, 0.98f, 0.31f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.46f, 0.23f, 0.71f, 0.80f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.42f, 0.19f, 0.68f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.37f, 0.10f, 0.75f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.37f, 0.10f, 0.75f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.60f, 0.26f, 0.98f, 0.20f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.60f, 0.26f, 0.98f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.60f, 0.26f, 0.98f, 0.95f);
		colors[ImGuiCol_Tab] = ImVec4(0.37f, 0.18f, 0.58f, 0.86f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.48f, 0.26f, 0.73f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.42f, 0.20f, 0.68f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.07f, 0.15f, 0.97f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.27f, 0.14f, 0.42f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.41f, 0.17f, 0.68f, 0.70f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.58f, 0.26f, 0.98f, 0.35f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.58f, 0.26f, 0.98f, 1.00f);*/

		/*colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.22f, 0.33f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.38f, 0.49f, 0.54f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.45f, 0.59f, 0.54f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.17f, 0.21f, 0.54f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.17f, 0.21f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.22f, 0.71f, 0.89f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.23f, 0.59f, 0.88f, 0.40f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.37f, 0.54f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.08f, 0.23f, 0.35f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.26f, 0.60f, 0.98f, 0.31f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.33f, 0.46f, 1.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.08f, 0.20f, 0.32f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.51f, 0.75f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.49f, 0.75f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.68f, 0.98f, 0.20f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.66f, 0.98f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.10f, 0.42f, 0.62f, 0.95f);
		colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.34f, 0.43f, 0.40f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.54f, 0.73f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.15f, 0.41f, 0.58f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.12f, 0.15f, 0.28f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.34f, 0.42f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.17f, 0.46f, 0.68f, 0.70f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.79f, 0.98f, 0.35f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.79f, 0.98f, 1.00f);*/



		// Try to use system font
#if defined(DEATH_TARGET_WINDOWS)
		String systemFont = fs::CombinePath({ fs::GetWindowsDirectory(), "Fonts"_s, "SegoeUI.ttf"_s });
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
		// Destroy all textures
		for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
			if (tex->RefCount == 1) {
				DestroyTexture(tex);
			}
		}

#if defined(IMGUI_HAS_VIEWPORT)
		if (vboHandle_) {
			glDeleteBuffers(1, &vboHandle_);
			vboHandle_ = 0;
		}
		if (elementsHandle_) {
			glDeleteBuffers(1, &elementsHandle_);
			elementsHandle_ = 0;
		}
#endif

		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = nullptr;
		io.BackendRendererUserData = nullptr;
		io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
#if defined(IMGUI_HAS_VIEWPORT)
		io.BackendFlags &= ~ImGuiBackendFlags_RendererHasViewports;
#endif
	}

	void ImGuiDrawing::NewFrame()
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

#if !defined(IMGUI_HAS_VIEWPORT)
		// projectionMatrix_ must be recaltulated when the main window moves if viewports are active
		ImGuiIO& io = ImGui::GetIO();
		if (lastFrameWidth_ != io.DisplaySize.x || lastFrameHeight_ != io.DisplaySize.y) {
			lastFrameWidth_ = io.DisplaySize.x;
			lastFrameHeight_ = io.DisplaySize.y;
			projectionMatrix_ = Matrix4x4f::Ortho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);

			if (!withSceneGraph_) {
				imguiShaderUniforms_->GetUniform(Material::GuiProjectionMatrixUniformName)->SetFloatVector(projectionMatrix_.Data());
				imguiShaderUniforms_->GetUniform(Material::DepthUniformName)->SetFloatValue(0.0f);
				imguiShaderUniforms_->CommitUniforms();
			}
		}
#endif

		ImGui::NewFrame();
	}

	void ImGuiDrawing::EndFrame(RenderQueue& renderQueue)
	{
		ImGui::EndFrame();
		ImGui::Render();

#if defined(WITH_GLFW)
		ImGuiGlfwInput::endFrame();
#elif defined(WITH_SDL)
		ImGuiSdlInput::endFrame();
#endif

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

		Draw(renderQueue);
	}

	void ImGuiDrawing::EndFrame()
	{
		ImGui::EndFrame();
		ImGui::Render();

		Draw();
	}

	void ImGuiDrawing::DestroyTexture(ImTextureData* tex)
	{
		GLTexture* texturePtr = (GLTexture*)(intptr_t)tex->TexID;
		textures_.erase(texturePtr);

		// Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
		tex->SetTexID(ImTextureID_Invalid);
		tex->SetStatus(ImTextureStatus_Destroyed);
	}

	void ImGuiDrawing::UpdateTexture(ImTextureData* tex)
	{
		// FIXME: Consider backing up and restoring
		if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
#ifdef GL_UNPACK_ROW_LENGTH // Not on WebGL/ES
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
#ifdef GL_UNPACK_ALIGNMENT
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#endif
		}

		if (tex->Status == ImTextureStatus_WantCreate) {
			// Create and upload new texture to graphics system
			//IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
			IM_ASSERT(tex->TexID == 0 && tex->BackendUserData == nullptr);
			IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
			const void* pixels = tex->GetPixels();

			// Upload texture to graphics system
			// (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
			GLint lastTexture = GLTexture::GetBoundHandle(GL_TEXTURE_2D);
			std::unique_ptr<GLTexture> texture = std::make_unique<GLTexture>(GL_TEXTURE_2D);
			texture->TexParameteri(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			texture->TexParameteri(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			texture->TexParameteri(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			texture->TexParameteri(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			texture->TexImage2D(0, GL_RGBA, tex->Width, tex->Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

			GLTexture* texturePtr = texture.get();
			textures_.emplace(texturePtr, std::move(texture));
			// Store identifiers
			tex->SetTexID((ImTextureID)(intptr_t)texturePtr);
			tex->SetStatus(ImTextureStatus_OK);

			// Restore state
			GLTexture::BindHandle(GL_TEXTURE_2D, lastTexture);
		} else if (tex->Status == ImTextureStatus_WantUpdates) {
			// Update selected blocks. We only ever write to textures regions which have never been used before!
			// This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
			GLint lastTexture = GLTexture::GetBoundHandle(GL_TEXTURE_2D);

			GLTexture* texturePtr = (GLTexture*)(intptr_t)tex->TexID;
			texturePtr->Bind();

#ifdef GL_UNPACK_ROW_LENGTH // Not on WebGL/ES
			glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width);
			for (ImTextureRect& r : tex->Updates) {
				texturePtr->TexSubImage2D(0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, tex->GetPixelsAt(r.x, r.y));
			}
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
			// GL ES doesn't have GL_UNPACK_ROW_LENGTH, so we need to (A) copy to a contiguous buffer or (B) upload line by line.
			for (ImTextureRect& r : tex->Updates) {
				const int srcPitch = r.w * tex->BytesPerPixel;
				if (tempTexBuffer_.size() < r.h * srcPitch)
					tempTexBuffer_.resize(r.h * srcPitch);
				char* outP = tempTexBuffer_.data();
				for (int y = 0; y < r.h; y++, outP += srcPitch)
					memcpy(outP, tex->GetPixelsAt(r.x, r.y + y), srcPitch);
				texturePtr->TexSubImage2D(0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, tempTexBuffer_.data());
			}
#endif
			tex->SetStatus(ImTextureStatus_OK);
			GLTexture::BindHandle(GL_TEXTURE_2D, lastTexture); // Restore state
		} else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
			DestroyTexture(tex);
		}
	}

	RenderCommand* ImGuiDrawing::RetrieveCommandFromPool()
	{
		bool commandAdded = false;
		RenderCommand* retrievedCommand = RenderResources::GetRenderCommandPool().RetrieveOrAdd(imguiShaderProgram_.get(), commandAdded);
		if (commandAdded) {
			SetupRenderCommand(*retrievedCommand);
		}
		return retrievedCommand;
	}

	void ImGuiDrawing::SetupRenderCommand(RenderCommand& cmd)
	{
		cmd.SetType(RenderCommand::Type::ImGui);

		Material& material = cmd.GetMaterial();
		material.SetShaderProgram(imguiShaderProgram_.get());
		material.ReserveUniformsDataMemory();
		material.Uniform(Material::TextureUniformName)->SetIntValue(0); // GL_TEXTURE0
		imguiShaderProgram_->GetAttribute(Material::PositionAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->GetAttribute(Material::TexCoordsAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetNormalized(true);
		material.SetBlendingEnabled(true);
		material.SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		cmd.GetGeometry().SetElementsPerVertex(sizeof(ImDrawVert) / sizeof(GLfloat));
		cmd.GetGeometry().SetDrawParameters(GL_TRIANGLES, 0, 0);
	}

	void ImGuiDrawing::Draw(RenderQueue& renderQueue)
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const std::uint32_t numElements = sizeof(ImDrawVert) / sizeof(GLfloat);

		ImGuiIO& io = ImGui::GetIO();
		const std::int32_t fbWidth = std::int32_t(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const std::int32_t fbHeight = std::int32_t(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}
		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		if (drawData->Textures != nullptr) {
			for (ImTextureData* tex : *drawData->Textures) {
				if (tex->Status != ImTextureStatus_OK) {
					UpdateTexture(tex);
				}
			}
		}

#if defined(IMGUI_HAS_VIEWPORT)
		// projectionMatrix_ must be recaltulated when the main window moves if viewports are active
		projectionMatrix_ = Matrix4x4f::Ortho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);
		projectionMatrix_.Translate(-clipOff.x, -clipOff.y, 0.0f);
#endif

		std::uint32_t numCmd = 0;
		for (std::int32_t n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];

			RenderCommand& firstCmd = *RetrieveCommandFromPool();

			firstCmd.GetMaterial().Uniform(Material::GuiProjectionMatrixUniformName)->SetFloatVector(projectionMatrix_.Data());

			firstCmd.GetGeometry().ShareVbo(nullptr);
			GLfloat* vertices = firstCmd.GetGeometry().AcquireVertexPointer(imCmdList->VtxBuffer.Size * numElements, numElements);
			memcpy(vertices, imCmdList->VtxBuffer.Data, imCmdList->VtxBuffer.Size * numElements * sizeof(GLfloat));
			firstCmd.GetGeometry().ReleaseVertexPointer();

			firstCmd.GetGeometry().ShareIbo(nullptr);
			GLushort* indices = firstCmd.GetGeometry().AcquireIndexPointer(imCmdList->IdxBuffer.Size);
			memcpy(indices, imCmdList->IdxBuffer.Data, imCmdList->IdxBuffer.Size * sizeof(GLushort));
			firstCmd.GetGeometry().ReleaseIndexPointer();

			if (lastLayerValue_ != theApplication().GetGuiSettings().imguiLayer) {
				// It is enough to set the uniform value once as every ImGui command share the same shader
				const float depth = RenderCommand::CalculateDepth(theApplication().GetGuiSettings().imguiLayer, -1.0f, 1.0f);
				firstCmd.GetMaterial().Uniform(Material::DepthUniformName)->SetFloatValue(depth);
				lastLayerValue_ = theApplication().GetGuiSettings().imguiLayer;
			}

			for (std::int32_t cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];
				RenderCommand& currCmd = (cmdIdx == 0) ? firstCmd : *RetrieveCommandFromPool();

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					continue;

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				currCmd.SetScissor(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
								   static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				if (cmdIdx > 0) {
					currCmd.GetGeometry().ShareVbo(&firstCmd.GetGeometry());
					currCmd.GetGeometry().ShareIbo(&firstCmd.GetGeometry());
				}

				currCmd.GetGeometry().SetIndexCount(imCmd->ElemCount);
				currCmd.GetGeometry().SetFirstIndex(imCmd->IdxOffset);
				currCmd.GetGeometry().SetFirstVertex(imCmd->VtxOffset);
				currCmd.SetLayer(theApplication().GetGuiSettings().imguiLayer);
				currCmd.SetVisitOrder(numCmd);
				currCmd.GetMaterial().SetTexture(reinterpret_cast<GLTexture*>(imCmd->GetTexID()));

				renderQueue.AddCommand(&currCmd);
				numCmd++;
			}
		}
	}

	void ImGuiDrawing::SetupBuffersAndShader()
	{
		vbo_ = std::make_unique<GLBufferObject>(GL_ARRAY_BUFFER);
		ibo_ = std::make_unique<GLBufferObject>(GL_ELEMENT_ARRAY_BUFFER);

		imguiShaderUniforms_ = std::make_unique<GLShaderUniforms>(imguiShaderProgram_.get());
		imguiShaderUniforms_->SetUniformsDataPointer(uniformsBuffer_);
		imguiShaderUniforms_->GetUniform(Material::TextureUniformName)->SetIntValue(0); // GL_TEXTURE0

		imguiShaderProgram_->GetAttribute(Material::PositionAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->GetAttribute(Material::TexCoordsAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->GetAttribute(Material::ColorAttributeName)->SetNormalized(true);
	}

	void ImGuiDrawing::Draw()
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const std::int32_t fbWidth = std::int32_t(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const std::int32_t fbHeight = std::int32_t(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}

		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		if (drawData->Textures != nullptr) {
			for (ImTextureData* tex : *drawData->Textures) {
				if (tex->Status != ImTextureStatus_OK) {
					UpdateTexture(tex);
				}
			}
		}

		GLBlending::State blendingState = GLBlending::GetState();
		GLBlending::Enable();
		GLBlending::SetBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLCullFace::State cullFaceState = GLCullFace::GetState();
		GLCullFace::Disable();
		GLDepthTest::State depthTestState = GLDepthTest::GetState();
		GLDepthTest::Disable();

		for (std::int32_t n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
			const ImDrawIdx* firstIndex = nullptr;
#endif

			// Always define vertex format (and bind VAO) before uploading data to buffers
			imguiShaderProgram_->DefineVertexFormat(vbo_.get(), ibo_.get());
			vbo_->BufferData(static_cast<GLsizeiptr>(imCmdList->VtxBuffer.Size) * sizeof(ImDrawVert), static_cast<const GLvoid*>(imCmdList->VtxBuffer.Data), GL_STREAM_DRAW);
			ibo_->BufferData(static_cast<GLsizeiptr>(imCmdList->IdxBuffer.Size) * sizeof(ImDrawIdx), static_cast<const GLvoid*>(imCmdList->IdxBuffer.Data), GL_STREAM_DRAW);
			imguiShaderProgram_->Use();

			for (std::int32_t cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
					continue;
				}

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				GLScissorTest::Enable(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
									  static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				// Bind texture, Draw
				GLTexture::BindHandle(GL_TEXTURE_2D, reinterpret_cast<GLTexture*>(imCmd->GetTexID())->GetGLHandle());
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, firstIndex);
				firstIndex += imCmd->ElemCount;
#else
				glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
										 reinterpret_cast<void*>(static_cast<intptr_t>(imCmd->IdxOffset * sizeof(ImDrawIdx))), static_cast<GLint>(imCmd->VtxOffset));
#endif
			}
		}

		GLScissorTest::Disable();
		GLDepthTest::SetState(depthTestState);
		GLCullFace::SetState(cullFaceState);
		GLBlending::SetState(blendingState);
	}

#if defined(IMGUI_HAS_VIEWPORT)
	void ImGuiDrawing::PrepareForViewports()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		platformIo.Renderer_RenderWindow = OnRenderPlatformWindow;

		// Backup GL state
		GLint lastTexture, lastArrayBuffer;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
		GLint lastPixelUnpackBuffer = 0;

		GLint lastVertexArray;
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);

		// TODO: Use nCine shaders directly instead
		auto shaderHandle = imguiShaderProgram_.get()->GetGLHandle();

		attribLocationTex_ = glGetUniformLocation(shaderHandle, "uTexture");
		attribLocationProjMtx_ = glGetUniformLocation(shaderHandle, "uGuiProjection");
		attribLocationVtxPos_ = (GLuint)glGetAttribLocation(shaderHandle, "aPosition");
		attribLocationVtxUV_ = (GLuint)glGetAttribLocation(shaderHandle, "aTexCoords");
		attribLocationVtxColor_ = (GLuint)glGetAttribLocation(shaderHandle, "aColor");

		// Create buffers
		// TODO: Use nCine GLBufferObject directly
		glGenBuffers(1, &vboHandle_);
		glGenBuffers(1, &elementsHandle_);

		// Restore modified GL state
		glBindTexture(GL_TEXTURE_2D, lastTexture);
		glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer);

#if !defined(WITH_OPENGL2)
		glBindVertexArray(lastVertexArray);
#endif
	}

	void ImGuiDrawing::OnRenderPlatformWindow(ImGuiViewport* viewport, void*)
	{
		ImGuiDrawing* _this = static_cast<ImGuiDrawing*>(ImGui::GetIO().BackendRendererUserData);
		_this->DrawPlatformWindow(viewport);
	}

	void ImGuiDrawing::DrawPlatformWindow(ImGuiViewport* viewport)
	{
		if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear)) {
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		ImDrawData* drawData = viewport->DrawData;

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		std::int32_t fbWidth = std::int32_t(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		std::int32_t fbHeight = std::int32_t(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}

		// Setup desired GL state
		// Recreate the VAO every time (this is to easily allow multiple GL contexts to be rendered to. VAO are not shared among GL contexts)
		// The renderer would actually work without any VAO bound, but then our VertexAttrib calls would overwrite the default one currently bound.
		GLuint vertexArrayObject = 0;
#if !defined(WITH_OPENGL2)
		GL_CALL(glGenVertexArrays(1, &vertexArrayObject));
#endif
		SetupRenderStateForPlatformWindow(drawData, fbWidth, fbHeight, vertexArrayObject);

		// Will project scissor/clipping rectangles into framebuffer space
		ImVec2 clipOff = drawData->DisplayPos;         // (0,0) unless using multi-viewports
		ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

		for (std::int32_t n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];

			const GLsizeiptr vtxBufferSize = GLsizeiptr(imCmdList->VtxBuffer.Size) * std::int32_t(sizeof(ImDrawVert));
			const GLsizeiptr idxBufferSize = GLsizeiptr(imCmdList->IdxBuffer.Size) * std::int32_t(sizeof(ImDrawIdx));
			GL_CALL(glBufferData(GL_ARRAY_BUFFER, vtxBufferSize, (const GLvoid*)imCmdList->VtxBuffer.Data, GL_STREAM_DRAW));
			GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxBufferSize, (const GLvoid*)imCmdList->IdxBuffer.Data, GL_STREAM_DRAW));

			for (std::int32_t cmd_i = 0; cmd_i < imCmdList->CmdBuffer.Size; cmd_i++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmd_i];
				if (imCmd->UserCallback != nullptr) {
					// User callback, registered via ImDrawList::AddCallback()
					// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
					if (imCmd->UserCallback == ImDrawCallback_ResetRenderState) {
						SetupRenderStateForPlatformWindow(drawData, fbWidth, fbHeight, vertexArrayObject);
					} else {
						imCmd->UserCallback(imCmdList, imCmd);
					}
				} else {
					// Project scissor/clipping rectangles into framebuffer space
					ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
					ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
						continue;
					}

					// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
					GL_CALL(glScissor(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
						static_cast<GLint>(clipMax.x - clipMin.x), static_cast<GLint>(clipMax.y - clipMin.y)));

					auto* texture = reinterpret_cast<nCine::GLTexture*>(imCmd->GetTexID());
					GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->GetGLHandle()));
					GL_CALL(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
						reinterpret_cast<void*>(static_cast<intptr_t>(imCmd->IdxOffset * sizeof(ImDrawIdx)))));
				}
			}
		}

		// Destroy the temporary VAO
#if !defined(WITH_OPENGL2)
		GL_CALL(glDeleteVertexArrays(1, &vertexArrayObject));
#endif
	}

	void ImGuiDrawing::SetupRenderStateForPlatformWindow(ImDrawData* drawData, std::int32_t fbWidth, std::int32_t fbHeight, unsigned int vertexArrayObject)
	{
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glEnable(GL_SCISSOR_TEST);

		// Setup viewport, orthographic projection matrix
		// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
		GL_CALL(glViewport(0, 0, (GLsizei)fbWidth, (GLsizei)fbHeight));
		float L = drawData->DisplayPos.x;
		float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
		float T = drawData->DisplayPos.y;
		float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

		const float ortho_projection[4][4] = {
			{ 2.0f / (R - L),		0.0f,				0.0f,		0.0f },
			{ 0.0f,					2.0f / (T - B),		0.0f,		0.0f },
			{ 0.0f,					0.0f,				-1.0f,		0.0f },
			{ (R + L) / (L - R),	(T + B) / (B - T),  0.0f,		1.0f },
		};

		// TODO: Use nCine shaders directly instead
		auto shaderHandle = imguiShaderProgram_.get()->GetGLHandle();

		glUseProgram(shaderHandle);
		glUniform1i(attribLocationTex_, 0);
		glUniformMatrix4fv(attribLocationProjMtx_, 1, GL_FALSE, &ortho_projection[0][0]);

#if !defined(WITH_OPENGL2)
		glBindVertexArray(vertexArrayObject);
#endif

		// Bind vertex/index buffers and setup attributes for ImDrawVert
		GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vboHandle_));
		GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementsHandle_));

		GL_CALL(glEnableVertexAttribArray(attribLocationVtxPos_));
		GL_CALL(glEnableVertexAttribArray(attribLocationVtxUV_));
		GL_CALL(glEnableVertexAttribArray(attribLocationVtxColor_));
		GL_CALL(glVertexAttribPointer(attribLocationVtxPos_, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos)));
		GL_CALL(glVertexAttribPointer(attribLocationVtxUV_, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, uv)));
		GL_CALL(glVertexAttribPointer(attribLocationVtxColor_, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col)));
	}
#endif
}

#endif