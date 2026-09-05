# ShaderCompiler

Standalone offline shader-preprocessing tool for Jazz² Resurrection.
It expands shader variants and performs GLSL declaration reflection **offline**, emitting a
self-contained C++ header with the sources and reflection data, so the runtime no longer needs
`glGetActiveUniform` introspection or double compilation of batched shaders to size the std140
`InstancesBlock`.

- C++17, zero dependencies beyond the standard library (portable across MSVC/GCC/Clang).

## Usage

The primary mode turns one `.shader` file into one generated header:

```
ShaderCompiler <input.shader> -o <output.h> [-n <namespace>] [--glslang <path>]
ShaderCompiler <input.shader> --check | --essl100-check | --hlsl | --cg | --vulkan
```

| Option | Meaning |
| --- | --- |
| `-o <output.h>` | Path of the generated C++ header. Required unless one of the five dump switches is given |
| `-n <namespace>` | Namespace for the generated program data (default `ShaderArtifacts`); `::` nesting is allowed, an empty name, a leading digit or a character outside `[A-Za-z0-9_:]` is an error |
| `--glslang <path>` | `glslangValidator` used to compile the embedded SPIR-V; otherwise discovered via `VULKAN_SDK\Bin`, `VULKAN_SDK\Bin32` and `PATH`. Without it the SPIR-V fields are emitted as null (Windows-only integration) |
| `--no-dxbc` | Embed the HLSL stage sources instead of precompiled DXBC bytecode (see the Direct3D 11 section below) |
| `--check` | Parse and print a human-readable reflection dump to stdout instead of writing output |
| `--essl100-check` (or `--target essl100`) | Print the ESSL 100 (OpenGL ES 2.0) transform of every variant's stage sources to stdout, for inspection (see below). `essl100` is the only accepted `--target` value |
| `--hlsl` | Print the HLSL (Shader Model 4/5) transform of every stage to stdout |
| `--cg` | Print the Cg transform of every stage to stdout, in the dialect the PS Vita's sceGxm backend compiles (see below) |
| `--vulkan` | Print the Vulkan GLSL (`#version 450`) transform of every stage to stdout — does not require glslang |
| `--help`, `-h`, `/?` | Print the usage text (to stderr) and exit successfully |

The five dump switches write nothing — they print to stdout and never touch the committed
artifacts. They are not mutually exclusive but they are ordered: when several are combined the
first of `--essl100-check`, `--hlsl`, `--cg`, `--vulkan`, `--check` wins and the others are ignored.
`--glslang` is ignored by every dump path.

Seven **standalone modes** are recognized only as the *first* argument (anywhere else they are
rejected as an unknown option):

```
ShaderCompiler --generate-all [--shaders-dir <dir>] [--out-dir <dir>] [--check] [--no-dxbc] [--glslang <path>]
ShaderCompiler --emit-types <output.h>
ShaderCompiler --emit-sw-generated <output.h> <input.shader ...>
ShaderCompiler --emit-cg <output.h> <input.shader ...>
ShaderCompiler --emit-fixed-function <pvr|gx|pica|gu|gs|rdp|legacygl> <output.h> <input.shader ...>
ShaderCompiler --hlsl-check <input.shader ...>
ShaderCompiler --spirv-check [--glslang <path>] <input.shader ...>
```

