'use strict';

/**
 * The `.shader` language vocabulary, kept in one place so the completion, hover and (indirectly) the
 * grammar all describe the same language. Every entry here mirrors a rule the offline ShaderCompiler
 * actually enforces - see Sources/Utilities/ShaderCompiler/README.md and Docs/ShaderCompiler.dox.
 *
 * This module must stay free of any `vscode` import: the tests run it in a bare JS engine.
 */

/** Top-level keyword directives, only legal at brace depth 0 */
const DIRECTIVES = [
	{
		name: 'program',
		insert: 'program ${1:Name};',
		detail: 'program <Name>;',
		doc: 'Required, exactly once, and it has to precede `shader_type`, `variant`, `batched`, `precision` and the entry points — in practice, write it first. Must be a C++/GLSL identifier.'
	},
	{
		name: 'shader_type',
		insert: 'shader_type ${1|canvas_item,custom|};',
		detail: 'shader_type canvas_item | custom;',
		doc: 'Optional — the default is `custom`. `canvas_item` opts into the sprite-template lowering: the vertex stage is generated around the engine\'s standard sprite contract and the `batched` twin becomes available.'
	},
	{
		name: 'variant',
		insert: 'variant ${1:NAME};',
		detail: 'variant <NAME>;',
		doc: 'Declares one optional variant; may appear several times. The output contains the unnamed base variant (always `Variants[0]`) plus one entry per variant, compiled with `#define <NAME> (1)` baked in. No cross-products.'
	},
	{
		name: 'render_mode',
		insert: 'render_mode ${1|blend_mix,blend_add,blend_sub,blend_mul,blend_premul_alpha,unshaded|};',
		detail: 'render_mode <mode>[, <mode>];',
		doc: 'Zero or more of `blend_mix`, `blend_add`, `blend_sub`, `blend_mul`, `blend_premul_alpha`, `unshaded`. Stored as the `RenderModes` bitmask on the emitted `Program`.'
	},
	{
		name: 'precision',
		insert: 'precision ${1|mediump,highp|};',
		detail: 'precision mediump | highp;',
		doc: 'Optional (default `mediump`) — selects the float precision of the auto-emitted `#ifdef GL_ES precision X float; #endif` fragment prologue.\n\nOnly the two-token form is a directive: a real GLSL statement with a type (`precision highp float;`) passes through as ordinary GLSL.'
	},
	{
		name: 'batched',
		insert: 'batched ${1:Name};',
		detail: 'batched <Name>;',
		doc: '`canvas_item` only — also emits the batched twin program (`InstancesBlock` + the 6-vertex corner formula) into the same header, sharing the fragment stage and the variants. **An error in custom mode.**'
	},
	{
		name: 'attribute',
		insert: 'attribute ${1:vec2} ${2:aValue};',
		detail: 'attribute [layout(location = N)] <declaration>;',
		doc: 'A vertex attribute: emitted as an `in` global in the **vertex stage only**. A leading `layout(...)` qualifier stays in front of the `in` keyword and the location is honored by reflection.'
	},
	{
		name: 'varying',
		insert: 'varying ${1:vec2} ${2:vValue};',
		detail: 'varying [flat] [precision] <type> <name>;',
		doc: 'Lowered to an `out` in the vertex stage and an `in` in the fragment stage, qualifiers preserved. A varying the fragment stage never reads is trimmed automatically, along with its store.'
	},
	{
		name: 'uniform',
		insert: 'uniform ${1:vec4} ${2:uValue};',
		detail: 'uniform <type> <name> [: <hint>[, <hint>]];',
		doc: 'Shared by both stages. `texture_unit(N)` is the only hint reflection keeps — it is the one way to assign a sampler\'s texture unit explicitly. An unused uniform (or block) is eliminated from the emitted stage.'
	}
];

/** `void <name>(...)` entry points */
const ENTRY_POINTS = [
	{
		name: 'vertex',
		insert: 'void vertex() {\n\t$0\n}',
		detail: 'void vertex() { ... }',
		doc: 'Custom mode: **required**, the body becomes the vertex `main()` verbatim and writes `gl_Position` itself.\n\nCanvas mode: optional; the generated `main()` computes `VERTEX`, `UV`, `COLOR` and `PALETTE_OFFSET` as locals, splices the body, then runs the standard epilogue. A `return;` there is a **parse error** — the epilogue is real post-work, so restructure with if/else.'
	},
	{
		name: 'fragment',
		insert: 'void fragment() {\n\tCOLOR = $0;\n}',
		detail: 'void fragment() { ... }',
		doc: '**Required.** `COLOR` *is* the fragment output variable (`out vec4 COLOR;`) — there is no epilogue, so an early `return;` is safe, but the value is **undefined until written**: assign it on every path.\n\nReferencing `fragColor` anywhere in a `.shader` file is a parse error.'
	},
	{
		name: 'fixed_function',
		insert: 'void fixed_function() {\n\tpass ${1:p};\n\t${1:p}.color = ${2:COLOR};\n\tsubmit_quad(${1:p});\n}',
		detail: 'void fixed_function([<target>[, <target>]]) { ... }',
		doc: 'The console fixed-function implementation of the effect, transpiled to C++ by `--emit-fixed-function`. Never part of the GLSL stages.\n\nEmpty parentheses declare the generic block; `pvr` (Dreamcast), `gx` (Wii/GameCube), `gu` (PlayStation Portable), `gs` (PlayStation 2) and `rdp` (Nintendo 64) override it for one backend, and a comma-separated target list (`void fixed_function(pvr, gu, gs, rdp)`) declares one implementation shared by several. Every target belongs to exactly one block per file.'
	}
];

const SHADER_TYPES = [
	{ name: 'canvas_item', doc: 'Opt into the sprite-template lowering and the `batched` twin.' },
	{ name: 'custom', doc: 'No template and no built-in substitutions — user identifiers pass through untouched. This is the default.' }
];

const RENDER_MODES = [
	{ name: 'blend_mix', doc: 'Standard source-alpha blending.' },
	{ name: 'blend_add', doc: 'Additive blending.' },
	{ name: 'blend_sub', doc: 'Subtractive blending.' },
	{ name: 'blend_mul', doc: 'Multiplicative blending.' },
	{ name: 'blend_premul_alpha', doc: 'Blending with premultiplied alpha.' },
	{ name: 'unshaded', doc: 'Skip the lighting pass for this material.' }
];

const PRECISION_QUALIFIERS = [
	{ name: 'mediump', doc: 'The default precision of the emitted `GL_ES` fragment prologue.' },
	{ name: 'highp', doc: 'Request high float precision in the emitted `GL_ES` fragment prologue.' }
];

