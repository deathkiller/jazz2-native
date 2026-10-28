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
		doc: 'Names the program. Required exactly once, before all other directives and the entry points.'
	},
	{
		name: 'shader_type',
		insert: 'shader_type ${1|canvas_item,custom|};',
		detail: 'shader_type canvas_item | custom;',
		doc: 'Default is `custom`. `canvas_item` generates the vertex stage from the engine\'s sprite template and enables `batched`.'
	},
	{
		name: 'variant',
		insert: 'variant ${1:NAME};',
		detail: 'variant <NAME>; | variant <A> + <B>;',
		doc: 'Declares an optional variant, compiled with `NAME` defined. `Variants[0]` is always the unnamed base.\n\n`variant A + B;` declares the combined variant `A_B`; both parts must be declared before it.'
	},
	{
		name: 'render_mode',
		insert: 'render_mode ${1|blend_mix,blend_add,blend_sub,blend_mul,blend_premul_alpha,unshaded|};',
		detail: 'render_mode <mode>[, <mode>];',
		doc: 'Any of `blend_mix`, `blend_add`, `blend_sub`, `blend_mul`, `blend_premul_alpha`, `unshaded`. Stored in the `RenderModes` bitmask.'
	},
	{
		name: 'precision',
		insert: 'precision ${1|mediump,highp|};',
		detail: 'precision mediump | highp;',
		doc: 'Float precision of the fragment stage on GL ES (default `mediump`).\n\n`precision highp float;` with a type is plain GLSL, not this directive.'
	},
	{
		name: 'batched',
		insert: 'batched ${1:Name};',
		detail: 'batched <Name>;',
		doc: 'Also emits a batched (instanced) version of the program. `canvas_item` only - an error in custom mode.'
	},
	{
		name: 'attribute',
		insert: 'attribute ${1:vec2} ${2:aValue};',
		detail: 'attribute [layout(location = N)] <declaration>;',
		doc: 'Vertex input, emitted as `in` in the vertex stage only. A `layout(location = N)` prefix is kept.'
	},
	{
		name: 'varying',
		insert: 'varying ${1:vec2} ${2:vValue};',
		detail: 'varying [flat] [precision] <type> <name>;',
		doc: '`out` in the vertex stage, `in` in the fragment stage. Removed automatically if the fragment stage never reads it.'
	},
	{
		name: 'uniform',
		insert: 'uniform ${1:vec4} ${2:uValue};',
		detail: 'uniform <type> <name> [: <hint>[, <hint>]];',
		doc: 'Shared by both stages, removed from a stage that does not use it. Only the `texture_unit(N)` hint has an effect.'
	}
];

/** `void <name>(...)` entry points */
const ENTRY_POINTS = [
	{
		name: 'vertex',
		insert: 'void vertex() {\n\t$0\n}',
		detail: 'void vertex() { ... }',
		doc: '**Custom mode:** required. Becomes `main()` and must write `gl_Position`.\n\n**Canvas mode:** optional. Adjust `VERTEX`, `UV`, `COLOR` or `PALETTE_OFFSET`; the template does the rest. `return;` is not allowed.'
	},
	{
		name: 'fragment',
		insert: 'void fragment() {\n\tCOLOR = $0;\n}',
		detail: 'void fragment() { ... }',
		doc: 'Required. Write the result to `COLOR` on every path. An early `return;` is fine.\n\nUse `COLOR`, never `fragColor`.'
	},
	{
		name: 'fixed_function',
		insert: 'void fixed_function() {\n\tpass ${1:p};\n\t${1:p}.color = ${2:COLOR};\n\tsubmit_quad(${1:p});\n}',
		detail: 'void fixed_function([<target>[, <target>]]) { ... }',
		doc: 'The effect for fixed-function consoles, transpiled to C++ (not GLSL).\n\n`()` is the generic block for every backend; `(pvr)`, `(gu, gs)` etc. override it for those backends. Each target may appear in only one block.\n\nTargets: `pvr`, `gx`, `pica`, `gu`, `gs`, `rdp`, `legacygl`.'
	}
];

const SHADER_TYPES = [
	{ name: 'canvas_item', doc: 'Sprite template: generated vertex stage, canvas built-ins and `batched`.' },
	{ name: 'custom', doc: 'No template and no built-ins (default).' }
];

const RENDER_MODES = [
	{ name: 'blend_mix', doc: 'Standard alpha blending.' },
	{ name: 'blend_add', doc: 'Additive blending.' },
	{ name: 'blend_sub', doc: 'Subtractive blending.' },
	{ name: 'blend_mul', doc: 'Multiplicative blending.' },
	{ name: 'blend_premul_alpha', doc: 'Premultiplied alpha blending.' },
	{ name: 'unshaded', doc: 'Skips lighting for this material.' }
];

