#if defined(WITH_RHI_PS1)

#include "RHI_PS1.h"

// ============================================================================
// PlayStation 1 GPU draw call implementations (PSn00bSDK)
//
// The PS1 GPU does not use a traditional Z-buffer.  Instead, geometry is
// inserted into an Ordering Table (OT) — a linked list ordered by depth
// priority.  The display list is submitted to the GPU at the end of each
// frame via DrawOTag().
//
// This backend converts generic Rhi vertex buffers into textured quad
// (POLY_FT4) or flat-shaded quad (POLY_F4) primitives and inserts them at
// the OT depth slot specified by DrawContext::otDepth.
//
// Double-buffering: two OTs and two primitive work buffers are kept.
// The frame() free function (called externally) performs the swap.
//
// Vertex layout assumptions:
//   float x, y, z    — projected screen-space integer coordinates
//   float u, v        — texture coords 0..1 (scaled to texture pixel range here)
//   uint8 r, g, b, a  — Gouraud / modulate colour
// ============================================================================

namespace nCine::RHI
{
	// -------------------------------------------------------------------------
	// Double-buffered OT + primitive work area
	// -------------------------------------------------------------------------
	static std::uint32_t s_ot[2][kOtLen];
	static std::uint8_t  s_primBuf[2][kPrimBufSz];

	static struct State {
		DrawContext  ctx;
		std::int32_t bufIdx    = 0;             // current double-buffer index
		std::uint8_t* nextPrim = s_primBuf[0];  // rolling pointer into s_primBuf
		std::uint32_t clearColor = 0;           // packed 0x00RRGGBB
		std::int32_t viewportX  = 0;
		std::int32_t viewportY  = 0;
		std::int32_t viewportW  = 320;
		std::int32_t viewportH  = 240;
		bool         depthTestEnabled = false;
	} s_state;

	// -------------------------------------------------------------------------
	// Primitive allocator — advance the rolling pointer
	// -------------------------------------------------------------------------
	template<typename T>
	static T* AllocPrim()
	{
		T* p = reinterpret_cast<T*>(s_state.nextPrim);
		s_state.nextPrim += sizeof(T);
		std::int32_t used = static_cast<std::int32_t>(s_state.nextPrim - s_primBuf[s_state.bufIdx]);
		if (used >= kPrimBufSz) {
			// Work buffer overflow — reset (causes corruption, but avoids crash)
			s_state.nextPrim = s_primBuf[s_state.bufIdx];
			p = reinterpret_cast<T*>(s_state.nextPrim);
			s_state.nextPrim += sizeof(T);
		}
		return p;
	}

	// -------------------------------------------------------------------------
	// Helper: read one vertex from the active vertex buffer
	// -------------------------------------------------------------------------
	struct RawVert { float x, y, z, u, v; std::uint8_t r, g, b, a; };

	static RawVert ReadVertex(const VertexFormat& fmt, std::size_t baseOffset, std::int32_t index)
	{
		RawVert out = { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 128, 128, 128, 255 };

		for (std::uint32_t i = 0; i < fmt.GetAttributeCount(); ++i) {
			const VertexFormatAttribute& attr = fmt.GetAttribute(i);
			if (!attr.enabled || attr.vbo == nullptr) {
				continue;
			}

			const std::uint8_t* base = static_cast<const std::uint8_t*>(attr.vbo->GetData());
			if (base == nullptr) {
				continue;
			}

			std::size_t elementOffset = baseOffset
			    + static_cast<std::size_t>(index) * static_cast<std::size_t>(attr.stride)
			    + attr.offset;
			const std::uint8_t* src = base + elementOffset;

			auto readFloat = [&](std::int32_t comp) -> float {
				switch (attr.type) {
					case VertexCompType::Float:
						return reinterpret_cast<const float*>(src)[comp];
					case VertexCompType::Short: {
						std::int16_t v;
						std::memcpy(&v, src + static_cast<std::size_t>(comp) * 2, 2);
						return attr.normalized ? v / 32767.0f : static_cast<float>(v);
					}
					case VertexCompType::UnsignedByte: {
						std::uint8_t v = src[comp];
						return attr.normalized ? v / 255.0f : static_cast<float>(v);
					}
					default:
						return 0.0f;
				}
			};

			switch (attr.index) {
				case 0:
					if (attr.size >= 1) { out.x = readFloat(0); }
					if (attr.size >= 2) { out.y = readFloat(1); }
					if (attr.size >= 3) { out.z = readFloat(2); }
					break;
				case 1:
					if (attr.size >= 1) { out.u = readFloat(0); }
					if (attr.size >= 2) { out.v = readFloat(1); }
					break;
				case 2:
					if (attr.type == VertexCompType::UnsignedByte && attr.size >= 4) {
						out.r = src[0]; out.g = src[1]; out.b = src[2]; out.a = src[3];
					} else if (attr.size >= 4) {
						out.r = static_cast<std::uint8_t>(readFloat(0) * 255.0f);
						out.g = static_cast<std::uint8_t>(readFloat(1) * 255.0f);
						out.b = static_cast<std::uint8_t>(readFloat(2) * 255.0f);
						out.a = static_cast<std::uint8_t>(readFloat(3) * 255.0f);
					}
					break;
				default:
					break;
			}
		}

		return out;
	}

