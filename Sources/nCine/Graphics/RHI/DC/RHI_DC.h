#pragma once

#if defined(WITH_RHI_DC)

// ============================================================================
// Sega Dreamcast backend - KallistiOS PVR API
// Provides the Rhi interface for the Dreamcast PowerVR2 (CLX2) GPU.
//
// The PVR hardware offers:
//   - Fixed-function transform & lighting (Z sort, alpha blending, fogging)
//   - Hardware texture mapping (twiddled RGB565 / ARGB4444 / ARGB1555)
//   - Three display lists: Opaque, Punch-Through, Translucent
//   - NO programmable vertex or fragment shaders
//   - NO off-screen framebuffer objects
//
// Feature set:
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_UNIFORM_BLOCKS,
//         RHI_CAP_BINARY_SHADERS, RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING,
//         RHI_CAP_VAO, RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_MIPMAPS				// PVR hardware mip-map support (power-of-2 only)
#define RHI_CAP_BUFFER_MAPPING		// All vertex buffers reside in host RAM

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE		// Per-vertex colour multiply (ARGB in pvr_vertex_t)
#define RHI_FF_ALPHA_BLEND			// Hardware alpha blending via PVR_BLEND_*
#define RHI_FF_TEXTURING			// PVR texture mapping

// ---------------------------------------------------------------------------
// KallistiOS PVR headers
// ---------------------------------------------------------------------------
#include <kos.h>
#include <dc/pvr.h>

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
	// Blending helpers
	// =========================================================================

	/// Translates a Rhi BlendFactor to a PVR_BLEND_* constant.
	inline int ToNativeBlendFactor(BlendFactor factor)
	{
		switch (factor) {
			case BlendFactor::Zero:         return PVR_BLEND_ZERO;
			case BlendFactor::One:          return PVR_BLEND_ONE;
			case BlendFactor::SrcColor:     return PVR_BLEND_SRCCOLOR;
			case BlendFactor::InvSrcColor:  return PVR_BLEND_INVSRCCOLOR;
			case BlendFactor::SrcAlpha:     return PVR_BLEND_SRCALPHA;
			case BlendFactor::InvSrcAlpha:  return PVR_BLEND_INVSRCALPHA;
			case BlendFactor::DstAlpha:     return PVR_BLEND_DESTALPHA;
			case BlendFactor::InvDstAlpha:  return PVR_BLEND_INVDESTALPHA;
			case BlendFactor::DstColor:     return PVR_BLEND_DESTCOLOR;
			case BlendFactor::InvDstColor:  return PVR_BLEND_INVDESTCOLOR;
			default:                        return PVR_BLEND_ONE;
		}
	}

	/// Returns the PVR display list that corresponds to the active blend mode.
	/// Opaque and additive geometry goes into OP_POLY; alpha-blended into TR_POLY.
	inline pvr_list_t BlendFactorsToPvrList(BlendFactor src, BlendFactor dst)
	{
		if (src == BlendFactor::One && dst == BlendFactor::Zero) {
			return PVR_LIST_OP_POLY;
		}
		return PVR_LIST_TR_POLY;
	}

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

	inline bool BindBuffer(Buffer& /*buf*/)
	{
		return true;
	}

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

	/// No explicit texture bind required — texture is referenced by pvr_poly_hdr_t.
	inline void BindTexture(const Texture& /*tex*/, std::uint32_t /*unit*/) {}
	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	// =========================================================================
	// Build a PVR polygon header from the current DrawContext.
	//
	// Call this each time the material changes (texture, blend mode, etc.)
	// before submitting vertices to the PVR list.
	// =========================================================================
	inline void BuildPvrHeader(pvr_poly_hdr_t& outHdr, const DrawContext& ctx)
	{
		pvr_poly_cxt_t cxt;
		Texture* tex = ctx.textures[0];
		pvr_list_t pvrList = ctx.blendingEnabled
		    ? BlendFactorsToPvrList(ctx.blendSrc, ctx.blendDst)
		    : PVR_LIST_OP_POLY;

		if (tex != nullptr && tex->GetVramPtr() != nullptr) {
			int filter = Texture::ToNativeFilter(ctx.blendSrc == BlendFactor::One
			    ? tex->GetMinFilter() : tex->GetMagFilter());
			int uvclamp = Texture::ToNativeClamp(tex->GetWrapS());
			pvr_poly_cxt_txr(&cxt, pvrList, tex->GetNativeFormat(),
			    tex->GetWidth(), tex->GetHeight(), tex->GetVramPtr(), filter);
			cxt.gen.uv_clamp = uvclamp;
		} else {
			pvr_poly_cxt_col(&cxt, pvrList);
		}

		if (ctx.blendingEnabled) {
			cxt.blend.src = ToNativeBlendFactor(ctx.blendSrc);
			cxt.blend.dst = ToNativeBlendFactor(ctx.blendDst);
		}

		pvr_poly_compile(&outHdr, &cxt);
	}

	// =========================================================================
	// Draw call declarations
	// =========================================================================

	/// Submit vertices from the host buffer to the active PVR display list.
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