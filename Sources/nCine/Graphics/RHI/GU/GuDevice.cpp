#include "GuDevice.h"
#include "GuBuffer.h"
#include "GuShaderProgram.h"
#include "GuRenderTarget.h"
#include "GuTexture.h"
#include "../FixedFunctionPass.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <psputils.h>

namespace nCine::RHI::GU
{
	namespace
	{
		// The physical panel. Its scanline pitch is 512 rather than 480 because the GE addresses a surface
		// with a power-of-two-friendly stride, so a row of a 480-wide buffer is followed by 32 unused texels.
		constexpr std::int32_t ScreenWidth = 480;
		constexpr std::int32_t ScreenHeight = 272;
		constexpr std::int32_t ScreenStride = 512;

		// The GE reads its commands out of a list the CPU fills, and the addresses in it are physical, so it
		// has to be 16-byte aligned; sceGuStart() maps it through the uncached mirror, so the commands
		// themselves never need a cache writeback (the vertex arena below is a different matter). Sized for a
		// frame's worth of state changes and draw calls - the vertices do NOT live here.
		alignas(16) std::uint32_t displayList[128 * 1024 / sizeof(std::uint32_t)];

		// Both framebuffers and the depth buffer live in the 2 MB of video memory, allocated by hand: there
		// is no allocator behind sceGeEdramGetAddr(), the GE just reads whatever offset it is given. At
		// 16 bits per texel the three surfaces take 512*272*2 bytes each, which is 816 KB of the 2 MB.
		constexpr std::uint32_t FramebufferBytes = ScreenStride * ScreenHeight * 2;
		constexpr std::uint32_t VramBytes = 2 * 1024 * 1024;
		// Everything past the three fixed surfaces is available to AllocateVram()
		constexpr std::uint32_t VramHeapStart = FramebufferBytes * 3;

		void* VramOffset(std::uint32_t byteOffset)
		{
			// The offsets handed to sceGuDrawBuffer/sceGuDispBuffer are relative to the start of video
			// memory, not absolute addresses (the GE adds the base itself)
			return reinterpret_cast<void*>(std::uintptr_t(byteOffset));
		}

		// First-fit free list of the video memory left after the display and depth buffers. Render targets
		// are the only clients and there are at most a handful of them, so a tiny fixed table with adjacent
		// merging is all this needs - it never has to compact.
		constexpr std::int32_t MaxVramSpans = 8;
		struct VramSpan
		{
			std::uint32_t Offset;
			std::uint32_t Size;
		};
		VramSpan vramFree[MaxVramSpans] = { { VramHeapStart, VramBytes - VramHeapStart } };
		std::int32_t vramFreeCount = 1;
		VramSpan vramUsed[MaxVramSpans] = {};
		std::int32_t vramUsedCount = 0;

		// Vertices are handed to the GE out of this per-frame bump arena rather than out of the display list:
		// the arena is ordinary cached memory, so building a batch is as cheap as any other memory write, and
		// each draw call writes back exactly the range it is about to submit. It is reset at present, after
		// sceGuSync() has established that the GE is done with the previous frame's data.
		constexpr std::size_t FrameArenaBytes = 256 * 1024;
		alignas(16) std::uint8_t frameArena[FrameArenaBytes];
		std::size_t frameArenaUsed = 0;
		bool warnedFrameArenaFull = false;

		// The GE reads vertex components in a fixed order (weights, texture, colour, normal, position), so
		// this member order is a hardware contract rather than a choice
		struct Vertex2D
		{
			float U, V;
			std::uint32_t Color;
			float X, Y, Z;
		};
		// GU_TRANSFORM_2D ("through" mode) hands the coordinates straight to the rasterizer: positions are
		// screen pixels, texture coordinates are TEXEL indices of the bound texture (the texture scale and
		// offset registers do not apply), and neither the viewport nor sceGuOffset() is involved
		constexpr std::int32_t VertexType2D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;

		// An axis-aligned quad can go out as a GU_SPRITES rectangle, which is two vertices instead of the six
		// a triangle pair needs - a third of the vertex traffic for the overwhelming majority of this game's
		// primitives (UI, tiles, unrotated sprites). Kept as a switch because the rectangle primitive has its
		// own fill rule, so it has to be possible to compare the two.
		constexpr bool PreferSpritePrimitive = true;

		// Per-frame draw statistics, logged when the switch below is on - the only practical way to see how
		// much geometry reaches the GE, since sceGu is a userspace library and leaves no syscall trace
		constexpr bool TraceDrawStatistics = false;
		std::uint32_t frameDrawCalls = 0;
		std::uint32_t frameVertices = 0;
		std::uint32_t frameSkippedDraws = 0;

		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software and
		// PowerVR backends' decode (see SwDevice.cpp / PvrDevice.cpp)
		constexpr std::uint32_t kModelMatrixOffset = 0;
		constexpr std::uint32_t kColorOffset = 64;
		constexpr std::uint32_t kTexRectOffset = 80;
		constexpr std::uint32_t kSpriteSizeOffset = 96;
		constexpr std::uint32_t kPaletteOffsetOffset = 104;
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

		const float IdentityMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		// Column-major 4x4 multiply, out = a * b (matches the software device)
		void Mat4Mul(const float* DEATH_RESTRICT a, const float* DEATH_RESTRICT b, float* DEATH_RESTRICT out)
		{
			for (std::int32_t col = 0; col < 4; col++) {
				for (std::int32_t row = 0; row < 4; row++) {
					out[col * 4 + row] =
						a[0 * 4 + row] * b[col * 4 + 0] +
						a[1 * 4 + row] * b[col * 4 + 1] +
						a[2 * 4 + row] * b[col * 4 + 2] +
						a[3 * 4 + row] * b[col * 4 + 3];
				}
			}
		}

		// Both draw paths only ever transform points of the form (x, y, 0, 1), so just six of the sixteen
		// products of projection*view*model are ever read back
		struct Transform2D
		{
			float Xx, Xy;	// Column 0, rows 0 and 1
			float Yx, Yy;	// Column 1, rows 0 and 1
			float Tx, Ty;	// Column 3, rows 0 and 1
		};

		void Mat4MulTransform2D(const float* DEATH_RESTRICT pv, const float* DEATH_RESTRICT model, Transform2D& out)
		{
			out.Xx = pv[0] * model[0] + pv[4] * model[1] + pv[8] * model[2] + pv[12] * model[3];
			out.Xy = pv[1] * model[0] + pv[5] * model[1] + pv[9] * model[2] + pv[13] * model[3];
			out.Yx = pv[0] * model[4] + pv[4] * model[5] + pv[8] * model[6] + pv[12] * model[7];
			out.Yy = pv[1] * model[4] + pv[5] * model[5] + pv[9] * model[6] + pv[13] * model[7];
			out.Tx = pv[0] * model[12] + pv[4] * model[13] + pv[8] * model[14] + pv[12] * model[15];
			out.Ty = pv[1] * model[12] + pv[5] * model[13] + pv[9] * model[14] + pv[13] * model[15];
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		// Straight to the 4 bits a 4444 channel actually keeps, skipping the round trip through 8 bits
		inline std::uint32_t Quantize4Bit(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint32_t(v * 15.0f + 0.5f);
		}

		// The GE takes clear/vertex/environment colors as ABGR, the opposite channel order from the ARGB the
		// PowerVR wants
		inline std::uint32_t PackAbgr(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(r);
		}

		// Maps a pipeline-neutral blend factor onto the GE factor set. Factors 0 and 1 mean "the OTHER
		// side's colour" - the destination colour in the source slot and the other way round - which is
		// exactly how the engine's SrcColor/DstColor pairs are ever used; One and Zero have no factors of
		// their own and go through the constant (GU_FIX) slot instead.
		std::int32_t MapBlendGu(nCine::BlendingFactor factor, std::uint32_t& fix)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				fix = 0x000000; return GU_FIX;
				case nCine::BlendingFactor::One:				fix = 0xFFFFFF; return GU_FIX;
				case nCine::BlendingFactor::SrcColor:
				case nCine::BlendingFactor::DstColor:			return GU_OTHER_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor:
				case nCine::BlendingFactor::OneMinusDstColor:	return GU_ONE_MINUS_OTHER_COLOR;
				case nCine::BlendingFactor::SrcAlpha:			return GU_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return GU_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha:			return GU_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return GU_ONE_MINUS_DST_ALPHA;
				default:										fix = 0xFFFFFF; return GU_FIX;
			}
		}