	// -------------------------------------------------------------------------
	// Map nCine::RHI::BlendFactor to PS1 semi-transparency mode (0..3)
	//   0 = (B+F)/2   1 = B+F   2 = B-F   3 = B+F/4
	// These are rough approximations of the requested GL blend modes.
	// -------------------------------------------------------------------------
	static std::int32_t ToSemiTransMode(BlendFactor src, BlendFactor dst)
	{
		if (src == BlendFactor::One && dst == BlendFactor::One) {
			return 1; // additive
		}
		if (src == BlendFactor::Zero && dst == BlendFactor::One) {
			return 0; // average (50%)
		}
		if (src == BlendFactor::One && dst == BlendFactor::Zero) {
			return -1; // opaque — no transparency
		}
		return 0; // default to average
	}

	// -------------------------------------------------------------------------
	// Emit a textured quad (POLY_FT4) from 4 vertices into the OT.
	// PS1 quads expect vertices in screen order: TL, TR, BL, BR.
	// -------------------------------------------------------------------------
	static void EmitQuadTextured(const RawVert v[4], const Texture* tex, std::int32_t depth,
	                              bool blend, BlendFactor bSrc, BlendFactor bDst)
	{
		POLY_FT4* prim = AllocPrim<POLY_FT4>();
		setPolyFT4(prim);
		setRGB0(prim, v[0].r, v[0].g, v[0].b);

		setXY4(prim,
		    static_cast<short>(v[0].x), static_cast<short>(v[0].y),
		    static_cast<short>(v[1].x), static_cast<short>(v[1].y),
		    static_cast<short>(v[2].x), static_cast<short>(v[2].y),
		    static_cast<short>(v[3].x), static_cast<short>(v[3].y)
		);

		if (tex != nullptr) {
			std::int32_t tw = tex->GetWidth();
			std::int32_t th = tex->GetHeight();
			setUV4(prim,
			    static_cast<std::uint8_t>(v[0].u * (tw - 1)),
			    static_cast<std::uint8_t>(v[0].v * (th - 1)),
			    static_cast<std::uint8_t>(v[1].u * (tw - 1)),
			    static_cast<std::uint8_t>(v[1].v * (th - 1)),
			    static_cast<std::uint8_t>(v[2].u * (tw - 1)),
			    static_cast<std::uint8_t>(v[2].v * (th - 1)),
			    static_cast<std::uint8_t>(v[3].u * (tw - 1)),
			    static_cast<std::uint8_t>(v[3].v * (th - 1))
			);
			prim->tpage = tex->GetTPage();
			prim->clut  = tex->GetClut();
		}

		if (blend) {
			std::int32_t mode = ToSemiTransMode(bSrc, bDst);
			if (mode >= 0) {
				setSemiTrans(prim, 1);
				setABRMode(prim, mode);
			}
		}

		std::int32_t clampedDepth = depth < 0 ? 0 : (depth >= kOtLen ? kOtLen - 1 : depth);
		addPrim(&s_ot[s_state.bufIdx][clampedDepth], prim);
	}