const PRECISION_QUALIFIERS = [
	{ name: 'mediump', doc: 'Default float precision on GL ES.' },
	{ name: 'highp', doc: 'High float precision on GL ES.' }
];

/** Hints legal after the `:` of a uniform declaration */
const UNIFORM_HINTS = [
	{
		name: 'texture_unit',
		insert: 'texture_unit(${1:0})',
		doc: 'Sets the texture unit (0-31) of a sampler uniform. At most one per uniform.'
	},
	{ name: 'source_color', doc: 'Accepted, but has no effect.' },
	{ name: 'hint_range', insert: 'hint_range(${1:0.0}, ${2:1.0})', doc: 'Accepted, but has no effect.' },
	{ name: 'filter_nearest', doc: 'Accepted, but has no effect.' },
	{ name: 'filter_linear', doc: 'Accepted, but has no effect.' },
	{ name: 'repeat_enable', doc: 'Accepted, but has no effect.' },
	{ name: 'repeat_disable', doc: 'Accepted, but has no effect.' }
];

// Each target's own line says what its texture combiner can do, because that - not the console's
// overall power - is what decides which `p.tev` presets a block naming it may use. A block with a
// target list is held to the INTERSECTION of its targets, and the generic block to every backend.
const FIXED_FUNCTION_TARGETS = [
	{
		name: 'pvr',
		doc: 'Dreamcast (PowerVR2).\n\nCan only modulate: `p.tev` is ignored entirely, and `TINT_MIX` / `LUMA_RAMP` are errors. Strips up to 8 vertices.'
	},
	{
		name: 'gx',
		doc: 'Wii / GameCube (GX).\n\nSupports every `p.tev` preset, including `LUMA_RAMP`. Strips up to 16 vertices.'
	},
	{
		name: 'pica',
		doc: 'Nintendo 3DS (PICA200).\n\nSupports every `p.tev` preset except `LUMA_RAMP`. Strips up to 16 vertices.'
	},
	{
		name: 'gu',
		doc: 'PlayStation Portable (GE).\n\nNo `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX` or `LUMA_RAMP` - use extra passes instead. Strips up to 16 vertices.'
	},
	{
		name: 'gs',
		doc: 'PlayStation 2 (GS).\n\nNo `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX` or `LUMA_RAMP` - use extra passes instead. Strips up to 16 vertices.'
	},
	{
		name: 'rdp',
		doc: 'Nintendo 64 (RDP).\n\nSupports `MODULATE_X2` and `TINT_MIX`, but not `MODULATE_X4` or `LUMA_RAMP`. Strips up to 16 vertices.'
	},
	{
		name: 'legacygl',
		doc: 'OpenGL 1.x (TinyGL on MorphOS, or desktop GL in a compatibility context).\n\nSupports every `p.tev` preset except `LUMA_RAMP`. Strips up to 16 vertices.'
	}
];

/** Canvas-mode built-ins (and `COLOR`, which is the fragment output in both modes) */
const BUILTINS = [
	{
		name: 'COLOR',
		doc: 'In `fragment()`: the fragment output. Starts as the instance colour in canvas mode, undefined in custom mode.\n\nIn a canvas `vertex()`: the instance colour passed to the fragment stage.'
	},
	{ name: 'UV', doc: 'Canvas mode. The sprite\'s texture coordinates.' },
	{ name: 'TEXTURE', doc: 'Canvas mode, `fragment()` only. The sprite texture (`uTexture`, declared automatically if missing).' },
	{ name: 'PALETTE_OFFSET', doc: 'Canvas mode. The sprite\'s palette offset.' },
	{ name: 'VERTEX', doc: 'Canvas `vertex()` only. The sprite-local position in pixels, before the model/view/projection transform.' }
];

/** Built-ins the canvas lowering explicitly reports as unsupported */
const UNSUPPORTED_BUILTINS = ['NORMAL', 'SCREEN_UV', 'SCREEN_PIXEL_SIZE', 'TIME', 'POINT_COORD'];

// Macros the compiler resolves itself, so that none of them ever appears in an emitted source. All
// of them work in `#ifdef` / `#ifndef` and in `#if` / `#elif` expressions; only `#define` / `#undef`
// of one is an error. The first two are resolved during stage assembly, the rest when a stage source
// is built for a given emission. A `.shader` writes every conditional as `#if` - including the ones
// on variant defines, which are resolved for each variant as its sources are built, and on other flags,
// which the compiler lowers to `#ifdef` / `defined(...)` on the way out so the emitted GLSL stays legal
// on the ES profiles.
const CONVENTION = '\n\nTest it with `#if`; it cannot be `#define`d or `#undef`ed.';
const STAGE_MACROS = [
	{ name: 'VERTEX_STAGE', doc: 'Defined only in the vertex stage.' + CONVENTION },
	{ name: 'FRAGMENT_STAGE', doc: 'Defined only in the fragment stage.' + CONVENTION },
	{ name: 'SOFTWARE_RENDERER', doc: 'Defined only for the software renderer.' + CONVENTION },
	{ name: 'NO_DYNAMIC_BRANCHING', doc: 'Defined only for the PS3, whose fragment stage cannot branch dynamically. Put branching code behind `#if !NO_DYNAMIC_BRANCHING`.' + CONVENTION },
	{ name: 'LOW_POWER_GPU', doc: 'Defined for PS Vita and OpenGL ES 2.0. Use it to pick a cheaper version of an expensive effect.' + CONVENTION }
];

