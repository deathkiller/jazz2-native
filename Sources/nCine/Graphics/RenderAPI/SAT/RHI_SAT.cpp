#include "RHI_SAT.h"

// ============================================================================
// Sega Saturn VDP1 draw call implementations (libyaul)
//
// The Saturn VDP1 is a sprite/polygon processor driven by a command list
// stored in VDP1 VRAM.  Each frame, a command list is built in CPU memory
// and then copied (or DMA'd) to VDP1 VRAM, after which VDP1 is triggered
// to render via vdp1_sync_render().
//
// VDP1 command types used here:
//   CMDPMOD / CMDCTRL / CMDSRCA / CMDSIZE / CMDXA+XB+XC+XD+YA+YB+YC+YD
//   - NORMAL_SPRITE (textured, axis-aligned — fastest path for 2D sprites)
//   - DISTORTED_SPRITE (textured quad, arbitrary corners)
//   - POLYGON (flat-shaded untextured quad)
//   - SYSTEM_CLIP / LOCAL_COORDINATE — set by init, not per draw
//
// Screen-space coordinates on Saturn are signed 16-bit integers measured
// from the LOCAL COORDINATE origin (set during init to the top-left corner
// so game coordinates match 1:1; the default Saturn origin is screen center).
//
// Vertex layout assumptions (from RenderBatcher):
//   float x, y, z    — screen-space pixel coordinates (top-left origin)
//   float u, v        — texture coordinates 0..1
//   uint8 r, g, b, a  — colour
// ============================================================================

namespace Rhi
{
	// -------------------------------------------------------------------------
	// VDP1 command table — one entry per draw call, stored in SH2 RAM and
	// copied to VDP1 VRAM at frame-end before triggering rendering.
	// -------------------------------------------------------------------------
	static vdp1_cmdt_t s_cmdList[kCmdListLen];
	static std::int32_t s_cmdCount = 0;

	static struct State {
		DrawContext   ctx;
		color_rgb1555_t clearColor = { .raw = 0x8000 }; // black with end flag
		std::int32_t  viewportX   = 0;
		std::int32_t  viewportY   = 0;
		std::int32_t  viewportW   = kScreenW;
		std::int32_t  viewportH   = kScreenH;
		bool          depthTestEnabled = false;
	} s_state;

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
	// Convert top-left screen coords to VDP1 int16 coords (local origin)
	// -------------------------------------------------------------------------
	static inline std::int16_t ToVdpX(float x) { return static_cast<std::int16_t>(x); }
	static inline std::int16_t ToVdpY(float y) { return static_cast<std::int16_t>(y); }

	// -------------------------------------------------------------------------
	// Emit a textured distorted sprite command from 4 vertices (TL,TR,BL,BR)
	// -------------------------------------------------------------------------
	static void EmitSpriteTextured(const RawVert v[4], const Texture* tex,
	                                bool blend, BlendFactor /*bSrc*/, BlendFactor /*bDst*/)
	{
		if (s_cmdCount >= kCmdListLen) {
			return;
		}

		vdp1_cmdt_t& cmd = s_cmdList[s_cmdCount++];
		std::memset(&cmd, 0, sizeof(cmd));

		vdp1_cmdt_distorted_sprite_set(&cmd);

		// Vertex order for VDP1 distorted sprite: D, A, B, C (CCW from top-left)
		const vdp1_coords_t coords[4] = {
			{ ToVdpX(v[0].x), ToVdpY(v[0].y) }, // A = TL
			{ ToVdpX(v[1].x), ToVdpY(v[1].y) }, // B = TR
			{ ToVdpX(v[3].x), ToVdpY(v[3].y) }, // C = BR
			{ ToVdpX(v[2].x), ToVdpY(v[2].y) }, // D = BL
		};
		vdp1_cmdt_distorted_sprite_vertices_set(&cmd, coords);

		if (tex != nullptr && tex->GetCharBase() != 0) {
			// Character base address (word offset / 4 in latest libyaul convention)
			vdp1_cmdt_char_base_set(&cmd, tex->GetCharBase());
			vdp1_cmdt_char_size_set(&cmd,
			    static_cast<uint16_t>(tex->GetWidth() / 8),
			    static_cast<uint16_t>(tex->GetHeight()));
			vdp1_cmdt_color_mode2_set(&cmd, tex->GetColorMode(), 0,
			    reinterpret_cast<void*>(static_cast<uintptr_t>(tex->GetClutOffset())));
		}

		if (blend) {
			// Semi-transparency mode 0 (avg) as default blend approximation
			vdp1_cmdt_pmod_semi_trans_set(&cmd);
		}
	}