/** Hints legal after the `:` of a uniform declaration */
const UNIFORM_HINTS = [
	{
		name: 'texture_unit',
		insert: 'texture_unit(${1:0})',
		doc: 'Assigns the texture unit (0–31) of a sampler uniform and records it in reflection — the **only** way to assign one explicitly. In the primary mode the name must match a sampler in at least one variant, and two hints naming the same uniform are an error.'
	},
	{ name: 'source_color', doc: 'Parsed and dropped — never reaches the emitted sources.' },
	{ name: 'hint_range', insert: 'hint_range(${1:0.0}, ${2:1.0})', doc: 'Parsed and dropped — never reaches the emitted sources.' },
	{ name: 'filter_nearest', doc: 'Parsed and dropped — never reaches the emitted sources.' },
	{ name: 'filter_linear', doc: 'Parsed and dropped — never reaches the emitted sources.' },
	{ name: 'repeat_enable', doc: 'Parsed and dropped — never reaches the emitted sources.' },
	{ name: 'repeat_disable', doc: 'Parsed and dropped — never reaches the emitted sources.' }
];

// Each target's own line says what its texture combiner can do, because that - not the console's
// overall power - is what decides which `p.tev` presets a block naming it may use. A block with a
// target list is held to the INTERSECTION of its targets, and the generic block to every backend.
const FIXED_FUNCTION_TARGETS = [
	{
		name: 'pvr',
		doc: 'The Dreamcast\'s PowerVR2 (CLX2) only.\n\nIts texture environment modulates a texel by the vertex colour and adds an offset colour, and that is the whole vocabulary: `p.tev` is **ignored entirely** here, so `SILHOUETTE` and the scaled modulates draw a plain modulated pass, while `TINT_MIX` and `LUMA_RAMP` are hard errors. Strip-builder capacity 8 vertices — the smallest, so any list naming `pvr` is held to it.'
	},
	{
		name: 'gx',
		doc: 'The Wii / GameCube GX only.\n\nThe richest fixed-function tier here: the multi-stage programmable TEV expresses **every** `p.tev` preset, including the GX-only `LUMA_RAMP`. Strip-builder capacity 16 vertices.'
	},
	{
		name: 'gu',
		doc: 'The PlayStation Portable\'s Graphics Engine (sceGu) only.\n\nIts five texture functions (modulate / decal / blend / replace / add) have no combiner output scale and no lerp of a texel toward a constant weighted by an interpolated alpha (`GU_TFX_BLEND` weighs by the *texel*), so `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX` and `LUMA_RAMP` are all rejected for any block that reaches it. Express a boost as extra passes instead. Strip-builder capacity 16 vertices.'
	},
	{
		name: 'gs',
		doc: 'The PlayStation 2\'s Graphics Synthesizer only.\n\nIts four texture functions (`MODULATE` / `DECAL` / `HIGHLIGHT` / `HIGHLIGHT2`) lack a scale stage and a lerp just like the GE\'s, so `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX` and `LUMA_RAMP` are rejected for any block that reaches it. Strip-builder capacity 16 vertices.'
	},
	{
		name: 'rdp',
		doc: 'The Nintendo 64\'s Reality Display Processor only.\n\nIts colour combiner computes `(A - B) * C + D` per cycle over up to **two** cycles, which buys it two presets the other small tiers lack: `TINT_MIX` *is* one cycle (`(PRIM - TEX) * PRIM_ALPHA + TEX`), and the second cycle can double the first one\'s output, so `MODULATE_X2` works. `MODULATE_X4` would need a third cycle, and there is no dot product to derive a luminance with, so `LUMA_RAMP` is out. Strip-builder capacity 16 vertices.'
	},
	{
		name: 'legacygl',
		doc: 'The fixed-function OpenGL 1.x backend (MorphOS\' TinyGL, and any desktop GL in a compatibility context) only.\n\n`GL_RGB_SCALE` gives it both combiner output scales and `GL_INTERPOLATE` gives it `TINT_MIX`, so `LUMA_RAMP` is the only preset it cannot express. Strip-builder capacity 16 vertices.'
	}
];

/** Canvas-mode built-ins (and `COLOR`, which is the fragment output in both modes) */
const BUILTINS = [
	{
		name: 'COLOR',
		doc: 'In `fragment()` this **is** the fragment output variable, and it is undefined until written. In canvas mode it enters the body pre-set to the instance colour (`vColor`); in custom mode nothing initializes it.\n\nIn a canvas `vertex()` it is the instance colour local, stored to the `vColor` varying by the epilogue.'
	},
	{ name: 'UV', doc: 'Canvas mode. In `fragment()` substituted with the `vTexCoords` varying; in `vertex()` it is the `texRect` mapping local.' },
	{ name: 'TEXTURE', doc: 'Canvas mode, `fragment()` only. Substituted with `uTexture`. A canvas document that references it without declaring `uTexture` gets `uniform sampler2D uTexture : texture_unit(0);` auto-declared.' },
	{ name: 'PALETTE_OFFSET', doc: 'Canvas mode. Substituted with the `vPaletteOffset` varying in `fragment()`; a prologue local in `vertex()`.' },
	{ name: 'VERTEX', doc: 'Canvas `vertex()` only — the sprite-local position in pixels (`aPosition * spriteSize`), before the model transform. The epilogue multiplies it through the projection/view/model matrices.' }
];

/** Built-ins the canvas lowering explicitly reports as unsupported */
const UNSUPPORTED_BUILTINS = ['NORMAL', 'SCREEN_UV', 'SCREEN_PIXEL_SIZE', 'TIME', 'POINT_COORD'];

// Macros the compiler resolves itself, so that none of them ever appears in an emitted source. All
// of them work in `#ifdef` / `#ifndef` and in `#if` / `#elif` expressions; only `#define` / `#undef`
// of one is an error. The first two are resolved during stage assembly, the rest when a stage source
// is built for a given emission. A `.shader` writes every conditional as `#if` - including the ones
// on variant defines and other flags, which the compiler lowers to `#ifdef` / `defined(...)` on the
// way out so the emitted GLSL stays legal on the ES profiles.
const CONVENTION = ' Written `#if X` / `#if !X`, so a second condition can join the same directive.';
const STAGE_MACROS = [
	{ name: 'VERTEX_STAGE', doc: 'Kept for the vertex stage, dropped for the fragment stage. Resolved during stage assembly — `#define` and `#undef` are errors.' + CONVENTION },
	{ name: 'FRAGMENT_STAGE', doc: 'Kept for the fragment stage, dropped for the vertex stage. Resolved during stage assembly — `#define` and `#undef` are errors.' + CONVENTION },
	{ name: 'SOFTWARE_RENDERER', doc: 'Resolved when a stage source is built. Only `--emit-sw-generated` builds with the macro defined; every other emission takes the undefined side.' + CONVENTION },
	{ name: 'NO_DYNAMIC_BRANCHING', doc: 'Resolved when a stage source is built. Only `--emit-rsx` (PlayStation 3) builds with the macro defined — a fragment stage compiling to NV40 `IF`/`LOOP`/`BRK` control flow does not survive cgcomp, so gate any dynamically branching block on it.' + CONVENTION }
];