// Which console can express which `p.tev` preset, rendered into the `tev` hover so the whole picture
// is one tooltip away. A dash is a hard compile error for every block that reaches that backend -
// including the generic one, which is transpiled for all of them - never a silent fallback; the PVR's
// "ignored" is the one silent case, and it is why the presets it ignores are still rejected for a
// block shared with a backend that would error on them.
const TEV_SUPPORT_TABLE = 'A block may only use presets that all of its targets support:\n\n' +
	'| preset | `pvr` | `gx` | `pica` | `gu` | `gs` | `rdp` | `legacygl` |\n' +
	'| --- | --- | --- | --- | --- | --- | --- | --- |\n' +
	'| `MODULATE` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |\n' +
	'| `SILHOUETTE` | *ignored* | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |\n' +
	'| `MODULATE_X2` | *ignored* | ✓ | ✓ | — | — | ✓ | ✓ |\n' +
	'| `MODULATE_X4` | *ignored* | ✓ | ✓ | — | — | — | ✓ |\n' +
	'| `TINT_MIX` | — | ✓ | ✓ | — | — | ✓ | ✓ |\n' +
	'| `LUMA_RAMP` | — | ✓ | — | — | — | — | — |\n\n' +
	'`—` is a compile error, also in the generic block. *ignored* draws a plain `MODULATE`.';

// The maths a fixed_function body accepts overlaps GLSL by name (`mix`, `min`, `abs`, `float`, …) but
// not by meaning: the body is transpiled to C++ that the console's CPU runs once per draw to BUILD a
// pass, so none of the GLSL entries' per-backend lowering notes apply. Saying so once, here, is what
// lets the hover pick the right entry for the cursor's context.
// The scratch the strip builder writes into is a backend capability, and a LITERAL index or count
// outside it is a hard error rather than a clamp, because at runtime an out-of-range index is
// dropped and an oversized count clamped - which would silently draw the wrong geometry.
const STRIP_CAPACITY = '\n\nUp to 8 vertices on `pvr`, 16 elsewhere.';

const FF_SUBSET = '\n\nEvaluated on the CPU **once per draw**, not per pixel.';