	// -------------------------------------------------------------------------
	// Emit a flat-shaded quad (POLY_F4) from 4 vertices
	// -------------------------------------------------------------------------
	static void EmitQuadFlat(const RawVert v[4], std::int32_t depth,
	                          bool blend, BlendFactor bSrc, BlendFactor bDst)
	{
		POLY_F4* prim = AllocPrim<POLY_F4>();
		setPolyF4(prim);
		setRGB0(prim, v[0].r, v[0].g, v[0].b);

		setXY4(prim,
		    static_cast<short>(v[0].x), static_cast<short>(v[0].y),
		    static_cast<short>(v[1].x), static_cast<short>(v[1].y),
		    static_cast<short>(v[2].x), static_cast<short>(v[2].y),
		    static_cast<short>(v[3].x), static_cast<short>(v[3].y)
		);

		if (blend) {
			std::int32_t mode = ToSemiTransMode(bSrc, bDst);
			if (mode >= 0) {
				setSemiTrans(prim, 1);
				setABRMode(prim, mode);
			}
		}

		std::int32_t clampedDepth = depth < 0 ? 0 : (depth >= kOtLen ? kOtLen - 1 : depth);
		addPrim(&s_ot[s_state.bufIdx][clampedDepth], prim);
	}

	// -------------------------------------------------------------------------
	// Convert a run of indexed triangles to quads for the OT submission.
	// The PS1 GPU works best with quads; triangles are submitted individually
	// as POLY_GT3 (Gouraud + texture) or POLY_G3 (Gouraud flat).
	// -------------------------------------------------------------------------
	static void EmitTriangle(const RawVert& r0, const RawVert& r1, const RawVert& r2,
	                          const Texture* tex, std::int32_t depth,
	                          bool blend, BlendFactor bSrc, BlendFactor bDst)
	{
		// Pad to a degenerate quad by duplicating the last vertex
		const RawVert verts[4] = { r0, r1, r2, r2 };
		if (tex != nullptr) {
			EmitQuadTextured(verts, tex, depth, blend, bSrc, bDst);
		} else {
			EmitQuadFlat(verts, depth, blend, bSrc, bDst);
		}
	}

	// =========================================================================
	// Draw
	// =========================================================================
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		const DrawContext& ctx = s_state.ctx;
		if (ctx.vertexFormat == nullptr || count <= 0) {
			return;
		}

		Texture* tex = ctx.textures[0];
		std::int32_t depth = ctx.otDepth;

