#if defined(WITH_RHI_SW)

#include "RHI_SW.h"

#include <cstring>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <vector>

namespace nCine::RHI
{
	// =========================================================================
	// Global SW render state
	// =========================================================================
	namespace
	{
		struct SWState
		{
			// Active (current) colour buffer — may point to mainColorBuffer or a bound texture
			std::uint8_t* colorBuffer   = nullptr;
			std::int32_t  bufferWidth   = 0;
			std::int32_t  bufferHeight  = 0;

			// Heap-allocated main window output buffer (never redirected)
			std::uint8_t* mainColorBuffer  = nullptr;
			std::int32_t  mainBufferWidth  = 0;
			std::int32_t  mainBufferHeight = 0;
			bool          isFboTarget      = false;

			// Depth buffer (float, same dimensions)
			float*        depthBuffer   = nullptr;

			bool   depthTestEnabled  = false;
			bool   blendingEnabled   = false;
			BlendFactor blendSrc     = BlendFactor::SrcAlpha;
			BlendFactor blendDst     = BlendFactor::OneMinusSrcAlpha;
			bool   scissorEnabled    = false;
			std::int32_t  scissorX   = 0;
			std::int32_t  scissorY   = 0;
			std::int32_t  scissorW   = 0;
			std::int32_t  scissorH   = 0;

			float clearR = 0, clearG = 0, clearB = 0, clearA = 1;

			// Viewport
			std::int32_t viewportX = 0;
			std::int32_t viewportY = 0;
			std::int32_t viewportW = 0;
			std::int32_t viewportH = 0;

			// Active draw context for the current draw call
			const DrawContext* drawCtx = nullptr;
		};

		SWState g_state;
	}

