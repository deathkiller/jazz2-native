#pragma once

#if defined(WITH_RHI_SW)

// ============================================================================
// Software rendering backend
// Provides the GAPI interface using pure CPU rasterization.
// This backend targets low-end devices, retro consoles without a fixed-
// function or programmable 3D GPU, and testing/reference builds.
//
// The renderer doesn't true hardware-accelerated shaders, so RHI_CAP_SHADERS is not defined.
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent flags: RHI_CAP_SHADERS, RHI_CAP_UNIFORM_BLOCKS, RHI_CAP_BINARY_SHADERS
// RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING, RHI_CAP_VAO, RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_FRAMEBUFFERS		// CPU-side offscreen surfaces
#define RHI_CAP_MIPMAPS				// Software mipmap lookup
#define RHI_CAP_BUFFER_MAPPING		// All buffers are always host-mapped

// Fixed-function configuration constants (replaces what shaders would do)
#define RHI_FF_TINTED_SPRITE		// Sprite multiply-tint without shaders
#define RHI_FF_ALPHA_BLEND			// Source-over alpha compositing

#include "GfxCapabilities.h"
#include "Texture.h"
#include "Shaders.h"
#include "Buffers.h"
#include "Framebuffer.h"

#include "../RenderTypes.h"
#include "../../../Primitives/Rect.h"

#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Constant
	// =========================================================================
	static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;

	// =========================================================================
	// Buffer factory helpers
	// =========================================================================
	inline std::unique_ptr<Buffer> CreateBuffer(BufferType type)
	{
		return std::make_unique<Buffer>(type);
	}

	// =========================================================================
	// Buffer operation wrappers (mirrors RHI_GL.h interface)
	// =========================================================================

	inline void BufferData(Buffer& buf, std::size_t size, const void* data, BufferUsage usage)
	{
		buf.SetData(size, data, usage);
	}

	inline void BufferSubData(Buffer& buf, std::size_t offset, std::size_t size, const void* data)
	{
		buf.UpdateSubData(offset, size, data);
	}

	inline void* MapBufferRange(Buffer& buf, std::size_t offset, std::size_t size, MapFlags flags)
	{
		return buf.MapRange(offset, size, flags);
	}

	inline void FlushMappedBufferRange(Buffer& buf, std::size_t offset, std::size_t size)
	{
		buf.FlushRange(offset, size);
	}

	inline void UnmapBuffer(Buffer& buf)
	{
		buf.Unmap();
	}

	inline bool BindBuffer(Buffer& /*buf*/)
	{
		return true; // No binding concept in SW renderer
	}

	inline void BindBufferBase(Buffer& /*buf*/, std::uint32_t /*index*/) {}

	inline void BindBufferRange(Buffer& /*buf*/, std::uint32_t /*index*/, std::size_t /*offset*/, std::size_t /*size*/) {}

	template<typename StringViewType>
	inline void SetBufferLabel(Buffer& /*buf*/, StringViewType /*label*/) {}

	// =========================================================================
	// Texture operation wrappers (mirrors RHI_GL.h interface)
	// =========================================================================

	inline std::unique_ptr<Texture> CreateTexture()
	{
		return std::make_unique<Texture>();
	}

	inline void BindTexture(const Texture& /*tex*/, std::uint32_t /*unit*/)
	{
		// SW renderer doesn't need explicit binding — textures are accessed via DrawContext
	}

	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	// =========================================================================
	// Draw calls
	// Full implementation lives in RHI_SW.cpp; declarations are here.
	// =========================================================================

	/// Per-draw context set before issuing draw calls.
	struct DrawContext
	{
		const VertexFormat* vertexFormat  = nullptr;
		std::size_t         vboByteOffset = 0;  // byte offset within the VBO
		Texture*            textures[MaxTextureUnits] = {};
		FFState             ff;
		FragmentShaderFn    fragmentShader = nullptr;
		void*               fragmentShaderUserData = nullptr;

		bool   blendingEnabled  = false;
		BlendFactor blendSrc    = BlendFactor::SrcAlpha;
		BlendFactor blendDst    = BlendFactor::OneMinusSrcAlpha;
		bool   scissorEnabled   = false;
		nCine::Recti scissorRect;
	};

	/// Sets the active DrawContext, making it available to the next nCine::RHI::Draw*() call.
	void SetDrawContext(const DrawContext& ctx);
	/// Clears the active DrawContext (disables rasterization until a new one is set).
	void ClearDrawContext();

	/// Rasterizes triangles / quads from a vertex buffer using the active DrawContext.
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count);
	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount);
	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex = 0);
	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex = 0);

	// =========================================================================
	// Render-state helpers (software equivalents)
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst);
	void SetDepthTest(bool enabled);
	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetScissorTest(bool enabled, const nCine::Recti& rect);
	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetClearColor(float r, float g, float b, float a);
	void Clear(ClearFlags flags);

	// SW-specific: state accessors used by RenderCommand
	struct ScissorState { bool enabled; std::int32_t x, y, w, h; };
	ScissorState GetScissorState();
	void         SetScissorState(const ScissorState& state);

	struct ViewportState { std::int32_t x, y, w, h; };
	ViewportState GetViewportState();
	void          SetViewportState(const ViewportState& s);

	struct ClearColorState { float r, g, b, a; };
	ClearColorState GetClearColorState();
	void            SetClearColorState(const ClearColorState& s);

	/// Depth mask is a no-op in software rendering (no GPU depth buffer).
	inline void SetDepthMask(bool /*enabled*/) {}

	// -------------------------------------------------------------------------
	// Framebuffer helpers — mirrors the RHI_GL.h interface, SW implementations
	// -------------------------------------------------------------------------

	/// Redirects SW rasterizer output to the framebuffer's attached texture.
	void FramebufferBind(Framebuffer& fbo);
	/// Restores the SW rasterizer output to the main window colour buffer.
	void FramebufferUnbind();

	inline void FramebufferSetDrawBuffers(Framebuffer& /*fbo*/, std::uint32_t /*n*/) {}  // no-op

	inline void FramebufferAttachTexture(Framebuffer& fbo, Texture& texture, std::uint32_t colorIndex)
	{
		fbo.AttachTexture(FramebufferAttachment(colorIndex), &texture);
	}

	inline void FramebufferDetachTexture(Framebuffer& fbo, std::uint32_t colorIndex)
	{
		fbo.AttachTexture(FramebufferAttachment(colorIndex), nullptr);
	}

	inline bool FramebufferIsComplete(Framebuffer& fbo)
	{
		return fbo.IsComplete();
	}

	template<typename StringViewType>
	inline void FramebufferSetLabel(Framebuffer& /*fbo*/, StringViewType /*label*/) {}  // no-op

	// =========================================================================
	// Framebuffer management — used by desktop presenters (GLFW/SDL2 blit)
	// =========================================================================

	/// Allocate (or reallocate) the internal RGBA8 color buffer.
	/// Must be called before the first frame and on every window resize.
	void ResizeColorBuffer(std::int32_t width, std::int32_t height);

	/// Returns a pointer to the current RGBA8 color buffer (row-major, top-down).
	/// Valid until the next ResizeColorBuffer() call.
	const std::uint8_t* GetColorBuffer();

	/// Returns a mutable pointer to the current RGBA8 color buffer.
	std::uint8_t* GetMutableColorBuffer();

	/// Returns the current color buffer width in pixels.
	std::int32_t GetColorBufferWidth();

	/// Returns the current color buffer height in pixels.
	std::int32_t GetColorBufferHeight();

}

#endif