#include "PvrDevice.h"
#include "PvrBuffer.h"
#include "PvrShaderProgram.h"
#include "PvrRenderTarget.h"
#include "PvrTexture.h"
#include "../FixedFunctionPass.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <dc/sq.h>

namespace nCine::RHI::PVR
{
	namespace
	{
		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software
		// backend's decode (see SwDevice.cpp)
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

		/**
			@brief The projection*view product of the last draw, rebuilt only when either input changed

			The product changes a handful of times a frame (a camera move, a viewport switch) while a frame
			runs a few hundred dispatches, so comparing 128 bytes replaces the 64 multiplies of the full
			product almost every time. Compared by VALUE, not by pointer - the matrices are rewritten in
			place when the camera moves.
		*/
		inline const float* CachedProjView(const float* projMat, const float* viewMat)
		{
			static float cachedPv[16];
			static float cachedProj[16];
			static float cachedView[16];
			static bool cachedValid = false;
			if (!cachedValid || std::memcmp(projMat, cachedProj, sizeof(cachedProj)) != 0 ||
					std::memcmp(viewMat, cachedView, sizeof(cachedView)) != 0) {
				std::memcpy(cachedProj, projMat, sizeof(cachedProj));
				std::memcpy(cachedView, viewMat, sizeof(cachedView));
				Mat4Mul(projMat, viewMat, cachedPv);
				cachedValid = true;
			}
			return cachedPv;
		}

		// Both draw paths only ever transform points of the form (x, y, 0, 1), so just six of the sixteen
		// products of projection*view*model are ever read back. Sprites pay this per instance, which made
		// the full 4x4 multiply the most expensive step of the per-instance loop.
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

		// Maps a pipeline-neutral blend factor onto the PVR factor set
		std::int32_t MapBlendPvr(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return PVR_BLEND_ZERO;
				case nCine::BlendingFactor::One:				return PVR_BLEND_ONE;
				case nCine::BlendingFactor::SrcColor:			return PVR_BLEND_DESTCOLOR;		// Valid as a dst factor only; the src slot maps below
				case nCine::BlendingFactor::OneMinusSrcColor:	return PVR_BLEND_INVDESTCOLOR;
				case nCine::BlendingFactor::DstColor:			return PVR_BLEND_DESTCOLOR;
				case nCine::BlendingFactor::OneMinusDstColor:	return PVR_BLEND_INVDESTCOLOR;
				case nCine::BlendingFactor::SrcAlpha:			return PVR_BLEND_SRCALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return PVR_BLEND_INVSRCALPHA;
				case nCine::BlendingFactor::DstAlpha:			return PVR_BLEND_DESTALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return PVR_BLEND_INVDESTALPHA;
				default:										return PVR_BLEND_ONE;
			}
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		// Straight to the 4 bits an ARGB4444 channel actually keeps, skipping the round trip through 8 bits
		inline std::uint32_t Quantize4Bit(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint32_t(v * 15.0f + 0.5f);
		}

		inline std::uint32_t PackArgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
		}

		// Clamps one screen-space quad edge pair (a..b with linearly mapped texture coordinates ua..ub)
		// into [lo, hi]; returns false when the whole span lies outside. Works for either edge direction.
		bool ClipQuadEdge(float& a, float& b, float& ua, float& ub, float lo, float hi)
		{
			if ((a <= lo && b <= lo) || (a >= hi && b >= hi)) {
				return false;
			}
			const float d = b - a;
			if (d != 0.0f) {
				const float du = (ub - ua) / d;
				const float na = (a < lo ? lo : (a > hi ? hi : a));
				const float nb = (b < lo ? lo : (b > hi ? hi : b));
				ua += (na - a) * du;
				ub += (nb - b) * du;
				a = na;
				b = nb;
			}
			return true;
		}

		// The tile accelerator is a state machine: a polygon header stays in effect for every strip that
		// follows it within the open list, so a header identical to the last submitted one does not have
		// to go out again - it is 32 of the 160 bytes of a typical quad, and batches reuse one header for
		// hundreds of primitives. Cleared whenever a new list opens (see InvalidateSubmittedHeader()).
		std::uint32_t lastHeaderWords[8];
		bool lastHeaderValid = false;

		void InvalidateSubmittedHeader()
		{
			lastHeaderValid = false;
		}

		// Writes the header into the store queues only when it differs from the last submitted one, and
		// returns the queue pointer for the vertices that follow
		DEATH_ALWAYS_INLINE std::uint32_t* SubmitHeaderIfChanged(const pvr_poly_hdr_t& hdr)
		{
			std::uint32_t* DEATH_RESTRICT sq = SQ_MASK_DEST(PVR_TA_INPUT);
			const std::uint32_t* DEATH_RESTRICT header = reinterpret_cast<const std::uint32_t*>(&hdr);
			if (lastHeaderValid &&
					lastHeaderWords[0] == header[0] && lastHeaderWords[1] == header[1] &&
					lastHeaderWords[2] == header[2] && lastHeaderWords[3] == header[3] &&
					lastHeaderWords[4] == header[4] && lastHeaderWords[5] == header[5] &&
					lastHeaderWords[6] == header[6] && lastHeaderWords[7] == header[7]) {
				return sq;
			}
			sq[0] = lastHeaderWords[0] = header[0]; sq[1] = lastHeaderWords[1] = header[1];
			sq[2] = lastHeaderWords[2] = header[2]; sq[3] = lastHeaderWords[3] = header[3];
			sq[4] = lastHeaderWords[4] = header[4]; sq[5] = lastHeaderWords[5] = header[5];
			sq[6] = lastHeaderWords[6] = header[6]; sq[7] = lastHeaderWords[7] = header[7];
			lastHeaderValid = true;
			sq_flush(sq);
			return sq + 8;
		}