// Which console can express which `p.tev` preset, rendered into the `tev` hover so the whole picture
// is one tooltip away. A dash is a hard compile error for every block that reaches that backend -
// including the generic one, which is transpiled for all of them - never a silent fallback; the PVR's
// "ignored" is the one silent case, and it is why the presets it ignores are still rejected for a
// block shared with a backend that would error on them.
const TEV_SUPPORT_TABLE = 'Support is **per backend**, and a block is validated against the intersection of its targets:\n\n' +
	'| preset | `pvr` | `gx` | `gu` | `gs` | `rdp` | `legacygl` |\n' +
	'| --- | --- | --- | --- | --- | --- | --- |\n' +
	'| `MODULATE` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |\n' +
	'| `SILHOUETTE` | *ignored* | ✓ | ✓ | ✓ | ✓ | ✓ |\n' +
	'| `MODULATE_X2` | *ignored* | ✓ | — | — | ✓ | ✓ |\n' +
	'| `MODULATE_X4` | *ignored* | ✓ | — | — | — | ✓ |\n' +
	'| `TINT_MIX` | — | ✓ | — | — | ✓ | ✓ |\n' +
	'| `LUMA_RAMP` | — | ✓ | — | — | — | — |\n\n' +
	'`—` is a **hard error** for every block that reaches that backend, the generic block included (it is ' +
	'transpiled for all of them). *ignored* means the PVR has no `p.tev` at all and draws a plain modulated pass.';

// The maths a fixed_function body accepts overlaps GLSL by name (`mix`, `min`, `abs`, `float`, …) but
// not by meaning: the body is transpiled to C++ that the console's CPU runs once per draw to BUILD a
// pass, so none of the GLSL entries' per-backend lowering notes apply. Saying so once, here, is what
// lets the hover pick the right entry for the cursor's context.
const FF_SUBSET = '\n\nOne of the small maths subset a `fixed_function` body accepts (`abs`, `ceil`, `clamp`, `cos`, `float`, `floor`, `int`, `max`, `min`, `mix`, `sin`, `sqrt`) — transpiled to C++ and evaluated **once per draw** while the pass is built, not per pixel.';

