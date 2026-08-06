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
		doc: 'The console fixed-function implementation of the effect, transpiled to C++ by `--emit-fixed-function`. Never part of the GLSL stages.\n\nEmpty parentheses declare the generic block; `pvr` (Dreamcast), `gx` (Wii/GameCube), `psp` (PlayStation Portable) and `gs` (PlayStation 2) override it for one backend, and a comma-separated target list (`void fixed_function(pvr, psp, gs)`) declares one implementation shared by several. Every target belongs to exactly one block per file.'
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

const FIXED_FUNCTION_TARGETS = [
	{ name: 'pvr', doc: 'The Dreamcast\'s PowerVR2 only.' },
	{ name: 'gx', doc: 'The Wii / GameCube GX only.' },
	{ name: 'psp', doc: 'The PlayStation Portable\'s Graphics Engine (sceGu) only.' },
	{ name: 'gs', doc: 'The PlayStation 2\'s Graphics Synthesizer only.' }
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

/** Macros that are resolved during stage assembly (they never appear in an emitted source) */
const STAGE_MACROS = [
	{ name: 'VERTEX_STAGE', doc: 'Kept for the vertex stage, dropped for the fragment stage. Only the `#ifdef` / `#ifndef` forms are supported — `#if defined(VERTEX_STAGE)`, `#elif`, `#define` and `#undef` are errors.' },
	{ name: 'FRAGMENT_STAGE', doc: 'Kept for the fragment stage, dropped for the vertex stage. Only the `#ifdef` / `#ifndef` forms are supported.' },
	{ name: 'SOFTWARE_RENDERER', doc: 'Resolved when a stage source is built. Only `--emit-sw-generated` builds with the macro defined; every other emission takes the `#ifndef` side.' }
];

/** The fixed-function DSL: statements, pass fields, context facilities and the allowed maths */
const FIXED_FUNCTION = {
	statements: [
		{ name: 'pass', insert: 'pass ${1:p};', detail: 'pass <name>;', doc: 'Declares one pass — a small bundle of hardware state applied over the sprite quad.' },
		{ name: 'pipeline', insert: 'pipeline ${1|tile_map_mesh,lighting_combine,line_strip_mesh|};', detail: 'pipeline <name>;', doc: 'Binds the effect to a backend pipeline stage instead of a pass list. The generated table then carries the `FixedFunctionIntrinsic` and no function pointer.' }
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
		{ name: 'has_uniform', insert: 'has_uniform("${1:uName}")', doc: 'True when the named shader uniform is present in the pass context. Sets the `NeedsUniforms` requirement.' },
		{ name: 'uniform_vec2', insert: 'uniform_vec2("${1:uName}")', doc: 'Reads a `vec2` uniform from the pass context. Sets the `NeedsUniforms` requirement.' },
		{ name: 'uniform_vec4', insert: 'uniform_vec4("${1:uName}")', doc: 'Reads a `vec4` uniform from the pass context. Sets the `NeedsUniforms` requirement.' }
	],
	passFields: [
		{ name: 'color', doc: 'The pass colour (`vec4`).' },
		{ name: 'offset_color', doc: 'The pass offset (specular) colour (`vec3`). The PVR compiles specular into the base polygon header, so the generated table records whether an effect can ever write one.' },
		{ name: 'screen_offset', doc: 'A screen-space offset applied to the quad (`vec2`).' },
		{ name: 'blend', doc: 'Blend mode — one of `MATERIAL`, `ADD`, `OPAQUE`, `ALPHA`. Plain `=` assignment of a bare identifier only.' },
		{ name: 'tev', doc: 'Texture-combiner preset — one of `MODULATE`, `SILHOUETTE`, `MODULATE_X2`, `MODULATE_X4`, `TINT_MIX`, `LUMA_RAMP`. Plain `=` assignment of a bare identifier only.' },
		{ name: 'luma_gain', doc: 'The only scalar pass field (`float`); it parameterizes the `LUMA_RAMP` preset.' }
	],
	blendModes: ['MATERIAL', 'ADD', 'OPAQUE', 'ALPHA'],
	tevPresets: ['MODULATE', 'SILHOUETTE', 'MODULATE_X2', 'MODULATE_X4', 'TINT_MIX', 'LUMA_RAMP'],
	pipelines: [
		{ name: 'tile_map_mesh', doc: 'The backend\'s tilemap mesh stage.' },
		{ name: 'lighting_combine', doc: 'The backend\'s lighting combine stage.' },
		{ name: 'line_strip_mesh', doc: 'The backend\'s line-strip mesh stage.' }
	],
	types: ['float', 'int', 'bool', 'pass'],
	/** The maths the transpiler accepts inside a fixed_function block - deliberately a small subset */
	functions: ['abs', 'ceil', 'clamp', 'cos', 'float', 'floor', 'int', 'max', 'min', 'mix', 'sin', 'sqrt']
};

const GLSL_TYPES = [
	'void', 'bool', 'int', 'uint', 'float',
	'vec2', 'vec3', 'vec4', 'bvec2', 'bvec3', 'bvec4', 'ivec2', 'ivec3', 'ivec4', 'uvec2', 'uvec3', 'uvec4',
	'mat2', 'mat3', 'mat4',
	'sampler2D', 'sampler2DArray', 'sampler3D', 'samplerCube', 'isampler2D', 'usampler2D'
];

const GLSL_FUNCTIONS = [
	'abs', 'acos', 'all', 'any', 'asin', 'atan', 'ceil', 'clamp', 'cos', 'cross', 'degrees', 'determinant',
	'dFdx', 'dFdy', 'distance', 'dot', 'equal', 'exp', 'exp2', 'faceforward', 'floor', 'fract', 'fwidth',
	'greaterThan', 'greaterThanEqual', 'inverse', 'inversesqrt', 'length', 'lessThan', 'lessThanEqual',
	'log', 'log2', 'matrixCompMult', 'max', 'min', 'mix', 'mod', 'normalize', 'not', 'notEqual',
	'outerProduct', 'pow', 'radians', 'reflect', 'refract', 'round', 'sign', 'sin', 'smoothstep', 'sqrt',
	'step', 'tan', 'texelFetch', 'texture', 'textureGrad', 'textureLod', 'textureProj', 'textureSize',
	'transpose', 'trunc'
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
	STAGE_MACROS: STAGE_MACROS,
	FIXED_FUNCTION: FIXED_FUNCTION,
	GLSL_TYPES: GLSL_TYPES,
	GLSL_FUNCTIONS: GLSL_FUNCTIONS,
	GLSL_KEYWORDS: GLSL_KEYWORDS,
	GL_BUILTIN_VARIABLES: GL_BUILTIN_VARIABLES,
	CANVAS_CONTRACT: CANVAS_CONTRACT
};
