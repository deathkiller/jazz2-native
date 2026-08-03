#pragma once

#include <Common.h>

#include <cstdint>

namespace nCine::RHI
{
	/**
		@brief One pass of a fixed-function effect, as described by a shader's `fixed_function` block

		The fixed-function backends (PVR on the Dreamcast, GX on the Wii/GameCube) have no fragment
		shaders; every effect is a short list of passes over the same sprite quad, each pass a small
		bundle of hardware state. This struct is that bundle, with concrete values: the generated
		effect code (see `Docs/FixedFunctionShaderDesign.md`) computes the fields at draw time and
		hands the pass to the backend's `EffectContext`, which turns it into a polygon header (PVR)
		or a TEV/blend setup (GX) and submits the quad.

		Fields a backend has no hardware for are ignored by it (`Gx*` on the PVR and the other way
		round), which is also what makes one shared description usable by both. Everything defaults
		to a plain modulated sprite pass with the material's own blending.
	*/
	struct FixedFunctionPass
	{
		/** @brief How the pass blends over what is already in the frame */
		enum class BlendMode : std::uint8_t
		{
			Material,		/**< Whatever the material configured (the default) */
			/**
				Additive blending for glow and split-multiplier passes. The GX maps it to `ONE + ONE`;
				the PVR deliberately maps it to `SRCALPHA + ONE` — the additive mechanism the
				split-multiplier passes (Colorized) have always used there, whose contributions are
				scaled by the pass alpha, so the mapping stays bit-identical with the handwritten code.
			*/
			Additive,
			Opaque,			/**< `ONE + ZERO` */
			/**
				Standard source-alpha over (`SRCALPHA + INVSRCALPHA`), independent of whatever the
				material configured — the horizon tint of the TexturedBackground warp runs over a
				material whose own blend does not apply to that pass.
			*/
			Alpha
		};

		/** @brief GX texture-environment preset for this pass (the PVR always modulates) */
		enum class TevPreset : std::uint8_t
		{
			Modulate,		/**< `texture * vertex colour` (the default) */
			Silhouette,		/**< Vertex colour where the texture has alpha (flat masks, shadows, glows) */
			ModulateX2,		/**< Modulate with the combiner's x2 output scale */
			ModulateX4,		/**< Modulate with the combiner's x4 output scale */
			/**
				`mix(texel, colour, alpha)` with an opaque result - the texel lerped toward the pass
				colour by the pass alpha. GX ONLY (one combiner stage `d + mix(a, b, c)`; the PVR has no
				texture environment that can lerp a texel toward a colour, so the transpiler rejects it
				outside a `void fixed_function(gx)` block).

				With a shaded strip both terms come from the per-vertex colour, which is how the
				TexturedBackground warp folds its horizon tint into the band's own draw instead of
				laying a second gradient pass over it.
			*/
			TintMix,
			/**
				Silhouette whose colour is a per-texel ramp instead of a flat tone: `grey` is the texel's
				Rec.601 luminance amplified by @ref LumaGain and saturated, and the colour is
				`mix(Color.rgb, OffsetColor, grey)` - i.e. @ref Color carries the tone at `grey = 0` and
				@ref OffsetColor the tone at `grey = 1`. Coverage stays the silhouette's `texel alpha *
				Color.a`. GX ONLY (channel swizzles through the TEV swap tables plus a KONST-weighted
				dot product; the PVR has no per-texel arithmetic at all, so the transpiler rejects it
				outside a `void fixed_function(gx)` block).
			*/
			LumaRamp
		};