/** The fixed-function DSL: statements, pass fields, context facilities and the allowed maths */
const FIXED_FUNCTION = {
	statements: [
		{ name: 'pass', insert: 'pass ${1:p};', detail: 'pass <name>;', doc: 'Declares one pass — a small bundle of hardware state applied over the sprite quad.' },
		{ name: 'pipeline', insert: 'pipeline ${1|tile_map_mesh,lighting_combine,line_strip_mesh|};', detail: 'pipeline <name>;', doc: 'Binds the effect to a backend pipeline stage — one that consumes an engine data structure (a tile-layer mesh, a line strip, the lighting hook) rather than describing shading.\n\nMust be the **first** statement of the block. Alone in the block it describes no passes at all and the generated table carries the `FixedFunctionIntrinsic` with no function pointer; **passes may follow it**, in which case the entry carries both and the backend runs the stage first, then the function over what it produced. That is how the lighting compositor keeps its per-texel lightmap loop in the backend while its water overlay lives in the shader.' }
	],
	submits: [
		{ name: 'submit_quad', insert: 'submit_quad(${1:p})', doc: 'Emits the configured pass over the sprite quad.' },
		{ name: 'submit_strip', insert: 'submit_strip(${1:p})', doc: 'Emits the pass over a triangle strip built with `strip_position` / `strip_uv` / `strip_color`. Requires the strip-builder facility.' },
		{ name: 'submit_strip_shaded', insert: 'submit_strip_shaded(${1:p})', doc: 'Like `submit_strip`, but with per-vertex colours taken from the strip builder.' }
	],
	stripHelpers: [
		{ name: 'strip_position', insert: 'strip_position(${1:x}, ${2:y})', doc: 'Appends a vertex position to the strip being built.' },
		{ name: 'strip_uv', insert: 'strip_uv(${1:u}, ${2:v})', doc: 'Appends a texture coordinate to the strip being built.' },
		{ name: 'strip_color', insert: 'strip_color(${1:rgba})', doc: 'Appends a vertex colour to the strip being built.' }
	],
	context: [
		{ name: 'texel_size', doc: 'The texel step of the bound texture (`vec2`). Sets the `NeedsTexelStep` requirement, so `Dispatch` only computes it for effects that ask.' },
		{ name: 'has_texel_size', doc: 'True when the pass context could resolve a texel step for the bound texture.' },
		{ name: 'quad_origin', doc: 'The sprite quad\'s origin in screen space (`vec2`). Sets the `NeedsQuadAxes` requirement.' },
		{ name: 'quad_axis_x', doc: 'The quad\'s X edge vector in screen space (`vec2`). Sets the `NeedsQuadAxes` requirement.' },
		{ name: 'quad_axis_y', doc: 'The quad\'s Y edge vector in screen space (`vec2`). Sets the `NeedsQuadAxes` requirement.' },
		// The argument is an IDENTIFIER, not a string literal - the block expression tokenizer has no
		// string literals at all, so a quoted name is a hard `unexpected token '"'` from the compiler.
		// That is why these snippets insert a bare name (see the uniform builtins in
		// ShaderCompiler/ConsoleFixedFunction.cpp). Reading uniforms by name is the only way a block
		// touches engine state, which is what keeps the language game-neutral.
		{ name: 'has_uniform', insert: 'has_uniform(${1:uName})', doc: 'True when the named shader uniform is present in the pass context. Sets the `NeedsUniforms` requirement.\n\nThe argument is a bare **identifier**, not a string literal — `has_uniform(uWaterLevel)`.' },
		{ name: 'uniform_float', insert: 'uniform_float(${1:uName})', doc: 'Reads a `float` uniform from the pass context. Sets the `NeedsUniforms` requirement.\n\nThe argument is a bare **identifier**, not a string literal — `uniform_float(uWaterLevel)`.' },
		{ name: 'uniform_vec2', insert: 'uniform_vec2(${1:uName})', doc: 'Reads a `vec2` uniform from the pass context. Sets the `NeedsUniforms` requirement.\n\nThe argument is a bare **identifier**, not a string literal — `uniform_vec2(uShift)`.' },
		{ name: 'uniform_vec4', insert: 'uniform_vec4(${1:uName})', doc: 'Reads a `vec4` uniform from the pass context. Sets the `NeedsUniforms` requirement.\n\nThe argument is a bare **identifier**, not a string literal — `uniform_vec4(uAmbientColor)`.' }
	],
	passFields: [
		{ name: 'color', doc: 'The pass colour (`vec4`).' },
		{ name: 'offset_color', doc: 'The pass offset (specular) colour (`vec3`). The PVR compiles specular into the base polygon header, so the generated table records whether an effect can ever write one.' },
		{ name: 'screen_offset', doc: 'A screen-space offset applied to the quad (`vec2`).' },
		{ name: 'blend', doc: 'Blend mode — one of `MATERIAL`, `ADD`, `OPAQUE`, `ALPHA`. Plain `=` assignment of a bare identifier only.\n\nEvery backend implements all four, so this field never constrains a block\'s target list.' },
		{ name: 'tev', doc: 'Texture-combiner preset — one of `MODULATE`, `SILHOUETTE`, `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX`, `LUMA_RAMP`. Plain `=` assignment of a bare identifier only.\n\n' + TEV_SUPPORT_TABLE },
		{ name: 'luma_gain', doc: 'The only scalar pass field (`float`); it parameterizes the `LUMA_RAMP` preset — the texel\'s Rec.601 luminance is multiplied by it and saturated before it picks the ramp tone.\n\nOnly `LUMA_RAMP` reads it, so it is **GX-only** in effect; the GX folds it into the KONST-held luminance weights and the combiner output scale, so any value up to 4 costs nothing extra.' }
	],
	// The support notes below are the authoring constraint, not trivia: a block is validated against
	// the intersection of what its targets can do, so a preset one of them lacks is a hard error for
	// the whole block. They mirror the capability gates in ShaderCompiler/ConsoleFixedFunction.cpp.
	blendModes: [
		{ name: 'MATERIAL', doc: 'Whatever the material configured — the default. Every backend.' },
		{ name: 'ADD', doc: 'Additive blending for glow and split-multiplier passes. Every backend.\n\nThe GX maps it to `ONE + ONE`; the PVR deliberately maps it to `SRCALPHA + ONE` — the additive mechanism the split-multiplier passes (Colorized) have always used there, so the mapping stays bit-identical with the handwritten code.' },
		{ name: 'OPAQUE', doc: '`ONE + ZERO`. Every backend.' },
		{ name: 'ALPHA', doc: 'Standard source-alpha over (`SRCALPHA + INVSRCALPHA`), **independent of whatever the material configured** — for a pass whose blending is its own, like the horizon tint of the TexturedBackground warp. Every backend.' }
	],
	tevPresets: [
		{
			name: 'MODULATE',
			doc: '`texture * vertex colour` — the default.\n\nEvery backend: `pvr`, `gx`, `gu`, `gs`, `rdp`, `legacygl`.'
		},
		{
			name: 'SILHOUETTE',
			doc: 'The vertex colour wherever the texture has alpha — flat masks, shadows and glows.\n\n`gx`, `gu`, `gs`, `rdp`, `legacygl`. **The PVR has no `p.tev` at all** (it always modulates), so a `pvr` block silently draws a plain modulated pass instead; that is not an error, but a silhouette the PVR must honour has to come from `p.offset_color` or its own pass.'
		},
		{
			name: 'MODULATE_X2',
			doc: 'Modulate with the combiner\'s ×2 output scale.\n\n`gx` (native), `rdp` (its second combiner cycle doubles the first — `(1 - 0) * COMBINED + COMBINED`), `legacygl` (`GL_RGB_SCALE`). Ignored by the PVR.\n\n**Rejected** for any block that reaches `gu` or `gs` — neither texture environment has a scale stage — *including the generic block*, which is transpiled for every backend. Express the boost as an extra additive pass there, the way Colorized splits its multiplier.'
		},
		{
			name: 'MODULATE_X4',
			doc: 'Modulate with the combiner\'s ×4 output scale.\n\n`gx` and `legacygl` (`GL_RGB_SCALE` takes 1, 2 or 4) only. Ignored by the PVR.\n\n**Rejected** for any block that reaches `gu`, `gs` or `rdp` — the RDP reaches a ×2 with its two combiner cycles but a ×4 would need a third one the hardware does not have — and for the generic block.'
		},
		{
			name: 'TINT_MIX',
			doc: '`mix(texel, colour, alpha)` with an opaque result — the texel lerped toward the pass colour by the pass alpha. With a shaded strip both terms come from the per-vertex colour, which is how the TexturedBackground warp folds its horizon tint into the band\'s own draw instead of laying a second gradient pass over it.\n\nNeeds a **lerping combiner**, so only `gx` (one TEV stage, `d + mix(a, b, c)`), `rdp` (`(PRIM - TEX) * PRIM_ALPHA + TEX`) and `legacygl` (`GL_INTERPOLATE`) — a block naming any of them, or several of them, may use it.\n\n**Rejected** for any block that reaches `pvr`, `gu` or `gs`, and for the generic block. Give those backends their own block.'
		},
		{
			name: 'LUMA_RAMP',
			doc: 'A silhouette whose tone is picked per texel from a two-endpoint ramp instead of being flat: the texel\'s Rec.601 luminance amplified by `p.luma_gain` and saturated gives `grey`, and the colour is `mix(p.color.rgb, p.offset_color, grey)` — so `p.color` carries the tone at `grey = 0` and `p.offset_color` the tone at `grey = 1`. Coverage stays the silhouette\'s `texel alpha * p.color.a`. This is FrozenMask\'s ice.\n\n**`fixed_function(gx)` only.** It needs the GX\'s multi-stage programmable TEV (channel swizzles through the swap tables plus a KONST-weighted dot product), so it is rejected in a block targeting anything else, in a target list that names `gx` **and** another backend (the intersection cannot include it), and in the generic block. No other tier here can even derive a per-texel luminance: the CLX2 only modulates and adds, the GE and the GS have fixed texture functions, and the RDP\'s combiner has no dot product.'
		}
	],
	pipelines: [
		{ name: 'tile_map_mesh', doc: 'The backend\'s tilemap mesh stage — a whole tile layer arrives as one mesh instead of one draw per tile.' },
		{ name: 'lighting_combine', doc: 'The backend\'s lighting combine stage — the viewport compositor\'s CPU-lightmap pass.\n\nThis is the stage that carries **passes after it**: the lightmap conversion is a per-texel loop over an engine buffer and stays in the backend, while the water overlay of the CombineWithWater programs is described as ordinary passes here and composites over what the stage produced.' },
		{ name: 'line_strip_mesh', doc: 'The backend\'s line-strip mesh stage — the vertex-fed textured strip of the weapon wheel.' }
	],
	// `COLOR` means something different here than in a GLSL stage, so the fixed-function half of the
	// language documents its own - the hover picks the entry the cursor's context calls for
	builtins: [
		{
			name: 'COLOR',
			doc: 'The instance colour the effect is being drawn with (`ctx.Color()` in the generated C++) — a pass\'s usual starting point, typically assigned straight to `p.color`.\n\n**Not** the fragment output: a `fixed_function` block never shades a pixel.'
		}
	],
	// The local types a block accepts, per ConsoleFixedFunction.h: the GLSL scalar/vector subset plus
	// `pass` itself. No matrices and no arrays - a pass is built from scalars and small vectors.
	types: [
		{ name: 'float', doc: 'A scalar local. The only scalar type a pass field takes (`p.luma_gain`).' + FF_SUBSET },
		{ name: 'int', doc: 'An integer local — also the loop counter of the C-style `for` the transpiler accepts.' + FF_SUBSET },
		{ name: 'bool', doc: 'A boolean local, for an `if` over a context query like `has_texel_size()`.' + FF_SUBSET },
		{ name: 'vec2', doc: 'A two-component local, and its constructor. What `texel_size()`, `quad_origin()` and the quad axes return, and what `p.screen_offset` takes.' + FF_SUBSET },
		{ name: 'vec3', doc: 'A three-component local, and its constructor. What `p.offset_color` takes.' + FF_SUBSET },
		{ name: 'vec4', doc: 'A four-component local, and its constructor. What `p.color` takes — `vec4(rgb, a)` is the usual form.' + FF_SUBSET },
		{ name: 'pass', doc: 'The type of a pass local. Declared as the bare statement `pass p;` — never with an initializer.' }
	],
	/** The maths the transpiler accepts inside a fixed_function block - deliberately a small subset */
	functions: [
		{ name: 'abs', doc: 'Absolute value.' + FF_SUBSET },
		{ name: 'ceil', doc: 'Nearest integer >= x.' + FF_SUBSET },
		{ name: 'clamp', doc: 'Clamps to `[min, max]`.' + FF_SUBSET },
		{ name: 'cos', doc: 'Cosine of an angle in radians. `float` only.' + FF_SUBSET },
		{ name: 'float', doc: 'Scalar constructor / cast, and the type of a scalar local.' + FF_SUBSET },
		{ name: 'floor', doc: 'Nearest integer <= x.' + FF_SUBSET },
		{ name: 'int', doc: 'Integer constructor / cast, and the type of an integer local.' + FF_SUBSET },
		{ name: 'max', doc: 'Component-wise maximum.' + FF_SUBSET },
		{ name: 'min', doc: 'Component-wise minimum.' + FF_SUBSET },
		{ name: 'mix', doc: 'Linear blend `a * (1 - t) + b * t`.' + FF_SUBSET },
		{ name: 'sin', doc: 'Sine of an angle in radians. `float` only.' + FF_SUBSET },
		{ name: 'sqrt', doc: 'Square root.' + FF_SUBSET }
	]
};

