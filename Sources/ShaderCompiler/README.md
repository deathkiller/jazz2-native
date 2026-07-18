# ShaderCompiler

Standalone offline shader-preprocessing tool for Jazz² Resurrection.
It expands shader variants and performs GLSL declaration reflection **offline**, emitting a
self-contained C++ header with the sources and reflection data, so the runtime no longer needs
`glGetActiveUniform` introspection or double compilation of batched shaders to size the std140
`InstancesBlock`.

- C++17, zero dependencies beyond the standard library (portable across MSVC/GCC/Clang).

## Usage

```
ShaderCompiler <input.shader> -o <output.h> [-n <namespace>] [--check] [--essl100-check]
```

| Option | Meaning |
| --- | --- |
| `-o <output.h>` | Path of the generated C++ header |
| `-n <namespace>` | Namespace for the generated program data (default `ShaderArtifacts`) |
| `--check` | Parse and print a human-readable reflection dump to stdout instead of writing output |
| `--essl100-check` (or `--target essl100`) | Print the ESSL 100 (OpenGL ES 2.0) transform of every variant's stage sources to stdout, for inspection (see below) — tool-only, does not write or change any header |

Errors are reported to stderr as `<file>:<line>: error: <message>` with a non-zero exit code.

## Building the tool

```
cmake -S Sources/ShaderCompiler -B build-shadercompiler
cmake --build build-shadercompiler
```

## Input format (`.shader`)

A `.shader` file is a custom shader language: GLSL globals plus `vertex()`/`fragment()`
entry points, annotated with plain **keyword directives** — top-level statements at brace depth 0,
terminated by `;`. Comments follow GLSL rules (`//` and `/* */`) and pass through into the
emitted sources.

| Directive | Meaning |
| --- | --- |
| `program <Name>;` | Required, exactly once, must come before any other directive or entry point. C++/GLSL identifier. |
| `shader_type canvas_item;` or `shader_type custom;` | Optional — the **default is `custom`**. `canvas_item` opts into the sprite-template lowering (see below). |
| `variant <NAME>;` | Declares an optional variant; may appear multiple times. The output contains the **unnamed base variant** (no variant defines, `Name` is `""`, always `Variants[0]`) plus **one** additional entry per variant, compiled with `#define <NAME> (1)` baked in. No cross-products. |
| `render_mode <mode>[, <mode>];` | Zero or more of `blend_mix`, `blend_add`, `blend_sub`, `blend_mul`, `blend_premul_alpha`, `unshaded`. Stored as the `RenderModes` bitmask on the emitted `Program` (`ShaderCompiler::RenderMode` flags) and shown in the `--check` dump. |
| `precision mediump;` or `precision highp;` | Optional (default `mediump`) — selects the float precision of the auto-emitted `#ifdef GL_ES precision X float; #endif` fragment prologue in both modes; anything else is an error. **Only the two-token form is a directive**: a real GLSL global precision statement with a type (`precision highp float;`) passes through as ordinary GLSL. |
| `batched <Name>;` | `canvas_item` only — also emits the batched twin program (`InstancesBlock` + 6-vertex corner formula) into the same header, sharing the fragment stage and variants. An error in custom mode. Offline-only for now — the runtime `CompileRuntimeProgram` compiles just the primary program. |
| `#include "relative/path"` | Replaced **textually** by the contents of the referenced file (relative to the including file), recursively up to depth 8. Runs on the raw text before parsing, so both reflection and the emitted sources see the included text inlined and the generated artifacts stay self-contained. Note: line numbers in diagnostics refer to the include-expanded stream. |

Elements shared by both modes:

| Element | Meaning |
| --- | --- |
| `varying [flat] [precision] <type> <name>;` | Lowered to an `out` in the vertex stage and an `in` in the fragment stage, qualifiers preserved. |
| `attribute [layout(location = N)] <declaration>;` | A vertex attribute: emitted as an `in` global in the **vertex stage only** (nothing appears in the fragment stage). The declaration passes through verbatim after `in`; a leading `layout(...)` qualifier stays in front of the `in` keyword (`attribute layout(location = 0) vec2 aPosition;` emits `layout(location = 0) in vec2 aPosition;`, and the location is honored by reflection). Allowed in both modes, but note the canvas templates do not reference user attributes. |
| `uniform <type> <name> : <hint>[, <hint>];` | `texture_unit(N)` assigns the texture unit (0–31) of a sampler uniform, recorded in reflection — the **only** way to assign one explicitly (the legacy `texture <name> <unit>;` directive was removed and is now a parse error suggesting this hint); the name must match a sampler in at least one variant. `source_color`, `hint_range(...)`, `filter_nearest`, `filter_linear`, `repeat_enable`, `repeat_disable` are parsed and dropped; anything else is an error. The lowered declaration keeps the original text with the hint list stripped, so hints never reach the emitted sources. |
| everything else | Global scope (uniforms, blocks, consts, structs, helper functions, `#ifdef` blocks) is **shared by both stages** of every lowered document. |