		// Submits one strip of `count` vertices (3 = a single triangle, 4 = a quad) under the given header
		// to the open translucent list. The corner order matches the procedural sprite strip (v0, v1, v2,
		// v3) exactly like the software FetchVertex synthesizes it. The offset colour is added after
		// texturing (only when the polygon enables it), which is how the actor state effects brighten or
		// tint the sprite - see the effect handling in Dispatch.
		//
		// The primitives are written straight into the store queues rather than assembled in main memory
		// and handed to pvr_prim(): that path copies every 32-byte primitive a second time on its way out,
		// which at four vertices per sprite and per tile was a large part of the submission cost. The queue
		// pointer advances a block at a time exactly as sq_cpy() does, which alternates the two hardware
		// banks so a bank is never rewritten while its write-back is still in flight. The PVR driver has
		// already pointed the queues at the TA FIFO (pvr_prim() itself relies on that), so no lock is taken.
		void SubmitStrip(const pvr_poly_hdr_t& hdr, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, std::uint32_t argb, std::uint32_t oargb = 0, float dx = 0.0f, float dy = 0.0f)
		{
			static_assert(sizeof(pvr_vertex_t) == 32 && sizeof(pvr_poly_hdr_t) == 32,
				"The store queues submit whole 32 byte blocks");

			std::uint32_t* DEATH_RESTRICT sq = SubmitHeaderIfChanged(hdr);

			for (std::int32_t i = 0; i < count; i++) {
				sq[0] = (i == count - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
				reinterpret_cast<float*>(sq)[1] = px[i] + dx;
				reinterpret_cast<float*>(sq)[2] = py[i] + dy;
				reinterpret_cast<float*>(sq)[3] = 1.0f;
				reinterpret_cast<float*>(sq)[4] = pu[i];
				reinterpret_cast<float*>(sq)[5] = pv[i];
				sq[6] = argb;
				sq[7] = oargb;
				sq_flush(sq);
				sq += 8;
			}
		}

		void SubmitQuad(const pvr_poly_hdr_t& hdr, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t argb, std::uint32_t oargb = 0, float dx = 0.0f, float dy = 0.0f)
		{
			SubmitStrip(hdr, px, py, pu, pv, 4, argb, oargb, dx, dy);
		}

		// As SubmitStrip(), but each vertex carries its own colour so the rasterizer interpolates it across
		// the primitive - which is how a gradient is expressed without a fragment shader
		void SubmitStripShaded(const pvr_poly_hdr_t& hdr, const float* px, const float* py, std::int32_t count,
			const std::uint32_t* argb)
		{
			std::uint32_t* DEATH_RESTRICT sq = SubmitHeaderIfChanged(hdr);

			for (std::int32_t i = 0; i < count; i++) {
				sq[0] = (i == count - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
				reinterpret_cast<float*>(sq)[1] = px[i];
				reinterpret_cast<float*>(sq)[2] = py[i];
				reinterpret_cast<float*>(sq)[3] = 1.0f;
				reinterpret_cast<float*>(sq)[4] = 0.0f;
				reinterpret_cast<float*>(sq)[5] = 0.0f;
				sq[6] = argb[i];
				sq[7] = 0;
				sq_flush(sq);
				sq += 8;
			}
		}

	}

	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still
	// defined only in this translation unit): the generated table struct below is itself at
	// namespace scope - so the backend's ShaderProgram can forward-declare it and hold a typed
	// entry pointer - and names EffectContext in a member type; the console toolchain's GCC
	// ICEs when such an external struct member references an internal-linkage type.
	// ---------------------------------------------------------- fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this
	// EffectContext - the structural contract documented in FixedFunctionPass.h. The per-effect
	// functions live in the shaders' fixed_function blocks and are transpiled by the
	// ShaderCompiler into Shaders/Generated/PvrGeneratedEffects.h, included below against this
	// concrete context (see Docs/FixedFunctionShaderDesign.md); only the submission machinery
	// stays here.

	struct EffectContext
	{
		// The strip builder is deliberately small: every geometry effect submits trapezoid or fan
		// pieces of at most 4 vertices, and a bigger scratch would only grow the per-instance stack
		static constexpr std::int32_t MaxStripVertices = 8;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// Backend internals the pass state maps onto: the compiled base material header, its
		// build context (for deriving blend twins on demand) and the current instance's
		// post-clip corner arrays
		const pvr_poly_hdr_t* Hdr;
		pvr_poly_cxt_t* BaseCxt;
		pvr_poly_hdr_t* HdrAdditive;
		bool* HdrAdditiveValid;
		pvr_poly_hdr_t* HdrOpaque;
		bool* HdrOpaqueValid;
		pvr_poly_hdr_t* HdrAlpha;
		bool* HdrAlphaValid;
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms), the material blend factors (the shaded-strip Material twin cannot read them
		// back from BaseCxt - the blend twins above mutate its blend fields) and the UV scale of the
		// padded texture store (folded into strip UVs exactly like the quad corner synthesis)
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const PvrShaderProgram* Program;
		std::int32_t MaterialBlendSrc;
		std::int32_t MaterialBlendDst;
		float UvScaleU, UvScaleV;

		// Colour-polygon twins for shaded strips, one per BlendMode, compiled lazily per draw (see
		// SubmitStripShaded); they do not depend on the instance's texture variant, so they are
		// never invalidated within a draw
		pvr_poly_hdr_t* HdrShaded;
		bool* HdrShadedValid;

		// The strip builder scratch; colours are packed at set time (same quantization as the quad
		// path, so identical float inputs produce identical vertex words)
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint32_t StripArgb[MaxStripVertices];

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
			// An unresolved name leaves the caller's zeros in place - blocks guard with
			// has_uniform() exactly like the handwritten code null-checked the pointers
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
				StripU[i] = u * UvScaleU;
				StripV[i] = v * UvScaleV;
			}
		}
		void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripArgb[i] = PackArgb(QuantizeChannel(r), QuantizeChannel(g), QuantizeChannel(b), QuantizeChannel(a));
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step
		// the Outline ring taps use. The corners are already in raster space, so the result is a
		// raster displacement (the padding scale applies to both the texel size and the quad's
		// span, so it cancels out).
		float TexelToScreenX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToScreenY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader
		// family carries the sprite's UV-space texel size in its instance color.xy (exactly like
		// the GLSL derives its tap offsets), folded through the raster-space conversion above
		float TexelStepX() const { return TexelToScreenX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToScreenY(InstanceColor[1]); }

		// The material header (or its lazily compiled blend twin) a pass's blend mode maps onto -
		// shared by the quad and the textured-strip submissions
		const pvr_poly_hdr_t* MaterialHeaderFor(FixedFunctionPass::BlendMode blend)
		{
			switch (blend) {
				case FixedFunctionPass::BlendMode::Additive:
					// Deliberately SRCALPHA rather than a literal ONE source factor: it is the
					// additive mechanism Colorized has always used, and its split-multiplier
					// passes rely on the source alpha scaling each contribution
					if (!*HdrAdditiveValid) {
						BaseCxt->blend.src = PVR_BLEND_SRCALPHA;
						BaseCxt->blend.dst = PVR_BLEND_ONE;
						pvr_poly_compile(HdrAdditive, BaseCxt);
						*HdrAdditiveValid = true;
					}
					return HdrAdditive;
				case FixedFunctionPass::BlendMode::Opaque:
					if (!*HdrOpaqueValid) {
						BaseCxt->blend.src = PVR_BLEND_ONE;
						BaseCxt->blend.dst = PVR_BLEND_ZERO;
						pvr_poly_compile(HdrOpaque, BaseCxt);
						*HdrOpaqueValid = true;
					}
					return HdrOpaque;
				case FixedFunctionPass::BlendMode::Alpha:
					if (!*HdrAlphaValid) {
						BaseCxt->blend.src = PVR_BLEND_SRCALPHA;
						BaseCxt->blend.dst = PVR_BLEND_INVSRCALPHA;
						pvr_poly_compile(HdrAlpha, BaseCxt);
						*HdrAlphaValid = true;
					}
					return HdrAlpha;
				default:
					return Hdr;
			}
		}

		// The packed offset colour of a pass: added after texturing only on polygons compiled with
		// specular enabled (a per-effect property of the base material, see usesOffsetColor in
		// Dispatch); a pass without one submits 0, which is also what specular-enabled polygons
		// expect for "no offset"
		std::uint32_t PackOffsetColor(const FixedFunctionPass& pass) const
		{
			return (pass.HasOffsetColor
				? PackArgb(QuantizeChannel(pass.OffsetColor[0]), QuantizeChannel(pass.OffsetColor[1]),
					QuantizeChannel(pass.OffsetColor[2]), 0)
				: 0);
		}

