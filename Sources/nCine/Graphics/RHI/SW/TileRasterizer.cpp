#if defined(WITH_RHI_SW) //&& defined(DEATH_TARGET_VITA)

#include "TileRenderer.h"

#if defined(DEATH_ENABLE_NEON)
#	include <arm_neon.h>
#endif

#include <algorithm>
#include <cstring>

using namespace Death::Containers;

namespace nCine::RHI
{
	// Re-use internal helpers from RHI_SW.cpp via shared inline implementations
	namespace TileInternal
	{
		// =====================================================================
		// Inline helpers (subset of RHI_SW.cpp helpers needed for tile rendering)
		// =====================================================================

		static inline std::int32_t WrapTexelFix(std::int32_t coordFix, std::int32_t texDim, SamplerWrapping mode)
		{
			std::int32_t idx = coordFix >> 16;
			switch (mode) {
				case SamplerWrapping::Repeat:
					idx = idx % texDim;
					if (idx < 0) idx += texDim;
					return idx;
				case SamplerWrapping::MirroredRepeat: {
					std::int32_t period = texDim * 2;
					idx = idx % period;
					if (idx < 0) idx += period;
					if (idx >= texDim) idx = period - 1 - idx;
					return idx;
				}
				default:
					if (idx < 0) return 0;
					if (idx >= texDim) return texDim - 1;
					return idx;
			}
		}

		static inline std::int32_t NormalizeRepeatFix(std::int32_t coordFix, std::int32_t texDimFix)
		{
			coordFix = coordFix % texDimFix;
			if (coordFix < 0) coordFix += texDimFix;
			return coordFix;
		}

		static inline std::int32_t AdvanceRepeatFix(std::int32_t coordFix, std::int32_t step, std::int32_t texDimFix)
		{
			coordFix += step;
			if (coordFix >= texDimFix) coordFix -= texDimFix;
			else if (coordFix < 0) coordFix += texDimFix;
			return coordFix;
		}

		static inline std::int32_t WrapTexelCoord(std::int32_t idx, std::int32_t texDim, SamplerWrapping mode)
		{
			switch (mode) {
				case SamplerWrapping::Repeat:
					idx = idx % texDim;
					if (idx < 0) idx += texDim;
					return idx;
				case SamplerWrapping::MirroredRepeat: {
					std::int32_t period = texDim * 2;
					idx = idx % period;
					if (idx < 0) idx += period;
					if (idx >= texDim) idx = period - 1 - idx;
					return idx;
				}
				default:
					if (idx < 0) return 0;
					if (idx >= texDim) return texDim - 1;
					return idx;
			}
		}

		struct BilinearRowCtx
		{
			const std::uint8_t* row0;
			const std::uint8_t* row1;
			std::int32_t ify;
			std::int32_t fy;
		};

		static inline BilinearRowCtx PrepareBilinearRow(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                                                std::int32_t vFix, SamplerWrapping wrapT)
		{
			const std::int32_t vf = vFix - (1 << 15);
			std::int32_t y0 = vf >> 16;
			const std::int32_t fy = (vf & 0xFFFF) >> 8;
			std::int32_t y1 = WrapTexelCoord(y0 + 1, texH, wrapT);
			y0 = WrapTexelCoord(y0, texH, wrapT);
			return {
				texPixels + static_cast<std::size_t>(y0) * texW * 4,
				texPixels + static_cast<std::size_t>(y1) * texW * 4,
				255 - fy, fy
			};
		}

		static inline void SampleBilinearFixRow(const BilinearRowCtx& row, std::int32_t texW,
		                                        std::int32_t uFix, SamplerWrapping wrapS,
		                                        std::uint8_t* out)
		{
			const std::int32_t uf = uFix - (1 << 15);
			std::int32_t x0 = uf >> 16;
			const std::int32_t fx = (uf & 0xFFFF) >> 8;
			std::int32_t x1 = WrapTexelCoord(x0 + 1, texW, wrapS);
			x0 = WrapTexelCoord(x0, texW, wrapS);

			const std::uint8_t* c00 = row.row0 + x0 * 4;
			const std::uint8_t* c10 = row.row0 + x1 * 4;
			const std::uint8_t* c01 = row.row1 + x0 * 4;
			const std::uint8_t* c11 = row.row1 + x1 * 4;

			const std::int32_t ifx = 255 - fx;
			const std::int32_t w00 = ifx * row.ify;
			const std::int32_t w10 = fx * row.ify;
			const std::int32_t w01 = ifx * row.fy;
			const std::int32_t w11 = fx * row.fy;

			out[0] = static_cast<std::uint8_t>((c00[0] * w00 + c10[0] * w10 + c01[0] * w01 + c11[0] * w11) >> 16);
			out[1] = static_cast<std::uint8_t>((c00[1] * w00 + c10[1] * w10 + c01[1] * w01 + c11[1] * w11) >> 16);
			out[2] = static_cast<std::uint8_t>((c00[2] * w00 + c10[2] * w10 + c01[2] * w01 + c11[2] * w11) >> 16);
			out[3] = static_cast<std::uint8_t>((c00[3] * w00 + c10[3] * w10 + c01[3] * w01 + c11[3] * w11) >> 16);
		}

