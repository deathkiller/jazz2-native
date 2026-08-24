#pragma once

/**
	@file ConsoleFixedFunction.h

	Fixed-function-effect transpiler for ShaderCompiler — the third C++ emitter beside the
	GLSL-to-C++ software transpiler (@ref GlslToCpp.h).

	The consoles (PVR on the Dreamcast, GX on the Wii/GameCube, GU on the PlayStation Portable) have
	no fragment shaders; a shader describes how to drive their fixed-function hardware in a
	`void fixed_function([<target>[, <target>...]]) { ... }` block — empty parentheses for the generic
	implementation, one backend name for an override, a comma-separated list for one implementation
	shared by several backends (see `Docs/FixedFunctionShaderDesign.md`). This
	unit transpiles one such block — once per program variant, with the variant's define baked in
	exactly like the fragment stage — into the BODY of a plain C++ effect function

		void <Program>[_<VARIANT>]_Effect(EffectContext& ctx)

	written against the runtime contract in `nCine/Graphics/RHI/FixedFunctionPass.h`: the function
	fills `FixedFunctionPass` descriptors and submits them through the backend's `EffectContext`.
	Main.cpp's `--emit-fixed-function` mode names and collects the functions into per-backend
	aggregate headers (`PvrGeneratedEffects.h` / `GxGeneratedEffects.h` / `GuGeneratedEffects.h` /
	`GsGeneratedEffects.h` / `RdpGeneratedEffects.h`),
	deduplicating byte-identical bodies into one shared function per backend (batched twins and
	palette variants usually differ only in dispatch-side decoding), mirroring how
	`--emit-sw-generated` produces `SwGeneratedShaders.h`.

	Unlike the software transpiler, which DECLINES shaders outside its subset (they simply stay
	absent from the table), a fixed_function block is authored intent: anything outside the block
	grammar is a hard ERROR with the offending line, so mistakes surface on the dev machine instead
	of silently dropping a console effect. The portable core (valid in every block):

	- `pipeline <name>;` as the SOLE statement binds the program to a backend pipeline stage
	  (`nCine::RHI::FixedFunctionIntrinsic`) instead of describing passes — no function is emitted
	- `pass p;` declares a `nCine::RHI::FixedFunctionPass` local (no initializer)
	- assignments to pass fields: `p.color = <vec4>;`, `p.offset_color = <vec3>;` (marks
	  HasOffsetColor), `p.screen_offset = <vec2>;`, `p.blend = MATERIAL|ADD|OPAQUE|ALPHA;`,
	  `p.tev = MODULATE|SILHOUETTE|MODULATE_X2|MODULATE_X4;` (portable intent — the PVR ignores it;
	  the two output scales have no GE form at all, so they are rejected for the gu target below),
	  `p.luma_gain = <float>;` (parameterizes the GX-only LUMA_RAMP preset below)
	- `submit_quad(p);`, locals of the GLSL scalar/vector subset (float/int/bool, vec2/3/4),
	  `if`/`else`, C-style `for` with integer bounds, (compound) assignment, `++`/`--`
	- expressions: arithmetic, comparisons, swizzles, `min`/`max`/`clamp`/`mix`/`ceil`/`floor`,
	  `abs`/`sqrt`/`sin`/`cos` (float), vec/scalar constructors, the `COLOR` built-in (the instance
	  color, `ctx.Color()`), `texel_size()` (`vec2(ctx.TexelStepX(), ctx.TexelStepY())` - the
	  displacement of one texel in the quad's own coordinate space, already converted per backend)
	  and `has_texel_size()` (`ctx.HasTexelStep()` - whether that step is derivable at all)

	The EXTENDED vocabulary is valid only in a block that NAMES its backends —
	`void fixed_function(pvr)` … `void fixed_function(gx, gu)` — since a generic block stays in the
	portable quad-only core, so a shared description can never silently depend on one console's
	geometry synthesis (rejected with a hard error otherwise):

	- `quad_origin()`, `quad_axis_x()`, `quad_axis_y()` (vec2) — the PRE-CLIP raster position of
	  the sprite's (0,0) corner and the raster displacements of its local axes; geometry synthesis
	  uses these instead of the post-scissor-clip corner arrays so clipping cannot distort it
	- `has_uniform(uName)` (bool), `uniform_vec2(uName)`, `uniform_vec4(uName)` — the program's
	  resolved uniforms by name (`ctx.HasUniform`/`ctx.LoadUniform` over the backend's existing
	  ResolveUniform machinery); the argument is an identifier, not a string literal
	- the strip builder: `strip_position(i, <vec2>)`, `strip_uv(i, <vec2>)` (texture-space UVs;
	  the backend folds its padded-store scale), `strip_color(i, <vec4>)`, then
	  `submit_strip(p, count)` (textured, flat pass colour) or `submit_strip_shaded(p, count)`
	  (per-vertex colours — gradients without a fragment shader; untextured unless the pass's TEV
	  preset consumes the texel too). Literal indices and counts are checked against the scratch
	  capacity of the block's targets (8 vertices on the PVR, 16 everywhere else), because at
	  runtime an out-of-range index is dropped and an oversized count clamped — silently wrong
	  geometry. A block naming several targets is held to the SMALLEST of their capacities.

	A block declaring a comma-separated target list is validated against the INTERSECTION of what its
	targets can do, so it may only use capabilities all of them have — the rule that keeps one shared
	description honest, since the alternative (checking only the backend whose header is being
	written) would accept a block that is silently wrong on the other backends it serves.

	Two TEV presets need a programmable texture combiner. `LUMA_RAMP` (a silhouette whose tone is
	picked per texel from a two-endpoint ramp by the texel's amplified luminance — FrozenMask's ice)
	only the GX's multi-stage TEV can express, so it is rejected outside a block targeting the GX
	ALONE (`void fixed_function(gx)`). `TINT_MIX` (`mix(texel, colour, alpha)`, one stage — the
	TexturedBackground warp folds its horizon tint into the band's own draw with it) the RDP's colour
	combiner can express too — one cycle of `(PRIM - TEX) * PRIM_ALPHA + TEX` IS that lerp — so it is
	allowed in any block ALL of whose targets have a lerping combiner (`gx`, `rdp` or a list of them)
	and rejected for every block that reaches a backend without one.

	The GE's texture environment has no combiner output scale, so `MODULATE_X2`/`MODULATE_X4` are
	rejected for every block the GU reaches (a `gu` block, a target list naming `gu`, AND a generic
	one, which is transpiled for every backend), and the GS's texture function lacks a scale stage the
	same way: the PVR silently ignores them, so a shared block using one would be honoured by only some
	of the backends it serves - the "silently depends on one console's feature" case the capability
	checks exist to prevent. The RDP sits in between: its second combiner cycle can double the first
	one's output (`(1 - 0) * COMBINED + COMBINED`), so `MODULATE_X2` is expressible there, while
	`MODULATE_X4` would need a third cycle the hardware does not have. On the tiers without a scale,
	express the boost as passes instead (an additive pass, the way Colorized splits its multiplier on
	the PVR and the PSP).
*/