		void SubmitQuad(const FixedFunctionPass& pass)
		{
			const pvr_poly_hdr_t* hdr = MaterialHeaderFor(pass.Blend);
			const std::uint32_t argb = PackArgb(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), QuantizeChannel(pass.Color[3]));
			// Qualified: the SubmitStrip member above hides the free helper of the same name
			nCine::RHI::PVR::SubmitStrip(*hdr, Px, Py, Pu, Pv, 4, argb, PackOffsetColor(pass),
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Textured strip out of the builder scratch: the pass's flat colour over the material state
		// (the warp's band pieces). Qualified call - the free SubmitStrip helper would otherwise be
		// hidden by this member's own name.
		void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			const pvr_poly_hdr_t* hdr = MaterialHeaderFor(pass.Blend);
			const std::uint32_t argb = PackArgb(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), QuantizeChannel(pass.Color[3]));
			nCine::RHI::PVR::SubmitStrip(*hdr, StripX, StripY, StripU, StripV, count, argb,
				PackOffsetColor(pass), pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch: always an UNTEXTURED colour
		// polygon - a gradient has no texture to modulate - whose blend comes from the pass. For an
		// untextured material the Material twin compiles to the exact words of the base header, so
		// the TA header dedup keeps the submitted stream identical to reusing the base (which is
		// what the handwritten iris did).
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			std::int32_t mode = std::int32_t(pass.Blend);
			if (std::uint32_t(mode) > std::uint32_t(FixedFunctionPass::BlendMode::Alpha)) {
				mode = 0;
			}
			if (!HdrShadedValid[mode]) {
				pvr_poly_cxt_t scxt;
				pvr_poly_cxt_col(&scxt, PVR_LIST_TR_POLY);
				scxt.gen.culling = PVR_CULLING_NONE;
				scxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
				scxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				switch (FixedFunctionPass::BlendMode(mode)) {
					case FixedFunctionPass::BlendMode::Additive:
						scxt.blend.src = PVR_BLEND_SRCALPHA;
						scxt.blend.dst = PVR_BLEND_ONE;
						break;
					case FixedFunctionPass::BlendMode::Opaque:
						scxt.blend.src = PVR_BLEND_ONE;
						scxt.blend.dst = PVR_BLEND_ZERO;
						break;
					case FixedFunctionPass::BlendMode::Alpha:
						scxt.blend.src = PVR_BLEND_SRCALPHA;
						scxt.blend.dst = PVR_BLEND_INVSRCALPHA;
						break;
					default:
						scxt.blend.src = pvr_blend_mode_t(MaterialBlendSrc);
						scxt.blend.dst = pvr_blend_mode_t(MaterialBlendDst);
						break;
				}
				pvr_poly_compile(&HdrShaded[mode], &scxt);
				HdrShadedValid[mode] = true;
			}
			nCine::RHI::PVR::SubmitStripShaded(HdrShaded[mode], StripX, StripY, count, StripArgb);
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::PVR itself.
#include "../../../../Shaders/Generated/PvrGeneratedEffects.h"

namespace nCine::RHI::PVR
{
	const FixedFunctionGeneratedEffect* PvrDevice::FindGeneratedEffect(const char* program, const char* variant)
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

	PvrDevice::BlendingState PvrDevice::_blending;
	PvrDevice::DepthTestState PvrDevice::_depthTest;
	PvrDevice::CullFaceState PvrDevice::_cullFace;
	PvrDevice::ScissorState PvrDevice::_scissor;
	Recti PvrDevice::_viewport(0, 0, 0, 0);
	Colorf PvrDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	PvrShaderProgram* PvrDevice::_currentProgram = nullptr;
	const PvrTexture* PvrDevice::_boundTextures[PvrDevice::MaxTextureUnits] = {};
	PvrDevice::UniformRange PvrDevice::_boundUniformRanges[PvrDevice::MaxUniformBindings] = {};
	PvrRenderTarget* PvrDevice::_currentRenderTarget = nullptr;

	bool PvrDevice::_pvrInitialized = false;
	std::int32_t PvrDevice::_logicalWidth = 640;
	std::int32_t PvrDevice::_logicalHeight = 480;
	PvrDevice::SceneTarget PvrDevice::_sceneTarget = PvrDevice::SceneTarget::None;
	std::uint32_t PvrDevice::_sceneCounter = 0;
	PvrRenderTarget* PvrDevice::_sceneRenderTarget = nullptr;

	PvrTexture* PvrDevice::_paletteTexture = nullptr;
	std::uint32_t PvrDevice::_paletteGeneration = 1;
	PvrDevice::PaletteBank PvrDevice::_paletteBanks[PvrDevice::MaxPaletteBanks] = {};
	std::uint32_t PvrDevice::_paletteUseCounter = 0;

	std::vector<PvrDevice::PendingSoftwareLight> PvrDevice::_pendingSoftwareLights;

	pvr_ptr_t PvrDevice::_lightmapVram = nullptr;
	std::size_t PvrDevice::_lightmapVramSize = 0;
	std::int32_t PvrDevice::_lightmapW = 0;
	std::int32_t PvrDevice::_lightmapH = 0;

	// ------------------------------------------------------------------ session

	void PvrDevice::InitializePvr()
	{
		if (_pvrInitialized) {
			return;
		}

		// Everything renders through the translucent list with autosort DISABLED, so the list preserves
		// submission order - the engine's painter's-order queue maps 1:1. Splitting cutout sprites into
		// the punch-through list was evaluated and REJECTED: the CLX2 renders PT in its own per-tile
		// phase BEFORE the translucent pass, so a PT sprite would fall under every TR draw regardless
		// of submission order (the background layers alone are TR quads under all sprites), and with
		// depth compare ALWAYS / writes off there is no Z to arbitrate - see the ordering analysis in
		// Docs/FixedFunctionShaderDesign.md section 5.
		pvr_init_params_t params = {
			// Opaque, opaque modifier, translucent, translucent modifier, punch-through
			{ PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0 },
			512 * 1024,		// Vertex buffer size
			0,				// DMA disabled (store-queue submission)
			0,				// No FSAA
			1,				// Autosort DISABLED (submission order = draw order)
			2				// Extra OPB overflow buffers
		};
		pvr_init(&params);
		pvr_set_bg_color(0.0f, 0.0f, 0.0f);
		pvr_set_pal_format(PVR_PAL_ARGB8888);

		_pvrInitialized = true;
	}

	void PvrDevice::EnsureScene()
	{
		const SceneTarget wanted = (_currentRenderTarget != nullptr ? SceneTarget::RenderTexture : SceneTarget::Screen);
		if (_sceneTarget == wanted && (wanted != SceneTarget::RenderTexture || _sceneRenderTarget == _currentRenderTarget)) {
			return;
		}
		FinishScene();

		pvr_wait_ready();
		if (wanted == SceneTarget::RenderTexture) {
			PvrTexture* texture = _currentRenderTarget->GetColorTexture(0);
			if (texture == nullptr || texture->GetVramPointer() == nullptr) {
				return;		// No surface to render into; draws will be skipped
			}
			// Render-to-texture scene into the target's RGB565 surface. Deliberately not through
			// pvr_scene_begin_txr(): that wrapper is deprecated, and it forwards the *screen* dimensions as
			// the render size while using the width it is given only as the stride. For any target narrower
			// than the display that trips the "stride < width" check inside pvr_scene_begin_rtt(), which
			// then returns without starting a scene at all - so nothing was ever rendered and the target
			// kept whatever its memory held.
			const std::uint32_t renderWidth = std::uint32_t(texture->GetPaddedWidth());
			const std::uint32_t renderHeight = std::uint32_t(texture->GetPaddedHeight());
			if (pvr_scene_begin_rtt(texture->GetVramPointer(), renderWidth, renderHeight, renderWidth) < 0) {
				LOGE("Cannot start a render-to-texture scene for a {}x{} target", renderWidth, renderHeight);
				return;
			}
			_sceneRenderTarget = _currentRenderTarget;
		} else {
			pvr_scene_begin();
			_sceneRenderTarget = nullptr;
		}
		pvr_list_begin(PVR_LIST_TR_POLY);
		// A new list starts with no polygon-header state in the tile accelerator
		InvalidateSubmittedHeader();
		_sceneTarget = wanted;
	}

	void PvrDevice::FinishScene()
	{
		if (_sceneTarget == SceneTarget::None) {
			return;
		}
		pvr_list_finish();
		pvr_scene_finish();
		_sceneTarget = SceneTarget::None;
		_sceneRenderTarget = nullptr;
		_sceneCounter++;
	}

	void PvrDevice::PresentFrame()
	{
		if (!_pvrInitialized) {
			return;
		}
		if (_sceneTarget == SceneTarget::None) {
			// Nothing was drawn this frame; run an empty scene to keep the display pacing
			pvr_wait_ready();
			pvr_scene_begin();
			pvr_list_begin(PVR_LIST_TR_POLY);
			InvalidateSubmittedHeader();
			_sceneTarget = SceneTarget::Screen;
		}
		FinishScene();
	}

	void PvrDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	void PvrDevice::GetTargetScale(float& scaleX, float& scaleY, float& offsetX, float& offsetY)
	{
		offsetX = 0.0f;
		offsetY = 0.0f;
		if (_currentRenderTarget != nullptr) {
			// Render-to-texture scenes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (_logicalWidth > 0 ? 640.0f / float(_logicalWidth) : 1.0f);
			scaleY = (_logicalHeight > 0 ? 480.0f / float(_logicalHeight) : 1.0f);
		}
	}

	// ------------------------------------------------------------------ state

	void PvrDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void PvrDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	PvrDevice::BlendingState PvrDevice::GetBlendingState() { return _blending; }
	void PvrDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void PvrDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void PvrDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	PvrDevice::DepthTestState PvrDevice::GetDepthTestState() { return _depthTest; }
	void PvrDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void PvrDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	PvrDevice::CullFaceState PvrDevice::GetCullFaceState() { return _cullFace; }
	void PvrDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	PvrDevice::ScissorState PvrDevice::GetScissorState() { return _scissor; }
	void PvrDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void PvrDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like
		// RenderCommand and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void PvrDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti PvrDevice::GetViewport() { return _viewport; }
	void PvrDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void PvrDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf PvrDevice::GetClearColor() { return _clearColor; }
	void PvrDevice::SetClearColor(const Colorf& color)
	{
		_clearColor = color;
		if (_pvrInitialized) {
			pvr_set_bg_color(color.R, color.G, color.B);
		}
	}

	void PvrDevice::Clear(ClearFlags flags)
	{
		static_cast<void>(flags);
		if (!_pvrInitialized) {
			return;
		}
		if (_currentRenderTarget == nullptr && _sceneTarget == SceneTarget::None) {
			// The first clear of a screen frame is provided for free by the scene background plane -
			// pushing ~300k blended pixels through the translucent pipe for it again would be one of
			// the most expensive draws of the whole frame. Only mid-scene clears (and render targets,
			// which have no reliable background plane) paint the quad below.
			pvr_set_bg_color(_clearColor.R, _clearColor.G, _clearColor.B);
			return;
		}
		// The scene background provides the frame clear; an explicit mid-scene clear draws a flat quad
		EnsureScene();
		if (_sceneTarget == SceneTarget::None) {
			return;
		}
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float w = float(_currentRenderTarget != nullptr ? _viewport.W : _logicalWidth) * scaleX;
		const float h = float(_currentRenderTarget != nullptr ? _viewport.H : _logicalHeight) * scaleY;

		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		cxt.blend.src = PVR_BLEND_ONE;
		cxt.blend.dst = PVR_BLEND_ZERO;
		pvr_poly_hdr_t hdr;
		pvr_poly_compile(&hdr, &cxt);

		const std::uint32_t argb = PackArgb(QuantizeChannel(_clearColor.R), QuantizeChannel(_clearColor.G),
			QuantizeChannel(_clearColor.B), QuantizeChannel(_clearColor.A));
		const float px[4] = { w, w, 0.0f, 0.0f };
		const float py[4] = { 0.0f, h, 0.0f, h };
		const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		SubmitQuad(hdr, px, py, uv, uv, argb);
	}

	// ------------------------------------------------------------------ draw entry points

	void PvrDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void PvrDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void PvrDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void PvrDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle PvrDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void PvrDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool PvrDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void PvrDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void PvrDevice::BindProgram(PvrShaderProgram* program) { _currentProgram = program; }
	PvrShaderProgram* PvrDevice::CurrentProgram() { return _currentProgram; }

	void PvrDevice::BindTexture(std::uint32_t unit, const PvrTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void PvrDevice::UnbindTexture(const PvrTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// Drop palette banks built from the destroyed palette so a stale pointer can never match
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (_paletteBanks[i].Palette == texture) {
				_paletteBanks[i].PaletteOffset = -1;
				_paletteBanks[i].Palette = nullptr;
			}
		}
	}

	const PvrTexture* PvrDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void PvrDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void PvrDevice::SetRenderTarget(PvrRenderTarget* renderTarget)
	{
		// The scene state machine reacts lazily at the next draw (EnsureScene); an in-flight scene for a
		// different target is finished there
		_currentRenderTarget = renderTarget;
	}

	void PvrDevice::UnbindRenderTarget(const PvrRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (_sceneRenderTarget == renderTarget) {
			FinishScene();
		}
	}

	// ------------------------------------------------------------------ palette banks

	void PvrDevice::RegisterPaletteTexture(PvrTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void PvrDevice::NotifyPaletteTextureChanged(PvrTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		_paletteGeneration++;
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (_paletteBanks[i].PaletteOffset >= (firstRow - 1) * 256 && _paletteBanks[i].PaletteOffset < (firstRow + rowCount) * 256) {
				_paletteBanks[i].PaletteOffset = -1;
			}
		}
	}

	std::int32_t PvrDevice::AcquirePaletteBankForRow(const PvrTexture* palette, std::int32_t paletteOffset)
	{
		// The offset is a flat index into the palette texture and does not need to be row-aligned
		// (e.g. the gem gradients pack two palettes into a single 256-entry row). The palette is usually
		// the registered global one, but effects like the profile character previews bind their own
		// recolored palette texture instead.
		const std::int32_t maxOffset = palette != nullptr
			? palette->GetWidth() * palette->GetHeight() - 256 : 0;
		if (palette == nullptr || palette->GetPixels() == nullptr ||
			paletteOffset < 0 || paletteOffset > maxOffset) {
			return -1;
		}

		return AcquirePaletteBank(palette, paletteOffset, palette->GetContentVersion(),
			reinterpret_cast<const std::uint32_t*>(palette->GetPixels()) + paletteOffset);
	}

	std::int32_t PvrDevice::AcquirePaletteBank(const PvrTexture* palette, std::int32_t paletteOffset,
		std::uint32_t version, const std::uint32_t* entries)
	{
		if (palette == nullptr || entries == nullptr) {
			return -1;
		}

		_paletteUseCounter++;

		std::int32_t bank = -1;
		std::uint32_t oldestUse = UINT32_MAX;
		std::int32_t oldestBank = 0;
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (_paletteBanks[i].PaletteOffset == paletteOffset && _paletteBanks[i].Palette == palette &&
				_paletteBanks[i].PaletteVersion == version) {
				bank = std::int32_t(i);
				break;
			}
			if (_paletteBanks[i].LastUse < oldestUse) {
				oldestUse = _paletteBanks[i].LastUse;
				oldestBank = std::int32_t(i);
			}
		}

		if (bank < 0) {
			bank = oldestBank;
			for (std::int32_t i = 0; i < 256; i++) {
				const std::uint32_t rgba = entries[i];
				pvr_set_pal_entry(std::uint32_t(bank) * 256 + std::uint32_t(i),
					PackArgb(std::uint8_t(rgba & 0xFF), std::uint8_t((rgba >> 8) & 0xFF),
						std::uint8_t((rgba >> 16) & 0xFF), std::uint8_t((rgba >> 24) & 0xFF)));
			}
			_paletteBanks[bank].PaletteOffset = paletteOffset;
			_paletteBanks[bank].Palette = palette;
			_paletteBanks[bank].PaletteVersion = version;
		}

		_paletteBanks[bank].LastUse = _paletteUseCounter;
		return bank;
	}

	// ------------------------------------------------------------------ lighting hook

	void PvrDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
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

	void PvrDevice::EndFrame()
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

	void PvrDevice::ApplyPendingSoftwareLighting()
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

		EnsureScene();
		if (_sceneTarget == SceneTarget::None) {
			return;
		}
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			// Multiply factor from the CPU lightmap: out ≈ scene * (r*(1+g) + amb*(1-r)) per channel (the
			// multiply-only approximation shared with the GX backend), as an ARGB4444 texture drawn with a
			// dst * src blend over the viewport
			std::int32_t texW = 8, texH = 8;
			while (texW < light.LmW && texW < 1024) texW <<= 1;
			while (texH < light.LmH && texH < 1024) texH <<= 1;
			const std::size_t size = std::size_t(texW) * std::size_t(texH) * 2;
			bool layoutChanged = (_lightmapW != texW || _lightmapH != texH);
			if (_lightmapVram == nullptr || _lightmapVramSize < size) {
				if (_lightmapVram != nullptr) {
					pvr_mem_free(_lightmapVram);
				}
				_lightmapVram = pvr_mem_malloc(size);
				_lightmapVramSize = size;
				layoutChanged = true;
			}
			if (_lightmapVram != nullptr) {
				_lightmapW = texW;
				_lightmapH = texH;
				// The factors are written straight into video memory as a non-twiddled surface. This is a
				// single screen-aligned quad, so the interleaved texel order would buy nothing at sampling
				// time while costing a full twiddling pass (plus a same-sized staging copy) every frame -
				// the same trade-off the sprite uploads make in PvrTexture::RefreshVramStore().
				std::uint16_t* const surface = static_cast<std::uint16_t*>(_lightmapVram);
				if (layoutChanged) {
					// Only the used LmW x LmH region is rewritten per frame; the padding is sampled through
					// the compensated texture coordinates only at the very edge, and is filled just once.
					// Spelled out as word stores - video memory only takes 16/32-bit accesses, and libc
					// memset does not guarantee that (the size is always a multiple of four here)
					std::uint32_t* DEATH_RESTRICT fill = reinterpret_cast<std::uint32_t*>(surface);
					for (std::size_t i = 0, n = size / 4; i < n; i++) {
						fill[i] = 0xFFFFFFFFu;
					}
				}
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
						prevTexel = std::uint16_t(0xF000 | (fr << 8) | (fg << 4) | fb);
						dst[x] = prevTexel;
					}
				}