		static inline void SampleBilinearFix(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                                     std::int32_t uFix, std::int32_t vFix,
		                                     SamplerWrapping wrapS, SamplerWrapping wrapT,
		                                     std::uint8_t* out)
		{
			const std::int32_t uf = uFix - (1 << 15);
			const std::int32_t vf = vFix - (1 << 15);
			std::int32_t x0 = uf >> 16;
			std::int32_t y0 = vf >> 16;
			const std::int32_t fx = (uf & 0xFFFF) >> 8;
			const std::int32_t fy = (vf & 0xFFFF) >> 8;
			std::int32_t x1 = WrapTexelCoord(x0 + 1, texW, wrapS);
			std::int32_t y1 = WrapTexelCoord(y0 + 1, texH, wrapT);
			x0 = WrapTexelCoord(x0, texW, wrapS);
			y0 = WrapTexelCoord(y0, texH, wrapT);

			const std::uint8_t* c00 = texPixels + (static_cast<std::size_t>(y0) * texW + x0) * 4;
			const std::uint8_t* c10 = texPixels + (static_cast<std::size_t>(y0) * texW + x1) * 4;
			const std::uint8_t* c01 = texPixels + (static_cast<std::size_t>(y1) * texW + x0) * 4;
			const std::uint8_t* c11 = texPixels + (static_cast<std::size_t>(y1) * texW + x1) * 4;

			const std::int32_t ifx = 255 - fx;
			const std::int32_t ify = 255 - fy;
			const std::int32_t w00 = ifx * ify;
			const std::int32_t w10 = fx * ify;
			const std::int32_t w01 = ifx * fy;
			const std::int32_t w11 = fx * fy;

			out[0] = static_cast<std::uint8_t>((c00[0] * w00 + c10[0] * w10 + c01[0] * w01 + c11[0] * w11) >> 16);
			out[1] = static_cast<std::uint8_t>((c00[1] * w00 + c10[1] * w10 + c01[1] * w01 + c11[1] * w11) >> 16);
			out[2] = static_cast<std::uint8_t>((c00[2] * w00 + c10[2] * w10 + c01[2] * w01 + c11[2] * w11) >> 16);
			out[3] = static_cast<std::uint8_t>((c00[3] * w00 + c10[3] * w10 + c01[3] * w01 + c11[3] * w11) >> 16);
		}

		// =====================================================================
		// NEON-optimized scanline blending (SrcAlpha/OneMinusSrcAlpha)
		// =====================================================================
		static void BlendScanlineNeon(std::uint8_t* DEATH_RESTRICT dst,
		                              const std::uint8_t* DEATH_RESTRICT src,
		                              std::int32_t count)
		{
#if defined(DEATH_ENABLE_NEON)
			static const std::uint8_t alphaIdxData[8] = { 3, 3, 3, 3, 7, 7, 7, 7 };
			const uint8x8_t alphaIdx = vld1_u8(alphaIdxData);
			const uint8x8_t c255x8 = vdup_n_u8(255);

			std::int32_t i = 0;
			for (; i + 4 <= count; i += 4, dst += 16, src += 16) {
				// Prefetch next texture data
				__builtin_prefetch(src + 64, 0, 1);

				uint8x16_t srcPx = vld1q_u8(src);
				uint8x16_t dstPx = vld1q_u8(dst);

				uint8x8_t srcLo = vget_low_u8(srcPx);
				uint8x8_t dstLo = vget_low_u8(dstPx);
				uint8x8_t aLo = vtbl1_u8(srcLo, alphaIdx);
				uint8x8_t iaLo = vsub_u8(c255x8, aLo);
				uint16x8_t rLo = vaddq_u16(vmull_u8(srcLo, aLo), vmull_u8(dstLo, iaLo));

				uint8x8_t srcHi = vget_high_u8(srcPx);
				uint8x8_t dstHi = vget_high_u8(dstPx);
				uint8x8_t aHi = vtbl1_u8(srcHi, alphaIdx);
				uint8x8_t iaHi = vsub_u8(c255x8, aHi);
				uint16x8_t rHi = vaddq_u16(vmull_u8(srcHi, aHi), vmull_u8(dstHi, iaHi));

				vst1q_u8(dst, vcombine_u8(vshrn_n_u16(rLo, 8), vshrn_n_u16(rHi, 8)));
			}
			for (; i < count; ++i, dst += 4, src += 4) {
				const std::int32_t sA = src[3];
				if (sA == 0) continue;
				if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
				const std::int32_t inv = 255 - sA;
				dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
				dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
				dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
				dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
			}
#else
			for (std::int32_t i = 0; i < count; ++i, dst += 4, src += 4) {
				const std::int32_t sA = src[3];
				if (sA == 0) continue;
				if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
				const std::int32_t inv = 255 - sA;
				dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
				dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
				dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
				dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
			}
#endif
		}

		// =====================================================================
		// NEON-optimized tint scanline
		// =====================================================================
		static void TintScanlineNeon(std::uint8_t* DEATH_RESTRICT buf, std::int32_t count,
		                             std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA)
		{
#if defined(DEATH_ENABLE_NEON)
			const uint8x8_t vtR = vdup_n_u8(static_cast<std::uint8_t>(tR));
			const uint8x8_t vtG = vdup_n_u8(static_cast<std::uint8_t>(tG));
			const uint8x8_t vtB = vdup_n_u8(static_cast<std::uint8_t>(tB));
			const uint8x8_t vtA = vdup_n_u8(static_cast<std::uint8_t>(tA));

			std::int32_t i = 0;
			for (; i + 8 <= count; i += 8, buf += 32) {
				uint8x8x4_t px = vld4_u8(buf);
				px.val[0] = vshrn_n_u16(vmull_u8(px.val[0], vtR), 8);
				px.val[1] = vshrn_n_u16(vmull_u8(px.val[1], vtG), 8);
				px.val[2] = vshrn_n_u16(vmull_u8(px.val[2], vtB), 8);
				px.val[3] = vshrn_n_u16(vmull_u8(px.val[3], vtA), 8);
				vst4_u8(buf, px);
			}
			for (; i < count; ++i, buf += 4) {
				buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
				buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
				buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
				buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
			}
#else
			for (std::int32_t i = 0; i < count; ++i, buf += 4) {
				buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
				buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
				buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
				buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
			}
#endif
		}

		// =====================================================================
		// Vertex fetch (same logic as RHI_SW.cpp FetchVertex)
		// =====================================================================
		struct Vertex2D { float x, y, u, v, r, g, b, a; };