		/** @brief Per-vertex colour of the pass (the PVR `argb`, the GX raster colour), premultiplied by nothing */
		float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		/** @brief Post-texture additive term (the PVR offset colour; the GX runs a silhouette pass instead) */
		float OffsetColor[3] = { 0.0f, 0.0f, 0.0f };
		/** @brief Whether @ref OffsetColor is used at all (enables `PVR_SPECULAR` on the polygon) */
		bool HasOffsetColor = false;
		/** @brief Displacement of the whole quad in the quad's own coordinate space (the Outline ring taps) */
		float ScreenOffset[2] = { 0.0f, 0.0f };
		/**
			@brief How much @ref TevPreset::LumaRamp amplifies the texel's luminance before saturating it

			Only read by that preset (the GLSL it approximates saturates a scaled luma - FrozenMask's
			`min(luma * 2.6, 1.0)`). The GX folds it into the KONST-held luminance weights and the
			combiner's output scale, so any value up to 4 costs nothing extra.
		*/
		float LumaGain = 1.0f;
		/** @brief Blend override for this pass */
		BlendMode Blend = BlendMode::Material;
		/** @brief GX combiner preset */
		TevPreset Tev = TevPreset::Modulate;
	};

	/**
		@brief Backend pipeline stage a shader binds itself to with a `pipeline <name>;` fixed_function block

		A few programs do not describe shading at all - they feed engine data structures (the tile-layer
		vertex stream, the weapon-wheel line strip) or hook a compositor stage (the CPU-lightmap
		lighting) whose implementation is backend mechanism, not effect policy. Their shader files still
		declare WHICH stage they are (so no shader name ever has to be matched in a backend), and the
		generated tables carry that declaration here instead of a transpiled function. The
		geometry-synthesized quad effects (the transition iris, the warped background) are NOT
		intrinsics anymore - since migration phase 4 they are ordinary transpiled blocks built on the
		strip-builder half of the contract below.
	*/
	enum class FixedFunctionIntrinsic : std::uint8_t
	{
		None,						/**< Not an intrinsic - the entry carries a transpiled effect function instead */
		TileMapMesh,				/**< A whole tile layer as one triangle-list mesh (8-float `TileMap::AppendTileQuad` contract) */
		LightingCombine,			/**< The viewport compositor - the direct-tier CPU-lightmap lighting hook */
		LineStripMesh				/**< Vertex-fed textured line strip (the weapon wheel) */
	};

	/**
		@brief Optional `EffectContext` facilities a generated effect function can ever call, as single-bit flags

		Computed statically by the fixed-function transpiler while it emits the function (a bit is set
		exactly when the corresponding builtin family appears in the emitted code) and carried in each
		generated table entry, so a backend's `Dispatch` can skip the per-draw/per-instance
		`EffectContext` setup that only feeds a facility the effect can never touch. This is purely a
		setup-skipping contract: because the flags come from the same static analysis that emitted the
		function's calls, gating setup on them can never change what the function submits.
	*/
	enum class FixedFunctionRequirements : std::uint8_t
	{
		None = 0,					/**< The portable minimum: instance colour + `submit_quad()` only */
		NeedsTexelStep = 0x01,		/**< Calls `texel_size()` / `has_texel_size()` (the texel-step conversion) */
		NeedsUniforms = 0x02,		/**< Calls `has_uniform()` / `uniform_vec2/vec4()` (resolved-uniform plumbing) */
		NeedsStripBuilder = 0x04,	/**< Calls `strip_*()` / `submit_strip[_shaded]()` (the strip-builder scratch and its state) */
		NeedsQuadAxes = 0x08		/**< Calls `quad_origin()` / `quad_axis_x/y()` (pre-clip quad geometry) */
	};

	DEATH_ENUM_FLAGS(FixedFunctionRequirements);