// Exactly the types the reflection parser knows (GlslReflect.cpp, BuiltinTypes) - anything else is an
// unknown type to the compiler, so it belongs in UNSUPPORTED_TYPES below instead of here. The sizes
// quoted are that table's std140 size/alignment, which is what decides a uniform block's offsets and
// therefore what the engine has to write into the buffer.
const NO_UINT_ES2 = '\n\n**ES2:** the ESSL100 lowering declines the shader outright — *"no unsigned integers in ESSL 100"*.';
const MATRIX_SW = '\n\n**Software renderer:** matrices are outside the transpiler\'s subset, so `--emit-sw-generated` declines the shader.';
const STD140_PAD = ' In a `layout(std140)` block that means the next member starts at the following 16-byte boundary **unless it fits in the 4-byte tail** — a `float` packs into the gap, a `vec2` does not.';

const GLSL_TYPES = [
	{ name: 'void', doc: 'No value — a return type only. Every entry point is `void`.' },
	{ name: 'bool', doc: 'Boolean. **4 bytes** in a std140 block (not 1), aligned to 4.' },
	{ name: 'int', doc: 'Signed 32-bit integer. 4 bytes, aligned to 4.\n\nA `flat`/integer varying becomes `nointerpolation` on the HLSL path automatically.' },
	{ name: 'uint', doc: 'Unsigned 32-bit integer. 4 bytes, aligned to 4.' + NO_UINT_ES2 },
	{ name: 'float', doc: 'Single-precision float. 4 bytes, aligned to 4. `double` does not exist in this language.' },
	{ name: 'vec2', doc: 'Two floats. **8 bytes, aligned to 8** — the one vector smaller than its alignment ceiling, so two of them pack into one 16-byte std140 slot.' },
	{ name: 'vec3', doc: 'Three floats. **12 bytes, but aligned to 16** — the std140 layout footgun.' + STD140_PAD + '\n\nIf you are laying out a block by hand, `vec4` with an unused `w` is usually the honest choice; the engine\'s own `InstanceBlock` avoids `vec3` entirely for this reason.' },
	{ name: 'vec4', doc: 'Four floats. 16 bytes, aligned to 16 — the only vector that needs no padding thought in a std140 block.' },
	{ name: 'bvec2', doc: 'Two booleans. 8 bytes, aligned to 8 — each component is a full 4 bytes.' },
	{ name: 'bvec3', doc: 'Three booleans. 12 bytes, aligned to 16.' + STD140_PAD },
	{ name: 'bvec4', doc: 'Four booleans. 16 bytes, aligned to 16.' },
	{ name: 'ivec2', doc: 'Two signed integers. 8 bytes, aligned to 8.' },
	{ name: 'ivec3', doc: 'Three signed integers. 12 bytes, aligned to 16.' + STD140_PAD },
	{ name: 'ivec4', doc: 'Four signed integers. 16 bytes, aligned to 16.' },
	{ name: 'uvec2', doc: 'Two unsigned integers. 8 bytes, aligned to 8.' + NO_UINT_ES2 },
	{ name: 'uvec3', doc: 'Three unsigned integers. 12 bytes, aligned to 16.' + STD140_PAD + NO_UINT_ES2 },
	{ name: 'uvec4', doc: 'Four unsigned integers. 16 bytes, aligned to 16.' + NO_UINT_ES2 },
	{ name: 'mat2', doc: 'A 2×2 matrix — **32 bytes**, not 16: std140 lays a matrix out as N columns at `vec4` stride, so each column wastes half a slot. Aligned to 16.' + MATRIX_SW },
	{ name: 'mat3', doc: 'A 3×3 matrix — **48 bytes**, not 36: three columns at `vec4` stride. Aligned to 16, and the classic source of a mismatch between the shader\'s idea of a block and the engine\'s.' + MATRIX_SW },
	{ name: 'mat4', doc: 'A 4×4 matrix — 64 bytes, aligned to 16. The one matrix whose std140 layout is the obvious one.' + MATRIX_SW },
	{
		name: 'sampler2D',
		doc: 'A 2D texture. A sampler uniform becomes a **texture binding** rather than a block member — it can never appear inside a `layout(std140)` block, and `texture_unit(N)` is the only way to assign its unit explicitly.\n\nIn canvas mode a document that references `TEXTURE` without declaring one gets `uniform sampler2D uTexture : texture_unit(0);` for free.'
	},
	{
		name: 'sampler3D',
		doc: 'A 3D texture. Recognized by reflection and emitted as-is for GL, ES and Vulkan.\n\n**D3D11:** mapped onto `Texture2D` like `sampler2D` — the engine binds no real 3D texture, and the committed HLSL depends on that mapping. The Cg dialect (PS3 / Vita) does emit a real `sampler3D`.'
	},
	{
		name: 'samplerCube',
		doc: 'A cube map. Recognized by reflection and emitted as-is for GL, ES and Vulkan; the Cg dialect emits `samplerCUBE`. Nothing in the engine\'s shaders uses one today, so treat this path as untravelled.'
	}
];