		static Vertex2D FetchVertex(const DrawContext& ctx, std::int32_t index,
		                            std::int32_t viewportW, std::int32_t viewportH)
		{
			Vertex2D out = { 0, 0, 0, 0, 1, 1, 1, 1 };

			if DEATH_LIKELY(ctx.vertexFormat == nullptr) {
				const float ax = ((index & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (index & 1) ? 1.0f : 0.0f;

				const float wx = ax * ctx.ff.spriteSize[0];
				const float wy = ay * ctx.ff.spriteSize[1];

				const float* m = ctx.ff.mvpMatrix;
				out.x = m[0] * wx + m[4] * wy + m[12];
				out.y = m[1] * wx + m[5] * wy + m[13];

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
					if DEATH_UNLIKELY(base == nullptr) continue;

					std::int32_t stride = attr.stride;
					if DEATH_UNLIKELY(stride == 0) stride = attr.size * 4;

					const std::uint8_t* ptr = base + ctx.vboByteOffset + static_cast<std::size_t>(index) * stride + attr.offset;

					if (i == 0 && attr.size >= 2) {
						// Use memcpy — ptr may not be 4-byte aligned; on ARM a direct
						// float* dereference of an unaligned address causes a Data Abort.
						std::memcpy(&out.x, ptr,            sizeof(float));
						std::memcpy(&out.y, ptr + sizeof(float), sizeof(float));
					} else if (i == 1 && attr.size >= 2) {
						std::memcpy(&out.u, ptr,            sizeof(float));
						std::memcpy(&out.v, ptr + sizeof(float), sizeof(float));
					}
				}

				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			}

			// Viewport transform
			out.x = (out.x + 1.0f) * 0.5f * static_cast<float>(viewportW);
			out.y = (1.0f - out.y) * 0.5f * static_cast<float>(viewportH);

			// Snap to pixel grid
			constexpr float SnapEps = 1.0f / 128.0f;
			float rxSnap = std::round(out.x);
			float rySnap = std::round(out.y);
			if (std::fabs(out.x - rxSnap) < SnapEps) out.x = rxSnap;
			if (std::fabs(out.y - rySnap) < SnapEps) out.y = rySnap;

			return out;
		}

		// =====================================================================
		// Tile-local axis-aligned quad rasterizer
		// Renders only the portion of the quad that intersects the tile.
		// The destination buffer is the tile (TileSize stride), not the full framebuffer.
		// =====================================================================
		static void DrawAxisAlignedQuadTile(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2, Vertex2D v3,
		                                   std::uint8_t* tileBuf, std::int32_t tileX, std::int32_t tileY,
		                                   std::int32_t tileW, std::int32_t tileH)
		{
			// Screen-space bounding box
			const float fxMin = std::min({v0.x, v1.x, v2.x, v3.x});
			const float fxMax = std::max({v0.x, v1.x, v2.x, v3.x});
			const float fyMin = std::min({v0.y, v1.y, v2.y, v3.y});
			const float fyMax = std::max({v0.y, v1.y, v2.y, v3.y});

			const float fullW = fxMax - fxMin;
			const float fullH = fyMax - fyMin;
			if (fullW < 0.5f || fullH < 0.5f) return;

			// UV corners
			const float uLeft  = (v2.x <= v0.x) ? v2.u : v0.u;
			const float uRight = (v2.x <= v0.x) ? v0.u : v2.u;
			const float vTop   = (v0.y <= v1.y) ? v0.v : v1.v;
			const float vBot   = (v0.y <= v1.y) ? v1.v : v0.v;

			// Clip to tile bounds (in framebuffer coordinates)
			std::int32_t xMin = std::max(tileX, static_cast<std::int32_t>(fxMin));
			std::int32_t xMax = std::min(tileX + tileW - 1, static_cast<std::int32_t>(fxMax - 0.5f));
			std::int32_t yMin = std::max(tileY, static_cast<std::int32_t>(fyMin));
			std::int32_t yMax = std::min(tileY + tileH - 1, static_cast<std::int32_t>(fyMax - 0.5f));

			// Also apply scissor
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				xMin = std::max(xMin, ctx.scissorRect.X);
				xMax = std::min(xMax, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				yMin = std::max(yMin, ctx.scissorRect.Y);
				yMax = std::min(yMax, ctx.scissorRect.Y + ctx.scissorRect.H - 1);
			}
			if DEATH_UNLIKELY(xMin > xMax || yMin > yMax) return;

			// Texture info
			static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;
			Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits
			                ? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			// PARANOID GUARD: zero-dimension texture causes % 0 (division by zero) in
			// WrapTexelFix / NormalizeRepeatFix, which kills the worker thread on ARM.
			if DEATH_UNLIKELY(texPixels != nullptr && (texW <= 0 || texH <= 0)) {
				return; // malformed texture — skip entire draw call
			}

			// Tint color
			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
			const bool whiteTint = (tR >= 255 && tG >= 255 && tB >= 255 && tA >= 255);

			const bool useBlend = ctx.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				ctx.blendSrc == BlendFactor::SrcAlpha &&
				ctx.blendDst == BlendFactor::OneMinusSrcAlpha);

			// Fixed-point UV setup
			const std::int32_t texWFix = texW << 16;
			const std::int32_t texHFix = texH << 16;
			const std::int32_t dtxFix = (texPixels != nullptr ? static_cast<std::int32_t>((uRight - uLeft) * texW * 65536.0f / fullW) : 0);
			const std::int32_t dtyFix = (texPixels != nullptr ? static_cast<std::int32_t>((vBot - vTop) * texH * 65536.0f / fullH) : 0);

			const std::int32_t txBase = (texPixels != nullptr ? static_cast<std::int32_t>((uLeft + (xMin + 0.5f - fxMin) * (uRight - uLeft) / fullW) * texW * 65536.0f) : 0);
			std::int32_t tyFix = (texPixels != nullptr ? static_cast<std::int32_t>((vTop + (yMin + 0.5f - fyMin) * (vBot - vTop) / fullH) * texH * 65536.0f) : 0);

			const bool useRepeatS = (wrapS == SamplerWrapping::Repeat);
			const bool useRepeatT = (wrapT == SamplerWrapping::Repeat);
			const bool useClampS = (wrapS == SamplerWrapping::ClampToEdge);
			const bool uvSafeX = (useClampS && txBase >= 0 && (txBase + dtxFix * (xMax - xMin)) >= 0 &&
			                      (txBase >> 16) < texW && ((txBase + dtxFix * (xMax - xMin)) >> 16) < texW);

			const std::int32_t scanWidth = xMax - xMin + 1;

			// Scanline buffer for tint+blend (fits within tile width = max 32 pixels)
			alignas(16) std::uint8_t scanBuf[TileRenderer::TileSize * 4];
			const bool useScanBuf = ((useFastBlend || !useBlend) && texPixels != nullptr);

			// Cache ctx.fragmentShader into a local to defeat compiler aliasing pessimism:
			// fsInput.textures aliases with ctx through the FragmentShaderInput struct, so the
			// compiler may otherwise re-read ctx.fragmentShader on every loop iteration,
			// producing unnecessary loads and (in pathological cases) stale or null reads.
			// This is NOT a frame-boundary race fix — Flush() already waits on workersActive
			// before returning, so ctx is guaranteed not to be overwritten while workers run.
			const FragmentShaderFn cachedShader = ctx.fragmentShader;

			for (std::int32_t py = yMin; py <= yMax; py++, tyFix += dtyFix) {
				// Tile-local row offset
				const std::int32_t localRow = py - tileY;
				const std::int32_t localCol = xMin - tileX;
				// PARANOID: float→int rounding can push these just outside the tile
				if DEATH_UNLIKELY(localRow < 0 || localRow >= tileH || localCol < 0 || localCol >= tileW) continue;
				std::uint8_t* dstRow = tileBuf + (localRow * TileRenderer::TileSize + localCol) * 4;

				// Get source Y
				std::int32_t srcY = 0;
				if (texPixels != nullptr) {
					srcY = (useRepeatT)
						? WrapTexelFix(tyFix, texH, SamplerWrapping::Repeat)
						: WrapTexelFix(tyFix, texH, wrapT);
				}
				const std::uint8_t* texRow = (texPixels != nullptr ? texPixels + static_cast<std::size_t>(srcY) * texW * 4 : nullptr);

#if defined(DEATH_TARGET_ARM)
				// Prefetch next texture row
				if (texPixels != nullptr) {
					__builtin_prefetch(texPixels + static_cast<std::size_t>(WrapTexelFix(tyFix + dtyFix, texH, wrapT)) * texW * 4, 0, 1);
				}
#endif

				if (useScanBuf) {
					std::int32_t txFix = txBase;
					if (useRepeatS && !useLinear) {
						txFix = NormalizeRepeatFix(txFix, texWFix);
					}

					// Phase 1: gather texels
					if (useLinear) {
						const BilinearRowCtx brow = PrepareBilinearRow(texPixels, texW, texH, tyFix, wrapT);
						for (std::int32_t i = 0; i < scanWidth; ++i) {
							SampleBilinearFixRow(brow, texW, txFix, wrapS, &scanBuf[i * 4]);
							txFix += dtxFix;
						}
					} else if (texRow != nullptr) {
						if (uvSafeX && dtxFix == 65536) {
							// uvSafeX guarantees (srcX + scanWidth - 1) < texW, so the full
							// scanline is in-bounds and we can copy scanWidth texels directly.
							const std::int32_t srcX = std::max(0, std::min(texW - 1, txFix >> 16));
							std::memcpy(scanBuf, &texRow[srcX * 4], static_cast<std::size_t>(scanWidth) * 4);
						} else if (uvSafeX) {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = std::max(0, std::min(texW - 1, txFix >> 16));
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix += dtxFix;
							}
						} else if (useRepeatS) {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = txFix >> 16;
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix = AdvanceRepeatFix(txFix, dtxFix, texWFix);
							}
						} else {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = WrapTexelFix(txFix, texW, wrapS);
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix += dtxFix;
							}
						}
					}