				pvr_poly_cxt_t cxt;
				pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
					texW, texH, _lightmapVram, PVR_FILTER_BILINEAR);
				cxt.gen.culling = PVR_CULLING_NONE;
				cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
				cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				cxt.blend.src = PVR_BLEND_DESTCOLOR;	// out = dst * src
				cxt.blend.dst = PVR_BLEND_ZERO;
				pvr_poly_hdr_t hdr;
				pvr_poly_compile(&hdr, &cxt);

				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer convention), so V runs (used/texH) -> 0 top -> bottom
				const float uMax = float(light.LmW) / float(texW);
				const float vMax = float(light.LmH) / float(texH);
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
				const float pu[4] = { uMax, uMax, 0.0f, 0.0f };
				const float pv[4] = { vMax, 0.0f, vMax, 0.0f };
				SubmitQuad(hdr, px, py, pu, pv, PackArgb(255, 255, 255, 255));
			}
		}

		if (hasWater) {
			// Water v1: constant underwater tint band + above-deep-water darkening (shared with GX)
			pvr_poly_cxt_t cxt;
			pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
			cxt.gen.culling = PVR_CULLING_NONE;
			cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
			cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
			cxt.blend.src = PVR_BLEND_SRCALPHA;
			cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
			pvr_poly_hdr_t hdr;
			pvr_poly_compile(&hdr, &cxt);

			const float waterTop = vpY + light.WaterLevelPx * scaleY;
			const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (waterTop < vpY + vpH) {
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { waterTop, vpY + vpH, waterTop, vpY + vpH };
				SubmitQuad(hdr, px, py, uv, uv, PackArgb(102, 153, 204, 102));
			}
			const float waterLevelNorm = (light.VpH > 0 ? light.WaterLevelPx / float(light.VpH) : 1.0f);
			if (waterLevelNorm < 0.4f && waterTop > vpY) {
				const std::uint8_t a = QuantizeChannel(0.4f - waterLevelNorm);
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, waterTop, vpY, waterTop };
				SubmitQuad(hdr, px, py, uv, uv,
					PackArgb(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a));
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void PvrDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const PvrBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const PvrUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		PvrTexture* texture = const_cast<PvrTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pv = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds
		// uTexturePalette in its reflection, which is exactly what UsesPalette() reports - the remap
		// intent needs no effect identity of its own.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const PvrTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the texture
		// residency, the palette bank and the polygon header are resolved once for the entire layer
		pvr_ptr_t vram = nullptr;
		std::uint32_t format = 0;
		if (texture->IsIndexed()) {
			// An 8bpp store can only be read through a palette, whatever it is being drawn with - the lookup
			// belongs to the texture read rather than to the effect. An effect that remaps takes the row from
			// the instance; anything else (the fonts, which are palette indices too) uses the base row.
			std::int32_t paletteOffset = 0;
			if (isPaletteRemap) {
				float palOffset = 0.0f;
				std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
				paletteOffset = std::int32_t(palOffset + 0.5f);
			}
			std::int32_t bank = AcquirePaletteBankForRow(paletteTex, paletteOffset);
			if (bank < 0) {
				bank = 0;
			}
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat() | PVR_TXRFMT_8BPP_PAL(std::uint32_t(bank));
		} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels()) + paletteOffset;
			vram = texture->EnsureBakedArgb4444(entries, paletteOffset,
				(paletteTex == _paletteTexture ? _paletteGeneration : paletteTex->GetContentVersion()), paletteTex);
			format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
		} else {
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat();
		}
		if (vram == nullptr) {
			return;
		}

		EnsureScene();
		if (_sceneTarget == SceneTarget::None) {
			return;
		}

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const bool screenPass = (_currentRenderTarget == nullptr);
		const float uvScaleU = texture->GetUScale();
		const float uvScaleV = texture->GetVScale();

		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, int(format), texture->GetPaddedWidth(), texture->GetPaddedHeight(),
			vram, (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? PVR_FILTER_BILINEAR : PVR_FILTER_NEAREST));
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		cxt.blend.src = (_blending.Enabled ? pvr_blend_mode_t(MapBlendPvr(_blending.SrcRgb)) : PVR_BLEND_ONE);
		cxt.blend.dst = (_blending.Enabled ? pvr_blend_mode_t(MapBlendPvr(_blending.DstRgb)) : PVR_BLEND_ZERO);
		cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
		pvr_poly_hdr_t hdr;
		pvr_poly_compile(&hdr, &cxt);

		const bool clipActive = (_scissor.Enabled && screenPass);
		float clipX0 = 0.0f, clipY0 = 0.0f, clipX1 = 0.0f, clipY1 = 0.0f;
		if (clipActive) {
			clipX0 = float(_scissor.Rect.X) * scaleX + offsetX;
			clipY0 = float(_scissor.Rect.Y) * scaleY + offsetY;
			clipX1 = float(_scissor.Rect.X + _scissor.Rect.W) * scaleX + offsetX;
			clipY1 = float(_scissor.Rect.Y + _scissor.Rect.H) * scaleY + offsetY;
		}

		// Projects one mesh vertex into raster space, matching the sprite path's corner synthesis
		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex - every vertex then costs one multiply-add
		// per axis. A screen pass mirrors NDC, which is just the sign of the Y scale (see below).
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU;
			outV = v[3] * uvScaleV;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the
		// four clamp+float-to-int quantizations run once per change instead of once per tile
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		std::uint32_t lastArgb = 0;
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the third and fourth repeat
			// the first and third. Recognizing that pattern lets a tile go out as a single four-vertex
			// strip rather than two three-vertex ones, which is a third less vertex traffic and half the
			// polygon headers. Anything that doesn't match is emitted as plain triangles.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			float px[4], py[4], pu[4], pvv[4];
			std::int32_t cornerCount;
			if (isQuad) {
				// Strip order (see SubmitQuad): the two corners of one edge, then the two of the opposite
				// one - vertices 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(group + std::size_t(QuadOrder[i]) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				cornerCount = 4;
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				cornerCount = 3;
				triangle++;
			}

			if (clipActive) {
				// The bounding-box reject is exact for the fully outside case
				float minX = px[0], maxX = px[0], minY = py[0], maxY = py[0];
				for (std::int32_t i = 1; i < cornerCount; i++) {
					minX = std::min(minX, px[i]); maxX = std::max(maxX, px[i]);
					minY = std::min(minY, py[i]); maxY = std::max(maxY, py[i]);
				}
				if (maxX <= clipX0 || minX >= clipX1 || maxY <= clipY0 || minY >= clipY1) {
					continue;
				}
				// A tile straddling the scissor edge is clipped exactly like the sprite path clips its
				// axis-aligned quads: there is no hardware scissor on this tier, and on the splitscreen
				// boundary an unclipped tile would draw up to a full tile into the other player's viewport.
				// The corner-sharing test mirrors the sprite path; anything else (the raw-triangle fallback,
				// a rotated layer) keeps the conservative bounding-box reject above.
				if (cornerCount == 4 && px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]) {
					float xA = px[2], xB = px[0], uA = pu[2], uB = pu[0];
					if (!ClipQuadEdge(xA, xB, uA, uB, clipX0, clipX1)) {
						continue;
					}
					px[2] = px[3] = xA; px[0] = px[1] = xB;
					pu[2] = pu[3] = uA; pu[0] = pu[1] = uB;
					float yA = py[0], yB = py[1], vA = pvv[0], vB = pvv[1];
					if (!ClipQuadEdge(yA, yB, vA, vB, clipY0, clipY1)) {
						continue;
					}
					py[0] = py[2] = yA; py[1] = py[3] = yB;
					pvv[0] = pvv[2] = vA; pvv[1] = pvv[3] = vB;
				}
			}

			// Every vertex of a tile carries the same colour, so it only has to be packed once per change
			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				lastArgb = PackArgb(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}
			SubmitStrip(hdr, px, py, pu, pvv, cornerCount, lastArgb);
		}
	}

	void PvrDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy,
		// texcoords.uv) - the layout the MeshSprite shader's attributes declare. The tile accelerator
		// has no line primitive, so every segment goes out as a quad half a pixel to each side of the
		// line, which is what the 1-wide GL lines this stands in for rasterize to.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2) {
			return;
		}

		const PvrBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const PvrUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		PvrTexture* texture = const_cast<PvrTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pv = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is packed once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));
		const std::uint32_t argb = PackArgb(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
			QuantizeChannel(color[2]), QuantizeChannel(color[3]));

		// The strip shares one texture, so residency (with the palette bank for indexed assets, which
		// use the base row like the fonts do) and the polygon header are resolved once
		pvr_ptr_t vram = nullptr;
		std::uint32_t format = 0;
		if (texture->IsIndexed()) {
			const PvrTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			std::int32_t bank = AcquirePaletteBankForRow(paletteTex, 0);
			if (bank < 0) {
				bank = 0;
			}
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat() | PVR_TXRFMT_8BPP_PAL(std::uint32_t(bank));
		} else {
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat();
		}
		if (vram == nullptr) {
			return;
		}

		EnsureScene();
		if (_sceneTarget == SceneTarget::None) {
			return;
		}

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const bool screenPass = (_currentRenderTarget == nullptr);
		const float uvScaleU = texture->GetUScale();
		const float uvScaleV = texture->GetVScale();

		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, int(format), texture->GetPaddedWidth(), texture->GetPaddedHeight(),
			vram, (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? PVR_FILTER_BILINEAR : PVR_FILTER_NEAREST));
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		cxt.blend.src = (_blending.Enabled ? pvr_blend_mode_t(MapBlendPvr(_blending.SrcRgb)) : PVR_BLEND_ONE);
		cxt.blend.dst = (_blending.Enabled ? pvr_blend_mode_t(MapBlendPvr(_blending.DstRgb)) : PVR_BLEND_ZERO);
		cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
		// Match the desktop sampler's ClampToEdge (the engine's texture default): with the KOS default
		// wrap mode, the strip's V=0 bilinear fetch on the power-of-two-padded texture blends row 0
		// half-and-half with the zeroed bottom padding row, halving both the colour and the alpha of
		// the whole line (the weapon-wheel gradient is a 400x1 image padded to 512x8)
		cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
		pvr_poly_hdr_t hdr;
		pvr_poly_compile(&hdr, &cxt);

		const bool clipActive = (_scissor.Enabled && screenPass);
		float clipX0 = 0.0f, clipY0 = 0.0f, clipX1 = 0.0f, clipY1 = 0.0f;
		if (clipActive) {
			clipX0 = float(_scissor.Rect.X) * scaleX + offsetX;
			clipY0 = float(_scissor.Rect.Y) * scaleY + offsetY;
			clipX1 = float(_scissor.Rect.X + _scissor.Rect.W) * scaleX + offsetX;
			clipY1 = float(_scissor.Rect.Y + _scissor.Rect.H) * scaleY + offsetY;
		}

		// The NDC-to-raster mapping is folded into the transform once, like the other mesh paths
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		const float pixelScale = std::max(scaleX, scaleY);

		float prevX = raster.Xx * vertices[0] + raster.Yx * vertices[1] + raster.Tx;
		float prevY = raster.Xy * vertices[0] + raster.Yy * vertices[1] + raster.Ty;
		float prevU = vertices[2] * uvScaleU;
		float prevV = vertices[3] * uvScaleV;
		for (std::int32_t i = 1; i < numVertices; i++) {
			const float* v = vertices + std::size_t(i) * FloatsPerVertex;
			const float curX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			const float curY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			const float curU = v[2] * uvScaleU;
			const float curV = v[3] * uvScaleV;

			const float dx = curX - prevX, dy = curY - prevY;
			const float len2 = dx * dx + dy * dy;
			if (len2 > 0.000001f) {
				const float len = std::sqrt(len2);
				// GL's line rasterization guarantees an unbroken one-pixel chain whatever the slope; a
				// quad exactly one pixel wide covers too few pixel centres on diagonals and the line
				// comes out dashed and dimmer. Widening by the slope's Manhattan factor (1 for axis
				// aligned, sqrt(2) at 45 degrees) restores the same continuous coverage.
				const float halfWidth = 0.5f * pixelScale * (std::abs(dx) + std::abs(dy)) / len;
				bool visible = true;
				if (clipActive) {
					// Segments are about a pixel wide, so a conservative reject is enough - exact
					// clipping could never make a visible difference
					const float minX = std::min(prevX, curX) - halfWidth, maxX = std::max(prevX, curX) + halfWidth;
					const float minY = std::min(prevY, curY) - halfWidth, maxY = std::max(prevY, curY) + halfWidth;
					visible = !(maxX <= clipX0 || minX >= clipX1 || maxY <= clipY0 || minY >= clipY1);
				}
				if (visible) {
					// Perpendicular of the segment, half a (slope-compensated) pixel long
					const float invLen = halfWidth / len;
					const float nx = -dy * invLen;
					const float ny = dx * invLen;

					const float px[4] = { prevX + nx, prevX - nx, curX + nx, curX - nx };
					const float py[4] = { prevY + ny, prevY - ny, curY + ny, curY - ny };
					const float pu[4] = { prevU, prevU, curU, curU };
					const float pvv[4] = { prevV, prevV, curV, curV };
					SubmitQuad(hdr, px, py, pu, pvv, argb);
				}
			}

			prevX = curX; prevY = curY; prevU = curU; prevV = curV;
		}
	}

	void PvrDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		static_cast<void>(firstVertex);
		if (_currentProgram == nullptr || numVertices <= 0 || !_pvrInitialized) {
			return;
		}

		// The program's whole console identity is its generated-table entry, resolved at load from the
		// true (program, variant) the loaders plumbed in - a program without an entry has no
		// fixed_function block in its .shader file (LightingMesh, Blur, the Resize* family,
		// runtime-compiled shaders, ...) and keeps the logged, skipped draw.
		const FixedFunctionGeneratedEffect* generated = _currentProgram->GetGeneratedEffect();
		if (generated == nullptr) {
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
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the PVR dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family: a transpiled effect function
		// (geometry synthesis included - the iris and the warp are ordinary blocks since phase 4)
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the PVR dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		// Resolved once at introspection (see DispatchFacts) - this used to re-scan the
		// reflection's name strings on every RenderCommand
		const PvrUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		const PvrShaderProgram::DispatchFacts& facts = _currentProgram->GetDispatchFacts();
		std::uint32_t instanceStride = facts.InstanceStride;

		const float* pv = CachedProjView(projMat, viewMat);

		// Batched programs are exactly the ones whose reflection declares a BATCH_SIZE-strided
		// InstancesBlock (non-batched programs use a flat InstanceBlock with no stride), so the
		// reflected stride IS the batching signal - no per-program identity needed
		const bool batched = (instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
			if (instanceStride == 0) {
				instanceStride = 112;
			}
		}

		// A program samples the sprite texture exactly when its reflection binds uTexture - the
		// no-texture sprite programs and the Transition (which carries texRect in its block but
		// samples nothing, hence the separate layout flag) simply do not declare it
		const bool hasTexture = facts.HasTexture;
		// The instance layout follows the block's own reflected declaration rather than any effect
		// identity: a block that declares texRect uses the textured member offsets whether or not
		// the program samples a texture (the Transition carries texRect but samples nothing)
		const bool texturedLayout = facts.TexturedLayout;
		// Every effect that samples indexed sprites through the palette texture binds uTexturePalette
		// in its reflection, which is what UsesPalette() reports (PaletteRemap and the "...Palette"
		// variants of the actor state effects alike)
		const bool isPaletteRemap = _currentProgram->UsesPalette();

		// The offset colour is added after texturing only on polygons compiled with specular enabled,
		// which is a per-program property of the base material - so it comes from the generated
		// table's static analysis (does any pass of the effect write p.offset_color?), not from a pass
		const bool usesOffsetColor = generated->UsesOffsetColor;
		// Same static analysis, for the optional context facilities: the flags record exactly which
		// builtin families the transpiled function calls, so the setup that only feeds an uncalled
		// facility is skipped below. Skipping is invisible to the effect by construction - it cannot
		// read what it never calls - so every submitted primitive stays bit-identical.
		const FixedFunctionRequirements reqs = generated->Requirements;
		const bool needsTexelStep = ((reqs & FixedFunctionRequirements::NeedsTexelStep) == FixedFunctionRequirements::NeedsTexelStep);
		const bool needsUniforms = ((reqs & FixedFunctionRequirements::NeedsUniforms) == FixedFunctionRequirements::NeedsUniforms);
		const bool needsStripBuilder = ((reqs & FixedFunctionRequirements::NeedsStripBuilder) == FixedFunctionRequirements::NeedsStripBuilder);
		const bool needsQuadAxes = ((reqs & FixedFunctionRequirements::NeedsQuadAxes) == FixedFunctionRequirements::NeedsQuadAxes);
		const std::int32_t textureUnit = facts.TextureUnit;
		PvrTexture* texture = const_cast<PvrTexture*>(hasTexture
			? _boundTextures[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		if (hasTexture && texture == nullptr) {
			return;
		}

		// The palette to remap with is whatever the material bound to the palette sampler (e.g. the
		// recolored preview palettes of the profile menu); the registered global palette is the fallback
		const PvrTexture* paletteTex = nullptr;
		if (isPaletteRemap || (texture != nullptr && texture->IsIndexed())) {
			const std::int32_t paletteUnit = facts.PaletteUnit;
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		EnsureScene();
		if (_sceneTarget == SceneTarget::None) {
			return;
		}

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);

		const std::int32_t blendSrc = (_blending.Enabled ? MapBlendPvr(_blending.SrcRgb) : PVR_BLEND_ONE);
		const std::int32_t blendDst = (_blending.Enabled ? MapBlendPvr(_blending.DstRgb) : PVR_BLEND_ZERO);
		const std::int32_t filter = (hasTexture && texture->GetMagFilter() == nCine::SamplerFilter::Linear
			? PVR_FILTER_BILINEAR : PVR_FILTER_NEAREST);

		// The build context outlives the compile so EffectContext::SubmitQuad can derive blend twins
		// (the additive passes of Colorized, an opaque pass) from the compiled base state on demand
		pvr_poly_cxt_t cxt;
		pvr_poly_hdr_t hdr;
		bool hdrValid = false;
		pvr_ptr_t lastVram = nullptr;
		std::int32_t lastBank = -2;
		// Blend twins of the same polygon, compiled lazily for effects that build their result out
		// of several differently blended passes
		pvr_poly_hdr_t hdrAdditive;
		bool hdrAdditiveValid = false;
		pvr_poly_hdr_t hdrOpaque;
		bool hdrOpaqueValid = false;
		pvr_poly_hdr_t hdrAlpha;
		bool hdrAlphaValid = false;
		// Colour-polygon twins for shaded strips, one per BlendMode; they depend only on the
		// material blend factors (constant across the draw), so they are compiled at most once per
		// draw and never invalidated by texture-variant changes
		pvr_poly_hdr_t hdrShaded[4];
		bool hdrShadedValid[4] = { false, false, false, false };

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract;
		// the effects that need an on-screen texel step derive it from the instance colour instead,
		// exactly like their GLSL does). Derived only for effects flagged with the texel-size
		// facility - everything else gets deterministic zeros without the divides.
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the PVR scans out its buffer top-down directly, so screen passes mirror NDC here
		// instead (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is
		// what the sampling passes already expect - which is just the sign of the raster Y scale below.
		const bool screenPass = (_currentRenderTarget == nullptr);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;

		// The PVR rasterizer has no scissor for the general case, so scissored quads are clipped
		// geometrically. The rect maps to raster coordinates the same way the vertices do (screen passes
		// mirror NDC, so the engine rect's Y addresses raster rows directly - see the GX device); only
		// screen passes are clipped, which covers every scissor user on this tier (menu clipping,
		// splitscreen viewports)
		const bool clipActive = (_scissor.Enabled && screenPass);
		float clipX0 = 0.0f, clipY0 = 0.0f, clipX1 = 0.0f, clipY1 = 0.0f;
		if (clipActive) {
			clipX0 = float(_scissor.Rect.X) * scaleX + offsetX;
			clipY0 = float(_scissor.Rect.Y) * scaleY + offsetY;
			clipX1 = float(_scissor.Rect.X + _scissor.Rect.W) * scaleX + offsetX;
			clipY1 = float(_scissor.Rect.Y + _scissor.Rect.H) * scaleY + offsetY;
		}

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

			// Select this instance's texture variant and (re)compile the poly header when it changes
			float uvScaleU = 1.0f, uvScaleV = 1.0f;
			if (hasTexture) {
				pvr_ptr_t vram = nullptr;
				std::uint32_t format = 0;
				std::int32_t bank = -1;
				if (texture->IsIndexed()) {
					// See the mesh path: a paletted store needs a bank selected under every effect
					std::int32_t paletteOffset = 0;
					if (isPaletteRemap) {
						float palOffset = 0.0f;
						std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
						paletteOffset = std::int32_t(palOffset + 0.5f);
					}
					bank = AcquirePaletteBankForRow(paletteTex, paletteOffset);
					if (bank < 0) {
						bank = 0;
					}
					vram = texture->AcquireVramPointer();
					format = texture->GetVramFormat() | PVR_TXRFMT_8BPP_PAL(std::uint32_t(bank));
				} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
					const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
						paletteTex->GetPixels()) + paletteOffset;
					vram = texture->EnsureBakedArgb4444(entries, paletteOffset,
						(paletteTex == _paletteTexture ? _paletteGeneration : paletteTex->GetContentVersion()), paletteTex);
					format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
				} else {
					vram = texture->AcquireVramPointer();
					format = texture->GetVramFormat();
				}
				if (vram == nullptr) {
					continue;
				}
				uvScaleU = texture->GetUScale();
				uvScaleV = texture->GetVScale();
				if (!hdrValid || vram != lastVram || bank != lastBank) {
					pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, int(format),
						texture->GetPaddedWidth(), texture->GetPaddedHeight(), vram, pvr_filter_mode_t(filter));
					cxt.gen.culling = PVR_CULLING_NONE;
					cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
					cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
					cxt.blend.src = pvr_blend_mode_t(blendSrc);
					cxt.blend.dst = pvr_blend_mode_t(blendDst);
					cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
					// The offset colour is added to the texturing result, which is how the actor state
					// effects brighten and tint the sprite (see the generated effect functions)
					if (usesOffsetColor) {
						cxt.gen.specular = PVR_SPECULAR_ENABLE;
					}
					pvr_poly_compile(&hdr, &cxt);
					// The compiled base changed, so any blend twins derived from it are stale
					hdrAdditiveValid = false;
					hdrOpaqueValid = false;
					hdrAlphaValid = false;
					hdrValid = true;
					lastVram = vram;
					lastBank = bank;
				}
			} else if (!hdrValid) {
				pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
				cxt.gen.culling = PVR_CULLING_NONE;
				cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
				cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				cxt.blend.src = pvr_blend_mode_t(blendSrc);
				cxt.blend.dst = pvr_blend_mode_t(blendDst);
				pvr_poly_compile(&hdr, &cxt);
				hdrValid = true;
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform (see DispatchTileMesh) so a corner costs one
			// multiply-add per axis. The corner weights are 0 or 1, so the sprite's extent in raster space
			// is just the transformed axes scaled by its size, and the corners are sums of those.
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
				pu[i] = (ax * texRect[0] + texRect[1]) * uvScaleU;
				pvv[i] = (ay * texRect[2] + texRect[3]) * uvScaleV;
			}

			if (clipActive) {
				// Corners 2/3 share the left edge and 0/1 the right one (ax); 0/2 share the top edge and
				// 1/3 the bottom one (ay) - see the corner synthesis above
				const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
				if (axisAligned) {
					float xA = px[2], xB = px[0], uA = pu[2], uB = pu[0];
					if (!ClipQuadEdge(xA, xB, uA, uB, clipX0, clipX1)) {
						continue;
					}
					px[2] = px[3] = xA; px[0] = px[1] = xB;
					pu[2] = pu[3] = uA; pu[0] = pu[1] = uB;
					float yA = py[0], yB = py[1], vA = pvv[0], vB = pvv[1];
					if (!ClipQuadEdge(yA, yB, vA, vB, clipY0, clipY1)) {
						continue;
					}
					py[0] = py[2] = yA; py[1] = py[3] = yB;
					pvv[0] = pvv[2] = vA; pvv[1] = pvv[3] = vB;
				} else {
					// Rotated quad: conservative bounding-box reject only (exact clipping of rotated
					// sprites is not worth it for the scissor users on this tier)
					const float minX = std::min(std::min(px[0], px[1]), std::min(px[2], px[3]));
					const float maxX = std::max(std::max(px[0], px[1]), std::max(px[2], px[3]));
					const float minY = std::min(std::min(py[0], py[1]), std::min(py[2], py[3]));
					const float maxY = std::max(std::max(py[0], py[1]), std::max(py[2], py[3]));
					if (maxX <= clipX0 || minX >= clipX1 || maxY <= clipY0 || minY >= clipY1) {
						continue;
					}
				}
			}

			// The pass descriptors the per-effect functions declare are mapped onto this instance's
			// corners and the compiled material state through the context
			EffectContext ctx;
			ctx.InstanceColor = color;
			ctx.TexelW = texelWidth;
			ctx.TexelH = texelHeight;
			ctx.Batched = batched;
			ctx.Hdr = &hdr;
			ctx.BaseCxt = &cxt;
			ctx.HdrAdditive = &hdrAdditive;
			ctx.HdrAdditiveValid = &hdrAdditiveValid;
			ctx.HdrOpaque = &hdrOpaque;
			ctx.HdrOpaqueValid = &hdrOpaqueValid;
			ctx.HdrAlpha = &hdrAlpha;
			ctx.HdrAlphaValid = &hdrAlphaValid;
			ctx.Px = px;
			ctx.Py = py;
			ctx.Pu = pu;
			ctx.Pv = pvv;
			ctx.TexRect = texRect;
			// The optional context facilities are only wired up for effects whose static analysis
			// says they can call them (see reqs above); the loop-invariant conditions predict
			// perfectly, and members of an unused facility are simply never read
			if (needsQuadAxes) {
				ctx.OriginX = originX;
				ctx.OriginY = originY;
				ctx.AxisXx = spanXx;
				ctx.AxisXy = spanXy;
				ctx.AxisYx = spanYx;
				ctx.AxisYy = spanYy;
			}
			// Resolved uniforms are the only thing the context needs the program for, so effects
			// without the facility get no program plumbed at all (no resolution can ever run)
			ctx.Program = (needsUniforms ? _currentProgram : nullptr);
			if (needsStripBuilder) {
				// The shaded-strip Material twin and the strip UV scale exist only for the strip
				// builder; the material blend factors feed that twin's compilation
				ctx.MaterialBlendSrc = blendSrc;
				ctx.MaterialBlendDst = blendDst;
				ctx.UvScaleU = uvScaleU;
				ctx.UvScaleV = uvScaleV;
				ctx.HdrShaded = hdrShaded;
				ctx.HdrShadedValid = hdrShadedValid;
			}

			// Every quad-family effect is the transpiled form of its shader's fixed_function block
			// (masks, outline, shields, colorized, palette remap, the default sprites - batched
			// twins and palette variants included - and the geometry-synthesized iris and warp)
			generated->Fn(ctx);
		}
	}
}