/**
 * Types the TextMate grammar highlights (it is deliberately permissive) but the compiler has no entry
 * for, so a declaration using one fails as an unknown type. Hover names them rather than completion
 * offering them - the same treatment UNSUPPORTED_BUILTINS gets.
 */
const UNSUPPORTED_TYPES = [
	'double', 'sampler2DArray', 'sampler2DShadow', 'isampler2D', 'usampler2D',
	'mat2x2', 'mat2x3', 'mat2x4', 'mat3x2', 'mat3x3', 'mat3x4', 'mat4x2', 'mat4x3', 'mat4x4'
];

/**
 * HLSL / Cg / Unity spellings mapped to the one this language uses. A `.shader` is GLSL and the
 * language deliberately has NO aliases, so these exist purely so that code pasted in from an HLSL or
 * Unity shader gets told what to rename rather than a bare "unknown identifier". The lowering to HLSL
 * happens inside the compiler (`mix` becomes `lerp`, `fract` becomes `frac`, …) — never in the source.
 */
const HLSL_SPELLINGS = {
	float2: 'vec2', float3: 'vec3', float4: 'vec4',
	int2: 'ivec2', int3: 'ivec3', int4: 'ivec4',
	uint2: 'uvec2', uint3: 'uvec3', uint4: 'uvec4',
	bool2: 'bvec2', bool3: 'bvec3', bool4: 'bvec4',
	float2x2: 'mat2', float3x3: 'mat3', float4x4: 'mat4',
	half: 'float', half2: 'vec2', half3: 'vec3', half4: 'vec4',
	fixed: 'float', fixed2: 'vec2', fixed3: 'vec3', fixed4: 'vec4',
	lerp: 'mix', frac: 'fract', ddx: 'dFdx', ddy: 'dFdy', rsqrt: 'inversesqrt',
	fmod: 'mod', atan2: 'atan (the two-argument form)', tex2D: 'texture', tex2Dlod: 'textureLod',
	saturate: 'clamp(x, 0.0, 1.0)', mul: 'the `*` operator', SV_Target: 'COLOR', SV_Position: 'gl_Position'
};

// A `.shader` is written in modern GLSL and then LOWERED to every backend, so what matters about a
// built-in here is not its spec wording (the editor's reader knows `min`) but whether it survives the
// trip. Three lowerings constrain the vocabulary, and each note below quotes the code that does it:
//
//   - `--emit-sw-generated` (the software renderer) transpiles to C++ against a fixed builtin set
//     (`GlslToCpp.cpp`, IsBuiltin/IsBannedCall). A shader using anything else is DECLINED - it simply
//     stays absent from the generated table, so the software backend falls back for that effect. That
//     is silent, which is exactly why it belongs in a tooltip.
//   - `--emit-hlsl` (D3D11, and the PS3 Cg path) knows a passthrough list plus a few remaps
//     (`Hlsl.cpp`, IsPassthroughBuiltin/EmitCall). Anything else is a hard `unknown function` error.
//   - `--emit-essl100` (the ES2 backend) rewrites only `texture`/`textureLod`, auto-enables
//     `GL_OES_standard_derivatives` for the derivatives, and explicitly declines `round()`
//     (`Essl100.cpp`). Every other ES3-only built-in passes through unrewritten and fails in the
//     driver instead of in the compiler - the one class of breakage the tool cannot catch for you.
//
// The GL3.3, Vulkan/SPIR-V and legacy-GL paths emit the GLSL essentially as written, so they never
// constrain a built-in and are not mentioned per entry.
const SW_DECLINES = '\n\n**Software renderer:** outside the transpiler\'s builtin set, so `--emit-sw-generated` **declines** the whole shader (it silently gets no software path).';
const HLSL_FAILS = '\n\n**D3D11 / PS3:** the HLSL emitter has no lowering for it — a hard `unknown function` error.';
const ES2_FAILS = '\n\n**ES2:** not in ESSL 1.00, and the lowering does not rewrite it, so it reaches the driver as-is and fails there.';
const MATRIX_NOTE = '\n\nMatrices are outside the software transpiler\'s subset entirely.';