	// =========================================================================
	// Texture implementation
	// =========================================================================
	void Texture::UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height, TextureFormat format,
	                        const void* data, std::size_t size)
	{
		if (mipLevel < 0 || mipLevel >= MaxMips) return;

		mips_[mipLevel].width  = width;
		mips_[mipLevel].height = height;

		std::size_t byteSize = size;
		// Only RGBA8 is directly stored; other formats converted at upload
		if (format == TextureFormat::RGBA8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		} else if (format == TextureFormat::RGB8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		}

		mips_[mipLevel].data = std::make_unique<std::uint8_t[]>(byteSize);

		if (data != nullptr) {
			if (format == TextureFormat::RGBA8) {
				std::memcpy(mips_[mipLevel].data.get(), data, byteSize);
			} else if (format == TextureFormat::RGB8) {
				// Convert RGB8 → RGBA8
				const std::uint8_t* src  = static_cast<const std::uint8_t*>(data);
				std::uint8_t*       dst  = mips_[mipLevel].data.get();
				std::int32_t pixels = width * height;
				for (std::int32_t i = 0; i < pixels; ++i) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
					src += 3;
					dst += 4;
				}
			} else {
				// Generic fallback: copy raw bytes
				std::memcpy(mips_[mipLevel].data.get(), data, size);
			}
		}

		if (mipLevel == 0) {
			width_    = width;
			height_   = height;
			format_   = format;
		}
		if (mipLevel + 1 > mipCount_) {
			mipCount_ = mipLevel + 1;
		}
	}

	const std::uint8_t* Texture::GetPixels(std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) return nullptr;
		return mips_[mipLevel].data.get();
	}

	std::uint8_t* Texture::GetMutablePixels(std::int32_t mipLevel)
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) return nullptr;
		return mips_[mipLevel].data.get();
	}

	void Texture::EnsureRenderTarget()
	{
		if (width_ <= 0 || height_ <= 0) return;
		MipLevel& m = mips_[0];
		if (m.data == nullptr || m.width != width_ || m.height != height_) {
			m.width  = width_;
			m.height = height_;
			m.data   = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(width_) * height_ * 4);
			if (mipCount_ < 1) mipCount_ = 1;
		}
	}

	nCine::Colorf Texture::Sample(float u, float v, std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) {
			return nCine::Colorf(1, 1, 1, 1);
		}

		const MipLevel& mip = mips_[mipLevel];
		if (mip.data == nullptr || mip.width == 0 || mip.height == 0) {
			return nCine::Colorf(1, 1, 1, 1);
		}

		// Apply wrapping
		auto wrapCoord = [](float t, SamplerWrapping mode) -> float {
			switch (mode) {
				case SamplerWrapping::Repeat:
					t -= std::floor(t);
					return t;
				case SamplerWrapping::MirroredRepeat: {
					float f = std::floor(t);
					t -= f;
					if (static_cast<std::int32_t>(f) & 1) t = 1.0f - t;
					return t;
				}
				case SamplerWrapping::ClampToEdge:
				default:
					return std::fmax(0.0f, std::fmin(1.0f, t));
			}
		};

		u = wrapCoord(u, wrapS_);
		v = wrapCoord(v, wrapT_);

		const std::int32_t px = static_cast<std::int32_t>(u * (mip.width  - 1) + 0.5f);
		const std::int32_t py = static_cast<std::int32_t>(v * (mip.height - 1) + 0.5f);
		const std::int32_t clampedX = std::max(0, std::min(mip.width  - 1, px));
		const std::int32_t clampedY = std::max(0, std::min(mip.height - 1, py));

		const std::uint8_t* pixel = mip.data.get() + (clampedY * mip.width + clampedX) * 4;
		return nCine::Colorf(pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f);
	}

	// =========================================================================
	// Render-state setters
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst)
	{
		g_state.blendingEnabled = enabled;
		g_state.blendSrc = src;
		g_state.blendDst = dst;
	}

	void SetDepthTest(bool enabled)
	{
		g_state.depthTestEnabled = enabled;
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.scissorEnabled = enabled;
		g_state.scissorX = x;
		g_state.scissorY = y;
		g_state.scissorW = width;
		g_state.scissorH = height;
	}

	void SetScissorTest(bool enabled, const nCine::Recti& rect)
	{
		SetScissorTest(enabled, rect.X, rect.Y, rect.W, rect.H);
	}

	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.viewportX = x;
		g_state.viewportY = y;
		g_state.viewportW = width;
		g_state.viewportH = height;
	}

	void SetClearColor(float r, float g, float b, float a)
	{
		g_state.clearR = r;
		g_state.clearG = g;
		g_state.clearB = b;
		g_state.clearA = a;
	}

	void Clear(ClearFlags flags)
	{
		const bool doColor = (static_cast<std::uint32_t>(flags & ClearFlags::Color) != 0);
		const bool doDepth = (static_cast<std::uint32_t>(flags & ClearFlags::Depth) != 0);

		if (doColor && g_state.colorBuffer != nullptr) {
			const std::uint8_t r = static_cast<std::uint8_t>(g_state.clearR * 255.0f);
			const std::uint8_t gv = static_cast<std::uint8_t>(g_state.clearG * 255.0f);
			const std::uint8_t b = static_cast<std::uint8_t>(g_state.clearB * 255.0f);
			const std::uint8_t a = static_cast<std::uint8_t>(g_state.clearA * 255.0f);
			std::int32_t totalPixels = g_state.bufferWidth * g_state.bufferHeight;
			std::uint8_t* dst = g_state.colorBuffer;
			for (std::int32_t i = 0; i < totalPixels; ++i) {
				dst[0] = r; dst[1] = gv; dst[2] = b; dst[3] = a;
				dst += 4;
			}
		}

		if (doDepth && g_state.depthBuffer != nullptr) {
			std::int32_t totalPixels = g_state.bufferWidth * g_state.bufferHeight;
			for (std::int32_t i = 0; i < totalPixels; ++i) {
				g_state.depthBuffer[i] = 1.0f;
			}
		}
	}

	ScissorState GetScissorState()
	{
		return { g_state.scissorEnabled, g_state.scissorX, g_state.scissorY, g_state.scissorW, g_state.scissorH };
	}

	void SetScissorState(const ScissorState& state)
	{
		g_state.scissorEnabled = state.enabled;
		g_state.scissorX = state.x;
		g_state.scissorY = state.y;
		g_state.scissorW = state.w;
		g_state.scissorH = state.h;
	}

	ViewportState GetViewportState()
	{
		return { g_state.viewportX, g_state.viewportY, g_state.viewportW, g_state.viewportH };
	}

	void SetViewportState(const ViewportState& s)
	{
		g_state.viewportX = s.x;
		g_state.viewportY = s.y;
		g_state.viewportW = s.w;
		g_state.viewportH = s.h;
	}

	ClearColorState GetClearColorState()
	{
		return { g_state.clearR, g_state.clearG, g_state.clearB, g_state.clearA };
	}

	void SetClearColorState(const ClearColorState& s)
	{
		g_state.clearR = s.r;
		g_state.clearG = s.g;
		g_state.clearB = s.b;
		g_state.clearA = s.a;
	}

	// =========================================================================
	// Blending helper — applies source-over (or other configured blend) for
	// a single pixel.
	// =========================================================================
	namespace
	{
		inline float BlendComponent(BlendFactor f, float srcR, float srcG, float srcB, float srcA,
		                            float dstR, float dstG, float dstB, float dstA, float component,
		                            bool isSrc)
		{
			(void)dstR; (void)dstG; (void)dstB;
			switch (f) {
				case BlendFactor::Zero:             return 0.0f;
				case BlendFactor::One:              return component;
				case BlendFactor::SrcAlpha:         return component * srcA;
				case BlendFactor::OneMinusSrcAlpha: return component * (1.0f - srcA);
				case BlendFactor::DstAlpha:         return component * dstA;
				case BlendFactor::OneMinusDstAlpha: return component * (1.0f - dstA);
				case BlendFactor::SrcColor:         return component * (isSrc ? 1.0f : srcR); // rough
				case BlendFactor::DstColor:         return component * (isSrc ? dstR : 1.0f); // rough
				default:                            return component;
			}
		}

		inline void BlendPixel(std::uint8_t* dst,
		                       float srcR, float srcG, float srcB, float srcA,
		                       BlendFactor sFactor, BlendFactor dFactor)
		{
			const float dstR = dst[0] / 255.0f;
			const float dstG = dst[1] / 255.0f;
			const float dstB = dst[2] / 255.0f;
			const float dstA = dst[3] / 255.0f;

			auto bf = [&](BlendFactor f, float c, bool src) {
				return BlendComponent(f, srcR, srcG, srcB, srcA, dstR, dstG, dstB, dstA, c, src);
			};

			const float outR = bf(sFactor, srcR, true)  + bf(dFactor, dstR, false);
			const float outG = bf(sFactor, srcG, true)  + bf(dFactor, dstG, false);
			const float outB = bf(sFactor, srcB, true)  + bf(dFactor, dstB, false);
			const float outA = bf(sFactor, srcA, true)  + bf(dFactor, dstA, false);

			auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
			dst[0] = static_cast<std::uint8_t>(clamp01(outR) * 255.0f);
			dst[1] = static_cast<std::uint8_t>(clamp01(outG) * 255.0f);
			dst[2] = static_cast<std::uint8_t>(clamp01(outB) * 255.0f);
			dst[3] = static_cast<std::uint8_t>(clamp01(outA) * 255.0f);
		}

		// Write a fragment at screen position (px, py) with RGBA colour.
		inline void WriteFragment(std::int32_t px, std::int32_t py, float r, float g, float b, float a)
		{
			if (g_state.colorBuffer == nullptr) return;

			// Bounds check
			if (px < 0 || py < 0 || px >= g_state.bufferWidth || py >= g_state.bufferHeight) return;

			// For FBO render targets, invert Y so pixels are stored in GL bottom-up convention
			// (v=0 = bottom). The main screen buffer stays top-down and is flipped in blitSwBuffer.
			const std::int32_t storeY = g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py;

			// Scissor test — use storeY (content-space Y) so it matches the scissor rect coordinates
			if (g_state.scissorEnabled) {
				if (px < g_state.scissorX || px >= g_state.scissorX + g_state.scissorW ||
				    storeY < g_state.scissorY || storeY >= g_state.scissorY + g_state.scissorH) {
					return;
				}
			}

			std::uint8_t* dst = g_state.colorBuffer + (storeY * g_state.bufferWidth + px) * 4;

			if (g_state.blendingEnabled) {
				BlendPixel(dst, r, g, b, a, g_state.blendSrc, g_state.blendDst);
			} else {
				auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
				dst[0] = static_cast<std::uint8_t>(clamp01(r) * 255.0f);
				dst[1] = static_cast<std::uint8_t>(clamp01(g) * 255.0f);
				dst[2] = static_cast<std::uint8_t>(clamp01(b) * 255.0f);
				dst[3] = static_cast<std::uint8_t>(clamp01(a) * 255.0f);
			}
		}

		// Fetch vertex position (xy) from the active vertex buffer.
		// Assumes pos is at offset=0, float[2].
		struct Vertex2D { float x, y, u, v, r, g, b, a; };

		Vertex2D FetchVertex(const DrawContext& ctx, std::int32_t index)
		{
			Vertex2D out = { 0, 0, 0, 0, 1, 1, 1, 1 };

			if (ctx.vertexFormat == nullptr) {
				// Procedural sprite quad — replicate the sprite_vs.glsl vertex synthesis:
				//   aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2))
				//   position  = aPosition * spriteSize
				//   texCoords = vec2(aPos.x * texRect.x + texRect.y,
				//                    aPos.y * texRect.z + texRect.w)
				const float ax = ((index & ~1) == 0) ? 1.0f : 0.0f; // indices 0,1 → ax=1; 2,3 → ax=0
				const float ay = (index & 1) ? 1.0f : 0.0f;         // indices 1,3 → ay=1; 0,2 → ay=0

				const float wx = ax * ctx.ff.spriteSize[0];
				const float wy = ay * ctx.ff.spriteSize[1];

				// Apply column-major 4×4 MVP matrix to (wx, wy, 0, 1)
				const float* m = ctx.ff.mvpMatrix;
				out.x = m[0] * wx + m[4] * wy + m[12];
				out.y = m[1] * wx + m[5] * wy + m[13];

				// UV from texRect components: (xScale, xBias, yScale, yBias)
				out.u = ax * ctx.ff.texRect[0] + ctx.ff.texRect[1];
				out.v = ay * ctx.ff.texRect[2] + ctx.ff.texRect[3];

				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			} else {
				const std::uint32_t attrCount = ctx.vertexFormat->GetAttributeCount();
				for (std::uint32_t i = 0; i < attrCount; ++i) {
					const VertexFormatAttribute& attr = ctx.vertexFormat->GetAttribute(i);
					if (!attr.enabled || attr.vbo == nullptr) continue;

					const std::uint8_t* base = static_cast<const std::uint8_t*>(attr.vbo->GetData());
					if (base == nullptr) continue;

					std::int32_t stride = attr.stride;
					if (stride == 0) stride = attr.size * 4; // assume float

					const std::uint8_t* ptr = base + ctx.vboByteOffset + static_cast<std::size_t>(index) * stride + attr.offset;

					if (i == 0 && attr.size >= 2) {
						// Attribute 0 = position (xy)
						const float* f = reinterpret_cast<const float*>(ptr);
						out.x = f[0];
						out.y = f[1];
					} else if (i == 1 && attr.size >= 2) {
						// Attribute 1 = texcoords (uv)
						const float* f = reinterpret_cast<const float*>(ptr);
						out.u = f[0];
						out.v = f[1];
					}
				}

				// Apply fixed-function tint from the ff state
				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			}

			// Viewport transform: NDC [-1,+1] → screen pixel coordinates.
			// Y is flipped: NDC +1 = top of screen = row 0 in the buffer.
			out.x = (out.x + 1.0f) * 0.5f * static_cast<float>(g_state.viewportW) + static_cast<float>(g_state.viewportX);
			out.y = (1.0f - out.y) * 0.5f * static_cast<float>(g_state.viewportH) + static_cast<float>(g_state.viewportY);

			return out;
		}

		// Fast rasterizer for an axis-aligned quad (4-vertex procedural sprite, no rotation).
		// Uses integer 16.16 fixed-point UV stepping and integer alpha blending.
		void DrawAxisAlignedQuad(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2, Vertex2D v3)
		{
			if (g_state.colorBuffer == nullptr) {
				return;
			}

			// Screen-space bounding box from all four vertices
			const float fxMin = std::min({v0.x, v1.x, v2.x, v3.x});
			const float fxMax = std::max({v0.x, v1.x, v2.x, v3.x});
			const float fyMin = std::min({v0.y, v1.y, v2.y, v3.y});
			const float fyMax = std::max({v0.y, v1.y, v2.y, v3.y});

			const float fullW = fxMax - fxMin;
			const float fullH = fyMax - fyMin;
			if (fullW < 0.5f || fullH < 0.5f) {
				return;
			}

			// Derive UV corners: find which screen edge corresponds to which UV
			// v2/v3 and v0/v1 share x; v0/v2 and v1/v3 share y
			const float uLeft = (v2.x <= v0.x) ? v2.u : v0.u;
			const float uRight = (v2.x <= v0.x) ? v0.u : v2.u;
			const float vTop = (v0.y <= v1.y) ? v0.v : v1.v;
			const float vBot = (v0.y <= v1.y) ? v1.v : v0.v;

			// Pixel integer bbox clamped to buffer
			std::int32_t xMin = std::max(0, static_cast<std::int32_t>(fxMin));
			std::int32_t xMax = std::min(g_state.bufferWidth  - 1, static_cast<std::int32_t>(fxMax - 0.5f));
			std::int32_t yMin = std::max(0, static_cast<std::int32_t>(fyMin));
			std::int32_t yMax = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(fyMax - 0.5f));

			// Scissor pre-clip (avoids per-pixel check)
			if (g_state.scissorEnabled) {
				xMin = std::max(xMin, g_state.scissorX);
				xMax = std::min(xMax, g_state.scissorX + g_state.scissorW - 1);
				if (g_state.isFboTarget) {
					// Convert content-space scissor Y to screen py range
					const std::int32_t pyMin = g_state.bufferHeight - g_state.scissorY - g_state.scissorH;
					const std::int32_t pyMax = g_state.bufferHeight - 1 - g_state.scissorY;
					yMin = std::max(yMin, pyMin);
					yMax = std::min(yMax, pyMax);
				} else {
					yMin = std::max(yMin, g_state.scissorY);
					yMax = std::min(yMax, g_state.scissorY + g_state.scissorH - 1);
				}
			}
			if (xMin > xMax || yMin > yMax) {
				return;
			}

			// Texture raw pixel access
			Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits)
			             ? ctx.textures[ctx.ff.textureUnit]
			             : nullptr;
			const std::uint8_t* texPixels = (tex != nullptr) ? tex->GetPixels(0) : nullptr;
			const std::int32_t texW = (tex != nullptr) ? tex->GetWidth()  : 0;
			const std::int32_t texH = (tex != nullptr) ? tex->GetHeight() : 0;

			// Tint color as integer [0..255]
			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);

			const bool useBlend = g_state.blendingEnabled;

			// 16.16 fixed-point UV steps — scaled by texture dimensions so >> 16 gives a pixel index directly
			const std::int32_t dtxFix = (texPixels != nullptr) ? static_cast<std::int32_t>((uRight - uLeft) * texW * 65536.0f / fullW) : 0;
			const std::int32_t dtyFix = (texPixels != nullptr) ? static_cast<std::int32_t>((vBot - vTop) * texH * 65536.0f / fullH) : 0;

			// Fixed-point UV base at (xMin+0.5, yMin+0.5), in pixel-index space
			const std::int32_t txBase = (texPixels != nullptr) ? static_cast<std::int32_t>((uLeft + (xMin + 0.5f - fxMin) * (uRight - uLeft) / fullW) * texW * 65536.0f) : 0;
			std::int32_t tyFix = (texPixels != nullptr) ? static_cast<std::int32_t>((vTop + (yMin + 0.5f - fyMin) * (vBot - vTop) / fullH) * texH * 65536.0f) : 0;

			for (std::int32_t py = yMin; py <= yMax; ++py, tyFix += dtyFix) {
				const std::int32_t storeY = g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py;
				std::uint8_t* dstRow = g_state.colorBuffer + (storeY * g_state.bufferWidth + xMin) * 4;

				const std::int32_t srcY = (texPixels != nullptr) ? std::max(0, std::min(texH - 1, tyFix >> 16)) : 0;
				std::int32_t txFix = txBase;

				for (std::int32_t px = xMin; px <= xMax; ++px, dstRow += 4, txFix += dtxFix) {
					std::int32_t sR, sG, sB, sA;
					if (texPixels != nullptr) {
						const std::int32_t srcX = std::max(0, std::min(texW - 1, txFix >> 16));
						const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
						sR = (src[0] * tR) >> 8;
						sG = (src[1] * tG) >> 8;
						sB = (src[2] * tB) >> 8;
						sA = (src[3] * tA) >> 8;
					} else {
						sR = tR; sG = tG; sB = tB; sA = tA;
					}

					if (useBlend) {
						const std::int32_t inv = 255 - sA;
						dstRow[0] = static_cast<std::uint8_t>((sR * sA + dstRow[0] * inv) >> 8);
						dstRow[1] = static_cast<std::uint8_t>((sG * sA + dstRow[1] * inv) >> 8);
						dstRow[2] = static_cast<std::uint8_t>((sB * sA + dstRow[2] * inv) >> 8);
						dstRow[3] = static_cast<std::uint8_t>((sA * 255 + dstRow[3] * inv) >> 8);
					} else {
						dstRow[0] = static_cast<std::uint8_t>(sR);
						dstRow[1] = static_cast<std::uint8_t>(sG);
						dstRow[2] = static_cast<std::uint8_t>(sB);
						dstRow[3] = static_cast<std::uint8_t>(sA);
					}
				}
			}
		}

		// Rasterize a single 2-D triangle (already in screen space).
		void RasterizeTriangle(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2)
		{
			// Bounding box in screen space
			const std::int32_t minX = std::max(0, static_cast<std::int32_t>(std::min({v0.x, v1.x, v2.x})));
			const std::int32_t maxX = std::min(g_state.bufferWidth  - 1, static_cast<std::int32_t>(std::max({v0.x, v1.x, v2.x})));
			const std::int32_t minY = std::max(0, static_cast<std::int32_t>(std::min({v0.y, v1.y, v2.y})));
			const std::int32_t maxY = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(std::max({v0.y, v1.y, v2.y})));

			// Edge function
			auto edgeFn = [](const Vertex2D& a, const Vertex2D& b, float px, float py) -> float {
				return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
			};

			const float area = edgeFn(v0, v1, v2.x, v2.y);
			if (std::fabs(area) < 1e-6f) return; // degenerate

			Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits)
			             ? ctx.textures[ctx.ff.textureUnit]
			             : nullptr;

			for (std::int32_t py = minY; py <= maxY; ++py) {
				for (std::int32_t px = minX; px <= maxX; ++px) {
					const float fpx = px + 0.5f;
					const float fpy = py + 0.5f;

					const float w0 = edgeFn(v1, v2, fpx, fpy);
					const float w1 = edgeFn(v2, v0, fpx, fpy);
					const float w2 = edgeFn(v0, v1, fpx, fpy);

					// Same sign as area = inside triangle
					if ((w0 >= 0) == (area >= 0) && (w1 >= 0) == (area >= 0) && (w2 >= 0) == (area >= 0)) {
						const float bary0 = w0 / area;
						const float bary1 = w1 / area;
						const float bary2 = w2 / area;

						const float u = bary0 * v0.u + bary1 * v1.u + bary2 * v2.u;
						const float v  = bary0 * v0.v + bary1 * v1.v + bary2 * v2.v;

						float r = ctx.ff.color[0];
						float g = ctx.ff.color[1];
						float b = ctx.ff.color[2];
						float a = ctx.ff.color[3];

						if (tex != nullptr) {
							const nCine::Colorf texel = tex->Sample(u, v, 0);
							r *= texel.R;
							g *= texel.G;
							b *= texel.B;
							a *= texel.A;
						}

						WriteFragment(px, py, r, g, b, a);
					}
				}
			}
		}

		void DrawPrimitive(const DrawContext& ctx, PrimitiveType type,
		                   const std::vector<std::int32_t>& indices)
		{
			switch (type) {
				case PrimitiveType::Triangles: {
					for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
						RasterizeTriangle(ctx,
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]),
						    FetchVertex(ctx, indices[i + 2]));
					}
					break;
				}
				case PrimitiveType::TriangleStrip: {
					for (std::size_t i = 0; i + 2 < indices.size(); ++i) {
						if (i & 1) {
							RasterizeTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 2]),
							    FetchVertex(ctx, indices[i + 1]));
						} else {
							RasterizeTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 1]),
							    FetchVertex(ctx, indices[i + 2]));
						}
					}
					break;
				}
				case PrimitiveType::TriangleFan: {
					for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
						RasterizeTriangle(ctx,
						    FetchVertex(ctx, indices[0]),
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]));
					}
					break;
				}
				default:
					// Points and lines are not rendered in the minimal SW backend
					break;
			}
		}
	} // anonymous namespace

	// =========================================================================
	// Public draw entry-points
	// =========================================================================
	void SetDrawContext(const DrawContext& ctx)
	{
		static DrawContext s_ctx;
		s_ctx = ctx;
		g_state.drawCtx = &s_ctx;
		// Mirror blend / scissor state into global state so rasterizer picks them up
		g_state.blendingEnabled = ctx.blendingEnabled;
		g_state.blendSrc = ctx.blendSrc;
		g_state.blendDst = ctx.blendDst;
		g_state.scissorEnabled = ctx.scissorEnabled;
		g_state.scissorX = ctx.scissorRect.X;
		g_state.scissorY = ctx.scissorRect.Y;
		g_state.scissorW = ctx.scissorRect.W;
		g_state.scissorH = ctx.scissorRect.H;
	}

	void ClearDrawContext()
	{
		g_state.drawCtx = nullptr;
	}

	void FramebufferBind(Framebuffer& fbo)
	{
		Texture* target = fbo.GetColorTarget();
		if (target == nullptr) return;

		target->EnsureRenderTarget();
		std::uint8_t* texPixels = target->GetMutablePixels(0);
		if (texPixels == nullptr) return;

		// Redirect rasterizer output to the texture
		g_state.mainColorBuffer  = g_state.colorBuffer;
		g_state.mainBufferWidth  = g_state.bufferWidth;
		g_state.mainBufferHeight = g_state.bufferHeight;
		g_state.isFboTarget      = true;

		g_state.colorBuffer  = texPixels;
		g_state.bufferWidth  = target->GetWidth();
		g_state.bufferHeight = target->GetHeight();
	}

	void FramebufferUnbind()
	{
		if (g_state.isFboTarget) {
			g_state.colorBuffer  = g_state.mainColorBuffer;
			g_state.bufferWidth  = g_state.mainBufferWidth;
			g_state.bufferHeight = g_state.mainBufferHeight;
			g_state.isFboTarget  = false;
		}
	}

	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		if (g_state.drawCtx == nullptr) return;

		// Fast path: axis-aligned quad (4-vertex procedural sprite, no VBO, no exotic blending)
		if (type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 &&
		    g_state.drawCtx->vertexFormat == nullptr &&
		    (!g_state.blendingEnabled ||
		     (g_state.blendSrc == BlendFactor::SrcAlpha && g_state.blendDst == BlendFactor::OneMinusSrcAlpha))) {
			const DrawContext& ctx = *g_state.drawCtx;
			Vertex2D v0 = FetchVertex(ctx, 0), v1 = FetchVertex(ctx, 1);
			Vertex2D v2 = FetchVertex(ctx, 2), v3 = FetchVertex(ctx, 3);
			// Verify axis-aligned (no rotation: same x for vertical pairs, same y for horizontal pairs)
			if (std::fabs(v0.x - v1.x) < 0.5f && std::fabs(v2.x - v3.x) < 0.5f &&
			    std::fabs(v0.y - v2.y) < 0.5f && std::fabs(v1.y - v3.y) < 0.5f) {
				DrawAxisAlignedQuad(ctx, v0, v1, v2, v3);
				return;
			}
		}

		std::vector<std::int32_t> indices(static_cast<std::size_t>(count));
		for (std::int32_t i = 0; i < count; ++i) indices[i] = firstVertex + i;
		DrawPrimitive(*g_state.drawCtx, type, indices);
	}

	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		for (std::int32_t inst = 0; inst < instanceCount; ++inst) {
			Draw(type, firstVertex, count);
		}
	}

	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex)
	{
		if (g_state.drawCtx == nullptr) return;
		const std::uint16_t* iboPtr = static_cast<const std::uint16_t*>(indexOffset);
		std::vector<std::int32_t> indices(static_cast<std::size_t>(count));
		for (std::int32_t i = 0; i < count; ++i) {
			indices[i] = static_cast<std::int32_t>(iboPtr[i]) + baseVertex;
		}
		DrawPrimitive(*g_state.drawCtx, type, indices);
	}

	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset,
	                          std::int32_t instanceCount, std::int32_t baseVertex)
	{
		for (std::int32_t inst = 0; inst < instanceCount; ++inst) {
			DrawIndexed(type, count, indexOffset, baseVertex);
		}
	}

	// =========================================================================
	// Framebuffer management
	// =========================================================================

	void ResizeColorBuffer(std::int32_t width, std::int32_t height)
	{
		if (g_state.mainBufferWidth == width && g_state.mainBufferHeight == height) {
			return;
		}

		g_state.mainBufferWidth  = width;
		g_state.mainBufferHeight = height;

		const std::size_t colorBytes = static_cast<std::size_t>(width) * height * 4;
		const std::size_t depthBytes = static_cast<std::size_t>(width) * height;

		delete[] g_state.mainColorBuffer;
		delete[] g_state.depthBuffer;

		g_state.mainColorBuffer = new std::uint8_t[colorBytes]();
		g_state.depthBuffer = new float[depthBytes];

		// Redirect active buffer to main (we're not in an FBO when this is called)
		g_state.colorBuffer  = g_state.mainColorBuffer;
		g_state.bufferWidth  = width;
		g_state.bufferHeight = height;

		// Clear depth to far plane
		const float farValue = 1.0f;
		for (std::size_t i = 0; i < depthBytes; ++i) {
			g_state.depthBuffer[i] = farValue;
		}

		// Mirror viewport to full buffer
		g_state.viewportX = 0;
		g_state.viewportY = 0;
		g_state.viewportW = width;
		g_state.viewportH = height;
	}

	const std::uint8_t* GetColorBuffer()
	{
		return g_state.mainColorBuffer;
	}

	std::int32_t GetColorBufferWidth()
	{
		return g_state.bufferWidth;
	}

	std::int32_t GetColorBufferHeight()
	{
		return g_state.bufferHeight;
	}

} // namespace nCine::RHI

#endif