	// -------------------------------------------------------------------------
	// Emit an untextured flat polygon command from 4 vertices
	// -------------------------------------------------------------------------
	static void EmitPolygonFlat(const RawVert v[4], bool blend)
	{
		if (s_cmdCount >= kCmdListLen) {
			return;
		}

		vdp1_cmdt_t& cmd = s_cmdList[s_cmdCount++];
		std::memset(&cmd, 0, sizeof(cmd));

		vdp1_cmdt_polygon_set(&cmd);

		const vdp1_coords_t coords[4] = {
			{ ToVdpX(v[0].x), ToVdpY(v[0].y) },
			{ ToVdpX(v[1].x), ToVdpY(v[1].y) },
			{ ToVdpX(v[3].x), ToVdpY(v[3].y) },
			{ ToVdpX(v[2].x), ToVdpY(v[2].y) },
		};
		vdp1_cmdt_polygon_vertices_set(&cmd, coords);

		// Convert RGBA8 colour to RGB1555
		color_rgb1555_t color;
		color.raw = 0;
		color.r   = v[0].r >> 3;
		color.g   = v[0].g >> 3;
		color.b   = v[0].b >> 3;
		vdp1_cmdt_color_set(&cmd, color);

		if (blend) {
			vdp1_cmdt_pmod_semi_trans_set(&cmd);
		}
	}

	// -------------------------------------------------------------------------
	// Triangle → degenerate quad (duplicate last vertex)
	// -------------------------------------------------------------------------
	static void EmitTriangle(const RawVert& r0, const RawVert& r1, const RawVert& r2,
	                          const Texture* tex, bool blend, BlendFactor bSrc, BlendFactor bDst)
	{
		const RawVert verts[4] = { r0, r1, r2, r2 };
		if (tex != nullptr) {
			EmitSpriteTextured(verts, tex, blend, bSrc, bDst);
		} else {
			EmitPolygonFlat(verts, blend);
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

		switch (type) {
			case PrimitiveType::TriangleStrip:
				if (count >= 4) {
					for (std::int32_t i = 0; i + 3 < count; i += 2) {
						RawVert verts[4];
						for (std::int32_t j = 0; j < 4; ++j) {
							verts[j] = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + j);
						}
						if (tex != nullptr) {
							EmitSpriteTextured(verts, tex, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
						} else {
							EmitPolygonFlat(verts, ctx.blendingEnabled);
						}
					}
				} else if (count == 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + 1);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + 2);
					EmitTriangle(v0, v1, v2, tex, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 1);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, firstVertex + i + 2);
					EmitTriangle(v0, v1, v2, tex, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
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
		const std::uint16_t* indices = static_cast<const std::uint16_t*>(indexOffset);

		switch (type) {
			case PrimitiveType::TriangleStrip:
				for (std::int32_t i = 0; i + 3 < count; i += 2) {
					RawVert verts[4];
					for (std::int32_t j = 0; j < 4; ++j) {
						verts[j] = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + j]);
					}
					if (tex != nullptr) {
						EmitSpriteTextured(verts, tex, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
					} else {
						EmitPolygonFlat(verts, ctx.blendingEnabled);
					}
				}
				break;
			case PrimitiveType::Triangles:
				for (std::int32_t i = 0; i + 2 < count; i += 3) {
					RawVert v0 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i]);
					RawVert v1 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 1]);
					RawVert v2 = ReadVertex(*ctx.vertexFormat, ctx.vboByteOffset, baseVertex + indices[i + 2]);
					EmitTriangle(v0, v1, v2, tex, ctx.blendingEnabled, ctx.blendSrc, ctx.blendDst);
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
		// VDP1 does not have a hardware Z-buffer; command order determines visibility.
		s_state.depthTestEnabled = enabled;
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		s_state.ctx.scissorEnabled = enabled;
		s_state.ctx.scissorRect = nCine::Recti(x, y, width, height);

		if (s_cmdCount < kCmdListLen) {
			vdp1_cmdt_t& cmd = s_cmdList[s_cmdCount++];
			std::memset(&cmd, 0, sizeof(cmd));

			vdp1_cmdt_system_clip_coord_set(&cmd);
			const vdp1_coords_t clipCoord = {
				static_cast<std::int16_t>(enabled ? (x + width - 1)  : (s_state.viewportW - 1)),
				static_cast<std::int16_t>(enabled ? (y + height - 1) : (s_state.viewportH - 1))
			};
			vdp1_cmdt_system_clip_coord_coords_set(&cmd, &clipCoord);
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
		// VDB2 backdrop colour is closest to a "clear" on Saturn.
		// Approximated here; the true call is vdp2_scrn_back_color_set().
		s_state.clearColor.r = static_cast<uint8_t>(r * 31.0f);
		s_state.clearColor.g = static_cast<uint8_t>(g * 31.0f);
		s_state.clearColor.b = static_cast<uint8_t>(b * 31.0f);
	}

	void Clear(ClearFlags flags)
	{
		if (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(ClearFlags::Color)) {
			// Set VDP2 back-screen colour (the layer beneath all VDP1 output)
			vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), s_state.clearColor);
		}
		// Full clear of the frame buffer: reset the VDP1 command list counter.
		if (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(ClearFlags::Depth)) {
			s_cmdCount = 0;
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

} // namespace Rhi