const GLSL_FUNCTIONS = [
	{ name: 'abs', detail: 'genType abs(genType x)', doc: 'Absolute value, component-wise.' },
	{ name: 'acos', detail: 'genType acos(genType x)', doc: 'Arc cosine, in radians. Undefined outside `[-1, 1]`.' },
	{ name: 'all', detail: 'bool all(bvecN v)', doc: 'True when **every** component is true — the reduction over a comparison built-in.' + SW_DECLINES },
	{ name: 'any', detail: 'bool any(bvecN v)', doc: 'True when **any** component is true — the reduction over a comparison built-in.' + SW_DECLINES },
	{ name: 'asin', detail: 'genType asin(genType x)', doc: 'Arc sine, in radians. Undefined outside `[-1, 1]`.' },
	{ name: 'atan', detail: 'genType atan(genType y_over_x) / atan(genType y, genType x)', doc: 'Arc tangent. The two-argument form takes the quadrant into account and is the one you want for an angle from a vector.\n\n**D3D11 / PS3:** the two-argument form lowers to `atan2`.' },
	{ name: 'ceil', detail: 'genType ceil(genType x)', doc: 'Nearest integer >= x.' },
	{ name: 'clamp', detail: 'genType clamp(genType x, min, max)', doc: 'Clamps to `[min, max]`; `min > max` is undefined. `min`/`max` may be scalars.' },
	{ name: 'cos', detail: 'genType cos(genType angle)', doc: 'Cosine of an angle in radians.' },
	{ name: 'cross', detail: 'vec3 cross(vec3 a, vec3 b)', doc: 'Cross product. `vec3` only.' },
	{ name: 'degrees', detail: 'genType degrees(genType radians)', doc: 'Radians to degrees.' },
	{ name: 'determinant', detail: 'float determinant(matN m)', doc: 'Determinant of a square matrix. GLSL 1.20+.' + SW_DECLINES + MATRIX_NOTE + ES2_FAILS },
	{
		name: 'dFdx',
		detail: 'genType dFdx(genType p)',
		doc: 'Partial derivative of `p` in screen x — fragment stage only, and only meaningful because the GPU shades in 2×2 quads.\n\n**Software renderer:** supported but **approximated with a small constant** — the CPU path has no quad. Fine for widening an anti-aliasing edge (what FrozenMask uses it for), wrong for anything that needs a real gradient.\n\n**ES2:** the lowering prepends `#extension GL_OES_standard_derivatives : enable` for you; a device without that extension still fails at link time.\n\n**D3D11 / PS3:** lowers to `ddx`.'
	},
	{
		name: 'dFdy',
		detail: 'genType dFdy(genType p)',
		doc: 'Partial derivative of `p` in screen y — fragment stage only. Same caveats as `dFdx`: approximated by a constant in software, needs `GL_OES_standard_derivatives` on ES2 (added for you), lowers to `ddy` for D3D11 and the PS3.'
	},
	{ name: 'distance', detail: 'float distance(vecN a, vecN b)', doc: '`length(a - b)`.' },
	{ name: 'dot', detail: 'float dot(vecN a, vecN b)', doc: 'Dot product.' },
	{ name: 'equal', detail: 'bvecN equal(vecN a, vecN b)', doc: 'Component-wise `==`, yielding a bool vector — feed it to `all()`/`any()`.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `==` operator, which is component-wise in HLSL anyway.' },
	{ name: 'exp', detail: 'genType exp(genType x)', doc: 'e raised to x.' + SW_DECLINES + ' The set has `exp2` but **not** `exp`, so write `exp2(x * 1.442695)` in a shader that must also render in software.' },
	{ name: 'exp2', detail: 'genType exp2(genType x)', doc: '2 raised to x. Portable everywhere, unlike `exp`.' },
	{ name: 'faceforward', detail: 'genType faceforward(genType N, genType I, genType Nref)', doc: '`N` flipped to face away from `I` — `dot(Nref, I) < 0 ? N : -N`.' + SW_DECLINES + HLSL_FAILS },
	{ name: 'floor', detail: 'genType floor(genType x)', doc: 'Nearest integer <= x.' },
	{ name: 'fract', detail: 'genType fract(genType x)', doc: '`x - floor(x)`.\n\n**D3D11 / PS3:** lowers to `frac`.' },
	{
		name: 'fwidth',
		detail: 'genType fwidth(genType p)',
		doc: '`abs(dFdx(p)) + abs(dFdy(p))` — the screen-space footprint of `p`, the usual width for an analytic anti-aliased edge.\n\nSame caveats as `dFdx`: **approximated by a constant** in the software renderer, needs `GL_OES_standard_derivatives` on ES2 (added for you), passthrough on D3D11 and the PS3.'
	},
	{ name: 'greaterThan', detail: 'bvecN greaterThan(vecN a, vecN b)', doc: 'Component-wise `>`, yielding a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `>` operator.' },
	{ name: 'greaterThanEqual', detail: 'bvecN greaterThanEqual(vecN a, vecN b)', doc: 'Component-wise `>=`, yielding a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `>=` operator.' },
	{ name: 'inverse', detail: 'matN inverse(matN m)', doc: 'Matrix inverse. GLSL 1.40+ — and expensive; prefer passing the inverse in as a uniform.' + SW_DECLINES + MATRIX_NOTE + HLSL_FAILS + ES2_FAILS },
	{ name: 'inversesqrt', detail: 'genType inversesqrt(genType x)', doc: '`1 / sqrt(x)`.\n\n**D3D11 / PS3:** lowers to `rsqrt`.' },
	{ name: 'length', detail: 'float length(vecN v)', doc: 'Euclidean length.' },
	{ name: 'lessThan', detail: 'bvecN lessThan(vecN a, vecN b)', doc: 'Component-wise `<`, yielding a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `<` operator.' },
	{ name: 'lessThanEqual', detail: 'bvecN lessThanEqual(vecN a, vecN b)', doc: 'Component-wise `<=`, yielding a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `<=` operator.' },
	{ name: 'log', detail: 'genType log(genType x)', doc: 'Natural logarithm.' + SW_DECLINES + ' The set has `log2` but **not** `log`, so write `log2(x) * 0.693147` in a shader that must also render in software.' },
	{ name: 'log2', detail: 'genType log2(genType x)', doc: 'Base-2 logarithm. Portable everywhere, unlike `log`.' },
	{ name: 'matrixCompMult', detail: 'matN matrixCompMult(matN a, matN b)', doc: 'Component-wise matrix product — **not** the linear-algebra one, which is plain `a * b`.' + SW_DECLINES + MATRIX_NOTE + HLSL_FAILS },
	{ name: 'max', detail: 'genType max(genType x, y)', doc: 'Component-wise maximum; `y` may be a scalar.' },
	{ name: 'min', detail: 'genType min(genType x, y)', doc: 'Component-wise minimum; `y` may be a scalar.' },
	{ name: 'mix', detail: 'genType mix(genType a, genType b, genType|float t)', doc: 'Linear blend `a * (1 - t) + b * t`; `t` outside `[0, 1]` extrapolates.\n\n**D3D11 / PS3:** lowers to `lerp`.' },
	{ name: 'mod', detail: 'genType mod(genType x, genType|float y)', doc: '`x - y * floor(x / y)` — the sign follows `y`, so it stays positive for a positive divisor (unlike C\'s `%`).\n\n**D3D11 / PS3:** expanded to that formula rather than `fmod`, which truncates toward zero and would differ for negative operands.\n\n**ES2:** the *float* `mod` is fine; the integer `%` operator is ES3-only and the lowering declines it.' },
	{ name: 'normalize', detail: 'genType normalize(genType v)', doc: 'Scales to unit length; undefined for a zero vector.' },
	{ name: 'not', detail: 'bvecN not(bvecN v)', doc: 'Component-wise logical negation of a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `!` operator.' },
	{ name: 'notEqual', detail: 'bvecN notEqual(vecN a, vecN b)', doc: 'Component-wise `!=`, yielding a bool vector.' + SW_DECLINES + '\n\n**D3D11 / PS3:** lowers to the `!=` operator.' },
	{ name: 'outerProduct', detail: 'matN outerProduct(vecN c, vecN r)', doc: 'Column vector times row vector. GLSL 1.20+.' + SW_DECLINES + MATRIX_NOTE + HLSL_FAILS + ES2_FAILS },
	{ name: 'pow', detail: 'genType pow(genType x, genType y)', doc: 'x raised to y. Undefined for `x < 0`, and for `x == 0 && y <= 0`.' },
	{ name: 'radians', detail: 'genType radians(genType degrees)', doc: 'Degrees to radians.' },
	{ name: 'reflect', detail: 'genType reflect(genType I, genType N)', doc: '`I - 2 * dot(N, I) * N`; `N` must already be normalized.' },
	{ name: 'refract', detail: 'genType refract(genType I, genType N, float eta)', doc: 'Refraction vector for a ratio of indices `eta`; both `I` and `N` must be normalized.' + SW_DECLINES },
	{ name: 'round', detail: 'genType round(genType x)', doc: 'Nearest integer; the half-way rounding direction is implementation-defined (use `roundEven` if it matters — which nothing here emits).\n\n**ES2: a hard error.** The ESSL100 lowering declines the shader with *"round() (GLSL ES 3.00+ only)"* — the one ES3-only built-in the compiler catches by name. Write `floor(x + 0.5)` instead if the shader has to reach the ES2 backend.' },
	{ name: 'sign', detail: 'genType sign(genType x)', doc: '-1, 0 or +1, component-wise.' },
	{ name: 'sin', detail: 'genType sin(genType angle)', doc: 'Sine of an angle in radians.' },
	{ name: 'smoothstep', detail: 'genType smoothstep(edge0, edge1, genType x)', doc: 'Hermite interpolation between the edges — 0 below `edge0`, 1 above `edge1`, smooth in between. Undefined when `edge0 >= edge1`.' },
	{ name: 'sqrt', detail: 'genType sqrt(genType x)', doc: 'Square root; undefined for `x < 0`.' },
	{ name: 'step', detail: 'genType step(genType|float edge, genType x)', doc: '0 when `x < edge`, else 1 — the branchless comparison, and the one to reach for under `NO_DYNAMIC_BRANCHING` (the PS3\'s RSX fragment path).' },
	{ name: 'tan', detail: 'genType tan(genType angle)', doc: 'Tangent of an angle in radians.' },
	{ name: 'texelFetch', detail: 'vec4 texelFetch(sampler2D s, ivec2 texel, int lod)', doc: 'Unfiltered read of one texel by integer coordinate, bypassing the sampler state. GLSL 1.30+ / ES 3.00+.\n\n**Software renderer:** explicitly banned.' + HLSL_FAILS + ES2_FAILS + '\n\nIn practice that leaves GL3.3 and Vulkan only — a shader using it needs a `#if` fallback for every other backend.' },
	{ name: 'texture', detail: 'vec4 texture(sampler2D s, vec2 uv [, float bias])', doc: 'Filtered sample at the sampler\'s own mip level. In canvas mode write `texture(TEXTURE, UV)`.\n\nLowered per backend: `texture2D` on ES2, `s.Sample(s_sampler, uv)` for D3D11, `tex2D` for the PS3\'s Cg, and a runtime sampler call in software. The only texture built-in that is portable across all of them.' },
	{ name: 'textureGrad', detail: 'vec4 textureGrad(sampler2D s, vec2 uv, vec2 dPdx, vec2 dPdy)', doc: 'Sample with explicit screen-space derivatives. GLSL 1.30+ / ES 3.00+.\n\n**Software renderer:** explicitly banned.' + HLSL_FAILS + ES2_FAILS },
	{ name: 'textureLod', detail: 'vec4 textureLod(sampler2D s, vec2 uv, float lod)', doc: 'Sample at an explicit mip level.\n\n**Software renderer:** explicitly banned — use `texture` there.\n\n**ES2:** rewritten to `texture2DLod`, which ESSL 1.00 only guarantees in the *vertex* stage.\n\n**D3D11 / PS3:** `SampleLevel`, and `tex2Dlod(s, float4(uv, 0.0, lod))` for Cg.' },
	{ name: 'textureProj', detail: 'vec4 textureProj(sampler2D s, vec3 uvq)', doc: 'Sample after dividing the coordinate by its last component.\n\n**Software renderer:** explicitly banned.' + HLSL_FAILS + '\n\n**ES2:** ESSL 1.00 spells it `texture2DProj` and the lowering does not rewrite it, so it fails in the driver.' },
	{ name: 'textureSize', detail: 'ivec2 textureSize(sampler2D s, int lod)', doc: 'Dimensions of a mip level. GLSL 1.30+ / ES 3.00+.\n\n**Software renderer:** explicitly banned.' + HLSL_FAILS + ES2_FAILS + '\n\nPass the size in as a uniform instead — that is what the engine\'s own shaders do.' },
	{ name: 'transpose', detail: 'matN transpose(matN m)', doc: 'Matrix transpose. GLSL 1.20+.' + SW_DECLINES + MATRIX_NOTE + ES2_FAILS },
	{ name: 'trunc', detail: 'genType trunc(genType x)', doc: 'Truncation toward zero. GLSL 1.30+ / ES 3.00+.' + SW_DECLINES + ES2_FAILS + '\n\nUnlike `round()` the compiler does **not** catch this one for ES2 — it fails in the driver.' }
];

