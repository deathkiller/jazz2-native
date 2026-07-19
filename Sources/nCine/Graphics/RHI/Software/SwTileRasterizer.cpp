#if defined(WITH_RHI_SOFTWARE)

#include "SwTileRenderer.h"
#include "SwScanlineOps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	// The sampler/filter enums live in the parent nCine namespace; pull them in so the ported rasterizer
	// bodies can name them unqualified (this TU never sees any legacy shadow of the same names)
	using nCine::SamplerFilter;
	using nCine::SamplerWrapping;

	// Per-tile rasterization, called from SwTileRenderer::ProcessTile. All the sampling / blend / vertex
	// helpers below are file-local (internal linkage) copies of the immediate rasterizer's math kept in
	// SwRaster.cpp — the tile path is standalone, so nothing crosses the TU boundary except
	// RenderCommandToTile (external linkage), which SwTileRenderer.cpp forward-declares and calls.
	namespace TileInternal
	{
		// =====================================================================
		// Fixed-point / integer texel wrapping helpers
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

		// Float UV wrap for the generic triangle path - a verbatim copy of SwRaster.cpp's WrapUV so a
		// deferred triangle samples exactly like an immediate one (the two paths must stay bit-identical:
		// the same draw may take either depending on the deferral queue's state).
		static inline float WrapUV(float t, SamplerWrapping mode)
		{
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
				default: // ClampToEdge
					if (t < 0.0f) return 0.0f;
					if (t > 1.0f) return 1.0f;
					return t;
			}
		}

		// =====================================================================
		// Bilinear sampling from 16.16 fixed-point UV coordinates
		// =====================================================================

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

		// Float-coordinate bilinear sample for the generic triangle path - a verbatim copy of
		// SwRaster.cpp's SampleBilinearFloat (see WrapUV above for why the copies must match).
		static inline void SampleBilinearFloat(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                                       float u, float v,
		                                       SamplerWrapping wrapS, SamplerWrapping wrapT,
		                                       std::uint8_t* out)
		{
			const float uf = u * texW - 0.5f;
			const float vf = v * texH - 0.5f;

			std::int32_t x0 = static_cast<std::int32_t>(std::floor(uf));
			std::int32_t y0 = static_cast<std::int32_t>(std::floor(vf));

			std::int32_t fx = static_cast<std::int32_t>((uf - x0) * 255.0f + 0.5f);
			std::int32_t fy = static_cast<std::int32_t>((vf - y0) * 255.0f + 0.5f);
			if (fx < 0) fx = 0; if (fx > 255) fx = 255;
			if (fy < 0) fy = 0; if (fy > 255) fy = 255;

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

		// Scanline blending and tinting use the CPU-dispatched implementations shared with the immediate
		// rasterizer (SwScanlineOps.h): the previous per-TU copies here were NEON-or-scalar, which left the
		// x86 desktop build running the hottest inner loops scalar while SwRaster.cpp's SSE2/AVX2 code idled.

		// =====================================================================
		// PaletteRemap LUT fast path
		// Applies the per-command table SwTileRenderer::SubmitCommand baked (ctx.paletteLut) in place of
		// the transpiled fragment: the whole palette + tint math collapses to indexing by the raw texel's
		// index byte. Output is bit-identical to the fragment (the table replicates its float math).
		// =====================================================================

		static inline void ApplyPaletteLutPixel(const SwPaletteLut& lut, std::uint8_t* px)
		{
			// Read both source bytes before writing: the index/alpha offsets may address any of the four
			const std::uint8_t* entry = lut.packed[px[lut.indexByteOffset]];
			if (lut.alphaByteOffset >= 0) {
				// Per-pixel source alpha (RG8 index textures: swizzle maps .a to the texel's alpha byte).
				// The 255 / 0 shortcuts are exact: 255/255 == 1.0 is the factor baked into entry[3], and a
				// zero source alpha multiplies through to a quantized 0.
				const std::uint8_t alphaByte = px[lut.alphaByteOffset];
				const std::uint8_t idx = px[lut.indexByteOffset];
				px[0] = entry[0];
				px[1] = entry[1];
				px[2] = entry[2];
				if (alphaByte == 255) {
					px[3] = entry[3];
				} else if (alphaByte == 0) {
					px[3] = 0;
				} else {
					// Exactly the fragment's (palette.a * src.a) * tint.a, quantized like packColor
					const float palA = lut.palAlphaByte[idx] / 255.0f;
					const float srcA = alphaByte / 255.0f;
					px[3] = SwQuantizeColor((palA * srcA) * lut.tintAlpha);
				}
			} else {
				// Constant source alpha: the entire output pixel is the 4-byte table entry
				px[0] = entry[0];
				px[1] = entry[1];
				px[2] = entry[2];
				px[3] = entry[3];
			}
		}

		static void ApplyPaletteLutScanline(const SwPaletteLut& lut, std::uint8_t* scanBuf, std::int32_t width)
		{
			if (lut.alphaByteOffset < 0) {
				const std::int32_t indexByteOffset = lut.indexByteOffset;
				for (std::int32_t i = 0; i < width; i++) {
					std::uint8_t* px = &scanBuf[i * 4];
					std::memcpy(px, lut.packed[px[indexByteOffset]], 4);
				}
			} else {
				for (std::int32_t i = 0; i < width; i++) {
					ApplyPaletteLutPixel(lut, &scanBuf[i * 4]);
				}
			}
		}

		// =====================================================================
		// Vertex fetch — identical transform to SwRaster::FetchVertex (procedural sprite quad or interleaved
		// clip-space vertices), only parameterized on the viewport so it stays thread-safe during a flush.
		// =====================================================================
		static Vertex2D FetchVertex(const DrawContext& ctx, std::int32_t index,
		                            std::int32_t viewportX, std::int32_t viewportY,
		                            std::int32_t viewportW, std::int32_t viewportH)
		{
			Vertex2D out = { 0, 0, 0, 0, 1, 1, 1, 1 };

			if DEATH_LIKELY(ctx.vertexData == nullptr) {
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
				// General path: interleaved clip-space vertices [x, y, u, v] (device pre-transforms positions)
				std::int32_t stride = ctx.vertexStride;
				if DEATH_UNLIKELY(stride <= 0) stride = 4 * static_cast<std::int32_t>(sizeof(float));
				const std::uint8_t* base = static_cast<const std::uint8_t*>(ctx.vertexData);
				const float* f = reinterpret_cast<const float*>(base + static_cast<std::size_t>(index) * stride);
				out.x = f[0];
				out.y = f[1];
				out.u = f[2];
				out.v = f[3];

				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			}

			// Viewport transform: NDC [-1,+1] → screen pixel coordinates
			out.x = (out.x + 1.0f) * 0.5f * static_cast<float>(viewportW) + static_cast<float>(viewportX);
			out.y = (1.0f - out.y) * 0.5f * static_cast<float>(viewportH) + static_cast<float>(viewportY);

			// Snap to the pixel grid when within epsilon of an integer (matches the immediate path)
			constexpr float SnapEps = 1.0f / 128.0f;
			float rxSnap = std::round(out.x);
			float rySnap = std::round(out.y);
			if (std::fabs(out.x - rxSnap) < SnapEps) out.x = rxSnap;
			if (std::fabs(out.y - rySnap) < SnapEps) out.y = rySnap;

			return out;
		}

		// =====================================================================
		// Submit-time preparation of a procedural quad command (see PreparedQuad in SwTileRenderer.h)
		// Runs everything the tile rasterizers used to re-derive per binned tile - the four FetchVertex
		// transforms, the texture / tint / blend derivation and the axis-aligned / affine setup - exactly
		// once per command, with the identical expressions so the per-tile rasterization is bit-identical.
		// Leaves prep.valid == false for a quad the rasterizers would reject as a no-op, so SubmitCommand
		// can discard the command without binning it.
		// =====================================================================
		void PrepareQuad(const DrawContext& ctx, std::int32_t viewportX, std::int32_t viewportY,
		                 std::int32_t viewportW, std::int32_t viewportH, SwTileRenderer::PreparedQuad& prep)
		{
			prep.valid = false;
			prep.constantFill = false;

			const Vertex2D v0 = FetchVertex(ctx, 0, viewportX, viewportY, viewportW, viewportH);
			const Vertex2D v1 = FetchVertex(ctx, 1, viewportX, viewportY, viewportW, viewportH);
			const Vertex2D v2 = FetchVertex(ctx, 2, viewportX, viewportY, viewportW, viewportH);
			const Vertex2D v3 = FetchVertex(ctx, 3, viewportX, viewportY, viewportW, viewportH);
			prep.v[0] = v0;
			prep.v[1] = v1;
			prep.v[2] = v2;
			prep.v[3] = v3;

			// Check axis-aligned (same tolerance as the immediate path)
			prep.axisAligned = (std::fabs(v0.x - v1.x) < 0.5f && std::fabs(v2.x - v3.x) < 0.5f &&
			                    std::fabs(v0.y - v2.y) < 0.5f && std::fabs(v1.y - v3.y) < 0.5f);

			// Screen-space bounding box (the axis-aligned setup and both tile clips read it)
			prep.fxMin = std::min({v0.x, v1.x, v2.x, v3.x});
			prep.fxMax = std::max({v0.x, v1.x, v2.x, v3.x});
			prep.fyMin = std::min({v0.y, v1.y, v2.y, v3.y});
			prep.fyMax = std::max({v0.y, v1.y, v2.y, v3.y});

			// Texture info
			const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
			                ? ctx.textures[ctx.ff.textureUnit] : nullptr);
			prep.texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			prep.texW = (tex != nullptr ? tex->GetWidth() : 0);
			prep.texH = (tex != nullptr ? tex->GetHeight() : 0);
			prep.wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			prep.wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			prep.useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && prep.texW > 1 && prep.texH > 1);

			// PARANOID GUARD (was per tile): a zero-dimension texture causes % 0 (division by zero) in
			// WrapTexelFix / NormalizeRepeatFix, which kills the worker thread on ARM.
			if DEATH_UNLIKELY(prep.texPixels != nullptr && (prep.texW <= 0 || prep.texH <= 0)) {
				return; // malformed texture — skip the entire draw call
			}

			// Tint color
			prep.tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			prep.tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			prep.tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			prep.tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
			prep.whiteTint = (prep.tR >= 255 && prep.tG >= 255 && prep.tB >= 255 && prep.tA >= 255);

			prep.useBlend = ctx.blendingEnabled;
			prep.useFastBlend = (prep.useBlend &&
				ctx.blendSrc == SwBlendFactor::SrcAlpha &&
				ctx.blendDst == SwBlendFactor::OneMinusSrcAlpha);

			// Constant-color solid fill: the hint promises the fragment writes packColor(vColor) regardless
			// of position or texel, so evaluate it exactly once here (bit-identical by construction) and let
			// the rasterizers fill / blend the constant instead of calling it per pixel
			if DEATH_UNLIKELY(ctx.constantColorHint && prep.texPixels == nullptr && ctx.fragmentShader != nullptr) {
				std::uint8_t px4[4] = { 255, 255, 255, 255 };
				FragmentShaderInput fsInput;
				fsInput.rgba = px4;
				fsInput.u = 0.0f;
				fsInput.v = 0.0f;
				fsInput.x = 0;
				fsInput.y = 0;
				fsInput.texWidth = prep.texW;
				fsInput.texHeight = prep.texH;
				fsInput.textures = ctx.textures;
				fsInput.color = ctx.ff.color;
				fsInput.userData = ctx.fragmentShaderUserData;
				ctx.fragmentShader(fsInput);
				std::memcpy(prep.constColor, px4, sizeof(prep.constColor));
				prep.constantFill = true;
			}

			if (prep.axisAligned) {
				prep.fullW = prep.fxMax - prep.fxMin;
				prep.fullH = prep.fyMax - prep.fyMin;
				if (prep.fullW < 0.5f || prep.fullH < 0.5f) {
					return; // degenerate — the axis-aligned rasterizer draws nothing
				}

				// UV corners
				prep.uLeft  = (v2.x <= v0.x) ? v2.u : v0.u;
				prep.uRight = (v2.x <= v0.x) ? v0.u : v2.u;
				prep.vTop   = (v0.y <= v1.y) ? v0.v : v1.v;
				prep.vBot   = (v0.y <= v1.y) ? v1.v : v0.v;

				// Fixed-point UV setup
				prep.texWFix = prep.texW << 16;
				prep.dtxFix = (prep.texPixels != nullptr ? static_cast<std::int32_t>((prep.uRight - prep.uLeft) * prep.texW * 65536.0f / prep.fullW) : 0);
				prep.dtyFix = (prep.texPixels != nullptr ? static_cast<std::int32_t>((prep.vBot - prep.vTop) * prep.texH * 65536.0f / prep.fullH) : 0);

				prep.useRepeatS = (prep.wrapS == SamplerWrapping::Repeat);
				prep.useRepeatT = (prep.wrapT == SamplerWrapping::Repeat);
				prep.useClampS = (prep.wrapS == SamplerWrapping::ClampToEdge);
				prep.useScanBuf = ((prep.useFastBlend || !prep.useBlend) && prep.texPixels != nullptr);
			} else {
				// Compute affine UV mapping from the first triangle (v0, v1, v2)
				const float e1x = v2.x - v0.x, e1y = v2.y - v0.y;
				const float e2x = v1.x - v0.x, e2y = v1.y - v0.y;
				const float det = e1x * e2y - e1y * e2x;
				if (std::fabs(det) < 1e-6f) {
					return; // degenerate — the affine rasterizer draws nothing
				}
				const float invDet = 1.0f / det;

				const float du_s = v2.u - v0.u, du_t = v1.u - v0.u;
				const float dv_s = v2.v - v0.v, dv_t = v1.v - v0.v;

				const float ds_dx = e2y * invDet, ds_dy = -e2x * invDet;
				const float dt_dx = -e1y * invDet, dt_dy = e1x * invDet;

				prep.dudx = du_s * ds_dx + du_t * dt_dx;
				prep.dudy = du_s * ds_dy + du_t * dt_dy;
				prep.dvdx = dv_s * ds_dx + dv_t * dt_dx;
				prep.dvdy = dv_s * ds_dy + dv_t * dt_dy;

				// Edge functions for coverage testing (two triangles forming the quad)
				prep.t1w0dx = v1.y - v2.y;
				prep.t1w1dx = v2.y - v0.y;
				prep.t1w2dx = v0.y - v1.y;
				const float area1 = (v2.x - v1.x) * (v0.y - v1.y) - (v2.y - v1.y) * (v0.x - v1.x);

				prep.t2w0dx = v1.y - v3.y;
				prep.t2w1dx = v3.y - v2.y;
				prep.t2w2dx = v2.y - v1.y;
				const float area2 = (v3.x - v1.x) * (v2.y - v1.y) - (v3.y - v1.y) * (v2.x - v1.x);

				if (std::fabs(area1) < 1e-6f && std::fabs(area2) < 1e-6f) {
					return; // degenerate — the affine rasterizer draws nothing
				}
				prep.tri1Valid = (std::fabs(area1) >= 1e-6f);
				prep.tri2Valid = (std::fabs(area2) >= 1e-6f);
				prep.sign1 = (area1 > 0.0f);
				prep.sign2 = (area2 > 0.0f);

				prep.dudxFix = (prep.texPixels != nullptr ? static_cast<std::int32_t>(prep.dudx * prep.texW * 65536.0f) : 0);
				prep.dvdxFix = (prep.texPixels != nullptr ? static_cast<std::int32_t>(prep.dvdx * prep.texH * 65536.0f) : 0);
			}

			prep.valid = true;
		}

		// =====================================================================
		// Tile-local axis-aligned quad rasterizer
		// Renders only the portion of the quad that intersects the tile. The destination buffer is the tile
		// (TileSize stride), not the full framebuffer. The per-command state (vertices, texture / tint /
		// blend derivation, fixed-point UV steps) comes precomputed in `prep` (see PrepareQuad); only the
		// tile clip and the row walk remain per-tile, evaluated with the identical expressions.
		// =====================================================================
		static void DrawAxisAlignedQuadTile(const DrawContext& ctx, const SwTileRenderer::PreparedQuad& prep,
		                                   std::uint8_t* tileBuf, std::int32_t tileX, std::int32_t tileY,
		                                   std::int32_t tileW, std::int32_t tileH)
		{
			// Clip to tile bounds (in framebuffer coordinates)
			std::int32_t xMin = std::max(tileX, static_cast<std::int32_t>(prep.fxMin));
			std::int32_t xMax = std::min(tileX + tileW - 1, static_cast<std::int32_t>(prep.fxMax - 0.5f));
			std::int32_t yMin = std::max(tileY, static_cast<std::int32_t>(prep.fyMin));
			std::int32_t yMax = std::min(tileY + tileH - 1, static_cast<std::int32_t>(prep.fyMax - 0.5f));

			// Also apply scissor (scissorRect.Y is already stored top-down by SubmitCommand)
			if DEATH_UNLIKELY(ctx.scissorEnabled) {
				xMin = std::max(xMin, ctx.scissorRect.X);
				xMax = std::min(xMax, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				yMin = std::max(yMin, ctx.scissorRect.Y);
				yMax = std::min(yMax, ctx.scissorRect.Y + ctx.scissorRect.H - 1);
			}
			if DEATH_UNLIKELY(xMin > xMax || yMin > yMax) return;

			// Precomputed per-command state (local names kept so the loops below stay verbatim)
			const std::uint8_t* texPixels = prep.texPixels;
			const std::int32_t texW = prep.texW;
			const std::int32_t texH = prep.texH;
			const SamplerWrapping wrapS = prep.wrapS;
			const SamplerWrapping wrapT = prep.wrapT;
			const bool useLinear = prep.useLinear;
			const std::int32_t tR = prep.tR, tG = prep.tG, tB = prep.tB, tA = prep.tA;
			const bool whiteTint = prep.whiteTint;
			const bool useBlend = prep.useBlend;
			const bool useFastBlend = prep.useFastBlend;
			const std::int32_t texWFix = prep.texWFix;
			const std::int32_t dtxFix = prep.dtxFix;
			const std::int32_t dtyFix = prep.dtyFix;
			const bool useRepeatS = prep.useRepeatS;
			const bool useRepeatT = prep.useRepeatT;

			const std::int32_t txBase = (texPixels != nullptr ? static_cast<std::int32_t>((prep.uLeft + (xMin + 0.5f - prep.fxMin) * (prep.uRight - prep.uLeft) / prep.fullW) * texW * 65536.0f) : 0);
			std::int32_t tyFix = (texPixels != nullptr ? static_cast<std::int32_t>((prep.vTop + (yMin + 0.5f - prep.fyMin) * (prep.vBot - prep.vTop) / prep.fullH) * texH * 65536.0f) : 0);

			const bool uvSafeX = (prep.useClampS && txBase >= 0 && (txBase + dtxFix * (xMax - xMin)) >= 0 &&
			                      (txBase >> 16) < texW && ((txBase + dtxFix * (xMax - xMin)) >> 16) < texW);

			const std::int32_t scanWidth = xMax - xMin + 1;

			// Constant-color solid fill (no texture): the fragment output is the per-command constant
			// prep.constColor, so skip per-pixel work entirely - a plain 32-bit pattern fill when the write
			// is opaque, the constant-source SIMD blend otherwise (both bit-identical to the per-pixel
			// path, including its destination-alpha convention). Generic (non-fast) blend factors fall
			// through to the per-pixel loop below, which consumes the constant in place of the fragment.
			if DEATH_UNLIKELY(prep.constantFill && (useFastBlend || !useBlend)) {
				if (useFastBlend && prep.constColor[3] == 0) {
					return; // Fully transparent: the per-pixel path would skip every pixel
				}
				const bool opaqueFill = (!useBlend || prep.constColor[3] >= 255);
				std::uint32_t pattern;
				std::memcpy(&pattern, prep.constColor, sizeof(pattern));
				for (std::int32_t py = yMin; py <= yMax; py++) {
					const std::int32_t localRow = py - tileY;
					const std::int32_t localCol = xMin - tileX;
					// PARANOID: float→int rounding can push these just outside the tile
					if DEATH_UNLIKELY(localRow < 0 || localRow >= tileH || localCol < 0 || localCol >= tileW) continue;
					std::uint8_t* dstRow = tileBuf + (localRow * SwTileRenderer::TileSize + localCol) * 4;
					if (opaqueFill) {
						std::uint32_t* dst32 = reinterpret_cast<std::uint32_t*>(dstRow);
						for (std::int32_t i = 0; i < scanWidth; i++) {
							dst32[i] = pattern;
						}
					} else {
						BlendScanlineConstSrcAlpha(dstRow, scanWidth, prep.constColor);
					}
				}
				return;
			}

			// Scanline buffer for tint+blend (fits within the tile width = max 32 pixels)
			alignas(16) std::uint8_t scanBuf[SwTileRenderer::TileSize * 4];
			const bool useScanBuf = prep.useScanBuf;

			// Cache ctx.fragmentShader into a local to defeat compiler aliasing pessimism (fsInput.textures
			// aliases with ctx through FragmentShaderInput). Flush() already waits on workersActive before
			// returning, so ctx is guaranteed not to be overwritten while workers run.
			const FragmentShaderFn cachedShader = ctx.fragmentShader;

			for (std::int32_t py = yMin; py <= yMax; py++, tyFix += dtyFix) {
				// Tile-local row offset
				const std::int32_t localRow = py - tileY;
				const std::int32_t localCol = xMin - tileX;
				// PARANOID: float→int rounding can push these just outside the tile
				if DEATH_UNLIKELY(localRow < 0 || localRow >= tileH || localCol < 0 || localCol >= tileW) continue;
				std::uint8_t* dstRow = tileBuf + (localRow * SwTileRenderer::TileSize + localCol) * 4;

				// Get source Y
				std::int32_t srcY = 0;
				if (texPixels != nullptr) {
					srcY = (useRepeatT)
						? WrapTexelFix(tyFix, texH, SamplerWrapping::Repeat)
						: WrapTexelFix(tyFix, texH, wrapT);
				}
				const std::uint8_t* texRow = (texPixels != nullptr ? texPixels + static_cast<std::size_t>(srcY) * texW * 4 : nullptr);

#if defined(DEATH_TARGET_ARM)
				// Prefetch the next texture row
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
							// uvSafeX guarantees (srcX + scanWidth - 1) < texW, so the full scanline is in-bounds
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

					// Phase 2: palette LUT, fragment shader or tint
					if DEATH_UNLIKELY(ctx.paletteLut != nullptr) {
						// PaletteRemap fast path: the gather above already fetched the raw index texels, so
						// each pixel is a table lookup (the generic fragment would re-sample and redo the
						// palette math per pixel). Phase 3 below blends the result unchanged.
						ApplyPaletteLutScanline(*ctx.paletteLut, scanBuf, scanWidth);
					} else if DEATH_UNLIKELY(cachedShader != nullptr) {
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
						TintScanline(scanBuf, scanWidth, tR, tG, tB, tA);
					}

					// Phase 3: blend or copy
					if (useFastBlend) {
						BlendScanlineSrcAlpha(dstRow, scanBuf, scanWidth);
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

						if DEATH_UNLIKELY(ctx.paletteLut != nullptr) {
							// PaletteRemap fast path (see the scanline variant above)
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							ApplyPaletteLutPixel(*ctx.paletteLut, px4);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else if DEATH_UNLIKELY(prep.constantFill) {
							// Solid fill under generic blend factors: consume the precomputed constant
							// (identical to what the fragment would write) instead of calling it per pixel
							sR = prep.constColor[0]; sG = prep.constColor[1]; sB = prep.constColor[2]; sA = prep.constColor[3];
						} else if DEATH_UNLIKELY(cachedShader != nullptr) {
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
								auto bf = [&](SwBlendFactor f, float c) -> float {
									switch (f) {
										case SwBlendFactor::Zero:             return 0.0f;
										case SwBlendFactor::One:              return c;
										case SwBlendFactor::SrcAlpha:         return c * srcAf;
										case SwBlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
										case SwBlendFactor::DstAlpha:         return c * dstAf;
										case SwBlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
										default:                              return c;
									}
								};
								auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
								dstRow[0] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcRf) + bf(ctx.blendDst, dstRf)) * 255.0f);
								dstRow[1] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcGf) + bf(ctx.blendDst, dstGf)) * 255.0f);
								dstRow[2] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcBf) + bf(ctx.blendDst, dstBf)) * 255.0f);
								dstRow[3] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcAf) + bf(ctx.blendDst, dstAf)) * 255.0f);
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
		// The per-command state (vertices, UV gradients, edge-function increments, texture / tint / blend
		// derivation) comes precomputed in `prep` (see PrepareQuad); only the tile clip and the row walk
		// remain per-tile, evaluated with the identical expressions.
		// =====================================================================
		static void DrawAffineQuadTile(const DrawContext& ctx, const SwTileRenderer::PreparedQuad& prep,
		                               std::uint8_t* tileBuf, std::int32_t tileX, std::int32_t tileY,
		                               std::int32_t tileW, std::int32_t tileH)
		{
			// Precomputed per-command state (local names kept so the loops below stay verbatim)
			const Vertex2D v0 = prep.v[0], v1 = prep.v[1], v2 = prep.v[2], v3 = prep.v[3];
			const float dudx = prep.dudx, dudy = prep.dudy;
			const float dvdx = prep.dvdx, dvdy = prep.dvdy;
			const float t1_w0_dx = prep.t1w0dx, t1_w1_dx = prep.t1w1dx, t1_w2_dx = prep.t1w2dx;
			const float t2_w0_dx = prep.t2w0dx, t2_w1_dx = prep.t2w1dx, t2_w2_dx = prep.t2w2dx;
			const bool sign1 = prep.sign1;
			const bool sign2 = prep.sign2;

			// Clip to tile bounds
			std::int32_t xMin = tileX, xMax = tileX + tileW - 1;
			std::int32_t yMin = tileY, yMax = tileY + tileH - 1;

			// Also clip to the quad bounding box
			xMin = std::max(xMin, static_cast<std::int32_t>(prep.fxMin));
			xMax = std::min(xMax, static_cast<std::int32_t>(prep.fxMax));
			yMin = std::max(yMin, static_cast<std::int32_t>(prep.fyMin));
			yMax = std::min(yMax, static_cast<std::int32_t>(prep.fyMax));

			if (ctx.scissorEnabled) {
				xMin = std::max(xMin, ctx.scissorRect.X);
				xMax = std::min(xMax, ctx.scissorRect.X + ctx.scissorRect.W - 1);
				yMin = std::max(yMin, ctx.scissorRect.Y);
				yMax = std::min(yMax, ctx.scissorRect.Y + ctx.scissorRect.H - 1);
			}
			if (xMin > xMax || yMin > yMax) return;

			const std::uint8_t* texPixels = prep.texPixels;
			const std::int32_t texW = prep.texW;
			const std::int32_t texH = prep.texH;
			const SamplerWrapping wrapS = prep.wrapS;
			const SamplerWrapping wrapT = prep.wrapT;
			const bool useLinear = prep.useLinear;
			const std::int32_t tR = prep.tR, tG = prep.tG, tB = prep.tB, tA = prep.tA;
			const bool whiteTint = prep.whiteTint;
			const bool useBlend = prep.useBlend;
			const bool useFastBlend = prep.useFastBlend;
			const std::int32_t dudxFix = prep.dudxFix;
			const std::int32_t dvdxFix = prep.dvdxFix;

			const FragmentShaderFn cachedShader = ctx.fragmentShader;

			// Fully transparent solid fill under the fast blend: every pixel would be skipped
			if DEATH_UNLIKELY(prep.constantFill && useFastBlend && prep.constColor[3] == 0) {
				return;
			}

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
					bool in1 = prep.tri1Valid &&
						((t1_w0 >= 0.0f) == sign1) && ((t1_w1 >= 0.0f) == sign1) && ((t1_w2 >= 0.0f) == sign1);
					bool in2 = prep.tri2Valid &&
						((t2_w0 >= 0.0f) == sign2) && ((t2_w1 >= 0.0f) == sign2) && ((t2_w2 >= 0.0f) == sign2);

					if (in1 || in2) {
						const std::int32_t localCol = px - tileX;
						// PARANOID: float→int rounding can push these just outside the tile
						if DEATH_UNLIKELY(localRow < 0 || localRow >= tileH || localCol < 0 || localCol >= tileW) goto NextPixel;
						{
							std::uint8_t* dstPx = tileBuf + (localRow * SwTileRenderer::TileSize + localCol) * 4;

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

							// Palette LUT, constant fill, tint or shader
							if DEATH_UNLIKELY(ctx.paletteLut != nullptr) {
								// PaletteRemap fast path (see the scanline variant above); rotated palette
								// sprites land here
								std::uint8_t px4[4] = {
									static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
									static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
								};
								ApplyPaletteLutPixel(*ctx.paletteLut, px4);
								sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
							} else if DEATH_UNLIKELY(prep.constantFill) {
								// Rotated solid fill: consume the precomputed constant (identical to what
								// the fragment would write) instead of calling it per pixel
								sR = prep.constColor[0]; sG = prep.constColor[1]; sB = prep.constColor[2]; sA = prep.constColor[3];
							} else if DEATH_UNLIKELY(cachedShader != nullptr) {
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
									auto bf = [&](SwBlendFactor f, float c) -> float {
										switch (f) {
											case SwBlendFactor::Zero:             return 0.0f;
											case SwBlendFactor::One:              return c;
											case SwBlendFactor::SrcAlpha:         return c * srcAf;
											case SwBlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
											case SwBlendFactor::DstAlpha:         return c * dstAf;
											case SwBlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
											default:                              return c;
										}
									};
									auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
									dstPx[0] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcRf) + bf(ctx.blendDst, dstRf)) * 255.0f);
									dstPx[1] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcGf) + bf(ctx.blendDst, dstGf)) * 255.0f);
									dstPx[2] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcBf) + bf(ctx.blendDst, dstBf)) * 255.0f);
									dstPx[3] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcAf) + bf(ctx.blendDst, dstAf)) * 255.0f);
								}
							} else {
								dstPx[0] = static_cast<std::uint8_t>(sR);
								dstPx[1] = static_cast<std::uint8_t>(sG);
								dstPx[2] = static_cast<std::uint8_t>(sB);
								dstPx[3] = static_cast<std::uint8_t>(sA);
							}
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
		void RenderCommandToTile(const DrawContext& ctx, const SwTileRenderer::PreparedQuad* prep,
		                         PrimitiveType type,
		                         std::int32_t firstVertex, std::int32_t count,
		                         std::uint8_t* tileBuffer, std::int32_t tileX, std::int32_t tileY,
		                         std::int32_t tileW, std::int32_t tileH,
		                         std::int32_t viewportX, std::int32_t viewportY,
		                         std::int32_t viewportW, std::int32_t viewportH)
		{
			// Fast path: procedural 4-vertex quad (most common case). SubmitCommand prepared the vertices
			// and derived state once per command; a caller without a prepared command derives one locally.
			if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 &&
							ctx.vertexData == nullptr) {
				SwTileRenderer::PreparedQuad localPrep;
				if DEATH_UNLIKELY(prep == nullptr) {
					PrepareQuad(ctx, viewportX, viewportY, viewportW, viewportH, localPrep);
					prep = &localPrep;
				}
				if DEATH_UNLIKELY(!prep->valid) {
					return; // degenerate quad — draws nothing on any path
				}
				if (prep->axisAligned) {
					DrawAxisAlignedQuadTile(ctx, *prep, tileBuffer, tileX, tileY, tileW, tileH);
				} else {
					DrawAffineQuadTile(ctx, *prep, tileBuffer, tileX, tileY, tileW, tileH);
				}
				return;
			}

			// Generic triangle rasterization path (per-pixel, tile-clipped)
			SmallVector<std::int32_t, 6> indices;
			if (type == PrimitiveType::TriangleStrip || type == PrimitiveType::Triangles || type == PrimitiveType::TriangleFan) {
				indices.resize(static_cast<std::size_t>(count));
				for (std::int32_t i = 0; i < count; ++i) {
					indices[i] = firstVertex + i;
				}
			}

			auto rasterTri = [&](Vertex2D va, Vertex2D vb, Vertex2D vc) {
				// Simplified triangle rasterizer clipped to the tile
				float w0_dx = vb.y - vc.y, w0_dy = vc.x - vb.x;
				float w1_dx = vc.y - va.y, w1_dy = va.x - vc.x;
				float w2_dx = va.y - vb.y, w2_dy = vb.x - va.x;
				float area = (vc.x - vb.x) * (va.y - vb.y) - (vc.y - vb.y) * (va.x - vb.x);
				if DEATH_UNLIKELY(std::fabs(area) < 1e-6f) {
					return;
				}
				// Orientation-normalize so the interior is positive for every winding, then decide the
				// exact w == 0 edge pixels with a top-left fill rule (include when the normalized edge
				// gradient points right, or straight down for horizontal edges). Two strip triangles
				// sharing a diagonal then cover each of its pixels exactly once - the previous symmetric
				// ">= vs sign" rule made both triangles exclude (or both include) the shared edge,
				// leaving a one-pixel crack (or a double-blend seam) across mesh strips.
				const float orient = (area > 0.0f ? 1.0f : -1.0f);
				area *= orient;
				w0_dx *= orient; w0_dy *= orient;
				w1_dx *= orient; w1_dy *= orient;
				w2_dx *= orient; w2_dy *= orient;
				const float invArea = 1.0f / area;
				const bool incl0 = (w0_dx > 0.0f || (w0_dx == 0.0f && w0_dy > 0.0f));
				const bool incl1 = (w1_dx > 0.0f || (w1_dx == 0.0f && w1_dy > 0.0f));
				const bool incl2 = (w2_dx > 0.0f || (w2_dx == 0.0f && w2_dy > 0.0f));

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

				const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
				                ? ctx.textures[ctx.ff.textureUnit] : nullptr);
				const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
				const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
				const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
				if DEATH_UNLIKELY(texPixels != nullptr && (texW <= 0 || texH <= 0)) {
					texPixels = nullptr;	// Malformed texture - treat as untextured (see PrepareQuad's guard)
				}
				const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
				const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
				const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

				const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
				const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
				const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
				const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
				const bool useBlend = ctx.blendingEnabled;
				const bool useFastBlend = (useBlend && ctx.blendSrc == SwBlendFactor::SrcAlpha && ctx.blendDst == SwBlendFactor::OneMinusSrcAlpha);
				const FragmentShaderFn cachedShader = ctx.fragmentShader;

				const float px0f = minX + 0.5f, py0f = minY + 0.5f;
				float w0_row = orient * ((vc.x - vb.x) * (py0f - vb.y) - (vc.y - vb.y) * (px0f - vb.x));
				float w1_row = orient * ((va.x - vc.x) * (py0f - vc.y) - (va.y - vc.y) * (px0f - vc.x));
				float w2_row = orient * ((vb.x - va.x) * (py0f - va.y) - (vb.y - va.y) * (px0f - va.x));

				for (std::int32_t py = minY; py <= maxY; py++) {
					float w0 = w0_row, w1 = w1_row, w2 = w2_row;
					for (std::int32_t px = minX; px <= maxX; ++px, w0 += w0_dx, w1 += w1_dx, w2 += w2_dx) {
						if ((w0 < 0.0f || (w0 == 0.0f && !incl0)) ||
						    (w1 < 0.0f || (w1 == 0.0f && !incl1)) ||
						    (w2 < 0.0f || (w2 == 0.0f && !incl2))) {
							continue;
						}

						const float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;
						float u = b0 * va.u + b1 * vb.u + b2 * vc.u;
						float vv = b0 * va.v + b1 * vb.v + b2 * vc.v;

						// The sampling below is a verbatim copy of the immediate path's RasterizeTriangle
						// (SwRaster.cpp) so a deferred triangle produces the same bytes as an immediate one
						std::int32_t sR, sG, sB, sA;
						if (texPixels != nullptr && useLinear) {
							u = WrapUV(u, wrapS);
							vv = WrapUV(vv, wrapT);
							std::uint8_t raw[4];
							SampleBilinearFloat(texPixels, texW, texH, u, vv, wrapS, wrapT, raw);
							sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
						} else if (texPixels != nullptr) {
							u = WrapUV(u, wrapS);
							vv = WrapUV(vv, wrapT);
							const std::int32_t srcX = std::max(0, std::min(texW - 1, static_cast<std::int32_t>(u * (texW - 1) + 0.5f)));
							const std::int32_t srcY = std::max(0, std::min(texH - 1, static_cast<std::int32_t>(vv * (texH - 1) + 0.5f)));
							const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
							sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
						}

						// Apply the transpiled fragment or the vertex-color tint (same as the immediate path)
						if DEATH_UNLIKELY(cachedShader != nullptr) {
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							FragmentShaderInput fsInput;
							fsInput.rgba = px4;
							fsInput.u = u;
							fsInput.v = vv;
							fsInput.x = px;
							fsInput.y = py;
							fsInput.texWidth = texW;
							fsInput.texHeight = texH;
							fsInput.textures = ctx.textures;
							fsInput.color = ctx.ff.color;
							fsInput.userData = ctx.fragmentShaderUserData;
							cachedShader(fsInput);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else {
							sR = (sR * tR) >> 8; sG = (sG * tG) >> 8;
							sB = (sB * tB) >> 8; sA = (sA * tA) >> 8;
						}

						const std::int32_t localRow = py - tileY;
						const std::int32_t localCol = px - tileX;
						std::uint8_t* dstPx = tileBuffer + (localRow * SwTileRenderer::TileSize + localCol) * 4;

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
								// Generic blend (same float math as the affine tile rasterizer above)
								const float srcRf = sR / 255.0f, srcGf = sG / 255.0f, srcBf = sB / 255.0f, srcAf = sA / 255.0f;
								const float dstRf = dstPx[0] / 255.0f, dstGf = dstPx[1] / 255.0f, dstBf = dstPx[2] / 255.0f, dstAf = dstPx[3] / 255.0f;
								auto bf = [&](SwBlendFactor f, float c) -> float {
									switch (f) {
										case SwBlendFactor::Zero:             return 0.0f;
										case SwBlendFactor::One:              return c;
										case SwBlendFactor::SrcAlpha:         return c * srcAf;
										case SwBlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
										case SwBlendFactor::DstAlpha:         return c * dstAf;
										case SwBlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
										default:                              return c;
									}
								};
								auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
								dstPx[0] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcRf) + bf(ctx.blendDst, dstRf)) * 255.0f);
								dstPx[1] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcGf) + bf(ctx.blendDst, dstGf)) * 255.0f);
								dstPx[2] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcBf) + bf(ctx.blendDst, dstBf)) * 255.0f);
								dstPx[3] = static_cast<std::uint8_t>(clamp01(bf(ctx.blendSrc, srcAf) + bf(ctx.blendDst, dstAf)) * 255.0f);
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
						rasterTri(FetchVertex(ctx, indices[i], viewportX, viewportY, viewportW, viewportH),
						          FetchVertex(ctx, indices[i + 1], viewportX, viewportY, viewportW, viewportH),
						          FetchVertex(ctx, indices[i + 2], viewportX, viewportY, viewportW, viewportH));
					}
					break;
				case PrimitiveType::TriangleStrip:
					for (std::size_t i = 0; i + 2 < indices.size(); i++) {
						if (i & 1) {
							rasterTri(FetchVertex(ctx, indices[i], viewportX, viewportY, viewportW, viewportH),
							          FetchVertex(ctx, indices[i + 2], viewportX, viewportY, viewportW, viewportH),
							          FetchVertex(ctx, indices[i + 1], viewportX, viewportY, viewportW, viewportH));
						} else {
							rasterTri(FetchVertex(ctx, indices[i], viewportX, viewportY, viewportW, viewportH),
							          FetchVertex(ctx, indices[i + 1], viewportX, viewportY, viewportW, viewportH),
							          FetchVertex(ctx, indices[i + 2], viewportX, viewportY, viewportW, viewportH));
						}
					}
					break;
				case PrimitiveType::TriangleFan:
					for (std::size_t i = 1; i + 1 < indices.size(); i++) {
						rasterTri(FetchVertex(ctx, indices[0], viewportX, viewportY, viewportW, viewportH),
						          FetchVertex(ctx, indices[i], viewportX, viewportY, viewportW, viewportH),
						          FetchVertex(ctx, indices[i + 1], viewportX, viewportY, viewportW, viewportH));
					}
					break;
				default:
					break;
			}
		}
	}
}

#endif