	/*
		The `EffectContext` contract (documented here because the generated code is compiled into
		each backend's device file and calls the backend's concrete context type - there is no
		virtual dispatch, the contract is structural):

			struct EffectContext {
				// Decoded instance data of the draw being dispatched
				const float* Color() const;          // instance colour (vec4, the shader's COLOR)
				float TexelWidth() const;            // 1 / texture width, in texture space
				float TexelHeight() const;           // 1 / texture height
				bool IsBatched() const;              // whether instances come from InstancesBlock

				// The displacement of one texel in the quad's own coordinate space, already converted
				// per backend (raster space on the PVR, logical pixels on the GX), derived from the
				// texel size in UV space the Outline shader family carries in its instance color.xy -
				// exactly like the GLSL derives its tap offsets. HasTexelStep() reports whether the
				// conversion is possible at all (a zero texRect has no scale); the steps are only
				// meaningful when it returns true. The block language exposes these as texel_size()
				// (vec2) and has_texel_size() (bool).
				bool HasTexelStep() const;
				float TexelStepX() const;
				float TexelStepY() const;

				// Submits one pass over the current instance's quad, with the pass state applied
				void SubmitQuad(const FixedFunctionPass& pass);

				// ---- Extended half of the contract, reachable only from backend-specific
				// void fixed_function(pvr|gx) blocks (the generator rejects it in generic blocks) ----

				// PRE-CLIP quad geometry: the raster position of the sprite's (0,0) corner and the
				// raster displacements of its local axes (full sprite width/height). Deliberately not
				// the corner arrays - those are post-scissor-clip, and geometry synthesized from them
				// (the iris circle, the warp bands) would be distorted by a clipped quad. The block
				// language exposes these as quad_origin() / quad_axis_x() / quad_axis_y() (vec2).
				float QuadOriginX() const;  float QuadOriginY() const;
				float QuadAxisXx() const;   float QuadAxisXy() const;
				float QuadAxisYx() const;   float QuadAxisYy() const;

				// Resolved uniforms by name, through the program's existing ResolveUniform machinery
				// (the warp consumes the same uViewSize/uShift/uHorizonColor its GLSL does). An
				// unresolved name loads nothing - out keeps its zeros - so blocks guard with
				// has_uniform() exactly like the handwritten code null-checked the pointers.
				bool HasUniform(const char* name) const;
				void LoadUniform(const char* name, float* out, std::int32_t floatCount) const;

				// The strip builder: a small scratch strip filled by the setters, then submitted in
				// triangle-strip order under a pass's state. Its capacity is a backend capability -
				// at least 8 vertices, more where the backend wants longer strips (the GX takes 16,
				// so its iris fan submits a whole radially subdivided wedge as one strip); the
				// transpiler validates literal indices and counts against the capacity of the
				// backend it is emitting for. UVs are given in the shader's texture space (the
				// backend folds its padded-store scale, exactly like the quad corner synthesis).
				// SubmitStrip draws textured with the pass's flat colour; SubmitStripShaded draws
				// with the per-vertex colours - which is how a gradient (the iris soft edge, the
				// horizon tint) is expressed without a fragment shader. A shaded strip is untextured
				// unless the pass's TEV preset consumes the texel as well (TevPreset::TintMix), in
				// which case the strip keeps its texture and its UVs.
				void SetStripVertexPosition(std::int32_t i, float x, float y);
				void SetStripVertexUv(std::int32_t i, float u, float v);
				void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a);
				void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count);
				void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count);
			};

		A backend implements it against its own submission machinery (corner synthesis, geometric
		scissor clipping and palette banks on the PVR; TEV presets, TLUTs and hardware scissor on
		the GX). The generated per-effect functions have the signature

			void (*)(EffectContext& ctx);

		and are registered in the generated tables (PvrGeneratedEffects.h / GxGeneratedEffects.h)
		keyed by (program name, variant define), mirroring how SwGeneratedShaders.h is consumed by
		the software backend; the backends resolve that key from the true program identity the
		loaders plumb in with ShaderProgram::SetProgramIdentity() - no shader name is ever matched
		in a backend. Each table entry also carries the statically computed UsesOffsetColor of its
		function (whether any reachable `p.offset_color = ...` assignment exists), because the PVR
		must know it when compiling the base polygon header - specular is enabled per program, not
		per pass - the FixedFunctionRequirements bitmask above (which optional context facilities
		the function can ever call, so Dispatch skips the setup for the rest), and the
		FixedFunctionIntrinsic a `pipeline <name>;` block declared instead of a function (see
		above). An entry has either a function or an intrinsic, never both; a program absent from
		the table is skipped with a one-time warning. Byte-identical function bodies are emitted
		once and shared by all their (program, variant) rows - batched twins and palette variants
		usually differ only in dispatch-side decoding, not in pass code.
	*/
}