/** The fixed-function DSL: statements, pass fields, context facilities and the allowed maths */
const FIXED_FUNCTION = {
	statements: [
		{ name: 'pass', insert: 'pass ${1:p};', detail: 'pass <name>;', doc: 'Declares a pass: hardware state applied over the sprite quad.' },
		{ name: 'pipeline', insert: 'pipeline ${1|tile_map_mesh,lighting_combine,line_strip_mesh|};', detail: 'pipeline <name>;', doc: 'Runs a built-in backend stage. Must be the **first** statement; passes after it draw over its output.' }
	],
	submits: [
		{ name: 'submit_quad', insert: 'submit_quad(${1:p})', doc: 'Draws the pass over the sprite quad.' },
		{ name: 'submit_strip', insert: 'submit_strip(${1:p}, ${2:4})', detail: 'submit_strip(<pass>, <count>)', doc: 'Draws the pass over a textured strip of `count` vertices from `strip_position` / `strip_uv`, in the pass colour.' + STRIP_CAPACITY },
		{ name: 'submit_strip_shaded', insert: 'submit_strip_shaded(${1:p}, ${2:4})', detail: 'submit_strip_shaded(<pass>, <count>)', doc: 'Like `submit_strip`, but with per-vertex colours from `strip_color`. Untextured unless `p.tev` is `TINT_MIX`.' + STRIP_CAPACITY }
	],
	// The strip builder is INDEXED: every helper takes the vertex slot first and the value second
	stripHelpers: [
		{ name: 'strip_position', insert: 'strip_position(${1:0}, ${2:vec2(0.0, 0.0)})', detail: 'strip_position(<index>, <vec2>)', doc: 'Sets the position of strip vertex `index`.' + STRIP_CAPACITY },
		{ name: 'strip_uv', insert: 'strip_uv(${1:0}, ${2:vec2(0.0, 0.0)})', detail: 'strip_uv(<index>, <vec2>)', doc: 'Sets the texture coordinate of strip vertex `index`.' + STRIP_CAPACITY },
		{ name: 'strip_color', insert: 'strip_color(${1:0}, ${2:vec4(1.0, 1.0, 1.0, 1.0)})', detail: 'strip_color(<index>, <vec4>)', doc: 'Sets the colour of strip vertex `index`. Used only by `submit_strip_shaded`.' + STRIP_CAPACITY },
		{ name: 'strip_q', insert: 'strip_q(${1:0}, ${2:1.0})', detail: 'strip_q(<index>, <float>)', doc: 'Sets the perspective q of strip vertex `index`, for the next strip only. `gu` ignores it.' + STRIP_CAPACITY },
		{ name: 'strip_clip', insert: 'strip_clip(${1:vec4(0.0, 0.0, 1.0, 1.0)})', detail: 'strip_clip(<vec4>)', doc: 'Clips all following strips to a screen rectangle (min x, min y, max x, max y).' }
	],
	context: [
		{ name: 'texel_size', doc: 'Texel size of the bound texture (`vec2`).' },
		{ name: 'has_texel_size', doc: 'True if `texel_size()` is available.' },
		{ name: 'quad_origin', doc: 'Origin of the sprite quad in screen space (`vec2`).' },
		{ name: 'quad_axis_x', doc: 'X edge of the sprite quad in screen space (`vec2`).' },
		{ name: 'quad_axis_y', doc: 'Y edge of the sprite quad in screen space (`vec2`).' },
		{ name: 'quad_tex_rect', doc: 'The instance texRect (`vec4`: U scale, U bias, V scale, V bias).' },
		// The argument is an IDENTIFIER, not a string literal - the block expression tokenizer has no
		// string literals at all, so a quoted name is a hard `unexpected token '"'` from the compiler.
		// That is why these snippets insert a bare name (see the uniform builtins in
		// ShaderCompiler/ConsoleFixedFunction.cpp). Reading uniforms by name is the only way a block
		// touches engine state, which is what keeps the language game-neutral.
		{ name: 'has_uniform', insert: 'has_uniform(${1:uName})', doc: 'True if the uniform exists. Pass a bare name, not a string: `has_uniform(uWaterLevel)`.' },
		{ name: 'uniform_float', insert: 'uniform_float(${1:uName})', doc: 'Reads a `float` uniform. Pass a bare name, not a string: `uniform_float(uWaterLevel)`.' },
		{ name: 'uniform_vec2', insert: 'uniform_vec2(${1:uName})', doc: 'Reads a `vec2` uniform. Pass a bare name, not a string: `uniform_vec2(uShift)`.' },
		{ name: 'uniform_vec4', insert: 'uniform_vec4(${1:uName})', doc: 'Reads a `vec4` uniform. Pass a bare name, not a string: `uniform_vec4(uAmbientColor)`.' }
	],
	passFields: [
		{ name: 'color', doc: 'The pass colour (`vec4`).' },
		{ name: 'offset_color', doc: 'The pass offset (specular) colour (`vec3`).' },
		{ name: 'screen_offset', doc: 'Screen-space offset of the quad (`vec2`).' },
		{ name: 'blend', doc: 'Blend mode: `MATERIAL`, `ADD`, `OPAQUE` or `ALPHA`. All work on every backend.' },
		{ name: 'tev', doc: 'Texture combiner preset.\n\n' + TEV_SUPPORT_TABLE },
		{ name: 'luma_gain', doc: 'Brightness multiplier for `LUMA_RAMP` (`float`), so effectively `gx` only.' },
		{ name: 'texture_repeat', doc: 'Wraps the texture instead of cutting the strip at its edges (`true`/`false`). Only in `fixed_function(rdp)`.' },
	],
	// The support notes below are the authoring constraint, not trivia: a block is validated against
	// the intersection of what its targets can do, so a preset one of them lacks is a hard error for
	// the whole block. They mirror the capability gates in ShaderCompiler/ConsoleFixedFunction.cpp.
	blendModes: [
		{ name: 'MATERIAL', doc: 'Uses the material\'s blending (default).' },
		{ name: 'ADD', doc: 'Additive blending, e.g. for glows.' },
		{ name: 'OPAQUE', doc: 'No blending.' },
		{ name: 'ALPHA', doc: 'Standard alpha blending, regardless of the material.' }
	],
	tevPresets: [
		{
			name: 'MODULATE',
			doc: '`texture * colour` (default). Every backend.'
		},
		{
			name: 'SILHOUETTE',
			doc: 'Solid pass colour wherever the texture has alpha - for masks, shadows and glows.\n\n`gx`, `pica`, `gu`, `gs`, `rdp`, `legacygl`. Ignored on `pvr` (draws `MODULATE`).'
		},
		{
			name: 'MODULATE_X2',
			doc: '`MODULATE`, doubled.\n\n`gx`, `pica`, `rdp`, `legacygl`. Ignored on `pvr`. Error on `gu`, `gs` and in the generic block - use an extra additive pass instead.'
		},
		{
			name: 'MODULATE_X4',
			doc: '`MODULATE`, quadrupled.\n\n`gx`, `pica`, `legacygl`. Ignored on `pvr`. Error on `gu`, `gs`, `rdp` and in the generic block.'
		},
		{
			name: 'TINT_MIX',
			doc: '`mix(texel, colour, alpha)` - tints the texture toward the pass colour.\n\n`gx`, `pica`, `rdp`, `legacygl`. Error on `pvr`, `gu`, `gs` and in the generic block.'
		},
		{
			name: 'LUMA_RAMP',
			doc: 'Silhouette coloured by texel brightness: `p.color` for dark, `p.offset_color` for bright, scaled by `p.luma_gain`.\n\n`gx` only - use it in a `fixed_function(gx)` block.'
		}
	],
	pipelines: [
		{ name: 'tile_map_mesh', doc: 'Draws a whole tile layer as one mesh.' },
		{ name: 'lighting_combine', doc: 'Composites the lightmap. Passes after it draw over the result (e.g. the water overlay).' },
		{ name: 'line_strip_mesh', doc: 'Draws a textured line strip (the weapon wheel).' }
	],
	// `COLOR` means something different here than in a GLSL stage, so the fixed-function half of the
	// language documents its own - the hover picks the entry the cursor's context calls for
	builtins: [
		{
			name: 'COLOR',
			doc: 'The instance colour, usually assigned to `p.color`.\n\nNot the fragment output - a `fixed_function` block never shades a pixel.'
		}
	],
	// The local types a block accepts, per ConsoleFixedFunction.h: the GLSL scalar/vector subset plus
	// `pass` itself. No matrices and no arrays - a pass is built from scalars and small vectors.
	types: [
		{ name: 'float', doc: 'Scalar local.' + FF_SUBSET },
		{ name: 'int', doc: 'Integer local, also usable as a `for` loop counter.' + FF_SUBSET },
		{ name: 'bool', doc: 'Boolean local.' + FF_SUBSET },
		{ name: 'vec2', doc: 'Two-component local and constructor.' + FF_SUBSET },
		{ name: 'vec3', doc: 'Three-component local and constructor.' + FF_SUBSET },
		{ name: 'vec4', doc: 'Four-component local and constructor.' + FF_SUBSET },
		{ name: 'pass', doc: 'Pass type. Declare it as `pass p;`, without an initializer.' }
	],
	/** The maths the transpiler accepts inside a fixed_function block - deliberately a small subset */
	functions: [
		{ name: 'abs', doc: 'Absolute value.' + FF_SUBSET },
		{ name: 'ceil', doc: 'Nearest integer >= x.' + FF_SUBSET },
		{ name: 'clamp', doc: 'Clamps to `[min, max]`.' + FF_SUBSET },
		{ name: 'cos', doc: 'Cosine, in radians. `float` only.' + FF_SUBSET },
		{ name: 'float', doc: 'Converts to `float`.' + FF_SUBSET },
		{ name: 'floor', doc: 'Nearest integer <= x.' + FF_SUBSET },
		{ name: 'int', doc: 'Converts to `int`.' + FF_SUBSET },
		{ name: 'max', doc: 'Component-wise maximum.' + FF_SUBSET },
		{ name: 'min', doc: 'Component-wise minimum.' + FF_SUBSET },
		{ name: 'mix', doc: 'Linear blend `a * (1 - t) + b * t`.' + FF_SUBSET },
		{ name: 'sin', doc: 'Sine, in radians. `float` only.' + FF_SUBSET },
		{ name: 'sqrt', doc: 'Square root.' + FF_SUBSET }
	]
};