					// Phase 2: fragment shader or tint
					if DEATH_UNLIKELY(cachedShader != nullptr) {
						FragmentShaderInput fsInput;
						fsInput.v = tyFix / 65536.0f / static_cast<float>(texH > 0 ? texH : 1);
						fsInput.texWidth = texW;
						fsInput.texHeight = texH;
						fsInput.textures = ctx.textures;
						fsInput.color = ctx.ff.color;
						fsInput.userData = ctx.fragmentShaderUserData;
						const float invTexW = 1.0f / static_cast<float>(texW > 0 ? texW : 1);
						std::int32_t txFixShader = txBase;
						for (std::int32_t i = 0; i < scanWidth; i++) {
							fsInput.rgba = &scanBuf[i * 4];
							fsInput.u = txFixShader / 65536.0f * invTexW;
							fsInput.x = xMin + i;
							fsInput.y = py;
							cachedShader(fsInput);
							txFixShader += dtxFix;
						}
					} else if DEATH_UNLIKELY(!whiteTint) {
						TintScanlineNeon(scanBuf, scanWidth, tR, tG, tB, tA);
					}

					// Phase 3: blend or copy
					if (useFastBlend) {
						BlendScanlineNeon(dstRow, scanBuf, scanWidth);
					} else {
						std::memcpy(dstRow, scanBuf, static_cast<std::size_t>(scanWidth) * 4);
					}
				} else {
					// Per-pixel path
					std::int32_t txFix = txBase;
					if (useRepeatS && texPixels != nullptr && !useLinear) {
						txFix = NormalizeRepeatFix(txFix, texWFix);
					}

					for (std::int32_t px = xMin; px <= xMax; px++, dstRow += 4) {
						std::int32_t sR, sG, sB, sA;
						if (useLinear && texPixels != nullptr) {
							std::uint8_t raw[4];
							SampleBilinearFix(texPixels, texW, texH, txFix, tyFix, wrapS, wrapT, raw);
							txFix += dtxFix;
							sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
						} else if (texRow != nullptr) {
							std::int32_t srcX;
							if (useRepeatS) {
								srcX = txFix >> 16;
								txFix = AdvanceRepeatFix(txFix, dtxFix, texWFix);
							} else if (uvSafeX) {
								srcX = txFix >> 16;
								txFix += dtxFix;
							} else {
								srcX = WrapTexelFix(txFix, texW, wrapS);
								txFix += dtxFix;
							}
							const std::uint8_t* src = &texRow[srcX * 4];
							sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
							txFix += dtxFix;
						}

						if DEATH_UNLIKELY(cachedShader != nullptr) {
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							FragmentShaderInput fsInput;
							fsInput.rgba = px4;
							fsInput.u = (txFix - dtxFix) / 65536.0f / static_cast<float>(texW > 0 ? texW : 1);
							fsInput.v = tyFix / 65536.0f / static_cast<float>(texH > 0 ? texH : 1);
							fsInput.x = px;
							fsInput.y = py;
							fsInput.texWidth = texW;
							fsInput.texHeight = texH;
							fsInput.textures = ctx.textures;
							fsInput.color = ctx.ff.color;
							fsInput.userData = ctx.fragmentShaderUserData;
							cachedShader(fsInput);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else if (!whiteTint) {
							sR = (sR * tR) >> 8;
							sG = (sG * tG) >> 8;
							sB = (sB * tB) >> 8;
							sA = (sA * tA) >> 8;
						}

						if (useBlend) {
							if (sA == 0) continue;
							if (useFastBlend) {
								if (sA >= 255) {
									dstRow[0] = static_cast<std::uint8_t>(sR);
									dstRow[1] = static_cast<std::uint8_t>(sG);
									dstRow[2] = static_cast<std::uint8_t>(sB);
									dstRow[3] = static_cast<std::uint8_t>(sA);
								} else {
									const std::int32_t inv = 255 - sA;
									dstRow[0] = static_cast<std::uint8_t>((sR * sA + dstRow[0] * inv) >> 8);
									dstRow[1] = static_cast<std::uint8_t>((sG * sA + dstRow[1] * inv) >> 8);
									dstRow[2] = static_cast<std::uint8_t>((sB * sA + dstRow[2] * inv) >> 8);
									dstRow[3] = static_cast<std::uint8_t>((sA * 255 + dstRow[3] * inv) >> 8);
								}
							} else {
								// Generic blend
								const float srcRf = sR / 255.0f, srcGf = sG / 255.0f, srcBf = sB / 255.0f, srcAf = sA / 255.0f;
								const float dstRf = dstRow[0] / 255.0f, dstGf = dstRow[1] / 255.0f, dstBf = dstRow[2] / 255.0f, dstAf = dstRow[3] / 255.0f;
								auto bf = [&](BlendFactor f, float c, bool isSrc) -> float {
									switch (f) {
										case BlendFactor::Zero:             return 0.0f;
										case BlendFactor::One:              return c;
										case BlendFactor::SrcAlpha:         return c * srcAf;
										case BlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
										case BlendFactor::DstAlpha:         return c * dstAf;
										case BlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
										default:                            return c;
									}
								};
								auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
								dstRow[0] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcRf, true) + bf(ctx.blendDst, dstRf, false)) * 255.0f);
								dstRow[1] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcGf, true) + bf(ctx.blendDst, dstGf, false)) * 255.0f);
								dstRow[2] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcBf, true) + bf(ctx.blendDst, dstBf, false)) * 255.0f);
								dstRow[3] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcAf, true) + bf(ctx.blendDst, dstAf, false)) * 255.0f);
							}
						} else {
							dstRow[0] = static_cast<std::uint8_t>(sR);
							dstRow[1] = static_cast<std::uint8_t>(sG);
							dstRow[2] = static_cast<std::uint8_t>(sB);
							dstRow[3] = static_cast<std::uint8_t>(sA);
						}
					}
				}
			}
		}

		// =====================================================================
		// Tile-local affine quad rasterizer (rotated/scaled sprites)
		// =====================================================================
		static void DrawAffineQuadTile(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2, Vertex2D v3,
		                               std::uint8_t* tileBuf, std::int32_t tileX, std::int32_t tileY,
		                               std::int32_t tileW, std::int32_t tileH)
		{
			// Compute affine UV mapping from first triangle (v0, v1, v2)
			float e1x = v2.x - v0.x, e1y = v2.y - v0.y;
			float e2x = v1.x - v0.x, e2y = v1.y - v0.y;
			float det = e1x * e2y - e1y * e2x;
			if (std::fabs(det) < 1e-6f) return;
			float invDet = 1.0f / det;

			float du_s = v2.u - v0.u, du_t = v1.u - v0.u;
			float dv_s = v2.v - v0.v, dv_t = v1.v - v0.v;

			float ds_dx = e2y * invDet, ds_dy = -e2x * invDet;
			float dt_dx = -e1y * invDet, dt_dy = e1x * invDet;

			float dudx = du_s * ds_dx + du_t * dt_dx;
			float dudy = du_s * ds_dy + du_t * dt_dy;
			float dvdx = dv_s * ds_dx + dv_t * dt_dx;
			float dvdy = dv_s * ds_dy + dv_t * dt_dy;

			// Edge functions for coverage testing (two triangles forming the quad)
			const float t1_w0_dx = v1.y - v2.y, t1_w0_dy = v2.x - v1.x;
			const float t1_w1_dx = v2.y - v0.y, t1_w1_dy = v0.x - v2.x;
			const float t1_w2_dx = v0.y - v1.y, t1_w2_dy = v1.x - v0.x;
			const float area1 = (v2.x - v1.x) * (v0.y - v1.y) - (v2.y - v1.y) * (v0.x - v1.x);

			const float t2_w0_dx = v1.y - v3.y, t2_w0_dy = v3.x - v1.x;
			const float t2_w1_dx = v3.y - v2.y, t2_w1_dy = v2.x - v3.x;
			const float t2_w2_dx = v2.y - v1.y, t2_w2_dy = v1.x - v2.x;
			const float area2 = (v3.x - v1.x) * (v2.y - v1.y) - (v3.y - v1.y) * (v2.x - v1.x);

			if (std::fabs(area1) < 1e-6f && std::fabs(area2) < 1e-6f) return;
			const bool sign1 = (area1 > 0.0f);
			const bool sign2 = (area2 > 0.0f);

			// Clip to tile bounds
			std::int32_t xMin = tileX, xMax = tileX + tileW - 1;
			std::int32_t yMin = tileY, yMax = tileY + tileH - 1;

			// Also clip to quad bounding box
			float fxMin = std::min({v0.x, v1.x, v2.x, v3.x});
			float fxMax = std::max({v0.x, v1.x, v2.x, v3.x});
			float fyMin = std::min({v0.y, v1.y, v2.y, v3.y});
			float fyMax = std::max({v0.y, v1.y, v2.y, v3.y});

			xMin = std::max(xMin, static_cast<std::int32_t>(fxMin));
			xMax = std::min(xMax, static_cast<std::int32_t>(fxMax));
			yMin = std::max(yMin, static_cast<std::int32_t>(fyMin));
			yMax = std::min(yMax, static_cast<std::int32_t>(fyMax));

			if (ctx.scissorEnabled) {
				xMin = std::max(xMin, ctx.scissorRect.X);
				xMax = std::min(xMax, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				yMin = std::max(yMin, ctx.scissorRect.Y);
				yMax = std::min(yMax, ctx.scissorRect.Y + ctx.scissorRect.H - 1);
			}
			if (xMin > xMax || yMin > yMax) return;

			// Texture info
			static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;
			Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits
			                ? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			// PARANOID GUARD: zero-dimension texture causes % 0 in WrapTexelFix → thread kill
			if DEATH_UNLIKELY(texPixels != nullptr && (texW <= 0 || texH <= 0)) {
				return;
			}

			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
			const bool whiteTint = (tR >= 255 && tG >= 255 && tB >= 255 && tA >= 255);

			const bool useBlend = ctx.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				ctx.blendSrc == BlendFactor::SrcAlpha &&
				ctx.blendDst == BlendFactor::OneMinusSrcAlpha);

			const std::int32_t dudxFix = (texPixels != nullptr ? static_cast<std::int32_t>(dudx * texW * 65536.0f) : 0);
			const std::int32_t dvdxFix = (texPixels != nullptr ? static_cast<std::int32_t>(dvdx * texH * 65536.0f) : 0);

			const FragmentShaderFn cachedShader = ctx.fragmentShader;

			for (std::int32_t py = yMin; py <= yMax; py++) {
				const float pyCtr = py + 0.5f;
				const std::int32_t localRow = py - tileY;

				// Edge functions at start of row
				float px0f = xMin + 0.5f;
				float t1_w0 = (v2.x - v1.x) * (pyCtr - v1.y) - (v2.y - v1.y) * (px0f - v1.x);
				float t1_w1 = (v0.x - v2.x) * (pyCtr - v2.y) - (v0.y - v2.y) * (px0f - v2.x);
				float t1_w2 = (v1.x - v0.x) * (pyCtr - v0.y) - (v1.y - v0.y) * (px0f - v0.x);
				float t2_w0 = (v3.x - v1.x) * (pyCtr - v1.y) - (v3.y - v1.y) * (px0f - v1.x);
				float t2_w1 = (v2.x - v3.x) * (pyCtr - v3.y) - (v2.y - v3.y) * (px0f - v3.x);
				float t2_w2 = (v1.x - v2.x) * (pyCtr - v2.y) - (v1.y - v2.y) * (px0f - v2.x);

				// UV at start of row
				float relX = px0f - v0.x;
				float relY = pyCtr - v0.y;
				float uStart = v0.u + dudx * relX + dudy * relY;
				float vStart = v0.v + dvdx * relX + dvdy * relY;

				std::int32_t uFix = (texPixels != nullptr ? static_cast<std::int32_t>(uStart * texW * 65536.0f) : 0);
				std::int32_t vFix = (texPixels != nullptr ? static_cast<std::int32_t>(vStart * texH * 65536.0f) : 0);

				for (std::int32_t px = xMin; px <= xMax; px++) {
					bool in1 = (std::fabs(area1) >= 1e-6f) &&
						((t1_w0 >= 0.0f) == sign1) && ((t1_w1 >= 0.0f) == sign1) && ((t1_w2 >= 0.0f) == sign1);
					bool in2 = (std::fabs(area2) >= 1e-6f) &&
						((t2_w0 >= 0.0f) == sign2) && ((t2_w1 >= 0.0f) == sign2) && ((t2_w2 >= 0.0f) == sign2);

					if (in1 || in2) {
						const std::int32_t localCol = px - tileX;
						// PARANOID: float→int rounding can push these just outside the tile
						if DEATH_UNLIKELY(localRow < 0 || localRow >= tileH || localCol < 0 || localCol >= tileW) goto NextPixel;
						std::uint8_t* dstPx = tileBuf + (localRow * TileRenderer::TileSize + localCol) * 4;

						std::int32_t sR, sG, sB, sA;
						if (texPixels != nullptr) {
							if (useLinear) {
								std::uint8_t raw[4];
								SampleBilinearFix(texPixels, texW, texH, uFix, vFix, wrapS, wrapT, raw);
								sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
							} else {
								std::int32_t srcX = std::max(0, std::min(texW - 1, WrapTexelFix(uFix, texW, wrapS)));
								std::int32_t srcY = std::max(0, std::min(texH - 1, WrapTexelFix(vFix, texH, wrapT)));
								const std::uint8_t* src = texPixels + (static_cast<std::size_t>(srcY) * texW + srcX) * 4;
								sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
							}
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
						}

						// Tint or shader
						if DEATH_UNLIKELY(cachedShader != nullptr) {
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							FragmentShaderInput fsInput;
							fsInput.rgba = px4;
							fsInput.u = uFix / 65536.0f / static_cast<float>(texW > 0 ? texW : 1);
							fsInput.v = vFix / 65536.0f / static_cast<float>(texH > 0 ? texH : 1);
							fsInput.x = px;
							fsInput.y = py;
							fsInput.texWidth = texW;
							fsInput.texHeight = texH;
							fsInput.textures = ctx.textures;
							fsInput.color = ctx.ff.color;
							fsInput.userData = ctx.fragmentShaderUserData;
							cachedShader(fsInput);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else if DEATH_UNLIKELY(!whiteTint) {
							sR = (sR * tR) >> 8;
							sG = (sG * tG) >> 8;
							sB = (sB * tB) >> 8;
							sA = (sA * tA) >> 8;
						}

						// Blend
						if (useBlend) {
							if (sA == 0) goto NextPixel;
							if (useFastBlend) {
								if (sA >= 255) {
									dstPx[0] = static_cast<std::uint8_t>(sR);
									dstPx[1] = static_cast<std::uint8_t>(sG);
									dstPx[2] = static_cast<std::uint8_t>(sB);
									dstPx[3] = static_cast<std::uint8_t>(sA);
								} else {
									const std::int32_t inv = 255 - sA;
									dstPx[0] = static_cast<std::uint8_t>((sR * sA + dstPx[0] * inv) >> 8);
									dstPx[1] = static_cast<std::uint8_t>((sG * sA + dstPx[1] * inv) >> 8);
									dstPx[2] = static_cast<std::uint8_t>((sB * sA + dstPx[2] * inv) >> 8);
									dstPx[3] = static_cast<std::uint8_t>((sA * 255 + dstPx[3] * inv) >> 8);
								}
							} else {
								const float srcRf = sR / 255.0f, srcGf = sG / 255.0f, srcBf = sB / 255.0f, srcAf = sA / 255.0f;
								const float dstRf = dstPx[0] / 255.0f, dstGf = dstPx[1] / 255.0f, dstBf = dstPx[2] / 255.0f, dstAf = dstPx[3] / 255.0f;
								auto bf = [&](BlendFactor f, float c, bool isSrc) -> float {
									switch (f) {
										case BlendFactor::Zero:             return 0.0f;
										case BlendFactor::One:              return c;
										case BlendFactor::SrcAlpha:         return c * srcAf;
										case BlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
										case BlendFactor::DstAlpha:         return c * dstAf;
										case BlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
										default:                            return c;
									}
								};
								auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
								dstPx[0] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcRf, true) + bf(ctx.blendDst, dstRf, false)) * 255.0f);
								dstPx[1] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcGf, true) + bf(ctx.blendDst, dstGf, false)) * 255.0f);
								dstPx[2] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcBf, true) + bf(ctx.blendDst, dstBf, false)) * 255.0f);
								dstPx[3] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcAf, true) + bf(ctx.blendDst, dstAf, false)) * 255.0f);
							}
						} else {
							dstPx[0] = static_cast<std::uint8_t>(sR);
							dstPx[1] = static_cast<std::uint8_t>(sG);
							dstPx[2] = static_cast<std::uint8_t>(sB);
							dstPx[3] = static_cast<std::uint8_t>(sA);
						}
					}

				NextPixel:
					t1_w0 += t1_w0_dx; t1_w1 += t1_w1_dx; t1_w2 += t1_w2_dx;
					t2_w0 += t2_w0_dx; t2_w1 += t2_w1_dx; t2_w2 += t2_w2_dx;
					uFix += dudxFix; vFix += dvdxFix;
				}
			}
		}

		// =====================================================================
		// Main entry point: render a single command into the tile buffer
		// =====================================================================
		void RenderCommandToTile(const DrawContext& ctx, PrimitiveType type,
		                         std::int32_t firstVertex, std::int32_t count,
		                         std::uint8_t* tileBuffer, std::int32_t tileX, std::int32_t tileY,
		                         std::int32_t tileW, std::int32_t tileH,
		                         std::int32_t fbWidth, std::int32_t fbHeight)
		{
			// Fast path: procedural 4-vertex quad (most common case)
			if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 &&
							ctx.vertexFormat == nullptr) {
				Vertex2D v0 = FetchVertex(ctx, 0, fbWidth, fbHeight);
				Vertex2D v1 = FetchVertex(ctx, 1, fbWidth, fbHeight);
				Vertex2D v2 = FetchVertex(ctx, 2, fbWidth, fbHeight);
				Vertex2D v3 = FetchVertex(ctx, 3, fbWidth, fbHeight);

				// Check axis-aligned
				if (std::fabs(v0.x - v1.x) < 0.5f && std::fabs(v2.x - v3.x) < 0.5f &&
				    std::fabs(v0.y - v2.y) < 0.5f && std::fabs(v1.y - v3.y) < 0.5f) {
					DrawAxisAlignedQuadTile(ctx, v0, v1, v2, v3, tileBuffer, tileX, tileY, tileW, tileH);
				} else {
					DrawAffineQuadTile(ctx, v0, v1, v2, v3, tileBuffer, tileX, tileY, tileW, tileH);
				}
				return;
			}

			// Generic triangle rasterization path (per-pixel, tile-clipped)
			// Build index list
			SmallVector<std::int32_t, 6> indices;
			if (type == PrimitiveType::TriangleStrip || type == PrimitiveType::Triangles || type == PrimitiveType::TriangleFan) {
				indices.resize(static_cast<std::size_t>(count));
				for (std::int32_t i = 0; i < count; ++i) {
					indices[i] = firstVertex + i;
				}
			}

			auto rasterTri = [&](Vertex2D va, Vertex2D vb, Vertex2D vc) {
				// Simplified triangle rasterizer clipped to tile
				const float w0_dx = vb.y - vc.y, w0_dy = vc.x - vb.x;
				const float w1_dx = vc.y - va.y, w1_dy = va.x - vc.x;
				const float w2_dx = va.y - vb.y, w2_dy = vb.x - va.x;
				const float area = (vc.x - vb.x) * (va.y - vb.y) - (vc.y - vb.y) * (va.x - vb.x);
				if DEATH_UNLIKELY(std::fabs(area) < 1e-6f) {
					return;
				}
				const float invArea = 1.0f / area;
				const bool signPos = (area > 0.0f);

				std::int32_t minX = std::max(tileX, static_cast<std::int32_t>(std::min({va.x, vb.x, vc.x})));
				std::int32_t maxX = std::min(tileX + tileW - 1, static_cast<std::int32_t>(std::max({va.x, vb.x, vc.x})));
				std::int32_t minY = std::max(tileY, static_cast<std::int32_t>(std::min({va.y, vb.y, vc.y})));
				std::int32_t maxY = std::min(tileY + tileH - 1, static_cast<std::int32_t>(std::max({va.y, vb.y, vc.y})));

				if DEATH_UNLIKELY(ctx.scissorEnabled) {
					minX = std::max(minX, ctx.scissorRect.X);
					maxX = std::min(maxX, ctx.scissorRect.X + ctx.scissorRect.W - 1);
					minY = std::max(minY, ctx.scissorRect.Y);
					maxY = std::min(maxY, ctx.scissorRect.Y + ctx.scissorRect.H - 1);
				}
				if DEATH_UNLIKELY(minX > maxX || minY > maxY) {
					return;
				}

				static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;
				Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits
				                ? ctx.textures[ctx.ff.textureUnit] : nullptr);
				const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
				const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
				const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
				const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
				const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);

				const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
				const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
				const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
				const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
				const bool useBlend = ctx.blendingEnabled;
				const bool useFastBlend = (useBlend && ctx.blendSrc == BlendFactor::SrcAlpha && ctx.blendDst == BlendFactor::OneMinusSrcAlpha);

				const float px0f = minX + 0.5f, py0f = minY + 0.5f;
				float w0_row = (vc.x - vb.x) * (py0f - vb.y) - (vc.y - vb.y) * (px0f - vb.x);
				float w1_row = (va.x - vc.x) * (py0f - vc.y) - (va.y - vc.y) * (px0f - vc.x);
				float w2_row = (vb.x - va.x) * (py0f - va.y) - (vb.y - va.y) * (px0f - va.x);

				for (std::int32_t py = minY; py <= maxY; py++) {
					float w0 = w0_row, w1 = w1_row, w2 = w2_row;
					for (std::int32_t px = minX; px <= maxX; ++px, w0 += w0_dx, w1 += w1_dx, w2 += w2_dx) {
						if ((w0 >= 0.0f) != signPos || (w1 >= 0.0f) != signPos || (w2 >= 0.0f) != signPos) {
							continue;
						}

						const float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;
						float u = b0 * va.u + b1 * vb.u + b2 * vc.u;
						float vv = b0 * va.v + b1 * vb.v + b2 * vc.v;

						std::int32_t sR, sG, sB, sA;
						if (texPixels != nullptr) {
							// Clamp UV
							if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f;
							if (vv < 0.0f) vv = 0.0f; if (vv > 1.0f) vv = 1.0f;
							std::int32_t srcX = std::min(texW - 1, static_cast<std::int32_t>(u * (texW - 1) + 0.5f));
							std::int32_t srcY = std::min(texH - 1, static_cast<std::int32_t>(vv * (texH - 1) + 0.5f));
							const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
							sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
						}

						sR = (sR * tR) >> 8; sG = (sG * tG) >> 8;
						sB = (sB * tB) >> 8; sA = (sA * tA) >> 8;

						const std::int32_t localRow = py - tileY;
						const std::int32_t localCol = px - tileX;
						std::uint8_t* dstPx = tileBuffer + (localRow * TileRenderer::TileSize + localCol) * 4;

						if (useBlend) {
							if (sA == 0) continue;
							if (useFastBlend) {
								if (sA >= 255) {
									dstPx[0] = static_cast<std::uint8_t>(sR); dstPx[1] = static_cast<std::uint8_t>(sG);
									dstPx[2] = static_cast<std::uint8_t>(sB); dstPx[3] = static_cast<std::uint8_t>(sA);
								} else {
									const std::int32_t inv = 255 - sA;
									dstPx[0] = static_cast<std::uint8_t>((sR * sA + dstPx[0] * inv) >> 8);
									dstPx[1] = static_cast<std::uint8_t>((sG * sA + dstPx[1] * inv) >> 8);
									dstPx[2] = static_cast<std::uint8_t>((sB * sA + dstPx[2] * inv) >> 8);
									dstPx[3] = static_cast<std::uint8_t>((sA * 255 + dstPx[3] * inv) >> 8);
								}
							} else {
								dstPx[0] = static_cast<std::uint8_t>(sR); dstPx[1] = static_cast<std::uint8_t>(sG);
								dstPx[2] = static_cast<std::uint8_t>(sB); dstPx[3] = static_cast<std::uint8_t>(sA);
							}
						} else {
							dstPx[0] = static_cast<std::uint8_t>(sR); dstPx[1] = static_cast<std::uint8_t>(sG);
							dstPx[2] = static_cast<std::uint8_t>(sB); dstPx[3] = static_cast<std::uint8_t>(sA);
						}
					}
					w0_row += w0_dy; w1_row += w1_dy; w2_row += w2_dy;
				}
			};

			// Dispatch based on primitive type
			switch (type) {
				case PrimitiveType::Triangles:
					for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
						rasterTri(FetchVertex(ctx, indices[i], fbWidth, fbHeight),
						          FetchVertex(ctx, indices[i + 1], fbWidth, fbHeight),
						          FetchVertex(ctx, indices[i + 2], fbWidth, fbHeight));
					}
					break;
				case PrimitiveType::TriangleStrip:
					for (std::size_t i = 0; i + 2 < indices.size(); i++) {
						if (i & 1) {
							rasterTri(FetchVertex(ctx, indices[i], fbWidth, fbHeight),
							          FetchVertex(ctx, indices[i + 2], fbWidth, fbHeight),
							          FetchVertex(ctx, indices[i + 1], fbWidth, fbHeight));
						} else {
							rasterTri(FetchVertex(ctx, indices[i], fbWidth, fbHeight),
							          FetchVertex(ctx, indices[i + 1], fbWidth, fbHeight),
							          FetchVertex(ctx, indices[i + 2], fbWidth, fbHeight));
						}
					}
					break;
				case PrimitiveType::TriangleFan:
					for (std::size_t i = 1; i + 1 < indices.size(); i++) {
						rasterTri(FetchVertex(ctx, indices[0], fbWidth, fbHeight),
						          FetchVertex(ctx, indices[i], fbWidth, fbHeight),
						          FetchVertex(ctx, indices[i + 1], fbWidth, fbHeight));
					}
					break;
			}
		}
	}
}

#endif