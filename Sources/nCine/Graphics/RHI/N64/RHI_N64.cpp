#if defined(WITH_RHI_N64)

#include "RHI_N64.h"

// ============================================================================
// Nintendo 64 RDP draw call implementations (libdragon rdpq API)
//
// The N64 Reality Signal Processor (RSP) runs microcode that transforms
// vertices and feeds the Reality Display Processor (RDP) rasteriser.
// libdragon's rdpq layer abstracts the RDP command FIFO, TMEM management,
// and the combiner / blender configuration.
//
// Vertex layout assumptions (as set up by RenderBatcher / Geometry):
//   float x, y, z         — projected screen-space position (pixels)
//   float u, v             — texture coordinates (0..1)
//   uint8 r, g, b, a      — tint colour (used as shade colour in combiner)
//
// All transforms between object- and screen-space happen in software before
// this layer; we only submit 2-D screen positions to the RDP.
// ============================================================================

namespace nCine::RHI
{
	// -------------------------------------------------------------------------
	// File-local render state
	// -------------------------------------------------------------------------
	static struct State {
		DrawContext  ctx;
		bool         depthTestEnabled  = false;
		color_t      clearColor        = { 0, 0, 0, 255 };
		std::int32_t viewportX         = 0;
		std::int32_t viewportY         = 0;
		std::int32_t viewportW         = 320;
		std::int32_t viewportH         = 240;
	} s_state;

	// -------------------------------------------------------------------------
	// Helper: read one vertex from the active vertex buffer
	// -------------------------------------------------------------------------
	struct RawVert { float x, y, z, u, v; std::uint32_t rgba; };

