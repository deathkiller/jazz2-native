#pragma once

#if defined(WITH_RHI_N64)

// ============================================================================
// Nintendo 64 backend - libdragon rdpq API
// Provides the Rhi interface for the N64 Reality Display Processor (RDP).
//
// The RDP hardware offers:
//   - Fixed-function triangle rasteriser with perspective-correct texturing
//   - Hardware alpha blending and Z-buffering (16-bit)
//   - Texture formats: RGBA16 (5551), RGBA32, CI8 (palette), IA8, I8
//   - Mipmaps (2–8 levels, power-of-2 textures only)
//   - NO programmable vertex or fragment shaders
//   - NO off-screen framebuffer objects
//
// Target SDK: libdragon (https://github.com/DragonMinded/libdragon)
// Build: mips64-elf cross-compiler, N64_INST toolchain
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_UNIFORM_BLOCKS,
//         RHI_CAP_BINARY_SHADERS, RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING,
//         RHI_CAP_VAO, RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_MIPMAPS				// RDP hardware mip-map support (power-of-2 only)
#define RHI_CAP_BUFFER_MAPPING		// All vertex buffers reside in host RAM (RDRAM)

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE		// Per-primitive shade colour (combine mode)
#define RHI_FF_ALPHA_BLEND			// Blender unit alpha blending
#define RHI_FF_TEXTURING			// RDP texture mapping via tiled TMEM

// ---------------------------------------------------------------------------
// libdragon headers - only available when targeting Nintendo 64
// ---------------------------------------------------------------------------
#include <libdragon.h>

#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Vector2.h"
#include "../../../Primitives/Color.h"
#include "../RenderTypes.h"

#include <memory>
#include <cstdint>
#include <cstring>

#include "Buffers.h"
#include "Texture.h"
#include "Shaders.h"

namespace nCine::RHI
{

	// =========================================================================
	// Per-draw context
	// =========================================================================
	struct DrawContext
	{
		const VertexFormat* vertexFormat  = nullptr;
		std::size_t         vboByteOffset = 0;
		Texture*            textures[Texture::MaxTextureUnitsConst] = {};
		FFState             ff;

		bool        blendingEnabled = false;
		BlendFactor blendSrc        = BlendFactor::SrcAlpha;
		BlendFactor blendDst        = BlendFactor::InvSrcAlpha;
		bool        scissorEnabled  = false;
		nCine::Recti scissorRect;
	};

	// =========================================================================
	// Constants
	// =========================================================================
	static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;

	// =========================================================================
	// Buffer factory / operation wrappers
	// =========================================================================

	inline std::unique_ptr<Buffer> CreateBuffer(BufferType type)
	{
		return std::make_unique<Buffer>(type);
	}

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

	inline bool BindBuffer(Buffer& /*buf*/) { return true; }
	inline void BindBufferBase(Buffer& /*buf*/, std::uint32_t /*index*/) {}
	inline void BindBufferRange(Buffer& /*buf*/, std::uint32_t /*index*/, std::size_t /*offset*/, std::size_t /*size*/) {}

	template<typename StringViewType>
	inline void SetBufferLabel(Buffer& /*buf*/, StringViewType /*label*/) {}

	// =========================================================================
	// Texture operation wrappers
	// =========================================================================

	inline std::unique_ptr<Texture> CreateTexture()
	{
		return std::make_unique<Texture>();
	}

	/// Bind a texture for the next rdpq draw call.
	void BindTexture(Texture& tex, std::uint32_t unit);
	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	template<typename StringViewType>
	inline void SetTextureLabel(Texture& /*tex*/, StringViewType /*label*/) {}

	// =========================================================================
	// Draw call declarations
	// =========================================================================
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count);
	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount);
	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex = 0);
	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex = 0);

	// =========================================================================
	// Render-state helpers
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst);
	void SetDepthTest(bool enabled);
	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetScissorTest(bool enabled, const nCine::Recti& rect);
	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetClearColor(float r, float g, float b, float a);
	void Clear(ClearFlags flags);

	struct ScissorState { bool enabled; std::int32_t x, y, w, h; };
	ScissorState GetScissorState();
	void         SetScissorState(const ScissorState& state);

}

#endif