#include <cstdint>

#include <Containers/String.h>
#include <Containers/StringView.h>

#include "ShaderParser.h"		// SourceLine, FixedFunctionBlock

namespace ShaderCompiler
{
	using namespace Death::Containers;

	/** @brief Fixed-function backend a generated aggregate header targets */
	enum class FixedFunctionBackend
	{
		Pvr,	/**< Dreamcast (CLX2 via KallistiOS) */
		Gx,		/**< Wii/GameCube (Flipper/Hollywood) */
		Gu,		/**< PlayStation Portable (Graphics Engine via sceGu) */
		Gs,		/**< PlayStation 2 (Graphics Synthesizer via PS2SDK's libdraw) */
		Rdp		/**< Nintendo 64 (Reality Display Processor via libdragon) */
	};

	/**
		@brief Optional `EffectContext` facilities an emitted function calls, as single-bit flags

		Mirrors `nCine::RHI::FixedFunctionRequirements` (FixedFunctionPass.h) name for name and bit
		for bit — Main.cpp renders these values as that enum's members in the generated table, and
		the backends gate their per-draw context setup on them.
	*/
	enum class FixedFunctionRequirements : std::uint8_t
	{
		None = 0,
		NeedsTexelStep = 0x01,		/**< `texel_size()` / `has_texel_size()` */
		NeedsUniforms = 0x02,		/**< `has_uniform()` / `uniform_vec2/vec4()` */
		NeedsStripBuilder = 0x04,	/**< `strip_*()` / `submit_strip[_shaded]()` */
		NeedsQuadAxes = 0x08		/**< `quad_origin()` / `quad_axis_x/y()` */
	};