	static RawVert ReadVertex(const VertexFormat& fmt, std::size_t baseOffset, std::int32_t index)
	{
		RawVert out = { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0xFFFFFFFFu };

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
				case 0: // position
					if (attr.size >= 1) { out.x = readFloat(0); }
					if (attr.size >= 2) { out.y = readFloat(1); }
					if (attr.size >= 3) { out.z = readFloat(2); }
					break;
				case 1: // texture coords
					if (attr.size >= 1) { out.u = readFloat(0); }
					if (attr.size >= 2) { out.v = readFloat(1); }
					break;
				case 2: // colour
					if (attr.type == VertexCompType::UnsignedByte && attr.size == 4) {
						out.rgba = (static_cast<std::uint32_t>(src[0]) << 24)
						         | (static_cast<std::uint32_t>(src[1]) << 16)
						         | (static_cast<std::uint32_t>(src[2]) <<  8)
						         |  static_cast<std::uint32_t>(src[3]);
					} else if (attr.size >= 4) {
						auto r = static_cast<std::uint32_t>(readFloat(0) * 255.0f);
						auto g = static_cast<std::uint32_t>(readFloat(1) * 255.0f);
						auto b = static_cast<std::uint32_t>(readFloat(2) * 255.0f);
						auto a = static_cast<std::uint32_t>(readFloat(3) * 255.0f);
						out.rgba = (r << 24) | (g << 16) | (b << 8) | a;
					}
					break;
				default:
					break;
			}
		}

		return out;
	}

	// -------------------------------------------------------------------------
	// Apply the current rdpq blender / combiner state before each draw call.
	// -------------------------------------------------------------------------
	static void ApplyRdpState(const DrawContext& ctx)
	{
		Texture* tex = ctx.textures[0];

		if (tex != nullptr && tex->GetPixelData() != nullptr) {
			// Configure combine mode: TEXTURE * SHADE + 0
			rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);

			rdpq_filter_t filter = Texture::ToNativeFilter(ctx.ff.hasTexture
			    ? tex->GetMinFilter() : tex->GetMagFilter());
			rdpq_mode_filter(filter);

			// Load texture into TMEM tile 0
			surface_t surf = surface_make(
			    const_cast<void*>(tex->GetPixelData()),
			    tex->GetNativeFormat(),
			    static_cast<uint32_t>(tex->GetWidth()),
			    static_cast<uint32_t>(tex->GetHeight()),
			    static_cast<uint32_t>(tex->GetWidth()) * static_cast<uint32_t>(Texture::BytesPerPixel(tex->GetNativeFormat()))
			);
			rdpq_tex_upload(TILE0, &surf, nullptr);

			if (ctx.blendingEnabled) {
				rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
			} else {
				rdpq_mode_blender(0);
			}
		} else {
			// Flat / shade-only colour
			rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
			if (ctx.blendingEnabled) {
				rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
			} else {
				rdpq_mode_blender(0);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Submit a single triangle to the RDP from three pre-read vertices.
	// -------------------------------------------------------------------------
	static void EmitTriangle(const RawVert& v0, const RawVert& v1, const RawVert& v2, bool hasTexture)
	{
		// rdpq_triangle with shade + optional texture
		// Vertex layout: {x, y, z, w, r, g, b, a, s, t}
		//   z in 0..1 (depthbuffer), w = 1/z for perspective
		//   s,t are ST coordinates in fixed-point (pixels in TMEM)
		rdpq_vertex_t rdpv[3];
		const RawVert* src[3] = { &v0, &v1, &v2 };

		for (std::int32_t i = 0; i < 3; ++i) {
			rdpv[i].x = src[i]->x;
			rdpv[i].y = src[i]->y;
			rdpv[i].z = src[i]->z;
			rdpv[i].w = (src[i]->z > 0.0f) ? (1.0f / src[i]->z) : 1.0f;
			rdpv[i].r = static_cast<float>((src[i]->rgba >> 24) & 0xFF) / 255.0f;
			rdpv[i].g = static_cast<float>((src[i]->rgba >> 16) & 0xFF) / 255.0f;
			rdpv[i].b = static_cast<float>((src[i]->rgba >>  8) & 0xFF) / 255.0f;
			rdpv[i].a = static_cast<float>( src[i]->rgba        & 0xFF) / 255.0f;
			if (hasTexture) {
				rdpv[i].s = src[i]->u;
				rdpv[i].t = src[i]->v;
			}
		}

		std::uint32_t triFlags = RDPQ_TRIANGLE_SHADE;
		if (hasTexture) {
			triFlags |= RDPQ_TRIANGLE_TEXCOORDS;
		}
		rdpq_triangle(triFlags, &rdpv[0], &rdpv[1], &rdpv[2]);
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

		ApplyRdpState(ctx);
		bool hasTex = (ctx.textures[0] != nullptr && ctx.textures[0]->GetPixelData() != nullptr);

		switch (type) {
			case PrimitiveType::TriangleStrip:
				// Submit as a fan of triangles: (0,i,i+1)
				for (std::int32_t i = 1; i + 1 < count; ++i) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 1);
					EmitTriangle(v0, v1, v2, hasTex);
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 1);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 2);
					EmitTriangle(v0, v1, v2, hasTex);
				}
				break;
			default:
				break;
		}
	}

	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		// N64 has no hardware instancing; emulate by repeated draws
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

		ApplyRdpState(ctx);
		bool hasTex = (ctx.textures[0] != nullptr && ctx.textures[0]->GetPixelData() != nullptr);
		const std::uint16_t* indices = static_cast<const std::uint16_t*>(indexOffset);

		switch (type) {
			case PrimitiveType::TriangleStrip:
				for (std::int32_t i = 1; i + 1 < count; ++i) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[0]);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i]);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 1]);
					EmitTriangle(v0, v1, v2, hasTex);
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i]);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 1]);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 2]);
					EmitTriangle(v0, v1, v2, hasTex);
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
	// Texture bind — loads texture into TMEM immediately (rdpq handles caching)
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
		s_state.depthTestEnabled = enabled;
		if (enabled) {
			rdpq_mode_zbuf(true, true);
		} else {
			rdpq_mode_zbuf(false, false);
		}
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		s_state.ctx.scissorEnabled = enabled;
		s_state.ctx.scissorRect = nCine::Recti(x, y, width, height);
		if (enabled) {
			rdpq_set_scissor(x, y, x + width, y + height);
		} else {
			rdpq_set_scissor(s_state.viewportX, s_state.viewportY,
			                 s_state.viewportX + s_state.viewportW,
			                 s_state.viewportY + s_state.viewportH);
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
		rdpq_set_scissor(x, y, x + width, y + height);
	}

	void SetClearColor(float r, float g, float b, float a)
	{
		s_state.clearColor = RGBA32(
		    static_cast<std::uint8_t>(r * 255.0f),
		    static_cast<std::uint8_t>(g * 255.0f),
		    static_cast<std::uint8_t>(b * 255.0f),
		    static_cast<std::uint8_t>(a * 255.0f)
		);
	}

	void Clear(ClearFlags flags)
	{
		if (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(ClearFlags::Color)) {
			rdpq_set_fill_color(s_state.clearColor);
			rdpq_fill_rectangle(
			    static_cast<float>(s_state.viewportX),
			    static_cast<float>(s_state.viewportY),
			    static_cast<float>(s_state.viewportX + s_state.viewportW),
			    static_cast<float>(s_state.viewportY + s_state.viewportH)
			);
		}
		// Depth clear: the Z buffer is cleared by display_attach before each frame,
		// or explicitly with rdpq_clear_z when WITH_ZBUF is set.
		if (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(ClearFlags::Depth)) {
			rdpq_clear_z();
		}
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