#pragma once

#if defined(WITH_RHI_SAT)

// ============================================================================
// Sega Saturn backend - libyaul VDP1 API
// Provides the Rhi interface for the Sega Saturn VDP1 sprite processor.
//
// The Saturn VDP1 hardware offers:
//   - Fixed-function sprite / polygon rendering via command tables
//   - Flat-shaded, Gouraud-shaded, and textured quads / polygons
//   - Hardware semi-transparency (4 modes, same as PS1)
//   - Texture formats: 4bpp CLUT, 8bpp CLUT, 15-bit RGB direct
//   - Screen coordinates are signed 16-bit integers relative to screen center
//   - NO programmable shaders
//   - NO hardware Z-buffer (command list order determines depth)
//   - NO mipmaps
//
// Background scrolling planes are handled by VDP2 and are outside this backend.
//
// Target SDK: libyaul (https://github.com/yaul-org/libyaul)
// Build: sh2-elf / m68k-elf cross-compiler, yaul toolchain
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_MIPMAPS,
//         RHI_CAP_UNIFORM_BLOCKS, RHI_CAP_BINARY_SHADERS,
//         RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING, RHI_CAP_VAO,
//         RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_BUFFER_MAPPING		// All vertex data resides in host RAM (SH2 LWRAM/HWRAM)

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE		// Gouraud shading / flat colour modulation
#define RHI_FF_ALPHA_BLEND			// Semi-transparency modes
#define RHI_FF_TEXTURING			// VDP1 VRAM sprite texturing

// ---------------------------------------------------------------------------
// libyaul headers - only available when targeting Sega Saturn
// ---------------------------------------------------------------------------
#include <yaul.h>

#include "../../../Primitives/Rect.h"
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