// Exactly the types the reflection parser knows (GlslReflect.cpp, BuiltinTypes) - anything else is an
// unknown type to the compiler, so it belongs in UNSUPPORTED_TYPES below instead of here. The sizes
// quoted are that table's std140 size/alignment, which is what decides a uniform block's offsets and
// therefore what the engine has to write into the buffer.
const NO_UINT_ES2 = '\n\n**ES2:** not supported - ESSL 100 has no unsigned integers.';
const MATRIX_SW = '\n\n**Software renderer:** not supported.';
const STD140_PAD = ' A following `float` fits into the gap, a `vec2` does not.';

const GLSL_TYPES = [
	{ name: 'void', doc: 'No value; return type only.' },
	{ name: 'bool', doc: 'Boolean. **4 bytes** in a std140 block.' },
	{ name: 'int', doc: 'Signed 32-bit integer.' },
	{ name: 'uint', doc: 'Unsigned 32-bit integer.' + NO_UINT_ES2 },
	{ name: 'float', doc: '32-bit float. There is no `double`.' },
	{ name: 'vec2', doc: 'Two floats. 8 bytes, aligned to 8.' },
	{ name: 'vec3', doc: 'Three floats. 12 bytes, but **aligned to 16** in std140.' + STD140_PAD + ' Prefer `vec4` in uniform blocks.' },
	{ name: 'vec4', doc: 'Four floats. 16 bytes, aligned to 16.' },
	{ name: 'bvec2', doc: 'Two booleans. 8 bytes, aligned to 8.' },
	{ name: 'bvec3', doc: 'Three booleans. 12 bytes, **aligned to 16**.' + STD140_PAD },
	{ name: 'bvec4', doc: 'Four booleans. 16 bytes, aligned to 16.' },
	{ name: 'ivec2', doc: 'Two signed integers. 8 bytes, aligned to 8.' },
	{ name: 'ivec3', doc: 'Three signed integers. 12 bytes, **aligned to 16**.' + STD140_PAD },
	{ name: 'ivec4', doc: 'Four signed integers. 16 bytes, aligned to 16.' },
	{ name: 'uvec2', doc: 'Two unsigned integers. 8 bytes, aligned to 8.' + NO_UINT_ES2 },
	{ name: 'uvec3', doc: 'Three unsigned integers. 12 bytes, **aligned to 16**.' + STD140_PAD + NO_UINT_ES2 },
	{ name: 'uvec4', doc: 'Four unsigned integers. 16 bytes, aligned to 16.' + NO_UINT_ES2 },
	{ name: 'mat2', doc: '2×2 matrix. **32 bytes** in std140 (each column padded to 16).' + MATRIX_SW },
	{ name: 'mat3', doc: '3×3 matrix. **48 bytes** in std140 (each column padded to 16).' + MATRIX_SW },
	{ name: 'mat4', doc: '4×4 matrix. 64 bytes.' + MATRIX_SW },
	{ name: 'sampler2D', doc: '2D texture. Cannot be in a uniform block; set its unit with `texture_unit(N)`.' },
	{ name: 'sampler3D', doc: '3D texture.\n\n**D3D11:** treated as a 2D texture.' },
	{ name: 'samplerCube', doc: 'Cube map. Untested - no engine shader uses one yet.' }
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
const SW_DECLINES = '\n\n**Software renderer:** not supported, the shader silently gets no software version.';
const SW_BANNED = '\n\n**Software renderer:** not supported.';
const HLSL_FAILS = '\n\n**D3D11 / PS3 / Metal:** not supported (`unknown function` error).';
const ES2_FAILS = '\n\n**ES2:** not supported, fails in the driver.';

const GLSL_FUNCTIONS = [
	{ name: 'abs', detail: 'genType abs(genType x)', doc: 'Absolute value, component-wise.' },
	{ name: 'acos', detail: 'genType acos(genType x)', doc: 'Arc cosine, in radians.' },
	{ name: 'all', detail: 'bool all(bvecN v)', doc: 'True if every component is true.' + SW_DECLINES },
	{ name: 'any', detail: 'bool any(bvecN v)', doc: 'True if any component is true.' + SW_DECLINES },
	{ name: 'asin', detail: 'genType asin(genType x)', doc: 'Arc sine, in radians.' },
	{ name: 'atan', detail: 'genType atan(genType y_over_x) / atan(genType y, genType x)', doc: 'Arc tangent, in radians. Use the two-argument form for the angle of a vector.' },
	{ name: 'ceil', detail: 'genType ceil(genType x)', doc: 'Nearest integer >= x.' },
	{ name: 'clamp', detail: 'genType clamp(genType x, min, max)', doc: 'Clamps to `[min, max]`.' },
	{ name: 'cos', detail: 'genType cos(genType angle)', doc: 'Cosine, in radians.' },
	{ name: 'cross', detail: 'vec3 cross(vec3 a, vec3 b)', doc: 'Cross product.' },
	{ name: 'degrees', detail: 'genType degrees(genType radians)', doc: 'Radians to degrees.' },
	{ name: 'determinant', detail: 'float determinant(matN m)', doc: 'Matrix determinant.' + SW_DECLINES + ES2_FAILS },
	{
		name: 'dFdx',
		detail: 'genType dFdx(genType p)',
		doc: 'Screen-space x derivative. Fragment stage only.\n\n**Software renderer:** approximated by a constant.\n\n**ES2:** needs `GL_OES_standard_derivatives` (enabled automatically).'
	},
	{
		name: 'dFdy',
		detail: 'genType dFdy(genType p)',
		doc: 'Screen-space y derivative. Fragment stage only. Same caveats as `dFdx`.'
	},
	{ name: 'distance', detail: 'float distance(vecN a, vecN b)', doc: '`length(a - b)`.' },
	{ name: 'dot', detail: 'float dot(vecN a, vecN b)', doc: 'Dot product.' },
	{ name: 'equal', detail: 'bvecN equal(vecN a, vecN b)', doc: 'Component-wise `==`, returns a bool vector.' + SW_DECLINES },
	{ name: 'exp', detail: 'genType exp(genType x)', doc: 'e raised to x.' + SW_DECLINES + ' Use `exp2(x * 1.442695)` instead.' },
	{ name: 'exp2', detail: 'genType exp2(genType x)', doc: '2 raised to x.' },
	{ name: 'faceforward', detail: 'genType faceforward(genType N, genType I, genType Nref)', doc: '`N` flipped to face away from `I`.' + SW_DECLINES + HLSL_FAILS },
	{ name: 'floor', detail: 'genType floor(genType x)', doc: 'Nearest integer <= x.' },
	{ name: 'fract', detail: 'genType fract(genType x)', doc: '`x - floor(x)`.' },
	{
		name: 'fwidth',
		detail: 'genType fwidth(genType p)',
		doc: '`abs(dFdx(p)) + abs(dFdy(p))`, useful for anti-aliased edges. Same caveats as `dFdx`.'
	},
	{ name: 'greaterThan', detail: 'bvecN greaterThan(vecN a, vecN b)', doc: 'Component-wise `>`, returns a bool vector.' + SW_DECLINES },
	{ name: 'greaterThanEqual', detail: 'bvecN greaterThanEqual(vecN a, vecN b)', doc: 'Component-wise `>=`, returns a bool vector.' + SW_DECLINES },
	{ name: 'inverse', detail: 'matN inverse(matN m)', doc: 'Matrix inverse. Expensive - prefer passing it as a uniform.' + SW_DECLINES + HLSL_FAILS + ES2_FAILS },
	{ name: 'inversesqrt', detail: 'genType inversesqrt(genType x)', doc: '`1 / sqrt(x)`.' },
	{ name: 'length', detail: 'float length(vecN v)', doc: 'Vector length.' },
	{ name: 'lessThan', detail: 'bvecN lessThan(vecN a, vecN b)', doc: 'Component-wise `<`, returns a bool vector.' + SW_DECLINES },
	{ name: 'lessThanEqual', detail: 'bvecN lessThanEqual(vecN a, vecN b)', doc: 'Component-wise `<=`, returns a bool vector.' + SW_DECLINES },
	{ name: 'log', detail: 'genType log(genType x)', doc: 'Natural logarithm.' + SW_DECLINES + ' Use `log2(x) * 0.693147` instead.' },
	{ name: 'log2', detail: 'genType log2(genType x)', doc: 'Base-2 logarithm.' },
	{ name: 'matrixCompMult', detail: 'matN matrixCompMult(matN a, matN b)', doc: 'Component-wise matrix product (not `a * b`).' + SW_DECLINES + HLSL_FAILS },
	{ name: 'max', detail: 'genType max(genType x, y)', doc: 'Component-wise maximum.' },
	{ name: 'min', detail: 'genType min(genType x, y)', doc: 'Component-wise minimum.' },
	{ name: 'mix', detail: 'genType mix(genType a, genType b, genType|float t)', doc: 'Linear blend `a * (1 - t) + b * t`.\n\n**D3D11 / PS3:** lowers to `lerp`.' },
	{ name: 'mod', detail: 'genType mod(genType x, genType|float y)', doc: '`x - y * floor(x / y)`. The result has the sign of `y`, unlike `%`.' },
	{ name: 'normalize', detail: 'genType normalize(genType v)', doc: 'Scales to unit length.' },
	{ name: 'not', detail: 'bvecN not(bvecN v)', doc: 'Component-wise `!` of a bool vector.' + SW_DECLINES },
	{ name: 'notEqual', detail: 'bvecN notEqual(vecN a, vecN b)', doc: 'Component-wise `!=`, returns a bool vector.' + SW_DECLINES },
	{ name: 'outerProduct', detail: 'matN outerProduct(vecN c, vecN r)', doc: 'Column vector × row vector.' + SW_DECLINES + HLSL_FAILS + ES2_FAILS },
	{ name: 'pow', detail: 'genType pow(genType x, genType y)', doc: 'x raised to y. Undefined for `x < 0`.' },
	{ name: 'radians', detail: 'genType radians(genType degrees)', doc: 'Degrees to radians.' },
	{ name: 'reflect', detail: 'genType reflect(genType I, genType N)', doc: 'Reflection vector; `N` must be normalized.' },
	{ name: 'refract', detail: 'genType refract(genType I, genType N, float eta)', doc: 'Refraction vector; `I` and `N` must be normalized.' + SW_DECLINES },
	{ name: 'round', detail: 'genType round(genType x)', doc: 'Nearest integer.\n\n**ES2: a hard error.** Use `floor(x + 0.5)` instead.' },
	{ name: 'sign', detail: 'genType sign(genType x)', doc: '-1, 0 or +1, component-wise.' },
	{ name: 'sin', detail: 'genType sin(genType angle)', doc: 'Sine, in radians.' },
	{ name: 'smoothstep', detail: 'genType smoothstep(edge0, edge1, genType x)', doc: 'Smooth 0-to-1 transition between `edge0` and `edge1`.' },
	{ name: 'sqrt', detail: 'genType sqrt(genType x)', doc: 'Square root.' },
	{ name: 'step', detail: 'genType step(genType|float edge, genType x)', doc: '0 if `x < edge`, else 1. Handy for avoiding branches (`NO_DYNAMIC_BRANCHING`).' },
	{ name: 'tan', detail: 'genType tan(genType angle)', doc: 'Tangent, in radians.' },
	{ name: 'texelFetch', detail: 'vec4 texelFetch(sampler2D s, ivec2 texel, int lod)', doc: 'Reads one texel by integer coordinates, without filtering. Works only on GL 3.3, Vulkan and Metal - add a `#if` fallback.' + SW_BANNED + '\n\n**D3D11 / PS3:** not supported (`unknown function` error).' + ES2_FAILS },
	{ name: 'texture', detail: 'vec4 texture(sampler2D s, vec2 uv [, float bias])', doc: 'Filtered texture sample. Works on every backend.\n\nIn canvas mode: `texture(TEXTURE, UV)`.' },
	{ name: 'textureGrad', detail: 'vec4 textureGrad(sampler2D s, vec2 uv, vec2 dPdx, vec2 dPdy)', doc: 'Sample with explicit derivatives.' + SW_BANNED + HLSL_FAILS + ES2_FAILS },
	{ name: 'textureLod', detail: 'vec4 textureLod(sampler2D s, vec2 uv, float lod)', doc: 'Sample at an explicit mip level.\n\n**Software renderer:** not supported - use `texture`.\n\n**ES2:** vertex stage only.' },
	{ name: 'textureProj', detail: 'vec4 textureProj(sampler2D s, vec3 uvq)', doc: 'Sample with the coordinate divided by its last component.' + SW_BANNED + HLSL_FAILS + ES2_FAILS },
	{ name: 'textureSize', detail: 'ivec2 textureSize(sampler2D s, int lod)', doc: 'Size of a mip level. Prefer passing the size as a uniform.' + SW_BANNED + '\n\n**D3D11 / PS3:** not supported (`unknown function` error).' + ES2_FAILS },
	{ name: 'transpose', detail: 'matN transpose(matN m)', doc: 'Matrix transpose.' + SW_DECLINES + ES2_FAILS },
	{ name: 'trunc', detail: 'genType trunc(genType x)', doc: 'Rounds toward zero.' + SW_DECLINES + ES2_FAILS }
];

const GLSL_KEYWORDS = [
	'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default', 'break', 'continue', 'return',
	'discard', 'const', 'struct', 'in', 'out', 'inout', 'flat', 'smooth', 'noperspective', 'centroid',
	'invariant', 'layout', 'lowp', 'mediump', 'highp', 'true', 'false'
];

const GL_BUILTIN_VARIABLES = [
	{ name: 'gl_Position', doc: 'Clip-space vertex position. Written by a custom-mode `vertex()`, or by the canvas template.' },
	{ name: 'gl_VertexID', doc: 'Index of the vertex.' },
	{ name: 'gl_FragCoord', doc: 'Window-space fragment coordinate.' },
	{ name: 'gl_PointSize', doc: 'Point size. Vertex stage only.' },
	{ name: 'gl_FrontFacing', doc: 'True for front-facing fragments.' }
];

/** Uniforms and varyings the engine's sprite contract provides in canvas mode */
const CANVAS_CONTRACT = [
	{ name: 'uProjectionMatrix', doc: 'Projection matrix (`mat4`), set by the engine.' },
	{ name: 'uViewMatrix', doc: 'View matrix (`mat4`), set by the engine.' },
	{ name: 'uTexture', doc: 'The sprite texture. Declared automatically at unit 0 when `TEXTURE` is used.' },
	{ name: 'vTexCoords', doc: 'Texture coordinates varying, used by `UV`.' },
	{ name: 'vColor', doc: 'Instance colour varying, the initial value of `COLOR` in `fragment()`.' },
	{ name: 'vPaletteOffset', doc: 'Palette offset varying, used by `PALETTE_OFFSET`.' },
	{ name: 'InstanceBlock', doc: 'Per-sprite std140 block: `modelMatrix`, `color`, `texRect`, `spriteSize`, `palOffset`.' },
	{ name: 'InstancesBlock', doc: 'The batched version of `InstanceBlock`: an array of `BATCH_SIZE` instances.' }
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