Do **not** put `#version` in the input — the engine injects the version header (`#version 330` /
`#version 300 es`) and platform defines at runtime (see `GLShader::LoadFromStringsAndFile`).

### Stage conditionals (resolved at compile time)

Shared globals can wrap stage-specific **declarations** in `#ifdef VERTEX_STAGE` /
`#ifdef FRAGMENT_STAGE` / `#ifndef …` conditionals (with an optional `#else`), with zero extra
section syntax. **They are rarely needed anymore**: vertex attributes have the `attribute`
keyword, varying pairs have `varying`, and everything else (uniforms, samplers, blocks, consts,
`#define`s) is harmless when shared — an unused declaration in the other stage changes neither
the merged reflection nor the rendered output, so no shipped `.shader` source uses stage guards
today. The feature stays supported for future edge cases:

```glsl
#ifdef VERTEX_STAGE
in vec2 aPosition;
#endif
```

These conditionals are **evaluated during stage assembly**: the guarded lines are kept or dropped
for the stage being built and the directive lines disappear — the emitted sources contain **no
stage macros at all** (no baked `#define`, no `#ifdef VERTEX_STAGE` text), and the reflection
preprocessor needs no stage predefines because it runs on the pre-resolved streams. All other
conditionals (`#ifdef GL_ES`, variant defines, `BATCH_SIZE`) pass through textually untouched.
Nesting works in both directions: an unknown conditional inside a stage block is kept/dropped
with the block, and a stage conditional inside an unknown conditional is still resolved in place.
Only the `#ifdef`/`#ifndef` forms are supported — a stage macro in any other preprocessor
construct (`#if defined(VERTEX_STAGE)`, `#elif`, `#define`, `#undef`) is an error.

Helper **functions** do not need such guards — see unused-function elimination below: an FS-only
helper (e.g. one using `dFdx`/`dFdy`) is simply removed from the emitted vertex stage because
nothing there references it. Vertex attributes — the case that used to *require* a guard, since a
bare `in` global leaking into the fragment stage would create a bogus varying — are covered by
the `attribute` keyword instead.

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
(GenerateAll.ps1 emits it as `Generated/ShaderCompilerTypes.h`); every generated header includes it, and
engine code can include it directly to consume reflection without pulling in any program's data.

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

## ESSL 100 / GLES2 target (in progress)

The tool's emitted headers carry **modern-GLSL** stage sources (`in`/`out`, `texture()`,
`out vec4 COLOR;`, `layout(std140)` UBO blocks, `gl_VertexID`) that serve both desktop GL 3.3
(`#version 330`) and GLES3/WebGL2 (`#version 300 es`) via runtime `#version` injection. OpenGL
ES 2.0 uses a **different dialect** — ESSL 100 (`#version 100`) — so it needs a genuinely
different source. `--essl100-check` prints a source-to-source transform of each already-lowered
stage into ESSL 100, for inspection (`Essl100.h`/`.cpp`). This is a **tool-only** surface: the
committed `Generated/` artifacts are unchanged, no runtime is wired yet, and `#version 100` (like
the other versions) is injected by the engine, not the tool.

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

### Batched-shader gap

ES2 has **neither uniform buffer objects nor `gl_VertexID`**. A stage source that uses a
`layout(std140)` block and/or `gl_VertexID` is **not** transformed — `--essl100-check` prints
`unsupported in ES2 (needs uniform-array batching + corner attribute)` with the offending
line for that stage (the other stage, if clean, still transforms). This affects **every** sprite
program, not only the `batched` twins: the shared sprite template lowers the corner position from
`gl_VertexID` and reads instance data from a std140 `InstanceBlock`/`InstancesBlock` UBO, so both
the batched twin **and** its non-batched primary trip the deferral. Of the shipped shaders only
`DefaultImGui` (hand-written `attribute`/`varying`, no UBO, no `gl_VertexID`) translates today.
The real transforms the rest need — UBO → uniform array, `gl_VertexID` → a supplied corner
attribute — are intentionally not attempted here.

## Known limitations

- Object-like macros only; no function-like macro expansion; no token pasting/stringizing.
- One `BATCH_SIZE` array per block, and it must be the last member.
- Struct-typed loose uniforms, sampler arrays, multi-dimensional arrays and array vertex
  attributes are rejected with an error.
- `BlockMember` records struct-typed members as `UniformType::Struct` without the struct's name
  (the `--check` dump preserves it for inspection; nested struct fields are not flattened into
  the block — the block's `InstanceStride` carries everything the runtime needs for batch sizing).
- No cross-product variants (each variant is exactly one define).