const GLSL_KEYWORDS = [
	'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default', 'break', 'continue', 'return',
	'discard', 'const', 'struct', 'in', 'out', 'inout', 'flat', 'smooth', 'noperspective', 'centroid',
	'invariant', 'layout', 'lowp', 'mediump', 'highp', 'true', 'false'
];

const GL_BUILTIN_VARIABLES = [
	{ name: 'gl_Position', doc: 'The clip-space vertex position. A custom-mode `vertex()` writes it itself; the canvas epilogue writes it for you.' },
	{ name: 'gl_VertexID', doc: 'The vertex index. Rewritten per backend where the target has no equivalent.' },
	{ name: 'gl_FragCoord', doc: 'The window-space fragment coordinate.' },
	{ name: 'gl_PointSize', doc: 'The point size, vertex stage only.' },
	{ name: 'gl_FrontFacing', doc: 'True for front-facing fragments.' }
];

/** Uniforms and varyings the engine's sprite contract provides in canvas mode */
const CANVAS_CONTRACT = [
	{ name: 'uProjectionMatrix', doc: 'Engine-provided projection matrix (`mat4`).' },
	{ name: 'uViewMatrix', doc: 'Engine-provided view matrix (`mat4`).' },
	{ name: 'uTexture', doc: 'The sprite texture. Auto-declared at texture unit 0 in canvas mode when `TEXTURE` is referenced and no explicit declaration exists.' },
	{ name: 'vTexCoords', doc: 'The template texture-coordinate varying that `UV` lowers to.' },
	{ name: 'vColor', doc: 'The template instance-colour varying that seeds `COLOR` in a canvas `fragment()`.' },
	{ name: 'vPaletteOffset', doc: 'The template palette-offset varying that `PALETTE_OFFSET` lowers to.' },
	{ name: 'InstanceBlock', doc: 'The engine\'s std140 per-sprite block: `modelMatrix`, `color`, `texRect`, `spriteSize`, `palOffset`.' },
	{ name: 'InstancesBlock', doc: 'The batched twin\'s std140 block — an array of `BATCH_SIZE` instance structs.' }
];

module.exports = {
	DIRECTIVES: DIRECTIVES,
	ENTRY_POINTS: ENTRY_POINTS,
	SHADER_TYPES: SHADER_TYPES,
	RENDER_MODES: RENDER_MODES,
	PRECISION_QUALIFIERS: PRECISION_QUALIFIERS,
	UNIFORM_HINTS: UNIFORM_HINTS,
	FIXED_FUNCTION_TARGETS: FIXED_FUNCTION_TARGETS,
	BUILTINS: BUILTINS,
	UNSUPPORTED_BUILTINS: UNSUPPORTED_BUILTINS,
	UNSUPPORTED_TYPES: UNSUPPORTED_TYPES,
	HLSL_SPELLINGS: HLSL_SPELLINGS,
	STAGE_MACROS: STAGE_MACROS,
	FIXED_FUNCTION: FIXED_FUNCTION,
	GLSL_TYPES: GLSL_TYPES,
	GLSL_FUNCTIONS: GLSL_FUNCTIONS,
	GLSL_KEYWORDS: GLSL_KEYWORDS,
	GL_BUILTIN_VARIABLES: GL_BUILTIN_VARIABLES,
	CANVAS_CONTRACT: CANVAS_CONTRACT
};