| Mode | Meaning |
| --- | --- |
| `--generate-all` | Regenerate **every** committed artifact in one run — see [below](#regenerating-the-committed-headers). This is the whole regeneration flow, not a convenience shortcut: it is the only entry point that cannot leave the committed set half-updated |
| `--emit-types` | Write the shared reflection-types header (`Generated/ShaderCompilerTypes.h`) and nothing else |
| `--emit-sw-generated` | Transpile the fragment stage of every variant of every input to C++ and write the aggregate `SwGeneratedShaders.h` consumed by the software renderer. Shaders outside the supported subset are **declined** and omitted (the printed summary lists each with its reason), so this mode never fails on unsupported input. It is also the only path that builds stage sources with `SOFTWARE_RENDERER` defined |
| `--emit-cg` | Transform every variant of every input to Cg and write the aggregate `CgGeneratedShaders.h` consumed by the PS Vita's sceGxm backend (see below). Like the software transpiler it never fails on unsupported input — a declined variant is omitted and listed in the summary |
| `--emit-fixed-function` | Transpile the applicable `fixed_function` block of every variant of every input and write the aggregate `PvrGeneratedEffects.h` / `GxGeneratedEffects.h` / `GuGeneratedEffects.h` / `GsGeneratedEffects.h` / `RdpGeneratedEffects.h` / `LegacyGlGeneratedEffects.h` (see below). Unlike the software transpiler, an invalid block is a **hard error** |
| `--hlsl-check` | Emit the VS + PS HLSL of every variant and compile each stage via `D3DCompile` (`vs_5_0`/`ps_5_0`), printing a pass/fail table. Windows only (`d3dcompiler_47.dll`); writes nothing |
| `--spirv-check` | Emit the Vulkan GLSL of every variant and compile each stage to SPIR-V via `glslangValidator`, printing a pass/fail table. Windows only; writes nothing |

Diagnostics go to stderr in three shapes: `<file>:<line>: error: <message>` for anything the parser,
reflection or an emitter reports, `<file>: error: <message>` for a failed `#include` expansion or an
unreadable input, and a bare `error: <message>` for command-line and I/O problems. The exit code is
`0` on success, `1` for an input or emission failure and `2` for a usage error. `--hlsl-check` and
`--spirv-check` exit `0` even when individual stages fail to compile — read their summary.

All committed artifacts are regenerated by `--generate-all`, see [below](#regenerating-the-committed-headers).

## Building the tool

```
cmake -S Sources/Utilities/ShaderCompiler -B build-shadercompiler
cmake --build build-shadercompiler
```

## Input format (`.shader`)

A `.shader` file is a custom shader language: GLSL globals plus `vertex()`/`fragment()`
entry points, annotated with plain **keyword directives** — top-level statements at brace depth 0,
terminated by `;`. Comments follow GLSL rules (`//` and `/* */`) and pass through into the
emitted sources.

| Directive | Meaning |
| --- | --- |
| `program <Name>;` | Required, exactly once, and it has to precede `shader_type`, `variant`, `batched`, `precision` and the entry points — in practice, write it first. C++/GLSL identifier. |
| `shader_type canvas_item;` or `shader_type custom;` | Optional — the **default is `custom`**. `canvas_item` opts into the sprite-template lowering (see below). |
| `variant <NAME>;` | Declares an optional variant; may appear multiple times. The output contains the **unnamed base variant** (no variant defines, `Name` is `""`, always `Variants[0]`) plus **one** additional entry per variant, compiled with `#define <NAME> (1)` baked in. No cross-products. Test one with a plain `#if <NAME>`; the emitted GLSL gets the `#ifdef` form automatically — see [Which conditional to write](#which-conditional-to-write). |
| `render_mode <mode>[, <mode>];` | Zero or more of `blend_mix`, `blend_add`, `blend_sub`, `blend_mul`, `blend_premul_alpha`, `unshaded`. Stored as the `RenderModes` bitmask on the emitted `Program` (`ShaderCompiler::RenderMode` flags) and shown in the `--check` dump. |
| `precision mediump;` or `precision highp;` | Optional (default `mediump`) — selects the float precision of the auto-emitted `#ifdef GL_ES precision X float; #endif` fragment prologue in both modes; anything else is an error. **Only the two-token form is a directive**: a real GLSL global precision statement with a type (`precision highp float;`) passes through as ordinary GLSL. `highp` is emitted guarded by `GL_FRAGMENT_PRECISION_HIGH`, because fragment `highp` is optional in GLSL ES 1.00 — ESSL 300 always defines the macro, so ES 3.0 and WebGL 2.0 keep it and only an ES 2.0 device without it falls back to `mediump`. Reach for it when a `std140` block reaches both stages: every global does, and GLSL ES requires a named block's members to agree on precision across stages, which a `mediump` prologue breaks against the vertex stage's `highp` default. |
| `batched <Name>;` | `canvas_item` only — also emits the batched twin program (`InstancesBlock` + 6-vertex corner formula) into the same header, sharing the fragment stage and variants. An error in custom mode. Offline-only for now — the runtime `CompileRuntimeProgram` compiles just the primary program. |
| `#include "relative/path"` | Replaced **textually** by the contents of the referenced file (relative to the including file), recursively up to depth 8. Runs on the raw text before parsing, so both reflection and the emitted sources see the included text inlined and the generated artifacts stay self-contained. Note: line numbers in diagnostics refer to the include-expanded stream. |
| `void fixed_function([<target>[, <target>...]]) { ... }` | Fixed-function implementation of the effect, transpiled to C++ by `--emit-fixed-function` (see below). Never part of the GLSL stages — a file with a block emits a byte-identical per-shader header. Empty parentheses declare the generic block; a `pvr`/`gx`/`gu`/`gs`/`rdp` block overrides it for that backend, and a comma-separated **target list** (`void fixed_function(pvr, gu)`) declares one implementation shared by all of them. Every target belongs to exactly one block per file. |

Elements shared by both modes:

| Element | Meaning |
| --- | --- |
| `varying [flat] [precision] <type> <name>;` | Lowered to an `out` in the vertex stage and an `in` in the fragment stage, qualifiers preserved. |
| `attribute [layout(location = N)] <declaration>;` | A vertex attribute: emitted as an `in` global in the **vertex stage only** (nothing appears in the fragment stage). The declaration passes through verbatim after `in`; a leading `layout(...)` qualifier stays in front of the `in` keyword (`attribute layout(location = 0) vec2 aPosition;` emits `layout(location = 0) in vec2 aPosition;`, and the location is honored by reflection). Allowed in both modes, but note the canvas templates do not reference user attributes. |
| `uniform <type> <name> : <hint>[, <hint>];` | `texture_unit(N)` assigns the texture unit (0–31) of a sampler uniform, recorded in reflection — the **only** way to assign one explicitly; in the primary mode the name must match a sampler in at least one variant, and two hints naming the same uniform are an error. `source_color`, `hint_range(...)`, `filter_nearest`, `filter_linear`, `repeat_enable`, `repeat_disable` are parsed and dropped; anything else is an error. The lowered declaration keeps the original text with the hint list stripped, so hints never reach the emitted sources. |
| everything else | Global scope (uniforms, blocks, consts, structs, helper functions, `#ifdef` blocks) is **shared by both stages** of every lowered document. |

Do **not** put `#version` in the input — the engine injects the version header (`#version 330` /
`#version 300 es`) and platform defines at runtime (see `GLShader::LoadFromStringsAndFile`).

### Stage conditionals (resolved at compile time)

Shared globals can wrap stage-specific **declarations** in `VERTEX_STAGE` / `FRAGMENT_STAGE`
conditionals (with an optional `#else`), with zero extra section syntax. **They are rarely needed
anymore**: vertex attributes have the `attribute`
keyword, varying pairs have `varying`, and everything else (uniforms, samplers, blocks, consts,
`#define`s) is harmless when shared — an unused declaration in the other stage changes neither
the merged reflection nor the rendered output, so no shipped `.shader` source uses stage guards
today. The feature stays supported for future edge cases:

```glsl
#if VERTEX_STAGE
in vec2 aPosition;
#endif
```

These conditionals are **evaluated during stage assembly**: the guarded lines are kept or dropped
for the stage being built and the directive lines disappear — the emitted sources contain **no
stage macros at all** (no baked `#define`, no `#if VERTEX_STAGE` text), and the reflection
preprocessor needs no stage predefines because it runs on the pre-resolved streams. All other
conditionals (`#ifdef GL_ES`, variant defines, `BATCH_SIZE`) pass through textually untouched.
Nesting works in both directions: an unknown conditional inside a stage block is kept/dropped
with the block, and a stage conditional inside an unknown conditional is still resolved in place.
`#define`/`#undef` of a stage macro is an error — they are resolved at compile time and never
defined in an emitted source.

Both the `#ifdef`/`#ifndef` and the `#if`/`#elif` **expression** forms are recognized — see
[Compile-time macros in `#if` expressions](#compile-time-macros-in-if-expressions) below.

Helper **functions** do not need such guards — see unused-function elimination below: an FS-only
helper (e.g. one using `dFdx`/`dFdy`) is simply removed from the emitted vertex stage because
nothing there references it. Vertex attributes — the case that used to *require* a guard, since a
bare `in` global leaking into the fragment stage would create a bogus varying — are covered by
the `attribute` keyword instead.

### The backend conditionals (`SOFTWARE_RENDERER`, `NO_DYNAMIC_BRANCHING`)

Two macros are resolved when a stage source is *built* rather than at assembly time, because their
value depends on the emission rather than on the stage:

| Macro | Defined by | Why a shader gates on it |
|---|---|---|
| `SOFTWARE_RENDERER` | `--emit-sw-generated` only | A fragment path that is too expensive to interpret per pixel can carry a cheaper CPU form |
| `NO_DYNAMIC_BRANCHING` | `--emit-rsx` (PlayStation 3) only | A fragment stage compiling to NV40 `IF`/`LOOP`/`BRK` control flow does not survive cgcomp — the branch body overwrites registers the surrounding code still holds |

Neither macro ever appears in a built source, and reflection is always taken from the view where
both are undefined (desktop GL), so the two sides must agree on declarations that reflect. Gating a
block therefore changes nothing for any other backend:

```glsl
#if !SOFTWARE_RENDERER
	float horizonOpacity = clamp(pow(distance, 1.5) - 0.3, 0.0, 1.0);
#else
	float horizonOpacity = clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.5)
#endif
```

Wrapped around **global-scope `varying` declarations** a `SOFTWARE_RENDERER` conditional does one
thing more: it gives the software renderer a *different set of varyings*. The directive lines are
consumed while parsing, each declaration is tagged with the side it came from, and
`AppendVaryingDecl` re-wraps it per stage — so the vertex and fragment stages stay consistent
automatically. Such a conditional may contain nothing but `varying` declarations, blank and comment
lines, and it must not nest (`a global-scope SOFTWARE_RENDERER conditional may only contain varying
declarations`). The tagged declarations still participate in unused-varying trimming, and reads
inside the inactive branch count as reads, so neither side is trimmed away. See
`tests/SwVarying.shader` (`vPos` for the shader backends, `vRect` for the software one) and
`TexturedBackground.shader`. Only this one spot is restricted to a single macro: it accepts
`#ifdef`/`#ifndef SOFTWARE_RENDERER` and the plain `#if [!]SOFTWARE_RENDERER` expression, nothing
more — the two sides are what it tags declarations with.

### Compile-time macros in `#if` expressions

Both families — the stage macros and the backend macros above — may be written in `#if`/`#elif`
expressions, not just `#ifdef`/`#ifndef`, so one directive replaces a nest of them:

```glsl
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING
	// The star field: dozens of sin() per pixel for the software renderer, and NV40 control flow
	// the PlayStation 3 toolchain miscompiles
	if (uHorizonColor.w > 0.0) { … }
#endif
```

The expression is split into its top-level `&&` terms and each term naming a macro of the family
being resolved is folded away. When nothing else is left the whole conditional is **resolved** —
its directive lines disappear along with the losing branch, exactly like the `#ifdef` form. When a
term names something the resolver does not own the conditional **survives** with only the macro
folded out, for the GLSL compiler to finish:

```glsl
#if DITHER && !SOFTWARE_RENDERER   →   #ifdef DITHER      (every other emission)
                                   →   (block removed)    (--emit-sw-generated)
```

What survives is real GLSL, so it is then lowered into a form every profile can evaluate — that is
the `#ifdef` in the first line, see [Which conditional to
write](#which-conditional-to-write).

That fold is what keeps the guarantee that no compile-time macro ever reaches an emitted source,
where it would be silently read as *undefined* instead of as *resolved*. `defined(X)` works, and
because each family is folded by its own pass, an expression may name macros of both.

An `#if` / `#elif` / `#else` **chain** is resolved as a whole, so a multi-way choice between backends
is one flat chain rather than a nest of two-way conditionals:

```glsl
#if SOFTWARE_RENDERER
	float horizonDepth = distance;                    // Cheap polynomial for the CPU
#elif LOW_POWER_GPU
	float horizonDepth = 0.8 * distancePow15 + 0.2 * distance;   // One sqrt() on the SGX543
#else
	float horizonDepth = pow(distance, 1.4);
#endif
```

The chain is walked branch by branch the way the preprocessor would take them: a determined-false
branch is skipped, the first determined-true branch **settles** it (every branch after that is dead
whatever it names, so an undetermined condition there is harmless), an `#else` settles it too, and a
chain whose branches are all false selects nothing. Whenever the chain settles, all of its directive
lines disappear along with every losing branch — exactly as for a two-way conditional, and with no
`#if 0` / `#elif 1` scaffolding left in the emitted source.

Only an **undetermined** condition reached *before* the chain settles leaves it standing: the outcome
then genuinely depends on a macro the resolver does not own, so the chain survives for the GLSL
compiler with its opener rewritten to `#if 1` / `#if 0` and the remaining conditions folded and
lowered as usual. `tests/BackendConditionals.shader` covers both outcomes.

#### Which conditional to write

**`#if` everywhere.** A `.shader` tests every macro — the compile-time ones, the variant defines,
the flags an includer or the engine sets — with plain `#if`, and only that form lets several
conditions share one directive:

```glsl
#if DITHER && !SOFTWARE_RENDERER
#if SLOPE && CLEANUP
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING
```

The compiler makes the result safe. This matters because the surviving directive is evaluated by a
real GLSL preprocessor, and

> **GLSL ES rejects an undefined macro in an `#if` expression.** A bare `#if USE_PALETTE` emitted
> into a variant that does not define `USE_PALETTE` is `'preprocessor evaluation' : undefined macro
> in expression not allowed in es profile`. Desktop GLSL substitutes `0` like C does, HLSL accepts
> it, and the Vulkan transform is `#version 450` **desktop** GLSL — so the mistake passes
> `--hlsl-check` *and* `--spirv-check` while breaking every ES2/ES3/WebGL/Emscripten target. A macro
> with an **empty** body (`#define SLOPE`) is worse still: `#if SLOPE` is a `bad expression` even on
> desktop.

So a **purely boolean** `#if` — identifiers combined with only `!`, `&&`, `||` and parentheses — is
lowered on the way out: one term collapses to the `#ifdef`/`#ifndef` form, and anything longer keeps
its shape with each flag wrapped in `defined(...)`, the operator that consumes an identifier so
nothing undefined is left to evaluate.

| Written in the `.shader` | Emitted GLSL |
| --- | --- |
| `#if USE_PALETTE` | `#ifdef USE_PALETTE` |
| `#if !SOFTWARE_RENDERER` | *(resolved away — the compiler owns this one)* |
| `#if DITHER && !SOFTWARE_RENDERER` | `#ifdef DITHER` |
| `#if SLOPE && CLEANUP` | `#if defined(SLOPE) && defined(CLEANUP)` |

The rewrite is meaning-preserving rather than a guess, because for a macro used as a *flag* `NAME`
and `defined(NAME)` agree whenever it is absent or defined to anything nonzero — which is every
variant define (baked as `(1)`), every valueless marker, and every `#define DEATH_TARGET_ANDROID`
-style flag the engine injects at runtime. The moment a literal, comparison or arithmetic appears
the expression is asking for a *value*, so it is left exactly as written; so are macros the document
defines with a real body, like `BATCH_SIZE`. That leaves one case for `#ifdef`:

- `#if BATCH_SIZE`-style presence tests of a macro that **has** a value — above all the
  `#ifndef BATCH_SIZE / #define BATCH_SIZE (585) / #endif` fallback trio, which genuinely asks "did
  the engine already define it?" (and which the reflection preprocessor treats specially, see
  [Two special rules](#two-special-rules)).

`tests/GlslSweep.ps1` compiles every generated stage source in all three profiles the engine injects
(`#version 330`, `#version 300 es`, `#version 100`) and is the check that catches a directive that
slipped through — run it after touching one in any `.shader`.

See `tests/BackendConditionals.shader` and `tests/StageGuards.shader`.

### Unused-function elimination

After assembly, each emitted stage source is cleaned of dead functions: every global-scope
function whose name is never referenced outside its own definition is removed, iterating to a
fixpoint (a helper only used by a removed helper disappears too). `main()`, `vertex()` and
`fragment()` are roots; overloads share fate by name; references are counted conservatively — an
identifier occurrence anywhere outside comments counts, including inactive preprocessor branches.
Comment lines directly above a removed function are removed with it. Reflection is untouched by
function removal. Note that a helper eliminated from a stage may freely reference declarations
that are guarded out of that stage — the reference disappears together with the helper before the
GLSL compiler ever sees it.

### Unused-varying trimming

After unused-function elimination, each document is cleaned of dead varyings: every
fragment-stage `in` declaration whose name the final fragment text never reads is **removed**
(comment-aware whole-identifier scan, conservative — an occurrence inside any preprocessor branch
counts), and the matching vertex-stage `out` declaration is then cleaned by one of two rules:

- **Full removal** (dead-store removal) — when EVERY vertex-stage occurrence of every declared
  name is provably a dead store, the declaration AND all its store statements are removed
  outright. A store qualifies when the name is the assignment target (optionally carrying
  `.swizzle`/`[index]` selectors) at the start of a **standalone statement** — the previous
  significant token is `{`, `}` or `;`, never an unbraced `if`/`else`/`for` body, a for-header or
  a call argument list — assigned with a single `=` (not `==`, not a compound form) and a **pure**
  right-hand side up to the terminating `;` (multi-line allowed, no preprocessor directive inside
  the extent): no nested assignment, no `++`/`--`, no comma operator outside a whitelisted call's
  argument list, and no calls except the GLSL type constructors (`vecN`/`ivecN`/`uvecN`/`bvecN`/
  `matN`/`float`/`int`/`uint`/`bool`) and known pure builtins (`sin`, `cos`, `tan`, `floor`,
  `ceil`, `fract`, `abs`, `sign`, `mod`, `min`, `max`, `clamp`, `mix`, `step`, `smoothstep`,
  `dot`, `cross`, `length`, `distance`, `normalize`, `pow`, `exp`, `log`, `exp2`, `log2`, `sqrt`,
  `inversesqrt`, `texture`, `texelFetch`, `textureLod`) — any other identifier followed by `(`
  disqualifies. Whole-line statements take their lines with them (a resulting double-blank seam
  collapses to one blank). The removal runs before unused-uniform elimination, so a uniform read
  only by removed stores cascades away in that pass's fixpoint.
- **Demotion** (the fallback) — if ANY occurrence fails the rule, the declaration is demoted to a
  plain global variable: the storage and interpolation qualifiers are stripped, precision + type +
  names stay (`flat out highp vec2 v;` → `highp vec2 v;`) and ALL stores are kept — all-or-nothing
  per name, no partial removal — so every side effect still executes and the GLSL compiler
  eliminates the dead code.

This applies to the canvas-template varyings too (e.g. `vPaletteOffset` — declaration and epilogue
store — disappears from non-palette programs) and to vertex `out`s that never had a fragment `in`
at all (stage-guarded declarations). Multi-name declarations (`out vec2 a, b;`) are handled
conservatively — kept intact when ANY name is read, fully removed only when ALL names qualify.
Reflection is unaffected: varyings do not reflect and demoted plain globals are skipped by the
reflection parser.

### Unused-uniform/block elimination

After varying trimming, each document is cleaned of per-stage dead declarations: a loose
`uniform` declaration (samplers included) whose name the stage text never references outside the
declaration itself, a `layout (std140)` uniform block whose block name, instance name and member
names are all unreferenced outside its extent, a dead `#define` (object- or function-like, NAME
referenced nowhere else in the stage — the `#ifndef X/#define X/#endif` fallback trio is matched
and removed as a unit so no empty guard survives) and a `struct` declaration whose type name is
unreferenced — iterating to a fixpoint so cascades resolve (removing
`#define i block.instances[...]` unpins `InstancesBlock`, whose removal unpins `struct Instance`
and the `BATCH_SIZE` fallback inside it). **Hard reflection-preservation rule**: a uniform or
block is removed from a stage only when the same declaration survives in the *other* stage of the
same document — a declaration unused in both stages stays in both, so the merged per-variant
reflection is byte-identical before and after the pass (the engine addresses uniforms by
reflected name). Dead `#define`s and dead structs never reflect and are exempt. The scan is
comment-aware, whole-identifier and conservative (a reference inside any preprocessor branch
counts; only declarations at unconditional global scope are considered; multi-name declarations
are kept when ANY name is referenced). This reproduces exactly the minimal per-stage declaration
sets the old `VERTEX_STAGE`/`FRAGMENT_STAGE` guards used to produce — automatically: emitted
fragment stages lose `uProjectionMatrix`/`uViewMatrix`/`InstanceBlock`/`InstancesBlock`, emitted
vertex stages lose fragment-only samplers, uniforms and constants.

### Constant folding

After trimming, each emitted stage source is constant-folded (`ConstFold.h`/`.cpp` — a reusable
GLSL expression tokenizer + precedence-climbing parser, the AST seed for future non-GLSL
emitters): every **multi-token literal-only subexpression** that collapses to a single literal is
rewritten in place, with exact GLSL semantics — int and float never mix (`1 / 255.0` stays), int
division/modulo truncate toward zero, int folds only within 32-bit signed range (overflow skips
the fold), floats are computed in double and emitted with `%.9g` only when the printed text
round-trips exactly and is not longer than the original (always containing `.` or `e`). Unsigned
(`2u`) and suffixed (`1.0f`) literals never fold. Nothing containing identifiers, calls, swizzles
or indexing folds, but call/constructor **arguments** fold individually
(`vec2(1.0 + 2.0, 3.0)` → `vec2(3.0, 3.0)`), and the parser models real operator precedence, so
`x - 1 + 2` is correctly left alone. The pass runs **only inside function bodies** — global-scope
declarations are never touched, which guarantees reflection (uniforms, blocks, array sizes) is
byte-identical before and after. Preprocessor directive lines are barriers: a conditional that
splits a statement suppresses folding until the next statement boundary in every affected branch,
and no fold ever crosses a line or a comment.

### Custom mode (the default)

No template and **no built-in substitutions** — user identifiers pass through untouched.

| Element | Meaning |
| --- | --- |
| `void vertex() { ... }` | **Required.** The body becomes the vertex `main()` **verbatim** — it writes `gl_Position` itself (and may use `gl_VertexID` etc.). Emitted after the lowered `out` varyings and the shared globals. |
| `void fragment() { ... }` | **Required.** Lowered to: the `#ifdef GL_ES` precision block, the user `in` varyings, the shared globals, `out vec4 COLOR;`, then a `main()` that is exactly `void main() {` + the body **verbatim at its source indentation** + `}` — no scope block, no default store. `COLOR` **is the fragment output variable itself** and is **undefined until written** (like any GLSL output variable) — assign it on every path. There is no epilogue and no `fragColor` name anywhere in generated code, so an early `return;` inside `fragment()` is safe. **Referencing `fragColor` anywhere in a `.shader` file is a parse error** — write `COLOR`, it is the output. |

### Canvas mode (`shader_type canvas_item;`)

Opts into the sprite template: the vertex stage is generated around the engine's standard sprite
contract (`InstanceBlock`/`InstancesBlock`, `vTexCoords`/`vColor`/`vPaletteOffset` varyings; when no
`vertex()` entry is given the generated vertex stage matches the engine's default sprite shader,
minus any template varyings the fragment stage never reads — their declarations and epilogue
stores are removed by the trimming pass above), and the `batched <Name>;` twin becomes available.

| Element | Meaning |
| --- | --- |
| `void vertex() { ... }` | Optional. The generated `main()` computes the **built-in** defaults as locals — `VERTEX` (sprite-local position in pixels, `aPosition * spriteSize`, pre-model-transform), `UV` (the `texRect` mapping), `COLOR` (the instance color) and `PALETTE_OFFSET` — splices the body **verbatim at its source indentation** (no scope block), then runs the standard epilogue: `gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(VERTEX, 0.0, 1.0)` plus the `UV`/`COLOR`/`PALETTE_OFFSET` varying stores. The body shares `main()`'s scope, so **redeclaring one of the prologue local names** (`VERTEX`, `UV`, `COLOR`, `PALETTE_OFFSET`, `aPosition`) is a GLSL redeclaration error at compile time — intended, better than silent shadowing. The epilogue is real post-work, so `return;` inside a canvas `vertex()` is a **parse error** — restructure with if/else. Without a `vertex()` entry the generated vertex stage matches the engine's default sprite shader (minus trimmed varyings and their stores). |
| `void fragment() { ... }` | Required. A generated `main()` stores `COLOR = vColor;` (the instance color — that IS the built-in's semantic, bodies may read `COLOR`) followed directly by the body **verbatim at its source indentation** (no scope block); `COLOR` is the fragment output variable itself (`out vec4 COLOR;`, no epilogue), so an early `return;` is safe — and referencing `fragColor` is a parse error. Whole-identifier substitutions `UV` → `vTexCoords`, `TEXTURE` → `uTexture`, `PALETTE_OFFSET` → `vPaletteOffset` are applied (comments are skipped). Other built-ins (`NORMAL`, `SCREEN_UV`, `SCREEN_PIXEL_SIZE`, `TIME`, `POINT_COORD`, `VERTEX`) are reported as unsupported. |

**Implicit TEXTURE**: a canvas document that references the `TEXTURE` built-in without declaring
`uTexture` gets `uniform sampler2D uTexture;` auto-declared at the head of the fragment globals
and texture unit **0** registered for it — equivalent to writing
`uniform sampler2D uTexture : texture_unit(0);` explicitly, byte-for-byte in reflection.
Explicit declarations (with or without a `texture_unit(N)` hint) win — no double declaration,
no unit conflict. Only `TEXTURE`/`uTexture` is implicit — `PALETTE`-style samplers
(`uTexturePalette`) stay fully explicit, because auto-declaring them would add a texture binding
to variants that never use it. Custom mode has no built-ins, so `TEXTURE` there is an ordinary
user identifier and nothing is auto-declared.

In canvas mode `COLOR` enters `fragment()` as the **instance color** (`vColor`) — the
texture is *not* pre-sampled, sample `TEXTURE` explicitly. The `COLOR = vColor;` default is
**omitted automatically when it is provably dead** — i.e. the body's first `COLOR` occurrence is
an unconditional, top-level, full plain assignment (`COLOR = …;`, no compound/component/comparison
forms) that does not read `COLOR` on its right-hand side, with no `return` before it and no
`COLOR` reference in the shared globals (any doubt keeps the default; the then-unread `vColor`
varying is trimmed by the usual pass). In custom mode `COLOR` is **undefined
until written**. Multi-program headers emit each program's data under its
own symbols; `--check` prints one dump per program in order.

The shared reflection types live in a standalone header written by `ShaderCompiler --emit-types <path>`
(`--generate-all` emits it as `Generated/ShaderCompilerTypes.h`); every generated header includes it, and
engine code can include it directly to consume reflection without pulling in any program's data.

### Fixed-function blocks (`fixed_function`)

The fixed-function backends (PVR on the Dreamcast, GX on the Wii/GameCube, GU on the PlayStation
Portable, GS on the PlayStation 2, RDP on the Nintendo 64, and LegacyGL on an OpenGL 1.x such as
MorphOS' TinyGL) have no fragment shaders — an effect there is a short list of passes over the sprite
quad, each a small bundle of hardware state
(see `Docs/FixedFunctionShaderDesign.md` and the runtime contract in
`nCine/Graphics/RHI/FixedFunctionPass.h`). A `fixed_function` block states that pass list in the
shader file itself, next to the GLSL it approximates:

```glsl
void fixed_function() {
	pass p;
	p.color = vec4(0.0, 0.0, 0.0, COLOR.a);
	p.offset_color = COLOR.rgb;
	submit_quad(p);
}
```

`void fixed_function()` (empty parentheses) is the generic implementation — the spelling matches
the `void vertex()` / `void fragment()` entry points; `void fixed_function(pvr)`,
`void fixed_function(gx)`, `void fixed_function(gu)`, `void fixed_function(gs)`,
`void fixed_function(rdp)`, `void fixed_function(legacygl)` and `void fixed_function(pica)` override it for
one backend, and a comma-separated **target list** overrides it for several at once:

| Spelling | Serves |
| --- | --- |
| `void fixed_function() { ... }` | every backend that has no more specific block — the generic implementation, restricted to the portable core |
| `void fixed_function(pvr) { ... }` | the Dreamcast only |
| `void fixed_function(gx) { ... }` | the Wii/GameCube only |
| `void fixed_function(gu) { ... }` | the PlayStation Portable only |
| `void fixed_function(gs) { ... }` | the PlayStation 2 only |
| `void fixed_function(rdp) { ... }` | the Nintendo 64 only |
| `void fixed_function(legacygl) { ... }` | the fixed-function OpenGL 1.x backend only (MorphOS' TinyGL, and a desktop GL in a compatibility context) |
| `void fixed_function(pica) { ... }` | the Nintendo 3DS only |
| `void fixed_function(pvr, gu) { ... }` | the Dreamcast **and** the PlayStation Portable, from one body |
| `void fixed_function(pvr, gu, gs) { ... }` | the three no-combiner consoles, from one body |
| `void fixed_function(gu, gx, pvr) { ... }` | any subset in any order; whitespace is free (`(pvr,gu)` and `( pvr , gu )` are the same declaration) |

A block that names a backend — on its own or inside a list — always wins over the generic block for
it, regardless of declaration order, and every target belongs to exactly **one** block per file (a
target claimed twice is an error, whether it was spelled singly or in a list). The list form exists
so a shader whose description for two consoles is *literally the same code* keeps one copy of it
instead of two that can drift apart; when the bodies genuinely differ, write separate blocks.
**Capabilities are then validated against the intersection of the listed targets** — see
[below](#capability-rules-for-a-target-list).

The standalone mode

```
ShaderCompiler --emit-fixed-function <pvr|gx|pica|gu|gs|rdp|legacygl> <output.h> <input.shader ...>
```

transpiles the applicable block of every program variant into a
`void <Program>[_<VARIANT>]_Effect(EffectContext&)` C++ function — the block is preprocessed **once
per variant** with the variant define baked in, exactly like the fragment stage, so conditionals on
variant names work inside it — and collects them into one aggregate header per backend
(`Generated/PvrGeneratedEffects.h` / `GxGeneratedEffects.h` / `GuGeneratedEffects.h` /
`GsGeneratedEffects.h` / `RdpGeneratedEffects.h` / `LegacyGlGeneratedEffects.h`, written by
`--generate-all`) with a
`FixedFunctionGeneratedEffects[]` table of
`{ program, variant, usesOffsetColor, requirements, intrinsic, &function }` entries.
**Byte-identical function bodies are deduplicated**: batched twins and `USE_PALETTE` variants
usually differ only in the dispatch loop's instance decoding, not in their pass code, so each
distinct body is emitted once — named after its first occurrence, with a `// Shared by:`
provenance comment listing every (program, variant) that points at it — and all matching table
rows share the function (roughly a 3× reduction in emitted code). Two fields are computed
statically per (program, variant) during transpilation:

- `usesOffsetColor` — `true` when any reachable `p.offset_color = ...` assignment exists in the
  emitted function (the PVR needs it when compiling the base polygon header, because specular
  enable is per program, not per pass).
- `requirements` — a `FixedFunctionRequirements` bitmask of the optional `EffectContext`
  facilities the function can ever call: `NeedsTexelStep` (`texel_size()`/`has_texel_size()`),
  `NeedsUniforms` (`has_uniform()`/`uniform_*()`), `NeedsStripBuilder`
  (`strip_*()`/`submit_strip[_shaded]()`), `NeedsQuadAxes` (`quad_origin()`/`quad_axis_*()`).
  The backends' `Dispatch` gates the corresponding context setup on these bits (texel-step
  derivation, uniform plumbing, strip/quad-geometry wiring), skipping work an effect can never
  observe — submitted primitives stay bit-identical by construction.

Programs without a block are simply absent from the table; the backends resolve entries from the
true (program, variant) identity plumbed at load time (`ShaderProgram::SetProgramIdentity()`),
never from shader names. The including device file supplies the concrete
`EffectContext` via a `using` alias before the include; the generated header is otherwise
self-contained (it carries its own small vector runtime).

The block language (validated strictly, anything else is a **hard error** with file/line,
unlike the software transpiler's silent declines). The **portable core**, valid in every block:

- `pipeline <name>;` — as the **sole** statement of a block, binds the program to a backend
  pipeline stage instead of describing passes: the table entry carries a
  `FixedFunctionIntrinsic` value and no function. Known names: `tile_map_mesh`,
  `lighting_combine`, `line_strip_mesh`. These are the stages that consume engine data
  structures (tile-layer vertex streams, the weapon-wheel line strip, the lighting hook) —
  mechanism that stays in the backend, but named in the shader file. The geometry-synthesized
  quad effects (the transition iris, the warped background) are **not** intrinsics anymore —
  they are ordinary blocks built on the strip builder below.
- `pass p;` — declares a `FixedFunctionPass` with the engine defaults; no initializer.
- Pass fields (write-only): `p.color = <vec4>;`, `p.offset_color = <vec3>;` (marks
  `HasOffsetColor`), `p.screen_offset = <vec2>;`, `p.blend = MATERIAL|ADD|OPAQUE|ALPHA;`
  (`ALPHA` = plain source-alpha over, independent of the material — the warp's horizon tint),
  `p.tev = MODULATE|SILHOUETTE|MODULATE_X2|MODULATE_X4;` (portable intent — the PVR ignores it;
  the two output scales are **rejected for every block the gu target reaches**, see below),
  `p.luma_gain = <float>;` (parameterizes the GX-only `LUMA_RAMP` preset below).
- `submit_quad(p);`, locals of the GLSL scalar/vector subset (`float`/`int`/`bool`, `vec2/3/4`),
  `if`/`else`, C-style `for` with an int counter, (compound) assignment, `++`/`--`.
- Expressions: arithmetic, comparisons, swizzles (single components plus `.xy`/`.zw`/`.xyz`/`.yzw`
  and their rgba spellings), `min`/`max`/`clamp`/`mix`/`ceil`/`floor`, `abs`/`sqrt`/`sin`/`cos`
  (float), vec/`float()`/`int()` constructors, and the built-ins:
  - `COLOR` — the instance color (`ctx.Color()`), exactly the shader's `COLOR` input.
  - `texel_size()` — `vec2` displacement of one texel in the quad's own coordinate space, already
    converted per backend (raster space on the PVR, logical pixels on the GX, screen pixels on the
    GU), derived from the UV-space texel size the Outline shader family carries in its instance
    `color.xy` (`vec2(ctx.TexelStepX(), ctx.TexelStepY())`).
  - `has_texel_size()` — `bool`, whether that step is derivable at all (`ctx.HasTexelStep()`; a
    zero texRect has no scale). Blocks guard their `texel_size()` uses with it.

The **extended vocabulary** is valid only in a block that names its backends — a single `pvr`, `gx`,
`gu`, `gs` or `rdp` block, or a target list such as `pvr, gu` — because a generic block stays in the portable
quad-only core, so a shared description can never silently depend on one console's geometry synthesis
(using it in a generic block is a hard error):

- `quad_origin()`, `quad_axis_x()`, `quad_axis_y()` — `vec2`s: the **pre-clip** raster position
  of the sprite's (0,0) corner and the raster displacements of its local axes. Geometry synthesis
  (the iris circle, the warp bands) uses these instead of the post-scissor-clip corner arrays so
  clipping cannot distort it.
- `has_uniform(uName)` (`bool`), `uniform_vec2(uName)`, `uniform_vec4(uName)` — the program's
  resolved uniforms by name, through the backend's existing `ResolveUniform` machinery (the warp
  consumes the same `uViewSize`/`uShift`/`uHorizonColor` its GLSL does). The argument is an
  identifier, not a string literal. An unresolved uniform reads as zeros — guard with
  `has_uniform()`.
- The strip builder: `strip_position(i, <vec2>)`, `strip_uv(i, <vec2>)` (UVs in the shader's
  texture space; the backend folds its padded-store scale), `strip_color(i, <vec4>)`, then
  `submit_strip(p, count)` (textured, the pass's flat colour — the warp's trapezoid band pieces)
  or `submit_strip_shaded(p, count)` (per-vertex colours — the iris soft edge and the horizon
  tint express gradients without a fragment shader; **untextured** unless the pass's TEV preset
  consumes the texel as well, i.e. `TINT_MIX`, in which case the strip keeps its texture and UVs).
  The scratch capacity is a backend capability — **8** vertices on the PVR, **16** everywhere else
  (the others prefer fewer, longer primitives; on the GE every strip is a draw call of its own) — and a
  **literal** index or count outside it is a hard error, because at runtime an out-of-range index is
  dropped and an oversized count clamped, which would silently draw the wrong geometry. A block
  serving several targets is held to the **minimum** capacity among them (a `pvr, gx` block gets the
  PVR's 8). Computed indices/counts stay unchecked.

Two TEV presets need a programmable texture combiner (the CLX2 modulates a texel by the vertex
colour and adds an offset colour — that is the whole vocabulary; the GE has five fixed texture
functions over one texel and the fragment colour, the GS four), so using them in a block that
reaches a backend without one is a hard error rather than a silently wrong frame:

- `TINT_MIX` — one stage of `d + mix(a, b, c)`: the texel lerped toward the pass/vertex colour by
  the (pass/vertex) alpha, with an opaque result. With a shaded strip both terms are per-vertex,
  which is how the TexturedBackground warp folds its horizon tint into the band's own draw
  instead of laying a second gradient pass over it. Three backends can express it — the GX in one
  TEV stage, the RDP because its colour combiner computes `(A - B) * C + D` per cycle and one cycle
  of `(PRIM - TEX) * PRIM_ALPHA + TEX` IS that lerp, and LegacyGL because `GL_INTERPOLATE` is the
  same operation again — so it is allowed in any block whose targets are drawn from
  `gx`/`pica`/`rdp`/`legacygl` only, and rejected as soon as the list drags in a backend without a
  lerping combiner.
- `LUMA_RAMP` — a silhouette whose tone is picked per texel instead of being flat: `grey` is the
  texel's Rec.601 luminance amplified by `p.luma_gain` and saturated, and the tone is
  `mix(p.color.rgb, p.offset_color, grey)` (so `color` is the tone at `grey = 0` and
  `offset_color` the tone at `grey = 1`); coverage stays `texel alpha * p.color.a`. Six stages —
  channel swizzles through the TEV swap tables, a KONST-weighted dot product, then the ramp — and
  the GX's pass merger still folds it with the sprite pass below it into a single draw. GX
  **alone** — the RDP's combiner has no dot product, so it cannot derive the luminance the ramp is
  indexed by.

`MODULATE_X2`/`MODULATE_X4` are the mirror-image case: the GE has no combiner **output scale** at
all (and neither does the GS's texture function), so they are rejected for every block the gu or gs
target can reach — such a block, a target list naming either, AND a generic one, since a generic
block is transpiled for every backend and the PVR *silently ignores* the preset. The RDP splits the
pair: its second combiner cycle can double the first one's output (`(1 - 0) * COMBINED + COMBINED`),
so `MODULATE_X2` is expressible there, while `MODULATE_X4` would need a third cycle the hardware
does not have. LegacyGL has both, like the GX: `GL_RGB_SCALE` takes 1, 2 or 4. A shared block using a scale a listed backend lacks would be honoured by only some of
the consoles it serves, which is the "silently depends on one console's feature" case these checks
exist to prevent; on this tier a boost is expressed as passes instead (`Colorized.shader` splits its
multiplier into up to three additive passes in its shared `pvr, gu, gs` block for the same reason).

### Capability rules for a target list

A block naming several backends is validated against the **intersection** of what they can do, not
against the backend whose header happens to be generated at that moment — otherwise a shared body
would be accepted while being silently wrong on the other backends it serves. Concretely:

| Rule | Effect on a target list |
| --- | --- |
| Extended vocabulary (strip builder, pre-clip quad axes, resolved uniforms) | allowed — every backend a list names is named in it, so nothing is implicit. Only the generic block is restricted to the portable core |
| `LUMA_RAMP` (GX-only) | allowed **only** in a block targeting `gx` and nothing else; `void fixed_function(gx, gu)` is rejected, because the GE cannot express it |
| `TINT_MIX` (lerping combiner: GX, PICA, RDP, LegacyGL) | allowed only when **every** listed target has one — any subset of `gx`, `pica`, `rdp`, `legacygl`; `void fixed_function(gx, gu)` is rejected, because the GE cannot express it |
| `MODULATE_X2` / `MODULATE_X4` (combiner output scale) | rejected as soon as `gu` or `gs` appears in the list; `void fixed_function(pvr, gu)` cannot use them even while the PVR header is being written. The RDP's two combiner cycles reach the x2 but not the x4, so `rdp` in a list rejects `MODULATE_X4` only. `gx` and `legacygl` have both |
| Strip-builder capacity | the **minimum** across the listed targets — `void fixed_function(pvr, gx)` is limited to the PVR's 8 vertices, so a literal index `8` or a count above `8` is an error there |

The diagnostics name which of the block's own targets rejects the feature, so the fix (split the
list back into separate blocks) is obvious:

```
error: LUMA_RAMP is a GX-only capability - it needs the programmable TEV combiner, so it is only
       available in a fixed_function(gx) block, not in one that also targets gu
error: MODULATE_X2 cannot be expressed for the gu target - the Graphics Engine's texture
       environment has no combiner output scale, which this block also names (write the boost as
       passes in a fixed_function(gu) block, e.g. an additive one)
error: vertex index 8 is outside the pvr strip builder's capacity of 8 vertices (the smallest
       capacity among the block's targets)
```

The GE also has no post-texture additive term (`GU_TFX_ADD` adds the texel to the fragment colour,
not a third value), so `p.offset_color` is not one GE draw either. That one is handled in the
**mechanism** rather than in the shaders: the GU `EffectContext::SubmitQuad` expands a pass carrying
an offset colour into the modulated sprite plus an additive flat-colour (silhouette) pass over it,
which is algebraically the same result the PVR's offset colour produces in a single draw — and
collapses to ONE draw when the pass colour's RGB is zero (the mask/outline/shield idiom, where the
offset colour *is* the effect). So a generic block that writes `p.offset_color` keeps working
unchanged on all three consoles; the GX likewise reinterprets it as its silhouette form.

Worked examples: `Transition.shader` (the iris fan, 32 segments in its `pvr` block and 64 with a
radially eased edge in one shared `gx, gu, gs, rdp` block — the iris is pure geometry, so what it needs
from the hardware is only a 16-vertex strip taken in one draw call, which is exactly what those four
agree on and the PVR's 8-vertex scratch cannot give) and `Include/TexturedBackgroundWarp.inc` (the warp
rebuild, shared by both background shaders **and** all five backends — the band geometry is portable
vocabulary; only the horizon-tint delivery is switched by a `WARP_TINT_IN_VERTEX_COLOR` macro the
including block defines, which only the `gx` block sets, so the other four consoles share a
`pvr, gu, gs, rdp` block that just includes the file — and which is also the idiom for specializing a shared
include per backend).

`PartialWhiteMask.shader`, `Colorized.shader` and `FrozenMask.shader` show the other half of the
pattern: each has one `pvr, gu, gs` block (the three no-combiner tiers, which reach the effect the
same way) plus a `gx` block that uses the combiner. Those six files are the reason the list form exists —
they used to carry two byte-identical bodies each.

## Preprocessing semantics (reflection only)

Reflection must run per variant, so the tool contains a mini C preprocessor that produces the
*declaration stream* fed to the reflection parser. The **emitted** GLSL sources are NOT
preprocessed output — they keep the original text verbatim (with only the compile-time stage
conditionals already resolved away — see above), plus the variant define (`#define <NAME> (1)`)
and `#line 1` baked at the top.

Supported: `#define`/`#undef` (object-like only), `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`
with integer constant expressions supporting `defined(X)`, `!`, `&&`, `||`, `==`, `!=`, `<`, `>`,
`<=`, `>=`, `+`, `-`, `*`, `/`, `%`, unary `-`, parentheses and decimal integers. Identifiers that
are not defined evaluate as `0` (C preprocessor rule). Function-like macros are recorded (so
`defined()` sees them) but never expanded.

Special cases:

- **`VERTEX_STAGE`/`FRAGMENT_STAGE` never reach this preprocessor.** Stage conditionals are
  resolved during stage assembly, before both reflection and emission, so reflection sees exactly
  the pre-resolved declarations each stage compiles (and no stage predefines exist).
- **`SOFTWARE_RENDERER`/`NO_DYNAMIC_BRANCHING` are treated as undefined,** because they are folded
  later still — when a stage source is *built* — and reflection is taken from the view where
  neither is set. That is the same desktop-GL view the rule below takes, and it is why the two
  sides of such a conditional must agree on everything that reflects.
- **`GL_ES` is treated as undefined.** Reflection is taken from the desktop GL view; `#ifdef GL_ES`
  blocks (precision statements etc.) do not participate in reflection but stay in the emitted source.
- **`BATCH_SIZE` is symbolic.** In `#if`/`#ifdef` expressions it behaves as *defined with value 1*
  (so the usual `#ifndef BATCH_SIZE / #define BATCH_SIZE (585) / #endif` fallback is skipped and the
  fallback value never leaks into reflection). When used as an **array size** it stays symbolic —
  see below.

## Reflection

Per stage and per variant, only global-scope declarations are parsed (function bodies are skipped
by brace counting):

- `struct <Name> { <members> };`
- `layout (std140) uniform <BlockName> { <members> } [instanceName];` (any spacing; other layout
  qualifiers such as `binding` are accepted and ignored; non-std140 blocks are rejected)
- `uniform <type> <name>[N];` — loose uniforms; `sampler2D`/`sampler3D`/`samplerCube` become
  texture bindings (declaration order preserved, unit from the `texture_unit(N)` hint or `-1`)
- `in <type> <name>;` in the **vertex** stage — vertex attributes (`layout(location = N)` is
  honored, otherwise location is `-1`). `in`/`out` in the fragment stage, `flat`/`noperspective`,
  `precision` statements and `highp`/`mediump`/`lowp` qualifiers are ignored/stripped.

Types: `float`, `int`, `uint`, `bool`, `vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`, `bvec2/3/4`, `mat2`,
`mat3`, `mat4`, plus previously declared user structs. Array sizes must be decimal integer literals
or the symbolic `BATCH_SIZE` (uniform block members only). Both `Type name[N];` and
`Type[N] name;` spellings are accepted.

The vertex and fragment reflections are merged into one program-level view (GL style): entries are
deduplicated by name, and a declaration mismatch between the stages is an error.

### std140 layout rules used

- scalars 4/4 (`bool` = 4); `vec2` 8/8; `vec3` size 12, align 16; `vec4` 16/16
- `matN` = N columns with `vec4` stride: `mat2` 32, `mat3` 48, `mat4` 64; align 16
- array base alignment = element base alignment rounded up to 16; array element stride = element
  size rounded up to that alignment
- struct base alignment = max member alignment rounded up to 16; struct size = end offset rounded
  up to the struct alignment

Arrays sized by `BATCH_SIZE` record their element stride (`InstanceStride` on the block) and mark
the count as symbolic (`SymbolicArraySize` sentinel, `0xFFFF`). The block's `BaseSize` then covers
the members before the array plus zero elements. The runtime computes the batch size as
`maxUniformBlockSize / InstanceStride` — e.g. the engine's batched sprite `Instance` struct
(`mat4` + `vec4` + `vec4` + `vec2` + `float`) yields a 112-byte stride, matching today's default
`585 = 65536 / 112`.

## Generated output

A self-contained header (only `<cstdint>`/`<cstddef>`):

- `inline constexpr char <Program>_<Variant>_Vs[] = R"__SHDR__(...)__SHDR__";` (and `_Fs`), where
  `<Variant>` is the variant name — the **unnamed base variant carries no infix** (`Lighting_Vs`,
  `Lighting_Uniforms`, `Lighting_Block0_Members`, …; named variants keep theirs:
  `Tinted_USE_PALETTE_Vs`). Sources start with `#define <NAME> (1)` (named variants
  only) and `#line 1`; the lowered GLSL body is verbatim user text with the stage conditionals
  resolved away (no `VERTEX_STAGE`/`FRAGMENT_STAGE` text survives into the output).
- The per-backend lowerings of those same sources, side by side in the same header, so one artifact
  serves every GLSL-family backend: `_Vs100`/`_Fs100` (ESSL 100, consumed under the OpenGL|ES 2.0
  profile with `#version 100`), `_HlslVs`/`_HlslFs` (Shader Model 4/5, consumed by the D3D11
  backend) and the offline-compiled SPIR-V words (consumed by the Vulkan backend, which also builds
  its descriptor-set layout from the same reflection). A field whose lowering was unavailable — a
  construct outside the HLSL or ESSL 100 subset, or a generation run without glslang — is emitted as
  null, and the corresponding backend then cannot use that program.
- Reflection as `constexpr` arrays of plain structs: `Uniform`, `BlockMember`, `UniformBlock`,
  `TextureBinding`, `Attribute`, tied together by `ProgramVariant` (name, defines, sources,
  counts/pointers) and `Program` (name, render-mode bitmask, variant count, variants).
  `Variants[0]` is **always the base variant and its `Name` is `""`** — engine lookups treat
  `nullptr`/empty as the base. A canvas_item file with `batched` emits two `Program`s into one
  header.
- The reflection **types** are emitted once under `#ifndef SHADERCOMPILER_REFLECTION_TYPES` into the
  fixed `ShaderCompiler` namespace (deliberately independent of `-n`, so headers generated with
  different data namespaces can be included together). Program **data** goes into the `-n`
  namespace (default `ShaderArtifacts`).

## Regenerating the committed headers

Every artifact under `Sources/Shaders/Generated/` is **committed**; the game's build never runs this
tool and never embeds a `.shader` file, so a shader edit has no effect until the headers are
regenerated and committed. `--generate-all` enumerates every `Sources/Shaders/*.shader` and writes, in
order:

1. `ShaderCompilerTypes.h` — the shared reflection types (`--emit-types`).
2. One `<Name>.h` per shader — `Default*.shader` (the nCine default programs) into the
   `nCine::ShadersGen` namespace, everything else into `Jazz2::ShadersGen`.
3. `ShadersGen.h` — the umbrella header including every generated program plus the per-namespace
   `AllPrograms[]` index arrays. Program symbols come from the `program` and `batched` directives, so
   a file with a batched twin contributes two entries.
4. `SwGeneratedShaders.h` — the software-renderer fragment functions (`--emit-sw-generated`).
5. `PvrGeneratedEffects.h`, `GxGeneratedEffects.h`, `GuGeneratedEffects.h`,
   `GsGeneratedEffects.h`, `RdpGeneratedEffects.h` and `LegacyGlGeneratedEffects.h` — the
   fixed-function effects (`--emit-fixed-function pvr` / `gx` / `pica` / `gu` / `gs` / `rdp` / `legacygl`).

```
Sources\Utilities\ShaderCompiler\x64\Release\ShaderCompiler.exe --generate-all
git add Sources\Shaders Sources\Shaders\Generated
```

Both directories are auto-detected — the shader directory by walking up from the executable (and then
from the working directory) looking for `Sources/Shaders`, the output directory as its `Generated`
subdirectory — so the mode needs no arguments from anywhere in the tree. `--shaders-dir` and
`--out-dir` override them for a build layout the walk does not cover.

`GenerateAll.ps1` (in this directory, expecting the executable at `x64/Release/ShaderCompiler.exe`) is
a thin wrapper that forwards `-Glslang`, `-NoDxbc` and `-Check` to the same mode, so existing muscle
memory and CI keep working:

```
cd Sources\Utilities\ShaderCompiler
powershell .\GenerateAll.ps1
```

Keeping the driver *inside* the tool is what makes a regeneration all-or-nothing. Everything comes out
of one process and one enumeration of the shader directory, so the failure that a shell script invites
— some aggregate written from an older shader list, one header re-emitted on a machine without
glslang, the rest left as they were — cannot leave the committed set internally inconsistent. The
enumeration is sorted with a plain byte-wise comparison, so the order of every aggregate is identical
on every machine, shell and locale (`Sort-Object` is culture-sensitive, and its result is baked into
the aggregates).

SPIR-V is embedded when a `glslangValidator` is found: an explicit `--glslang <path>`, then
`VULKAN_SDK\Bin` and `VULKAN_SDK\Bin32`, then `PATH`, then a Visual Studio-bundled copy (globbed out of
`%ProgramFiles%\Microsoft Visual Studio\*\*\Common7\IDE\Extensions\*\external`), then a repo-local
build-tree copy. It is a **generation-time-only** dependency — without it a warning is printed and the
SPIR-V fields are emitted as null, so the headers still build but the Vulkan backend cannot use them.
Since the glslang and `D3DCompile` integrations are Windows-only, a full regeneration has to happen on
Windows.

`--generate-all --check` is the **staleness guard**: it generates into a temporary directory,
byte-compares every result against the committed header, deletes the temporary directory and then
either lists the stale files and exits non-zero or reports that everything is up to date — the tree
is never modified. Missing and extra files count as stale too, and `--check` without glslang warns up
front that every header with embedded SPIR-V will be reported stale. Run it after editing a shader,
or in CI: the build itself never detects stale committed headers, and hand-editing anything under
`Generated/` shows up here as staleness and is overwritten by the next run.

## `--check` dump format

```
program <Name>
render_mode <mode>[, <mode>]                     (omitted when no render_mode is set)
variant (base)                                   (the unnamed base variant; named: "variant <Name>")
  define <NAME>                                  (omitted for the base variant)
  struct <Name> size=<S> align=<A>
    field <type> <name>[N|*] offset=<O>
  uniform <type> <name>[N]
  block <Name> baseSize=<S> instanceStride=<T>
    member <type> <name>[N|*] offset=<O>
  texture <name> unit=<U>
  attribute <type> <name> location=<L>
```

`[*]` marks a `BATCH_SIZE`-sized array; the array suffix is omitted for non-arrays. Empty sections
are omitted. See `tests/` for sample inputs and `tests/expected/` for their exact dumps;
`tests/errors/` holds inputs that must fail to parse. `tests/RunTests.ps1` runs the whole suite
(dump comparisons, expected errors and emitted-header shape assertions).

`tests/GlslSweep.ps1` is a second, independent check: it compiles **every** generated stage source of
every shader with `glslangValidator`, in all three profiles the engine injects at runtime
(`#version 330`, `#version 300 es`, `#version 100`). It exists because the ES profiles reject things
the desktop one accepts — see [Which conditional to write](#which-conditional-to-write) — so a
directive change can pass `--hlsl-check` and `--spirv-check` and still break every ES2/ES3/WebGL
target. Run it after touching a preprocessor directive in any `.shader`.

## ESSL 100 / GLES2 target

The tool's emitted headers carry **modern-GLSL** stage sources (`in`/`out`, `texture()`,
`out vec4 COLOR;`, `layout(std140)` UBO blocks, `gl_VertexID`) that serve both desktop GL 3.3
(`#version 330`) and GLES3/WebGL2 (`#version 300 es`) via runtime `#version` injection. OpenGL
ES 2.0 uses a **different dialect** — ESSL 100 (`#version 100`) — so it needs a genuinely
different source, which `Essl100.h`/`.cpp` produces from the already-lowered modern-GLSL stage.
Every generated header carries that lowering next to the modern one (`_Vs100`/`_Fs100`, the
`ProgramVariant::VsSource100`/`FsSource100` fields), and the engine consumes it under
`NCINE_RHI_GL_PROFILE=ES2`; `#version 100` (like the other versions) is injected by the engine, not
the tool. The ES2 profile additionally links this emitter into the game, so a runtime-compiled
`.shader` gets the same lowering at load time. `--essl100-check` prints the transform of every
variant for inspection without writing anything.

Transforms (vertex-vs-fragment aware, comment-aware, whole-identifier):

| Modern GLSL | ESSL 100 |
| --- | --- |
| `in T name;` (vertex) | `attribute T name;` — a leading `layout(...)` qualifier is dropped (ES2 has none) |
| `out T name;` (vertex) | `varying T name;` |
| `in T name;` (fragment) | `varying T name;` |
| `out vec4 COLOR;` (fragment) | **removed** — `COLOR` becomes a `vec4 COLOR;` local at the top of `main()`, each `return;` is preceded by `gl_FragColor = COLOR;`, and a final `gl_FragColor = COLOR;` is appended before `main()`'s closing brace (the inverse of the modern lowering) |
| `texture(` / `textureLod(` | `texture2D(` / `texture2DLod(` |
| `#ifdef GL_ES … #endif` | **unwrapped** — `GL_ES` is predefined under `#version 100`, so the fragment `precision <p> float;` prologue becomes unconditional |
| `flat` interpolation qualifier | dropped (ES2 has none) |

### Batching and the ES2 rewrites

ES2 has **neither uniform buffer objects nor `gl_VertexID`**, and the shared sprite template uses
both, so the transform rewrites them rather than deferring:

- A `layout(std140)` block becomes plain loose uniforms, or — for the batched `InstancesBlock` — a
  uniform struct array, with the `<instance>.` qualifier dropped now that the wrapper is gone.
- The template's `gl_VertexID` corner formulas become reads of two vertex attributes the runtime
  supplies: `aQuadCorner` (a static corner VBO) and, for batched programs, `aInstanceIndex`
  (`#define i instances[int(aInstanceIndex)]`). The substitutions are exact — each recognized
  formula maps to the corner value the runtime's corner data produces for that vertex.
- `dFdx`/`dFdy`/`fwidth` get a `#extension GL_OES_standard_derivatives : enable` pragma prepended.

A stage is still **declined** (`Transform` returns false with the offending line, and the SPIR-V-style
`*100` fields stay null) for constructs ESSL 100 genuinely cannot express: more than one fragment
`out` (ES2 has only `gl_FragColor`, no MRT), and — as a safety net that catches ES2 breakage the
strict compiler would — a surviving `gl_VertexID` or `std140`, `uint`/`uvec`, `round()`, the integer
`%` operator, an `f`-suffixed float literal, or derivatives without the extension pragma. Signed
`int`/`ivecN` and `int()` casts are deliberately not flagged; they are valid in ESSL 100.

## Direct3D 11 target (HLSL → DXBC)

Each already-lowered modern-GLSL stage is also transformed to HLSL (Shader Model 4/5) by
`Hlsl.h`/`.cpp` (`--hlsl` prints the transform, `--hlsl-check` emits + `D3DCompile`s every stage and
prints a pass/fail table). When emitting a header, the tool then loads `d3dcompiler_47.dll` (ships
with every Windows) and **precompiles both stages of every variant offline** — entry points
`VSMain`/`PSMain`, targets `vs_4_0`/`ps_4_0`, column-major matrix packing plus strictness (the same
contract the D3D11 backend's runtime compilation uses) and full optimization. When both stages
compile, the header embeds **only the DXBC bytecode blobs** (`<Prefix>_VsDxbc`/`_FsDxbc` +
`HlslVsDxbc`/`HlslFsDxbc` pointers/sizes on the `ProgramVariant`) and the HLSL text stays out of the
binary; the runtime — desktop and UWP/Xbox alike, since the blobs live in the shared generated
headers — creates its shader objects directly from the blobs, with no startup `D3DCompile` and no
on-disk shader cache.

The HLSL **source** form remains fully supported as the fallback: with `--no-dxbc`, on a non-Windows
generation machine, or when a stage fails to compile (a warning is printed), the header embeds the
HLSL sources (`HlslVsSource`/`HlslFsSource`) as before and the D3D11 backend runtime-compiles them
(with its on-disk DXBC cache). Like SPIR-V/glslang, `d3dcompiler_47` is a **build-time-only**
dependency of the generated artifacts — headers built without it still compile everywhere. All the
D3D11 artifacts (blobs or sources) are gated behind `#if defined(WITH_RHI_D3D11)`, so other backend
builds carry none of them.

## PlayStation Vita target (Cg → GXP on the console)

`sceGxm` consumes compiled GXP shader binaries, and the VitaSDK ships no offline compiler for them:
the only Cg compiler for the platform is `libshacccg.suprx`, a firmware module extracted from the
console itself. So unlike every other precompiled target, this one ships **source** —
`--emit-cg` writes `Generated/CgGeneratedShaders.h`, two Cg stage strings per program variant, and
the backend compiles them at load time through vitaShaRK. That firmware module is therefore a hard
requirement of the GXM backend, which says so explicitly at startup when it is missing — and of the
platform as a whole, since the vitaGL alternative compiles the GLSL it is handed through the very
same SceShaccCg.

Cg is the same language family as HLSL (`floatN`, `mul()`, `lerp`/`frac`/`ddx`, `TEXCOORD<i>`
interpolants), so it is emitted by the *same* emitter — `Hlsl.h`/`.cpp` with
`HlslEmitter::Dialect::Cg` — rather than one of its own. What differs:

- both entry points are named `main` (not `VSMain`/`PSMain`);
- system semantics are the fixed-function-era Cg set: `POSITION` for the clip position, `WPOS` for
  the fragment position, `COLOR` for the colour output;
- no `cbuffer` — uniforms are plain `uniform` declarations, a std140 block's members are hoisted to
  top-level uniforms, and samplers are combined `sampler2D`/`sampler3D` objects with the GXM
  `TEXUNIT<n>` semantic, read with `tex2D()`/`tex2Dlod()`;
- **no vertex-ID or instance-ID input exists at all**, so `VertexIdRewrite.h` — shared with the ESSL
  100 profile, which has the same gap — rewrites the engine's `gl_VertexID` quad synthesis into reads
  of the `aQuadCorner`/`aInstanceIndex` attributes. A stage that still references either built-in
  afterwards is rejected with a diagnostic rather than mis-emitted;
- a batched shader's `BATCH_SIZE` is baked in as a plain `#define` (a Cg source is compiled as one
  string, with no place to inject a define ahead of it); the backend rewrites that number when the
  runtime settles on a different batch size.

Why its own header rather than two more `ProgramVariant` fields: the DXBC and SPIR-V blobs in the
per-shader headers can only be produced on Windows, so regenerating those headers on any other host
would silently drop both. A separate aggregate keeps a Cg run from touching anything else. The whole
header is gated behind `#if defined(WITH_RHI_GXM)`.

## Editor support

[`../VSCodeExtension`](../VSCodeExtension/README.md) is a Visual Studio Code extension for this
language: highlighting, context-aware completion, hovers, an outline, `#include` navigation and live
diagnostics obtained by running `ShaderCompiler <file> --check` on the buffer. It also exposes the
five inspection dumps (`--check`, `--hlsl`, `--vulkan`, `--cg`, `--essl100-check`) as side-by-side
previews. Plain JavaScript, no build step, no npm dependency.

If you change the language — a new directive, a new `render_mode`, another `fixed_function` pass field
or context facility — update `src/language.js` there as well; it is the single table the completion
and the hovers both read, and its test suite asserts the sets match this document.

## Known limitations

- Object-like macros only; no function-like macro expansion; no token pasting/stringizing.
- One `BATCH_SIZE` array per block, and it must be the last member.
- Struct-typed loose uniforms, sampler arrays, multi-dimensional arrays and array vertex
  attributes are rejected with an error.
- `BlockMember` records struct-typed members as `UniformType::Struct` without the struct's name
  (the `--check` dump preserves it for inspection; nested struct fields are not flattened into
  the block — the block's `InstanceStride` carries everything the runtime needs for batch sizing).
- No cross-product variants (each variant is exactly one define).