		std::int32_t MapWrapGu(SamplerWrapping wrap)
		{
			// Only a store whose real size is already a power of two tiles correctly: the GE wraps at the
			// TEXTURE size, which is the padded one. That is exactly the case for the tiling layers of the
			// legacy main menu (16x16 / 32x32 / 128x128), the only content that asks for it.
			return (wrap == SamplerWrapping::Repeat || wrap == SamplerWrapping::MirroredRepeat ? GU_REPEAT : GU_CLAMP);
		}
	}

	/**
		@brief The whole GE state one primitive is drawn under

		Consecutive primitives whose state matches field for field are accumulated into a single
		`sceGuDrawArray`, so a tile layer, a text run or a particle batch costs one draw call. The state is
		derived once per draw from the material (texture page, filter, wrap, blend) and then adjusted per
		pass by the effect (blend override, texture function) - which is what makes the batching automatic
		rather than something the effects have to think about.

		At namespace scope rather than in the anonymous namespace below because EffectContext names it in a
		member type and itself has to be externally visible (see the note there).
	*/
	struct DrawState
	{
		const void* TextureData = nullptr;		// nullptr = untextured
		const std::uint32_t* Clut = nullptr;	// The per-frame CLUT copy an indexed page resolves through
		std::int32_t TexturePsm = 0;
		std::int32_t TextureWidth = 0, TextureHeight = 0, TextureStride = 0;
		std::int32_t Filter = GU_NEAREST;
		std::int32_t WrapU = GU_CLAMP, WrapV = GU_CLAMP;
		std::int32_t Tfx = GU_TFX_MODULATE;
		std::int32_t Tcc = GU_TCC_RGBA;
		std::uint32_t EnvColor = 0;
		std::int32_t BlendOp = GU_ADD;
		std::int32_t BlendSrc = GU_SRC_ALPHA, BlendDst = GU_ONE_MINUS_SRC_ALPHA;
		std::uint32_t BlendSrcFix = 0, BlendDstFix = 0;
		std::int32_t Prim = GU_SPRITES;
		bool TextureSwizzled = false;
		bool BlendEnabled = true;
	};

	namespace
	{
		bool SameDrawState(const DrawState& a, const DrawState& b)
		{
			return a.TextureData == b.TextureData && a.Clut == b.Clut && a.TexturePsm == b.TexturePsm &&
				a.TextureWidth == b.TextureWidth && a.TextureHeight == b.TextureHeight &&
				a.TextureStride == b.TextureStride && a.Filter == b.Filter &&
				a.WrapU == b.WrapU && a.WrapV == b.WrapV && a.Tfx == b.Tfx && a.Tcc == b.Tcc &&
				a.EnvColor == b.EnvColor && a.BlendOp == b.BlendOp && a.BlendSrc == b.BlendSrc &&
				a.BlendDst == b.BlendDst && a.BlendSrcFix == b.BlendSrcFix && a.BlendDstFix == b.BlendDstFix &&
				a.Prim == b.Prim && a.TextureSwizzled == b.TextureSwizzled && a.BlendEnabled == b.BlendEnabled;
		}

		DrawState batchState;
		std::size_t batchFirstByte = 0;
		std::int32_t batchVertexCount = 0;

		DrawState appliedState;
		bool appliedStateValid = false;

		void ApplyDrawState(const DrawState& state)
		{
			const bool textureChanged = (!appliedStateValid ||
				appliedState.TextureData != state.TextureData || appliedState.Clut != state.Clut ||
				appliedState.TexturePsm != state.TexturePsm || appliedState.TextureWidth != state.TextureWidth ||
				appliedState.TextureHeight != state.TextureHeight || appliedState.TextureStride != state.TextureStride ||
				appliedState.TextureSwizzled != state.TextureSwizzled);
			if (textureChanged) {
				if (state.TextureData == nullptr) {
					sceGuDisable(GU_TEXTURE_2D);
				} else {
					sceGuEnable(GU_TEXTURE_2D);
					if (state.Clut != nullptr) {
						// 8-bit indices address the CLUT directly, so no shift and the full 8-bit mask; 32
						// blocks of 8 entries are the 256 the format has. The entries are ordinary RGBA8
						// values, which is byte for byte what GU_PSM_8888 expects.
						sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
						sceGuClutLoad(32, state.Clut);
					}
					sceGuTexMode(state.TexturePsm, 0, 0, state.TextureSwizzled ? GU_TRUE : GU_FALSE);
					sceGuTexImage(0, state.TextureWidth, state.TextureHeight, state.TextureStride, state.TextureData);
					// The GE caches texels, and a rebuilt store (or a bake switched to another palette row)
					// reuses the same addresses, so the cache has to be dropped whenever the binding changes
					sceGuTexFlush();
				}
			}
			if (state.TextureData != nullptr && (textureChanged ||
					appliedState.Tfx != state.Tfx || appliedState.Tcc != state.Tcc ||
					appliedState.EnvColor != state.EnvColor)) {
				sceGuTexFunc(state.Tfx, state.Tcc);
				sceGuTexEnvColor(state.EnvColor);
			}
			if (state.TextureData != nullptr && (textureChanged || appliedState.Filter != state.Filter)) {
				sceGuTexFilter(state.Filter, state.Filter);
			}
			if (state.TextureData != nullptr && (textureChanged ||
					appliedState.WrapU != state.WrapU || appliedState.WrapV != state.WrapV)) {
				sceGuTexWrap(state.WrapU, state.WrapV);
			}
			if (!appliedStateValid || appliedState.BlendEnabled != state.BlendEnabled ||
					(state.BlendEnabled && (appliedState.BlendOp != state.BlendOp ||
						appliedState.BlendSrc != state.BlendSrc || appliedState.BlendDst != state.BlendDst ||
						appliedState.BlendSrcFix != state.BlendSrcFix || appliedState.BlendDstFix != state.BlendDstFix))) {
				if (state.BlendEnabled) {
					sceGuEnable(GU_BLEND);
					sceGuBlendFunc(state.BlendOp, state.BlendSrc, state.BlendDst, state.BlendSrcFix, state.BlendDstFix);
				} else {
					sceGuDisable(GU_BLEND);
				}
			}
			appliedState = state;
			appliedStateValid = true;
		}

		void FlushBatch()
		{
			if (batchVertexCount <= 0) {
				return;
			}
			ApplyDrawState(batchState);
			std::uint8_t* const base = frameArena + batchFirstByte;
			const std::size_t bytes = std::size_t(batchVertexCount) * sizeof(Vertex2D);
			// The GE reads main memory without seeing the data cache; this is the writeback that makes the
			// vertices visible to it (the PSP equivalent of the GX's DCFlushRange)
			sceKernelDcacheWritebackRange(base, bytes);
			sceGuDrawArray(batchState.Prim, VertexType2D, batchVertexCount, nullptr, base);
			frameDrawCalls++;
			frameVertices += std::uint32_t(batchVertexCount);
			batchVertexCount = 0;
		}

		/** @brief Reserves per-frame data (a CLUT copy, a generated texture) the GE will read */
		void* AllocFrameData(std::size_t size, std::size_t alignment)
		{
			// The open batch's vertices have to stay contiguous in the arena, so anything else that takes
			// space from it closes the batch first
			FlushBatch();
			const std::size_t aligned = (frameArenaUsed + alignment - 1) & ~(alignment - 1);
			if (aligned + size > FrameArenaBytes) {
				return nullptr;
			}
			frameArenaUsed = aligned + size;
			return frameArena + aligned;
		}

		/** @brief Reserves @p count vertices of the open (or a new) batch drawn under @p state */
		Vertex2D* AllocVertices(const DrawState& state, std::int32_t count)
		{
			if (batchVertexCount > 0 && !SameDrawState(batchState, state)) {
				FlushBatch();
			}
			const std::size_t bytes = std::size_t(count) * sizeof(Vertex2D);
			if (batchVertexCount == 0) {
				frameArenaUsed = (frameArenaUsed + 15) & ~std::size_t(15);
				batchFirstByte = frameArenaUsed;
				batchState = state;
			}
			if (frameArenaUsed + bytes > FrameArenaBytes) {
				FlushBatch();
				if (!warnedFrameArenaFull) {
					warnedFrameArenaFull = true;
					LOGW("The {} KB per-frame vertex arena is full; the rest of the frame is dropped", FrameArenaBytes / 1024);
				}
				return nullptr;
			}
			Vertex2D* const result = reinterpret_cast<Vertex2D*>(frameArena + frameArenaUsed);
			frameArenaUsed += bytes;
			batchVertexCount += count;
			return result;
		}

		/**
			@brief Submits one quad, as a GE rectangle when it is axis-aligned and as a triangle pair otherwise

			Corners 0/1 share the sprite's local x = 1 edge and 2/3 its x = 0 edge, 0/2 the y = 0 edge and 1/3
			the y = 1 edge (see the corner synthesis in Dispatch), so an axis-aligned quad is exactly the case
			where those pairs agree on the other axis.
		*/
		void SubmitQuadPrimitive(DrawState state, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t abgr, float dx = 0.0f, float dy = 0.0f)
		{
			const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
			if (axisAligned && PreferSpritePrimitive) {
				state.Prim = GU_SPRITES;
				Vertex2D* const v = AllocVertices(state, 2);
				if (v == nullptr) {
					return;
				}
				// The rectangle primitive takes two opposite corners in increasing order, so a mirrored
				// sprite (a negative scale in the model matrix) trades them over - and its texture
				// coordinates travel with them, which is what keeps the flip
				float x0 = px[2] + dx, u0 = pu[2], x1 = px[0] + dx, u1 = pu[0];
				if (x0 > x1) {
					std::swap(x0, x1);
					std::swap(u0, u1);
				}
				float y0 = py[0] + dy, v0 = pv[0], y1 = py[1] + dy, v1 = pv[1];
				if (y0 > y1) {
					std::swap(y0, y1);
					std::swap(v0, v1);
				}
				v[0] = { u0, v0, abgr, x0, y0, 0.0f };
				v[1] = { u1, v1, abgr, x1, y1, 0.0f };
			} else {
				state.Prim = GU_TRIANGLES;
				Vertex2D* const v = AllocVertices(state, 6);
				if (v == nullptr) {
					return;
				}
				// The two triangles of the v0..v3 strip; culling is off, so the winding is free
				static const std::int32_t Order[6] = { 0, 1, 2, 2, 1, 3 };
				for (std::int32_t i = 0; i < 6; i++) {
					const std::int32_t o = Order[i];
					v[i] = { pu[o], pv[o], abgr, px[o] + dx, py[o] + dy, 0.0f };
				}
			}
		}

		/** @brief Submits a triangle strip of its own draw call (arbitrary synthesized geometry) */
		void SubmitStripPrimitive(DrawState state, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, const std::uint32_t* abgr, std::uint32_t flatAbgr, float dx, float dy)
		{
			// A strip cannot share a draw call with anything else - the GE would connect it to whatever
			// vertices follow - so it is bracketed by flushes
			state.Prim = GU_TRIANGLE_STRIP;
			FlushBatch();
			Vertex2D* const v = AllocVertices(state, count);
			if (v == nullptr) {
				return;
			}
			for (std::int32_t i = 0; i < count; i++) {
				v[i].U = (pu != nullptr ? pu[i] : 0.0f);
				v[i].V = (pv != nullptr ? pv[i] : 0.0f);
				v[i].Color = (abgr != nullptr ? abgr[i] : flatAbgr);
				v[i].X = px[i] + dx;
				v[i].Y = py[i] + dy;
				v[i].Z = 0.0f;
			}
			FlushBatch();
		}

		/** @brief Folds a pass descriptor's blend mode and texture function into a copy of the material state */
		void ApplyPassToState(DrawState& state, const FixedFunctionPass& pass)
		{
			switch (pass.Blend) {
				case FixedFunctionPass::BlendMode::Additive:
					// Deliberately SRCALPHA rather than a literal ONE source factor, matching the PVR: it is
					// the additive mechanism the split-multiplier passes rely on, whose contributions are
					// scaled by the pass alpha
					state.BlendEnabled = true;
					state.BlendOp = GU_ADD;
					state.BlendSrc = GU_SRC_ALPHA;
					state.BlendDst = GU_FIX;
					state.BlendDstFix = 0xFFFFFF;
					break;
				case FixedFunctionPass::BlendMode::Opaque:
					state.BlendEnabled = false;
					break;
				case FixedFunctionPass::BlendMode::Alpha:
					state.BlendEnabled = true;
					state.BlendOp = GU_ADD;
					state.BlendSrc = GU_SRC_ALPHA;
					state.BlendDst = GU_ONE_MINUS_SRC_ALPHA;
					break;
				default:
					break;		// The material's own blending
			}

			switch (pass.Tev) {
				case FixedFunctionPass::TevPreset::Silhouette:
					// The GE has no "flat colour where the texture has alpha" environment of its own, but
					// GU_TFX_BLEND computes Cv = Cf*(1-Ct) + Cc*Ct, which collapses to exactly Cf - per
					// channel, whatever the texel is - as soon as the environment colour Cc equals the
					// fragment colour Cf. With GU_TCC_RGBA the alpha stays At*Af, so this IS the hardware
					// silhouette the mask/outline/shield family needs, in one pass.
					state.Tfx = GU_TFX_BLEND;
					state.Tcc = GU_TCC_RGBA;
					state.EnvColor = PackAbgr(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
						QuantizeChannel(pass.Color[2]), 0) & 0x00FFFFFFu;
					break;
				default:
					// Modulate. The x2/x4 output scales have no GE equivalent (the texture environment has no
					// scale stage at all) and the transpiler rejects them for the gu target, exactly as it
					// rejects the GX-only TintMix/LumaRamp outside a gx block - so nothing but a Modulate
					// pass can reach this arm from a generated effect.
					state.Tfx = GU_TFX_MODULATE;
					state.Tcc = GU_TCC_RGBA;
					break;
			}
		}

		// The CLUT the GE resolves indexed texels through can only hold one 256-entry table at a time, so a
		// palette row change is a state change like any other. Loads reference their data until the GE
		// executes them, and the GE runs behind the CPU, so each distinct row of a frame gets its own copy
		// out of the frame arena - one 1 KB copy per palette row per frame, keyed exactly like the PowerVR's
		// palette banks so repeated draws of the same row reload nothing.
		constexpr std::int32_t MaxClutCacheEntries = 8;
		struct ClutCacheEntry
		{
			const void* Palette;
			std::int32_t Offset;
			std::uint32_t Version;
			const std::uint32_t* Data;
		};
		ClutCacheEntry clutCache[MaxClutCacheEntries] = {};
		std::int32_t clutCacheCount = 0;
	}

	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still defined only in
	// this translation unit), for the same reason as on the PVR: the effect-table struct below is at
	// namespace scope - so the backend's ShaderProgram can forward-declare it and hold a typed entry
	// pointer - and names EffectContext in a member type.
	// ---------------------------------------------------------- fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this EffectContext -
	// the structural contract documented in FixedFunctionPass.h, implemented here against the GE's
	// batching submission helpers above. The per-effect functions themselves are GENERATED from the
	// shaders' void fixed_function([gu]) blocks by the ShaderCompiler, exactly as on the PVR and the GX
	// (Shaders/Generated/GuGeneratedEffects.h, included below), so this file contains no effect-specific
	// code at all.

	struct EffectContext
	{
		// Matches the GX's capacity rather than the PVR's, because nothing here needs the geometry split
		// into small pieces: the GE takes a strip of any length in one draw call
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// The GE state the material resolved to, and the current instance's corner arrays (already in
		// screen pixels and texel-space texture coordinates)
		const DrawState* Material;
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms) and the conversion from the shader's normalized texture space into the texel
		// coordinates through mode wants, page origin included
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const GuShaderProgram* Program;
		float UvScaleU, UvScaleV;
		float UvBiasU, UvBiasV;

		// The strip builder scratch; colours are packed at set time (same quantization as the quad path, so
		// identical float inputs produce identical vertex words)
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint32_t StripAbgr[MaxStripVertices];

		const float* Color() const { return InstanceColor; }
		float TexelWidth() const { return TexelW; }
		float TexelHeight() const { return TexelH; }
		bool IsBatched() const { return Batched; }

		float QuadOriginX() const { return OriginX; }
		float QuadOriginY() const { return OriginY; }
		float QuadAxisXx() const { return AxisXx; }
		float QuadAxisXy() const { return AxisXy; }
		float QuadAxisYx() const { return AxisYx; }
		float QuadAxisYy() const { return AxisYy; }

		bool HasUniform(const char* name) const
		{
			return (Program->ResolveUniform(name) != nullptr);
		}
		void LoadUniform(const char* name, float* out, std::int32_t floatCount) const
		{
			// An unresolved name leaves the caller's zeros in place - blocks guard with has_uniform()
			const std::uint8_t* bytes = Program->ResolveUniform(name);
			if (bytes != nullptr) {
				std::memcpy(out, bytes, std::size_t(floatCount) * sizeof(float));
			}
		}

		void SetStripVertexPosition(std::int32_t i, float x, float y)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripX[i] = x;
				StripY[i] = y;
			}
		}
		void SetStripVertexUv(std::int32_t i, float u, float v)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripU[i] = u * UvScaleU - UvBiasU;
				StripV[i] = v * UvScaleV - UvBiasV;
			}
		}
		void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripAbgr[i] = PackAbgr(QuantizeChannel(r), QuantizeChannel(g), QuantizeChannel(b), QuantizeChannel(a));
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step the
		// Outline ring taps use. The corners are already in screen space, so the result is a screen-space
		// displacement.
		float TexelToScreenX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToScreenY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader family
		// carries the sprite's UV-space texel size in its instance color.xy (exactly like the GLSL derives
		// its tap offsets), folded through the screen-space conversion above
		float TexelStepX() const { return TexelToScreenX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToScreenY(InstanceColor[1]); }

		// One quad draw for a pass the GE can express directly (no offset colour left on it)
		void SubmitQuadPass(const FixedFunctionPass& pass)
		{
			DrawState state = *Material;
			ApplyPassToState(state, pass);
			const std::uint32_t abgr = PackAbgr(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), QuantizeChannel(pass.Color[3]));
			SubmitQuadPrimitive(state, Px, Py, Pu, Pv, abgr, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		/*
			Submits one pass over the current instance's quad. A pass carrying an offset colour is
			EXPANDED here, because the GE has no post-texture additive term: GU_TFX_ADD adds the texel to
			the fragment colour rather than a third value, so `texel*colour + offset` is not any single
			GE draw. Doing the expansion in the mechanism (rather than spelling both passes in every
			shader's gu block) is what keeps the portable core portable - the same generic block still
			describes the effect on all three consoles, exactly as the GX reinterprets an offset colour
			as its silhouette form.

			The expansion is EXACT, not an approximation. With a = texel.a * colour.a, the PVR's single
			draw over the destination dst is (its blend being SRCALPHA + INVSRCALPHA):

				dst*(1 - a) + a*(texel*colour.rgb + offset)

			and the two draws below produce, in order,

				pass 1 (modulate, the pass's own blend):   dst1 = dst*(1 - a) + a*texel*colour.rgb
				pass 2 (silhouette, additive):             dst2 = dst1 + a*offset

			whose sum is the same expression term for term. Pass 2 is a GE silhouette (flat colour where
			the texture has alpha) whose vertex AND environment colour is the offset colour, so its own
			contribution is a*offset with no texel dependence, and its ADDITIVE blend is what keeps it
			from attenuating the destination a second time.

			When colour.rgb is zero - the "pure offset colour" idiom of the mask/outline/shield family,
			where the offset colour IS the effect - the modulate term vanishes and the pair collapses to
			ONE draw: the silhouette under the pass's own (alpha-over) blend already computes
			dst*(1 - a) + a*offset. That is the common case, so those effects cost the PVR's draw count.
		*/
		void SubmitQuad(const FixedFunctionPass& pass)
		{
			if (pass.HasOffsetColor) {
				const bool modulateVisible = (pass.Color[0] != 0.0f || pass.Color[1] != 0.0f || pass.Color[2] != 0.0f);
				if (modulateVisible) {
					FixedFunctionPass modulate = pass;
					modulate.HasOffsetColor = false;
					SubmitQuadPass(modulate);
				}
				FixedFunctionPass silhouette = pass;
				silhouette.HasOffsetColor = false;
				silhouette.Tev = FixedFunctionPass::TevPreset::Silhouette;
				silhouette.Color[0] = pass.OffsetColor[0];
				silhouette.Color[1] = pass.OffsetColor[1];
				silhouette.Color[2] = pass.OffsetColor[2];
				// Only the second half of a split pair is additive; a collapsed one keeps the pass's blend
				// (the alpha-over the effects rely on) so it still attenuates the destination itself
				if (modulateVisible) {
					silhouette.Blend = FixedFunctionPass::BlendMode::Additive;
				}
				SubmitQuadPass(silhouette);
				return;
			}
			SubmitQuadPass(pass);
		}

		// Textured strip out of the builder scratch: the pass's flat colour over the material state
		void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			// Same intent mapping as the GX's SubmitStrip: with no post-texture add on this hardware, an
			// offset colour becomes the silhouette form filled flat with it at the pass alpha. Deliberately
			// NOT split into the exact two draws SubmitQuad builds - a strip's geometry would have to be
			// resubmitted, and no effect asks for it (both strip users, the iris and the warp bands, carry
			// no offset colour at all).
			FixedFunctionPass effective = pass;
			if (pass.HasOffsetColor) {
				effective.HasOffsetColor = false;
				effective.Tev = FixedFunctionPass::TevPreset::Silhouette;
				effective.Color[0] = pass.OffsetColor[0];
				effective.Color[1] = pass.OffsetColor[1];
				effective.Color[2] = pass.OffsetColor[2];
			}
			DrawState state = *Material;
			ApplyPassToState(state, effective);
			const std::uint32_t abgr = PackAbgr(QuantizeChannel(effective.Color[0]), QuantizeChannel(effective.Color[1]),
				QuantizeChannel(effective.Color[2]), QuantizeChannel(effective.Color[3]));
			SubmitStripPrimitive(state, StripX, StripY, StripU, StripV, count, nullptr, abgr,
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch: always UNTEXTURED - a gradient has no
		// texture to modulate - whose blend comes from the pass
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			DrawState state = *Material;
			ApplyPassToState(state, pass);
			state.TextureData = nullptr;
			state.Clut = nullptr;
			SubmitStripPrimitive(state, StripX, StripY, nullptr, nullptr, count, StripAbgr, 0,
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::GU itself. Programs with no fixed_function block are absent
// from its table and their draws are skipped with a one-time warning, exactly as on the other
// consoles - which is also what keeps the CPU-lightmap tier (Lighting, whose Combine hook IS in the
// table) from painting its light quads over the scene.
#include "../../../../Shaders/Generated/GuGeneratedEffects.h"

namespace nCine::RHI::GU
{
	const FixedFunctionGeneratedEffect* GuDevice::FindGeneratedEffect(const char* program, const char* variant)
	{
		// A linear scan is fine - the lookup runs once per program load, not per draw
		for (std::size_t i = 0; i < FixedFunctionGeneratedEffectCount; i++) {
			const FixedFunctionGeneratedEffect& e = FixedFunctionGeneratedEffects[i];
			if (std::strcmp(e.Program, program) == 0 && std::strcmp(e.Variant, variant) == 0) {
				return &e;
			}
		}
		return nullptr;
	}

	GuDevice::BlendingState GuDevice::_blending;
	GuDevice::DepthTestState GuDevice::_depthTest;
	GuDevice::CullFaceState GuDevice::_cullFace;
	GuDevice::ScissorState GuDevice::_scissor;
	Recti GuDevice::_viewport(0, 0, 0, 0);
	Colorf GuDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	GuShaderProgram* GuDevice::_currentProgram = nullptr;
	const GuTexture* GuDevice::_boundTextures[GuDevice::MaxTextureUnits] = {};
	GuDevice::UniformRange GuDevice::_boundUniformRanges[GuDevice::MaxUniformBindings] = {};
	GuRenderTarget* GuDevice::_currentRenderTarget = nullptr;

	bool GuDevice::_guInitialized = false;
	bool GuDevice::_listOpen = false;
	std::int32_t GuDevice::_logicalWidth = ScreenWidth;
	std::int32_t GuDevice::_logicalHeight = ScreenHeight;
	std::uint32_t GuDevice::_sceneCounter = 0;

	GuTexture* GuDevice::_paletteTexture = nullptr;
	std::uint32_t GuDevice::_paletteGeneration = 1;

	std::vector<GuDevice::PendingSoftwareLight> GuDevice::_pendingSoftwareLights;

	namespace
	{
		// Which surface the GE is currently rendering into, so a target switch only re-emits the commands
		// when it really changes. The screen half alternates with the display flip.
		std::uint32_t screenDrawOffset = 0;
		const GuRenderTarget* appliedTarget = nullptr;
		bool appliedTargetValid = false;
		// The scissor rect last programmed, in raster pixels of the current target
		std::int32_t appliedScissor[4] = { -1, -1, -1, -1 };
	}

	// ------------------------------------------------------------------ session

	void GuDevice::InitializeGu()
	{
		if (_guInitialized) {
			return;
		}

		sceGuInit();

		sceGuStart(GU_DIRECT, displayList);
		// Draw into the first framebuffer while the second is being scanned out; the depth buffer follows
		// them. RGB565 is chosen over RGBA8888 because the game's 2D output needs no destination alpha and
		// the panel cannot show more, so the wider format would only double the fill bandwidth.
		sceGuDrawBuffer(GU_PSM_5650, VramOffset(0), ScreenStride);
		sceGuDispBuffer(ScreenWidth, ScreenHeight, VramOffset(FramebufferBytes), ScreenStride);
		sceGuDepthBuffer(VramOffset(FramebufferBytes * 2), ScreenStride);

		// The GE rasterizes in a 4096x4096 space whose centre is the screen centre, which is what the
		// 2048-based offset/viewport pair expresses. Depth runs backwards on this hardware (near = 65535).
		// Neither applies to the GU_TRANSFORM_2D primitives this backend submits - through mode goes
		// straight to the rasterizer - but the clear path and any future transformed draw rely on them.
		sceGuOffset(2048 - (ScreenWidth / 2), 2048 - (ScreenHeight / 2));
		sceGuViewport(2048, 2048, ScreenWidth, ScreenHeight);
		sceGuDepthRange(65535, 0);

		sceGuScissor(0, 0, ScreenWidth, ScreenHeight);
		sceGuEnable(GU_SCISSOR_TEST);
		// The game draws 2D in painter's order, so there is nothing for the depth test to arbitrate
		sceGuDepthMask(GU_TRUE);
		sceGuDisable(GU_DEPTH_TEST);
		sceGuDisable(GU_CULL_FACE);
		sceGuShadeModel(GU_SMOOTH);
		sceGuEnable(GU_BLEND);
		sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
		sceGuFinish();
		sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

		sceDisplayWaitVblankStart();
		sceGuDisplay(GU_TRUE);

		// The display list and the vertex arena are ordinary .bss, so the loader's zero fill may still sit
		// in the data cache; the GE would read whatever is behind it. One writeback settles that for good.
		sceKernelDcacheWritebackInvalidateAll();

		_guInitialized = true;
		LOGI("GU session initialized: {}x{} RGB565 (stride {}), {} KB display list, {} KB vertex arena",
			ScreenWidth, ScreenHeight, ScreenStride, sizeof(displayList) / 1024, FrameArenaBytes / 1024);
	}

	void GuDevice::ShutdownGu()
	{
		if (!_guInitialized) {
			return;
		}
		if (_listOpen) {
			FlushBatch();
			sceGuFinish();
			sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
			_listOpen = false;
		}
		sceGuDisplay(GU_FALSE);
		sceGuTerm();
		_guInitialized = false;
	}

	void GuDevice::EnsureList()
	{
		if (!_guInitialized || _listOpen) {
			return;
		}
		sceGuStart(GU_DIRECT, displayList);
		_listOpen = true;
		// A fresh list re-sends the draw buffer of the context, and nothing else about the previous frame's
		// state can be relied upon, so everything is reissued once
		appliedStateValid = false;
		appliedTargetValid = false;
		appliedScissor[0] = -1;
	}

	void GuDevice::ApplyDrawTarget()
	{
		if (appliedTargetValid && appliedTarget == _currentRenderTarget) {
			return;
		}
		FlushBatch();

		if (_currentRenderTarget != nullptr) {
			GuTexture* texture = _currentRenderTarget->GetColorTexture(0);
			void* surface = (texture != nullptr ? texture->GetRenderTargetSurface() : nullptr);
			if (surface == nullptr) {
				// No surface to render into; the draws will be skipped by their own checks
				return;
			}
			// Only the draw buffer and the scissor matter: through-mode primitives bypass the viewport and
			// the coordinate offset entirely, so neither has to be reprogrammed for a target
			sceGuDrawBufferList(GU_PSM_5650, surface, texture->GetRenderTargetStride());
		} else {
			sceGuDrawBufferList(GU_PSM_5650, VramOffset(screenDrawOffset), ScreenStride);
		}
		appliedTarget = _currentRenderTarget;
		appliedTargetValid = true;
		// The rect is expressed in the target's raster space, so it has to be reprogrammed as well
		appliedScissor[0] = -1;
	}

	void GuDevice::ApplyScissor()
	{
		std::int32_t x = 0, y = 0, w = ScreenWidth, h = ScreenHeight;
		std::int32_t targetW = ScreenWidth, targetH = ScreenHeight;
		if (_currentRenderTarget != nullptr) {
			const GuTexture* texture = _currentRenderTarget->GetColorTexture(0);
			targetW = (texture != nullptr ? texture->GetWidth() : ScreenWidth);
			targetH = (texture != nullptr ? texture->GetHeight() : ScreenHeight);
			w = targetW;
			h = targetH;
		}
		if (_scissor.Enabled) {
			float scaleX, scaleY;
			GetTargetScale(scaleX, scaleY);
			// The engine hands scissor rectangles in top-down logical coordinates. A screen pass maps them
			// straight onto raster rows (it mirrors NDC, see Dispatch); a render-to-texture pass keeps the
			// unmirrored top-down store, so its rect is flipped - the same split the GX device makes. A
			// target renders 1:1, so its logical height IS its raster height.
			const std::int32_t rasterY = (_currentRenderTarget == nullptr
				? _scissor.Rect.Y : targetH - _scissor.Rect.Y - _scissor.Rect.H);
			x = std::int32_t(float(_scissor.Rect.X) * scaleX);
			y = std::int32_t(float(rasterY) * scaleY);
			w = std::int32_t(float(_scissor.Rect.W) * scaleX);
			h = std::int32_t(float(_scissor.Rect.H) * scaleY);
			// The GE takes the rect unclamped and a negative origin would wrap, so it is clipped to the
			// surface here
			if (x < 0) { w += x; x = 0; }
			if (y < 0) { h += y; y = 0; }
			if (x + w > targetW) { w = targetW - x; }
			if (y + h > targetH) { h = targetH - y; }
			if (w < 0) { w = 0; }
			if (h < 0) { h = 0; }
		}
		if (appliedScissor[0] == x && appliedScissor[1] == y && appliedScissor[2] == w && appliedScissor[3] == h) {
			return;
		}
		FlushBatch();
		sceGuScissor(x, y, w, h);
		appliedScissor[0] = x;
		appliedScissor[1] = y;
		appliedScissor[2] = w;
		appliedScissor[3] = h;
	}

	void GuDevice::GetTargetScale(float& scaleX, float& scaleY)
	{
		if (_currentRenderTarget != nullptr) {
			// Render-to-texture passes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (_logicalWidth > 0 ? float(ScreenWidth) / float(_logicalWidth) : 1.0f);
			scaleY = (_logicalHeight > 0 ? float(ScreenHeight) / float(_logicalHeight) : 1.0f);
		}
	}

	void GuDevice::PresentFrame()
	{
		if (!_guInitialized) {
			return;
		}
		if (!_listOpen) {
			// Nothing was drawn this frame; still run a list that clears, so the display keeps its pacing
			// and a frame that produced no geometry does not show the previous one's contents
			EnsureList();
			ApplyDrawTarget();
			const std::uint32_t abgr = PackAbgr(QuantizeChannel(_clearColor.R), QuantizeChannel(_clearColor.G),
				QuantizeChannel(_clearColor.B), QuantizeChannel(_clearColor.A));
			sceGuClearColor(abgr);
			sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
		} else {
			FlushBatch();
		}

		sceGuFinish();
		// Waits for the GE to finish the list, which is also what makes the frame arena reusable below
		sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
		_listOpen = false;

		if (TraceDrawStatistics) {
			// Once a second at 60 Hz, so the trace stays readable while the game runs
			if ((_sceneCounter % 60) == 0) {
				LOGI("Frame {}: {} GE draw calls, {} vertices, {} KB of the vertex arena, {} skipped draws",
					_sceneCounter, frameDrawCalls, frameVertices, frameArenaUsed / 1024, frameSkippedDraws);
			}
		}

		sceDisplayWaitVblankStart();
		sceGuSwapBuffers();
		// sceGuSwapBuffers() trades the draw and display halves, which this mirrors so ApplyDrawTarget()
		// can point the GE back at the right one after a render-target pass
		screenDrawOffset = (screenDrawOffset == 0 ? FramebufferBytes : 0);
		appliedTargetValid = false;
		_sceneCounter++;

		// The GE is idle, so the frame's vertices, CLUT copies and generated textures can be overwritten
		frameArenaUsed = 0;
		clutCacheCount = 0;
		frameDrawCalls = 0;
		frameVertices = 0;
		frameSkippedDraws = 0;
	}

	void GuDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	// ------------------------------------------------------------------ video memory

	void* GuDevice::AllocateVram(std::size_t size)
	{
		if (size == 0 || vramUsedCount >= MaxVramSpans) {
			return nullptr;
		}
		const std::uint32_t wanted = std::uint32_t((size + 15) & ~std::size_t(15));
		for (std::int32_t i = 0; i < vramFreeCount; i++) {
			if (vramFree[i].Size < wanted) {
				continue;
			}
			const std::uint32_t offset = vramFree[i].Offset;
			vramFree[i].Offset += wanted;
			vramFree[i].Size -= wanted;
			if (vramFree[i].Size == 0) {
				vramFree[i] = vramFree[--vramFreeCount];
			}
			vramUsed[vramUsedCount++] = { offset, wanted };
			return VramOffset(offset);
		}
		return nullptr;
	}

	void GuDevice::FreeVram(void* ptr)
	{
		const std::uint32_t offset = std::uint32_t(reinterpret_cast<std::uintptr_t>(ptr));
		for (std::int32_t i = 0; i < vramUsedCount; i++) {
			if (vramUsed[i].Offset != offset) {
				continue;
			}
			std::uint32_t size = vramUsed[i].Size;
			vramUsed[i] = vramUsed[--vramUsedCount];
			// Merge into any span the block is adjacent to, so repeated create/destroy cycles of a target
			// cannot fragment the heap
			for (std::int32_t j = 0; j < vramFreeCount; j++) {
				if (vramFree[j].Offset + vramFree[j].Size == offset) {
					vramFree[j].Size += size;
					return;
				}
				if (offset + size == vramFree[j].Offset) {
					vramFree[j].Offset = offset;
					vramFree[j].Size += size;
					return;
				}
			}
			if (vramFreeCount < MaxVramSpans) {
				vramFree[vramFreeCount++] = { offset, size };
			}
			return;
		}
	}

	// ------------------------------------------------------------------ state

	void GuDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void GuDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	GuDevice::BlendingState GuDevice::GetBlendingState() { return _blending; }
	void GuDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void GuDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void GuDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	GuDevice::DepthTestState GuDevice::GetDepthTestState() { return _depthTest; }
	void GuDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void GuDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	GuDevice::CullFaceState GuDevice::GetCullFaceState() { return _cullFace; }
	void GuDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	GuDevice::ScissorState GuDevice::GetScissorState() { return _scissor; }
	void GuDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void GuDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like RenderCommand
		// and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void GuDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti GuDevice::GetViewport() { return _viewport; }
	void GuDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void GuDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf GuDevice::GetClearColor() { return _clearColor; }
	void GuDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void GuDevice::Clear(ClearFlags flags)
	{
		if (!_guInitialized) {
			return;
		}
		EnsureList();
		ApplyDrawTarget();
		// The clear is bounded by the scissor, exactly like glClear is by the scissor test
		ApplyScissor();
		// Whatever is still batched was submitted BEFORE this clear and has to reach the list first,
		// otherwise the clear would wipe it (a mid-frame clear is exactly what the render targets do)
		FlushBatch();

		std::int32_t guFlags = GU_FAST_CLEAR_BIT;
		if ((flags & ClearFlags::Color) == ClearFlags::Color) {
			guFlags |= GU_COLOR_BUFFER_BIT;
		}
		// The depth buffer lives at a fixed video-memory offset sized for the display, so clearing it while
		// a render target of another size is bound would write outside it; depth is disabled anyway
		if ((flags & ClearFlags::Depth) == ClearFlags::Depth && _currentRenderTarget == nullptr) {
			guFlags |= GU_DEPTH_BUFFER_BIT;
		}
		if ((flags & ClearFlags::Stencil) == ClearFlags::Stencil && _currentRenderTarget == nullptr) {
			guFlags |= GU_STENCIL_BUFFER_BIT;
		}

		const std::uint32_t abgr = PackAbgr(QuantizeChannel(_clearColor.R), QuantizeChannel(_clearColor.G),
			QuantizeChannel(_clearColor.B), QuantizeChannel(_clearColor.A));
		sceGuClearColor(abgr);
		sceGuClear(guFlags);
		// sceGuClear() draws its own sprite with its own vertex format and leaves the clear-mode register
		// touched, so nothing about the cached pipeline state survives it
		appliedStateValid = false;
	}

	// ------------------------------------------------------------------ draw entry points

	void GuDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GuDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GuDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void GuDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle GuDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void GuDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool GuDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void GuDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void GuDevice::BindProgram(GuShaderProgram* program) { _currentProgram = program; }
	GuShaderProgram* GuDevice::CurrentProgram() { return _currentProgram; }

	void GuDevice::BindTexture(std::uint32_t unit, const GuTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void GuDevice::UnbindTexture(const GuTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// A destroyed texture's store must not stay referenced by an open batch. Anything already submitted
		// is safe: resources are only ever destroyed between frames (level loads, menu transitions), and
		// PresentFrame() has synced the previous frame's list by then.
		FlushBatch();
		if (appliedStateValid && appliedState.TextureData != nullptr) {
			appliedStateValid = false;
		}
		// Drop CLUT copies built from the destroyed palette so a stale pointer can never match
		for (std::int32_t i = 0; i < clutCacheCount; i++) {
			if (clutCache[i].Palette == texture) {
				clutCache[i].Palette = nullptr;
			}
		}
	}

	const GuTexture* GuDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void GuDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void GuDevice::SetRenderTarget(GuRenderTarget* renderTarget)
	{
		// The draw path reacts lazily at the next draw or clear (ApplyDrawTarget), which is also where the
		// open batch is closed - it belongs to the previous surface
		_currentRenderTarget = renderTarget;
	}

	void GuDevice::UnbindRenderTarget(const GuRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (appliedTarget == renderTarget) {
			FlushBatch();
			appliedTargetValid = false;
			appliedTarget = nullptr;
		}
	}

	// ------------------------------------------------------------------ palette CLUTs

	void GuDevice::RegisterPaletteTexture(GuTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void GuDevice::NotifyPaletteTextureChanged(GuTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		_paletteGeneration++;
		for (std::int32_t i = 0; i < clutCacheCount; i++) {
			if (clutCache[i].Palette == texture && clutCache[i].Offset >= (firstRow - 1) * 256 &&
				clutCache[i].Offset < (firstRow + rowCount) * 256) {
				clutCache[i].Palette = nullptr;
			}
		}
	}

	namespace
	{
		/**
			@brief Returns the frame's CLUT copy of one palette row, loading it into the frame arena if needed

			The offset is a flat index into the palette texture and does not need to be row-aligned (the gem
			gradients pack two palettes into a single 256-entry row). Keyed exactly like the PowerVR's palette
			banks, so every draw of a row after the first one costs a pointer comparison.
		*/
		const std::uint32_t* AcquireClutForRow(const GuTexture* palette, std::int32_t paletteOffset,
			std::uint32_t version)
		{
			const std::int32_t maxOffset = (palette != nullptr
				? palette->GetWidth() * palette->GetHeight() - 256 : 0);
			if (palette == nullptr || palette->GetPixels() == nullptr ||
				paletteOffset < 0 || paletteOffset > maxOffset) {
				return nullptr;
			}

			for (std::int32_t i = 0; i < clutCacheCount; i++) {
				if (clutCache[i].Palette == palette && clutCache[i].Offset == paletteOffset &&
					clutCache[i].Version == version) {
					return clutCache[i].Data;
				}
			}

			// The GE reads the table when it executes the load command, which is behind the CPU, so every
			// distinct row of the frame needs its own copy rather than one shared scratch buffer
			std::uint32_t* data = static_cast<std::uint32_t*>(AllocFrameData(256 * sizeof(std::uint32_t), 16));
			if (data == nullptr) {
				return nullptr;
			}
			// A palette entry is an RGBA8 value with red in the lowest byte, which is byte for byte what
			// GU_PSM_8888 expects - unlike the PowerVR and the GX, no channel reordering is needed
			std::memcpy(data, reinterpret_cast<const std::uint32_t*>(palette->GetPixels()) + paletteOffset,
				256 * sizeof(std::uint32_t));
			sceKernelDcacheWritebackRange(data, 256 * sizeof(std::uint32_t));

			if (clutCacheCount < MaxClutCacheEntries) {
				clutCache[clutCacheCount++] = { palette, paletteOffset, version, data };
			}
			return data;
		}

		/** @brief Points the draw state at one page of a texture */
		void ApplyPageToState(DrawState& state, const GuTexture* texture, const GuTexture::Page& page)
		{
			state.TextureData = page.Data;
			state.TexturePsm = texture->GetGuPixelFormat();
			state.TextureWidth = page.PaddedWidth;
			state.TextureHeight = page.PaddedHeight;
			state.TextureStride = page.PaddedWidth;
			state.TextureSwizzled = page.Swizzled;
		}

		/**
			@brief Resolves the texture state of one draw: the page a texture rectangle samples, and its CLUT

			@p texRect is the instance's (uSpan, uOffset, vSpan, vOffset). Through-mode texture coordinates
			are texel indices, so the returned scale is simply the source size and the bias is the page
			origin - a single-page texture (everything up to 512x512) biases by zero.
		*/
		bool ResolveTextureState(DrawState& state, GuTexture* texture, const float* texRect,
			float& uvScaleU, float& uvScaleV, float& uvBiasU, float& uvBiasV)
		{
			const float u0 = texRect[1] * float(texture->GetWidth());
			const float u1 = (texRect[1] + texRect[0]) * float(texture->GetWidth());
			const float v0 = texRect[3] * float(texture->GetHeight());
			const float v1 = (texRect[3] + texRect[2]) * float(texture->GetHeight());
			const GuTexture::Page* page = texture->AcquirePage(std::int32_t(std::min(u0, u1)),
				std::int32_t(std::min(v0, v1)));
			if (page == nullptr || page->Data == nullptr) {
				return false;
			}
			if (texture->GetPageCountX() > 1 || texture->GetPageCountY() > 1) {
				// A primitive samples one frame rect of an atlas, so it practically never straddles a page
				// boundary; when it does, the containing page is used and the overhang is clamped by the
				// hardware, which is a visibly wrong edge rather than a missing sprite
				const float maxU = std::max(u0, u1), maxV = std::max(v0, v1);
				if (maxU > float(page->OriginX + page->Width) + 0.5f ||
					maxV > float(page->OriginY + page->Height) + 0.5f) {
					static bool warnedStraddle = false;
					if (!warnedStraddle) {
						warnedStraddle = true;
						LOGW("A primitive samples across a GE texture page boundary; its edge will be clamped");
					}
				}
			}

			ApplyPageToState(state, texture, *page);
			state.Filter = (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? GU_LINEAR : GU_NEAREST);
			state.WrapU = MapWrapGu(texture->GetWrapS());
			state.WrapV = MapWrapGu(texture->GetWrapT());
			uvScaleU = float(texture->GetWidth());
			uvScaleV = float(texture->GetHeight());
			uvBiasU = float(page->OriginX);
			uvBiasV = float(page->OriginY);
			return true;
		}
	}

	// ------------------------------------------------------------------ lighting hook

	void GuDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		PendingSoftwareLight light;
		light.Lightmap = lightmap;
		light.LmW = lmW;
		light.LmH = lmH;
		light.Scale = (scale > 0 ? scale : 1);
		light.VpX = vpX;
		light.VpY = vpY;
		light.VpW = vpW;
		light.VpH = vpH;
		light.AmbR = ambR;
		light.AmbG = ambG;
		light.AmbB = ambB;
		light.WaterActive = waterActive;
		light.WaterLevelPx = waterLevelPx;
		light.WaterTime = waterTime;
		light.WaterCamY = waterCamY;
		_pendingSoftwareLights.push_back(light);
	}

	void GuDevice::EndFrame()
	{
		if (!_pendingSoftwareLights.empty()) {
			static bool warnedLeftoverLights = false;
			if (!warnedLeftoverLights) {
				warnedLeftoverLights = true;
				LOGW("Dropping {} unconsumed software-lighting entries", _pendingSoftwareLights.size());
			}
			_pendingSoftwareLights.clear();
		}
	}

	void GuDevice::ApplyPendingSoftwareLighting()
	{
		if (_pendingSoftwareLights.empty()) {
			return;
		}
		const PendingSoftwareLight light = _pendingSoftwareLights.front();
		_pendingSoftwareLights.erase(_pendingSoftwareLights.begin());

		const bool hasLighting = (light.Lightmap != nullptr && light.LmW > 0 && light.LmH > 0);
		const bool hasWater = light.WaterActive;
		if (!hasLighting && !hasWater) {
			return;
		}

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			// Multiply factor from the CPU lightmap: out ~= scene * (r*(1+g) + amb*(1-r)) per channel (the
			// multiply-only approximation shared with the GX and PVR backends), as a 4444 texture drawn with
			// a dst * src blend over the viewport. The texture is generated into the frame arena, which is
			// exactly the lifetime it needs - the GE consumes it within this frame and it is gone at present.
			std::int32_t texW = 8, texH = 8;
			while (texW < light.LmW && texW < GuTexture::MaxPageDimension) texW <<= 1;
			while (texH < light.LmH && texH < GuTexture::MaxPageDimension) texH <<= 1;
			const std::size_t size = std::size_t(texW) * std::size_t(texH) * 2;
			std::uint16_t* const surface = static_cast<std::uint16_t*>(AllocFrameData(size, 16));
			if (surface != nullptr) {
				for (std::int32_t y = 0; y < light.LmH; y++) {
					const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(y) * light.LmW * 2;
					std::uint16_t* DEATH_RESTRICT dst = surface + std::size_t(y) * texW;
					// Unlit runs repeat the same pair of factors across long spans, so remembering the last
					// converted texel turns most of the surface into a compare and a store
					float prevR = -1.0f, prevG = -1.0f;
					std::uint16_t prevTexel = 0;
					for (std::int32_t x = 0; x < light.LmW; x++) {
						const float rawR = src[x * 2];
						const float rawG = src[x * 2 + 1];
						if (rawR == prevR && rawG == prevG) {
							dst[x] = prevTexel;
							continue;
						}
						prevR = rawR;
						prevG = rawG;
						const float r = (rawR < 0.0f ? 0.0f : (rawR > 1.0f ? 1.0f : rawR));
						const float g = (rawG < 0.0f ? 0.0f : (rawG > 1.0f ? 1.0f : rawG));
						const float lit = r * (1.0f + g);
						const float inv = 1.0f - r;
						const std::uint32_t fr = Quantize4Bit(lit + light.AmbR * inv);
						const std::uint32_t fg = Quantize4Bit(lit + light.AmbG * inv);
						const std::uint32_t fb = Quantize4Bit(lit + light.AmbB * inv);
						prevTexel = std::uint16_t(0xF000 | (fb << 8) | (fg << 4) | fr);
						dst[x] = prevTexel;
					}
					// The padding columns are reached by the bilinear tap at the last texel
					for (std::int32_t x = light.LmW; x < texW; x++) {
						dst[x] = prevTexel;
					}
				}
				for (std::int32_t y = light.LmH; y < texH; y++) {
					std::memcpy(surface + std::size_t(y) * texW, surface + std::size_t(light.LmH - 1) * texW,
						std::size_t(texW) * 2);
				}
				sceKernelDcacheWritebackRange(surface, size);

				DrawState state;
				state.TextureData = surface;
				state.TexturePsm = GU_PSM_4444;
				state.TextureWidth = texW;
				state.TextureHeight = texH;
				state.TextureStride = texW;
				state.TextureSwizzled = false;
				state.Filter = GU_LINEAR;
				state.Tfx = GU_TFX_MODULATE;
				state.Tcc = GU_TCC_RGB;
				// out = dst * src: the source factor is "the other side's colour", the destination factor zero
				state.BlendEnabled = true;
				state.BlendOp = GU_ADD;
				state.BlendSrc = GU_OTHER_COLOR;
				state.BlendDst = GU_FIX;
				state.BlendDstFix = 0x000000;

				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer convention), so V runs used -> 0 top -> bottom
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
				const float pu[4] = { float(light.LmW), float(light.LmW), 0.0f, 0.0f };
				const float pv[4] = { float(light.LmH), 0.0f, float(light.LmH), 0.0f };
				SubmitQuadPrimitive(state, px, py, pu, pv, PackAbgr(255, 255, 255, 255));
			}
		}

		if (hasWater) {
			// Water v1: constant underwater tint band + above-deep-water darkening (shared with GX/PVR)
			DrawState state;
			state.BlendEnabled = true;
			state.BlendOp = GU_ADD;
			state.BlendSrc = GU_SRC_ALPHA;
			state.BlendDst = GU_ONE_MINUS_SRC_ALPHA;

			const float waterTop = vpY + light.WaterLevelPx * scaleY;
			const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (waterTop < vpY + vpH) {
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { waterTop, vpY + vpH, waterTop, vpY + vpH };
				SubmitQuadPrimitive(state, px, py, uv, uv, PackAbgr(102, 153, 204, 102));
			}
			const float waterLevelNorm = (light.VpH > 0 ? light.WaterLevelPx / float(light.VpH) : 1.0f);
			if (waterLevelNorm < 0.4f && waterTop > vpY) {
				const std::uint8_t a = QuantizeChannel(0.4f - waterLevelNorm);
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, waterTop, vpY, waterTop };
				SubmitQuadPrimitive(state, px, py, uv, uv,
					PackAbgr(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a));
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void GuDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const GuBuffer* vbo = _currentProgram->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		const std::size_t firstFloat = (std::size_t(_currentProgram->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		const std::size_t floatCount = std::size_t(numVertices) * FloatsPerVertex;
		if ((firstFloat + floatCount) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;

		const GuUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		GuTexture* texture = const_cast<GuTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		float pv[16];
		Mat4Mul(projMat, viewMat, pv);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds uTexturePalette
		// in its reflection, which is exactly what UsesPalette() reports.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const GuTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed() || texture->NeedsPaletteBake()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the residency,
		// the CLUT and the GE state are resolved once for the entire layer
		std::int32_t paletteOffset = 0;
		if (isPaletteRemap) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			paletteOffset = std::int32_t(palOffset + 0.5f);
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		DrawState state;
		if (texture->NeedsPaletteBake()) {
			if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr) {
				return;
			}
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels())
				+ paletteOffset;
			if (!texture->EnsureBakedStore(entries, std::uint32_t(paletteOffset), paletteVersion, paletteTex)) {
				return;
			}
		} else if (texture->IsIndexed()) {
			state.Clut = AcquireClutForRow(paletteTex, paletteOffset, paletteVersion);
			if (state.Clut == nullptr) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}
		std::uint32_t srcFix = 0, dstFix = 0;
		state.BlendEnabled = _blending.Enabled;
		state.BlendOp = GU_ADD;
		state.BlendSrc = MapBlendGu(_blending.SrcRgb, srcFix);
		state.BlendDst = MapBlendGu(_blending.DstRgb, dstFix);
		state.BlendSrcFix = srcFix;
		state.BlendDstFix = dstFix;

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex. A screen pass mirrors NDC, which is just the
		// sign of the Y scale.
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		// A tileset atlas wider or taller than one GE page is split into several, and different tiles of the
		// same layer then live in different pages - so the page is chosen per primitive rather than once for
		// the mesh. Single-page atlases (the common case: a 10-tile-wide sheet is a 340 px atlas) skip all of
		// this and keep one state for the whole layer.
		const bool pagedTexture = (texture->GetPageCountX() > 1 || texture->GetPageCountY() > 1);
		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU - uvBiasU;
			outV = v[3] * uvScaleV - uvBiasV;
		};
		// Repoints the state at the page a primitive's texture coordinates fall into and rebases them onto it
		auto selectPage = [&](DrawState& target, float* pu, float* pvv, std::int32_t count) {
			float minU = pu[0], minV = pvv[0];
			for (std::int32_t i = 1; i < count; i++) {
				minU = std::min(minU, pu[i]);
				minV = std::min(minV, pvv[i]);
			}
			const GuTexture::Page* page = texture->AcquirePage(std::int32_t(minU), std::int32_t(minV));
			if (page == nullptr || page->Data == nullptr) {
				return false;
			}
			ApplyPageToState(target, texture, *page);
			for (std::int32_t i = 0; i < count; i++) {
				pu[i] -= float(page->OriginX);
				pvv[i] -= float(page->OriginY);
			}
			return true;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the four
		// clamp+float-to-int quantizations run once per change instead of once per tile
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		std::uint32_t lastAbgr = 0;
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the third and fourth repeat the
			// first and third. Recognizing that pattern lets a tile go out as one quad - and, since tile
			// quads are axis-aligned, as a two-vertex GE rectangle rather than six triangle vertices.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			float px[4], py[4], pu[4], pvv[4];
			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				lastAbgr = PackAbgr(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}

			if (isQuad) {
				// Corner order of the sprite strip (v0, v1, v2, v3): vertices 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(group + std::size_t(QuadOrder[i]) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				DrawState quadState = state;
				if (!pagedTexture || selectPage(quadState, pu, pvv, 4)) {
					SubmitQuadPrimitive(quadState, px, py, pu, pvv, lastAbgr);
				}
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				DrawState triState = state;
				triState.Prim = GU_TRIANGLES;
				if (!pagedTexture || selectPage(triState, pu, pvv, 3)) {
					Vertex2D* v = AllocVertices(triState, 3);
					if (v == nullptr) {
						return;
					}
					for (std::int32_t i = 0; i < 3; i++) {
						v[i] = { pu[i], pvv[i], lastAbgr, px[i], py[i], 0.0f };
					}
				}
				triangle++;
			}
		}
	}

	void GuDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy, texcoords.uv)
		// - the layout the MeshSprite shader's attributes declare. The GE has a native single-pixel line
		// primitive, so unlike the PowerVR (which expands every segment into a thin quad) the strip goes out
		// as it is, in one draw call.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2) {
			return;
		}

		const GuBuffer* vbo = _currentProgram->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		const std::size_t firstFloat = (std::size_t(_currentProgram->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		const std::size_t floatCount = std::size_t(numVertices) * FloatsPerVertex;
		if ((firstFloat + floatCount) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;

		const GuUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		GuTexture* texture = const_cast<GuTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		float pvMat[16];
		Mat4Mul(projMat, viewMat, pvMat);
		Transform2D mvp;
		Mat4MulTransform2D(pvMat, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is packed once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));
		const std::uint32_t abgr = PackAbgr(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
			QuantizeChannel(color[2]), QuantizeChannel(color[3]));

		DrawState state;
		const std::uint32_t paletteVersion = _paletteGeneration;
		if (texture->NeedsPaletteBake()) {
			const GuTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr ||
				!texture->EnsureBakedStore(reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels()), 0,
					paletteVersion, paletteTex)) {
				return;
			}
		} else if (texture->IsIndexed()) {
			const GuTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			state.Clut = AcquireClutForRow(paletteTex, 0, paletteVersion);
			if (state.Clut == nullptr) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}
		std::uint32_t srcFix = 0, dstFix = 0;
		state.BlendEnabled = _blending.Enabled;
		state.BlendOp = GU_ADD;
		state.BlendSrc = MapBlendGu(_blending.SrcRgb, srcFix);
		state.BlendDst = MapBlendGu(_blending.DstRgb, dstFix);
		state.BlendSrcFix = srcFix;
		state.BlendDstFix = dstFix;
		state.Prim = GU_LINE_STRIP;

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		// A line strip is one primitive, so it gets its own draw call
		FlushBatch();
		Vertex2D* v = AllocVertices(state, numVertices);
		if (v == nullptr) {
			return;
		}
		for (std::int32_t i = 0; i < numVertices; i++) {
			const float* src = vertices + std::size_t(i) * FloatsPerVertex;
			v[i].U = src[2] * uvScaleU - uvBiasU;
			v[i].V = src[3] * uvScaleV - uvBiasV;
			v[i].Color = abgr;
			v[i].X = raster.Xx * src[0] + raster.Yx * src[1] + raster.Tx;
			v[i].Y = raster.Xy * src[0] + raster.Yy * src[1] + raster.Ty;
			v[i].Z = 0.0f;
		}
		FlushBatch();
	}

	void GuDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		if (_currentProgram == nullptr || numVertices <= 0 || !_guInitialized) {
			return;
		}

		// The program's whole console identity is its effect-table entry, resolved at load from the true
		// (program, variant) the loaders plumbed in - a program without an entry has no fixed_function block
		// in its .shader file (Lighting, Blur, the Resize* family, runtime-compiled shaders, ...) and keeps
		// the logged, skipped draw
		const FixedFunctionGeneratedEffect* generated = _currentProgram->GetGeneratedEffect();
		if (generated == nullptr) {
			frameSkippedDraws++;
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": No fixed_function effect declared by the shader", _currentProgram->GetObjectLabel());
			}
			return;
		}
		const FixedFunctionIntrinsic intrinsic = generated->Intrinsic;

		// The Combine draw is the direct-tier lighting hook (see the software backend)
		if (intrinsic == FixedFunctionIntrinsic::LightingCombine) {
			ApplyPendingSoftwareLighting();
			return;
		}

		// A whole tile layer arrives as one mesh instead of one command per tile
		if (intrinsic == FixedFunctionIntrinsic::TileMapMesh) {
			DispatchTileMesh(primitive, firstVertex, numVertices);
			return;
		}

		// The weapon wheel is the one vertex-fed mesh on this tier, a textured line strip
		if (intrinsic == FixedFunctionIntrinsic::LineStripMesh) {
			if (primitive == PrimitiveType::LineStrip) {
				DispatchLineStrip(firstVertex, numVertices);
			} else if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the GU dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			frameSkippedDraws++;
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the GU dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		const GuUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			block = _currentProgram->FindBlock("InstancesBlock");
		}
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		const ShaderCompiler::ProgramVariant* reflection = _currentProgram->GetReflection();
		auto samplerUnit = [reflection](const char* name, std::int32_t def) -> std::int32_t {
			if (reflection != nullptr) {
				for (std::size_t i = 0; i < reflection->TextureCount; i++) {
					if (std::strcmp(reflection->Textures[i].Name, name) == 0) {
						return (reflection->Textures[i].Unit >= 0 ? reflection->Textures[i].Unit : def);
					}
				}
			}
			return def;
		};
		std::uint32_t instanceStride = 0;
		if (reflection != nullptr) {
			for (std::size_t i = 0; i < reflection->BlockCount; i++) {
				if (reflection->Blocks[i].InstanceStride > 0) {
					instanceStride = reflection->Blocks[i].InstanceStride;
					break;
				}
			}
		}

		float pv[16];
		Mat4Mul(projMat, viewMat, pv);

		// Batched programs are exactly the ones whose reflection declares a BATCH_SIZE-strided
		// InstancesBlock (non-batched programs use a flat InstanceBlock with no stride), so the reflected
		// stride IS the batching signal - no per-program identity needed
		const bool batched = (instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
		}

		// A program samples the sprite texture exactly when its reflection binds uTexture
		bool hasTexture = false;
		if (reflection != nullptr) {
			for (std::size_t i = 0; i < reflection->TextureCount; i++) {
				if (std::strcmp(reflection->Textures[i].Name, "uTexture") == 0) {
					hasTexture = true;
					break;
				}
			}
		}
		// The instance layout follows the block's own reflected declaration rather than any effect identity:
		// a block that declares texRect uses the textured member offsets whether or not the program samples
		// a texture (the Transition carries texRect but samples nothing)
		bool texturedLayout = hasTexture;
		if (!texturedLayout && reflection != nullptr) {
			for (std::size_t i = 0; i < reflection->BlockCount && !texturedLayout; i++) {
				const ShaderCompiler::UniformBlock& b = reflection->Blocks[i];
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					if (std::strcmp(b.Members[j].Name, "texRect") == 0) {
						texturedLayout = true;
						break;
					}
				}
			}
		}
		// Every effect that samples indexed sprites through the palette texture binds uTexturePalette in its
		// reflection, which is what UsesPalette() reports
		const bool isPaletteRemap = _currentProgram->UsesPalette();

		const FixedFunctionRequirements reqs = generated->Requirements;
		const bool needsTexelStep = ((reqs & FixedFunctionRequirements::NeedsTexelStep) == FixedFunctionRequirements::NeedsTexelStep);
		const bool needsUniforms = ((reqs & FixedFunctionRequirements::NeedsUniforms) == FixedFunctionRequirements::NeedsUniforms);
		const bool needsStripBuilder = ((reqs & FixedFunctionRequirements::NeedsStripBuilder) == FixedFunctionRequirements::NeedsStripBuilder);
		const bool needsQuadAxes = ((reqs & FixedFunctionRequirements::NeedsQuadAxes) == FixedFunctionRequirements::NeedsQuadAxes);
		const std::int32_t textureUnit = samplerUnit("uTexture", 0);
		GuTexture* texture = const_cast<GuTexture*>(hasTexture
			? _boundTextures[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		if (hasTexture && texture == nullptr) {
			return;
		}

		// The palette to resolve indices with is whatever the material bound to the palette sampler (e.g.
		// the recolored preview palettes of the profile menu); the registered global palette is the fallback
		const GuTexture* paletteTex = nullptr;
		if (isPaletteRemap || (texture != nullptr && (texture->IsIndexed() || texture->NeedsPaletteBake()))) {
			const std::int32_t paletteUnit = samplerUnit("uTexturePalette", 1);
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);

		// The material's own blending, shared by every instance of the draw
		DrawState material;
		{
			std::uint32_t srcFix = 0, dstFix = 0;
			material.BlendEnabled = _blending.Enabled;
			material.BlendOp = GU_ADD;
			material.BlendSrc = MapBlendGu(_blending.SrcRgb, srcFix);
			material.BlendDst = MapBlendGu(_blending.DstRgb, dstFix);
			material.BlendSrcFix = srcFix;
			material.BlendDstFix = dstFix;
		}

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract), derived
		// only for effects flagged with the texel-size facility
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the GE scans out its buffer top-down directly, so screen passes mirror NDC here
		// instead (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is
		// what the sampling passes already expect - which is just the sign of the raster Y scale below.
		const bool screenPass = (_currentRenderTarget == nullptr);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;

		for (std::int32_t k = 0; k < numInstances; k++) {
			const std::uint8_t* inst = blockData + std::size_t(k) * (batched ? instanceStride : 0);

			Transform2D mvp;
			Mat4MulTransform2D(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
			float color[4];
			std::memcpy(color, inst + kColorOffset, sizeof(color));
			float texRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			float spriteSize[2];
			if (texturedLayout) {
				std::memcpy(texRect, inst + kTexRectOffset, sizeof(texRect));
				std::memcpy(spriteSize, inst + kSpriteSizeOffset, sizeof(spriteSize));
			} else {
				std::memcpy(spriteSize, inst + kSpriteSizeNoTexOffset, sizeof(spriteSize));
			}

			// Select this instance's texture page (and CLUT or bake) - through-mode texture coordinates are
			// texel indices of the bound page, so the conversion also carries the page origin
			DrawState state = material;
			float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
			if (hasTexture) {
				std::int32_t paletteOffset = 0;
				if (isPaletteRemap) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					paletteOffset = std::int32_t(palOffset + 0.5f);
				}
				if (texture->NeedsPaletteBake()) {
					// Index + per-pixel alpha has no paletted form: a CPU bake through the palette row is
					// what the other consoles do too
					if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr) {
						continue;
					}
					const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels())
						+ paletteOffset;
					if (!texture->EnsureBakedStore(entries, std::uint32_t(paletteOffset), paletteVersion, paletteTex)) {
						continue;
					}
				} else if (texture->IsIndexed()) {
					// An 8bpp store can only be read through a CLUT, whatever it is being drawn with - the
					// lookup belongs to the texture read rather than to the effect. An effect that remaps
					// takes the row from the instance; anything else (the fonts) uses the base row.
					state.Clut = AcquireClutForRow(paletteTex, paletteOffset, paletteVersion);
					if (state.Clut == nullptr) {
						continue;
					}
				}
				if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
					continue;
				}
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform so a corner costs one multiply-add per axis.
			// The corner weights are 0 or 1, so the sprite's extent in raster space is just the transformed
			// axes scaled by its size, and the corners are sums of those.
			const float originX = mvp.Tx * rasterScaleX + rasterBiasX;
			const float originY = mvp.Ty * rasterScaleY + rasterBiasY;
			const float spanXx = mvp.Xx * rasterScaleX * spriteSize[0];
			const float spanXy = mvp.Xy * rasterScaleY * spriteSize[0];
			const float spanYx = mvp.Yx * rasterScaleX * spriteSize[1];
			const float spanYy = mvp.Yy * rasterScaleY * spriteSize[1];
			float px[4], py[4], pu[4], pvv[4];
			for (std::int32_t i = 0; i < 4; i++) {
				const float ax = ((i & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (i & 1) ? 1.0f : 0.0f;
				px[i] = originX + ax * spanXx + ay * spanYx;
				py[i] = originY + ax * spanXy + ay * spanYy;
				pu[i] = (ax * texRect[0] + texRect[1]) * uvScaleU - uvBiasU;
				pvv[i] = (ay * texRect[2] + texRect[3]) * uvScaleV - uvBiasV;
			}

			// Nothing is clipped geometrically - the GE has a real raster scissor and it is already
			// programmed - but a quad that cannot touch the surface at all is worth not submitting
			{
				const float minX = std::min(std::min(px[0], px[1]), std::min(px[2], px[3]));
				const float maxX = std::max(std::max(px[0], px[1]), std::max(px[2], px[3]));
				const float minY = std::min(std::min(py[0], py[1]), std::min(py[2], py[3]));
				const float maxY = std::max(std::max(py[0], py[1]), std::max(py[2], py[3]));
				if (maxX <= float(appliedScissor[0]) || minX >= float(appliedScissor[0] + appliedScissor[2]) ||
					maxY <= float(appliedScissor[1]) || minY >= float(appliedScissor[1] + appliedScissor[3])) {
					continue;
				}
			}

			// The pass descriptors the per-effect functions declare are mapped onto this instance's corners
			// and the resolved GE state through the context
			EffectContext ctx;
			ctx.InstanceColor = color;
			ctx.TexelW = texelWidth;
			ctx.TexelH = texelHeight;
			ctx.Batched = batched;
			ctx.Material = &state;
			ctx.Px = px;
			ctx.Py = py;
			ctx.Pu = pu;
			ctx.Pv = pvv;
			ctx.TexRect = texRect;
			// The optional context facilities are only wired up for effects whose static analysis says they
			// can call them; members of an unused facility are simply never read
			if (needsQuadAxes) {
				ctx.OriginX = originX;
				ctx.OriginY = originY;
				ctx.AxisXx = spanXx;
				ctx.AxisXy = spanXy;
				ctx.AxisYx = spanYx;
				ctx.AxisYy = spanYy;
			}
			ctx.Program = (needsUniforms ? _currentProgram : nullptr);
			if (needsStripBuilder) {
				ctx.UvScaleU = uvScaleU;
				ctx.UvScaleV = uvScaleV;
				ctx.UvBiasU = uvBiasU;
				ctx.UvBiasV = uvBiasV;
			}

			generated->Fn(ctx);
		}
	}
}