		switch (type) {
			case PrimitiveType::TriangleStrip:
				if (count >= 4) {
					// Triangle strip → emit as quads (pairs of triangles)
					for (std::int32_t i = 0; i + 3 < count; i += 2) {
						RawVert verts[4];
						for (std::int32_t j = 0; j < 4; ++j) {
							verts[j] = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + j);
						}
						if (tex != nullptr) {
							EmitQuadTextured(verts, tex, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
						} else {
							EmitQuadFlat(verts, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
						}
					}
				} else if (count == 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + 1);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + 2);
					EmitTriangle(v0, v1, v2, tex, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 1);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 2);
					EmitTriangle(v0, v1, v2, tex, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
				}
				break;
			default:
				break;
		}
	}

	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		for (std::int32_t i = 0; i < instanceCount; ++i) {
			Draw(type, firstVertex + i * count, count);
		}
	}

	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex)
	{
		const DrawContext& ctx = s_state.ctx;
		if (ctx.vertexFormat == nullptr || count <= 0 || indexOffset == nullptr) {
			return;
		}

		Texture* tex = ctx.textures[0];
		std::int32_t depth = ctx.otDepth;
		const std::uint16_t* indices = static_cast<const std::uint16_t*>(indexOffset);

		switch (type) {
			case PrimitiveType::TriangleStrip:
				for (std::int32_t i = 0; i + 3 < count; i += 2) {
					RawVert verts[4];
					for (std::int32_t j = 0; j < 4; ++j) {
						verts[j] = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + j]);
					}
					if (tex != nullptr) {
						EmitQuadTextured(verts, tex, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
					} else {
						EmitQuadFlat(verts, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
					}
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i]);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 1]);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 2]);
					EmitTriangle(v0, v1, v2, tex, depth, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
				}
				break;
			default:
				break;
		}
	}

	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex)
	{
		for (std::int32_t i = 0; i < instanceCount; ++i) {
			DrawIndexed(type, count, indexOffset, baseVertex + i * count);
		}
	}

	// =========================================================================
	// Texture bind
	// =========================================================================
	void BindTexture(Texture& tex, std::uint32_t unit)
	{
		if (unit < static_cast<std::uint32_t>(Texture::MaxTextureUnitsConst)) {
			s_state.ctx.textures[unit] = &tex;
		}
	}

	// =========================================================================
	// Render-state setters
	// =========================================================================

	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst)
	{
		s_state.ctx.blendingEnabled = enabled;
		s_state.ctx.blendSrc = src;
		s_state.ctx.blendDst = dst;
	}

	void SetDepthTest(bool enabled)
	{
		// PS1 has no hardware Z-buffer; depth ordering is via the OT.
		s_state.depthTestEnabled = enabled;
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		s_state.ctx.scissorEnabled = enabled;
		s_state.ctx.scissorRect = nCine::Recti(x, y, width, height);
		if (enabled) {
			// PS1 GPU scissor via DrawArea — requires modifying the DrawEnv
			// Typically set at init time; runtime changes require ResetGraph(0).
			// This is a best-effort stub.
			DRAWENV denv;
			GetDrawEnv(&denv);
			setRECT(&denv.clip, static_cast<short>(x), static_cast<short>(y),
			        static_cast<short>(width), static_cast<short>(height));
			PutDrawEnv(&denv);
		} else {
			DRAWENV denv;
			GetDrawEnv(&denv);
			setRECT(&denv.clip,
			    static_cast<short>(s_state.viewportX),
			    static_cast<short>(s_state.viewportY),
			    static_cast<short>(s_state.viewportW),
			    static_cast<short>(s_state.viewportH));
			PutDrawEnv(&denv);
		}
	}

	void SetScissorTest(bool enabled, const nCine::Recti& rect)
	{
		SetScissorTest(enabled, rect.X, rect.Y, rect.W, rect.H);
	}

	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		s_state.viewportX = x;
		s_state.viewportY = y;
		s_state.viewportW = width;
		s_state.viewportH = height;
	}

	void SetClearColor(float r, float g, float b, float /*a*/)
	{
		s_state.clearColor =
		    (static_cast<std::uint32_t>(r * 255.0f) << 16) |
		    (static_cast<std::uint32_t>(g * 255.0f) <<  8) |
		     static_cast<std::uint32_t>(b * 255.0f);
	}

	void Clear(ClearFlags flags)
	{
		if (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(ClearFlags::Color)) {
			// Fill framebuffer rectangle using FILL primitive
			FILL* fill = AllocPrim<FILL>();
			setFill(fill);
			setRGB0(fill,
			    static_cast<std::uint8_t>((s_state.clearColor >> 16) & 0xFF),
			    static_cast<std::uint8_t>((s_state.clearColor >>  8) & 0xFF),
			    static_cast<std::uint8_t>( s_state.clearColor        & 0xFF));
			setXY0(fill,
			    static_cast<short>(s_state.viewportX),
			    static_cast<short>(s_state.viewportY));
			setWH(fill,
			    static_cast<short>(s_state.viewportW),
			    static_cast<short>(s_state.viewportH));
			addPrim(&s_ot[s_state.bufIdx][kOtLen - 1], fill); // at back of OT
		}
		// Depth clears are implicit: OT is reset at frame start.
	}

	ScissorState GetScissorState()
	{
		const auto& r = s_state.ctx.scissorRect;
		return { s_state.ctx.scissorEnabled, r.X, r.Y, r.W, r.H };
	}

	void SetScissorState(const ScissorState& state)
	{
		SetScissorTest(state.enabled, state.x, state.y, state.w, state.h);
	}

} // namespace nCine::RHI

#endif