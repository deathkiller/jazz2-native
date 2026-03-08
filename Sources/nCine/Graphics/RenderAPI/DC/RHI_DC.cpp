#include "RHI_DC.h"

// ============================================================================
// Dreamcast PVR draw call implementations
//
// The Dreamcast PVR does not use a traditional draw-call model.
// Instead, vertex data is streamed into one of three display lists per frame.
// This implementation converts generic Rhi vertex buffers into pvr_vertex_t
// structures and submits them using pvr_prim().
//
// Vertex layout assumptions (as set up by RenderBatcher / Geometry):
//   float x, y, z         — position
//   float u, v             — texture coordinates (optional)
//   uint8 r, g, b, a      — colour (packed as ARGB in pvr_vertex_t)
// ============================================================================

namespace Rhi
{
	// Thread-local (single-threaded on DC) render state
	static struct State {
		DrawContext  ctx;
		bool         depthTestEnabled = false;
		std::uint32_t clearColor      = 0xFF000000u; // ARGB black
	} s_state;

	// -------------------------------------------------------------------------
	// Helper: unpack a generic vertex from the active Buffer + VertexFormat
	// -------------------------------------------------------------------------
	struct RawVert { float x, y, z, u, v; std::uint32_t argb; };

	/// Reads one vertex at @p index from the VBO described by @p fmt.
	/// Returns a plain-struct with position, UV, and packed ARGB colour.
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

			std::size_t elementOffset = baseOffset + static_cast<std::size_t>(index) * static_cast<std::size_t>(attr.stride) + attr.offset;
			const std::uint8_t* src = base + elementOffset;

			auto readFloat = [&](std::int32_t component) -> float {
				switch (attr.type) {
					case VertexCompType::Float:
						return reinterpret_cast<const float*>(src)[component];
					case VertexCompType::Short: {
						std::int16_t v;
						std::memcpy(&v, src + static_cast<std::size_t>(component) * 2, 2);
						return attr.normalized ? v / 32767.0f : static_cast<float>(v);
					}
					case VertexCompType::UnsignedByte: {
						std::uint8_t v = src[component];
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
						std::uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
						out.argb = (static_cast<std::uint32_t>(a) << 24) |
						           (static_cast<std::uint32_t>(r) << 16) |
						           (static_cast<std::uint32_t>(g) << 8)  |
						            static_cast<std::uint32_t>(b);
					} else if (attr.size >= 4) {
						auto r = static_cast<std::uint32_t>(readFloat(0) * 255.0f);
						auto g = static_cast<std::uint32_t>(readFloat(1) * 255.0f);
						auto b = static_cast<std::uint32_t>(readFloat(2) * 255.0f);
						auto a = static_cast<std::uint32_t>(readFloat(3) * 255.0f);
						out.argb = (a << 24) | (r << 16) | (g << 8) | b;
					}
					break;
				default:
					break;
			}
		}

		return out;
	}

	// -------------------------------------------------------------------------
	// Emit a pvr_vertex_t to the active PVR list.
	// @p eol  true for the last vertex in a strip/list.
	// -------------------------------------------------------------------------
	static void EmitVertex(const RawVert& v, bool eol)
	{
		pvr_vertex_t pv;
		pv.flags = eol ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		pv.x = v.x;
		pv.y = v.y;
		// PVR uses 1/Z perspective; clamp to avoid divide-by-zero
		pv.z = (v.z > 0.0f) ? (1.0f / v.z) : 1.0f;
		pv.u    = v.u;
		pv.v    = v.v;
		pv.argb = v.argb;
		pv.oargb = 0; // no offset colour
		pvr_prim(&pv, sizeof(pv));
	}

	// -------------------------------------------------------------------------
	// Draw — main entry point for non-indexed draws
	// -------------------------------------------------------------------------
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		const DrawContext& ctx = s_state.ctx;
		if (ctx.vertexFormat == nullptr || count <= 0) {
			return;
		}

		// Build and submit the polygon header for this material
		pvr_poly_hdr_t hdr;
		BuildPvrHeader(hdr, ctx);
		pvr_prim(&hdr, sizeof(hdr));

		switch (type) {
			case PrimitiveType::TriangleStrip:
				for (std::int32_t i = 0; i < count; ++i) {
					RawVert v = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i);
					EmitVertex(v, i == count - 1);
				}
				break;
			case PrimitiveType::Triangles:
				// Submit as individual triangles, each being a 3-vertex strip
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					pvr_poly_hdr_t triHdr;
					BuildPvrHeader(triHdr, ctx);
					pvr_prim(&triHdr, sizeof(triHdr));
					for (std::int32_t v = 0; v < 3; ++v) {
						RawVert vert = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + v);
						EmitVertex(vert, v == 2);
					}
				}
				break;
			default:
				// Fan/Lines/Points not natively supported; skip
				break;
		}
	}

	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		// DC has no hardware instancing; emulate by calling Draw repeatedly
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

		pvr_poly_hdr_t hdr;
		BuildPvrHeader(hdr, ctx);
		pvr_prim(&hdr, sizeof(hdr));

		// 16-bit indices assumed (GLushort)
		const std::uint16_t* indices = static_cast<const std::uint16_t*>(indexOffset);

		switch (type) {
			case PrimitiveType::TriangleStrip:
				for (std::int32_t i = 0; i < count; ++i) {
					RawVert v = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i]);
					EmitVertex(v, i == count - 1);
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					pvr_poly_hdr_t triHdr;
					BuildPvrHeader(triHdr, ctx);
					pvr_prim(&triHdr, sizeof(triHdr));
					for (std::int32_t v = 0; v < 3; ++v) {
						RawVert vert = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + v]);
						EmitVertex(vert, v == 2);
					}
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
		// PVR depth test is configured per-polygon in pvr_poly_cxt_t.gen.zbuffer
		// Handled implicitly by the list type (OP sorts by depth automatically)
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		s_state.ctx.scissorEnabled = enabled;
		s_state.ctx.scissorRect = nCine::Recti(x, y, width, height);
		// DC PVR tile clip can emulate scissor; not yet wired
	}

	void SetScissorTest(bool enabled, const nCine::Recti& rect)
	{
		SetScissorTest(enabled, rect.X, rect.Y, rect.W, rect.H);
	}

	void SetViewport(std::int32_t /*x*/, std::int32_t /*y*/, std::int32_t /*width*/, std::int32_t /*height*/)
	{
		// DC renders to the full 640×480 framebuffer; viewport changes not supported
	}

	void SetClearColor(float r, float g, float b, float a)
	{
		auto ur = static_cast<std::uint32_t>(r * 255.0f);
		auto ug = static_cast<std::uint32_t>(g * 255.0f);
		auto ub = static_cast<std::uint32_t>(b * 255.0f);
		auto ua = static_cast<std::uint32_t>(a * 255.0f);
		s_state.clearColor = (ua << 24) | (ur << 16) | (ug << 8) | ub;
		pvr_set_bg_color(r, g, b);
	}

	void Clear(ClearFlags flags)
	{
		// PVR clears the background automatically via pvr_set_bg_color().
		// Colour clearing is a no-op here; depth is always cleared per-frame.
		(void)flags;
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

} // namespace Rhi