	/** @brief Outcome of transpiling one fixed_function block variant to C++ */
	struct FixedFunctionResult
	{
		/** @brief `true` when the block fits the grammar and @ref Body was emitted */
		bool Ok = false;
		/** @brief Error message (only meaningful when @ref Ok is `false`) */
		String Error;
		/** @brief 1-based input line of the error (0 when unknown) */
		std::int32_t Line = 0;
		/**
			@brief The emitted function BODY (prologue + statements at 3-tab indent, no signature), empty when @ref Intrinsic is set

			Deliberately name-free: two (program, variant) blocks that transpile to the same passes
			produce byte-identical bodies (batched twins, palette variants whose difference lives in
			the dispatch loop), which is what lets Main.cpp deduplicate them — one emitted function,
			named after its first occurrence, shared by every matching table row.
		*/
		String Body;
		/**
			@brief `FixedFunctionIntrinsic` member name declared by a `pipeline <name>;` block, or empty

			A block whose sole statement is `pipeline <name>;` transpiles to no function at all: the
			program binds itself to a backend pipeline stage that consumes engine data structures
			(tile-layer meshes, line strips, the lighting hook). The table entry then carries the enum
			value instead of a function pointer, so even these stages are named in the shader file
			rather than matched by label in a backend.
		*/
		String Intrinsic;
		/**
			@brief Whether any reachable `p.offset_color = ...` assignment exists in the emitted function

			Computed statically per (program, variant) - the variant's preprocessing has already been
			applied - and written into the generated table entry, because the PVR needs it when
			compiling the base polygon header (specular enable is per program, not per pass).
		*/
		bool UsesOffsetColor = false;
		/**
			@brief Which optional `EffectContext` facilities the emitted body calls

			Computed statically during emission (a bit is set exactly when the corresponding builtin
			family was transpiled) and written into the generated table entry, so the backends can
			skip the per-draw context setup for facilities the effect can never touch.
		*/
		FixedFunctionRequirements Requirements = FixedFunctionRequirements::None;
	};

	/** @brief Transpiles `fixed_function` blocks into C++ effect functions over the EffectContext contract */
	class ConsoleFixedFunction
	{
	public:
		/**
			Transpiles the captured @p block into the BODY of a `void (EffectContext&)` function —
			Main.cpp wraps it in a signature and provenance comment, sharing one function among all
			(program, variant) entries whose bodies came out byte-identical. @p define is the variant
			define baked into the block's preprocessing (empty for the base variant), so
			`#ifdef <VARIANT>` conditionals inside the block resolve per variant. The portable core
			emits identically for every backend (the PVR ignores `tev`, the GX approximates
			`offset_color` with its silhouette preset, the GU expands it into modulate + additive
			silhouette); the extended vocabulary and the per-hardware capabilities are gated on the
			block's own target LIST — blocks that name their backends only, and against the
			intersection of what those backends can do, so @p backend selects which aggregate header
			the body is destined for, never how strictly it is checked.
		*/
		static FixedFunctionResult TranspileBlock(const FixedFunctionBlock& block,
			StringView define, FixedFunctionBackend backend);

		/**
			Returns the support code every generated aggregate header carries once, at 2-tab indent
			(backend namespace + anonymous namespace): a `ff` namespace with GLSL-style float vector
			types, the builtins of the expression subset and the pass-field store helpers. Emitted
			into the header (rather than shipped as an engine header) so the generated artifact stays
			self-contained — `FixedFunctionPass.h` remains the only runtime contract.
		*/
		static String BuildRuntimeSupport();
	};
}
