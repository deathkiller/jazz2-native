# Death™ Shader Language — Visual Studio Code extension

Editor support for the custom `.shader` language that
[`Sources/Utilities/ShaderCompiler`](../ShaderCompiler/README.md) compiles: syntax highlighting,
context-aware completion, hovers, an outline, `#include` navigation, and **live diagnostics from the
real compiler**.

There is no build step and no npm dependency — the extension is plain CommonJS JavaScript that the
VS Code extension host runs directly.

## Installing

**Run it from the tree (development / everyday use).** Open this directory in VS Code and press
<kbd>F5</kbd>; a second window opens with the extension loaded. Or symlink/copy the directory into
your extensions folder and restart:

| Platform | Extensions folder |
| --- | --- |
| Windows | `%USERPROFILE%\.vscode\extensions\death-shader` |
| Linux | `~/.vscode/extensions/death-shader` |
| macOS | `~/.vscode/extensions/death-shader` |

**Package it as a `.vsix`** (needs Node, only for packaging):

```
npx @vscode/vsce package
code --install-extension death-shader-1.0.0.vsix
```

## What it does

### Highlighting

A self-contained TextMate grammar — it does not depend on any other GLSL extension. Beyond ordinary
GLSL it knows the language's own vocabulary and colours it distinctly:

- the top-level directives `program`, `shader_type`, `variant`, `render_mode`, `precision`,
  `batched`, `attribute`, `varying`, and the `uniform … : hint` hint list
- the entry points `void vertex()`, `void fragment()` and `void fixed_function([pvr, gx, psp, gs])`
- the fixed-function DSL inside a `fixed_function` block: `pass`, `pipeline`, `submit_quad`,
  `submit_strip`, `submit_strip_shaded`, the pass fields (`color`, `offset_color`, `screen_offset`,
  `blend`, `tev`, `luma_gain`), their `MATERIAL`/`ADD`/… and `MODULATE`/`LUMA_RAMP`/… values, and the
  optional context facilities (`texel_size`, `quad_origin`, `has_uniform`, …)
- the canvas built-ins `COLOR`, `UV`, `TEXTURE`, `PALETTE_OFFSET`, `VERTEX`
- the compile-time stage macros `VERTEX_STAGE`, `FRAGMENT_STAGE`, `SOFTWARE_RENDERER`

Things the language rejects are scoped as errors, so a theme paints them as mistakes on sight:
`fragColor`, a `#version` line, an unsupported canvas built-in (`TIME`, `SCREEN_UV`, …), an unknown
render mode or hint, and a stage macro used in `#if defined(...)` / `#elif` / `#define` / `#undef`.

### Completion

Offered per cursor context rather than as one flat list:

| Where the cursor is | What is offered |
| --- | --- |
| brace depth 0 | the directives, the entry points, `#include`, types, and the names this file declares |
| after `shader_type` / `render_mode` / `precision` | only that directive's legal values |
| after the `:` of a uniform | only the seven uniform hints |
| inside `void fixed_function(…)`'s parentheses | `pvr`, `gx`, `psp`, `gs` |
| inside a `fixed_function` body | the fixed-function DSL, the pass fields and the small maths subset the transpiler accepts |
| inside `vertex()` / `fragment()` | GLSL built-ins, `gl_*`, the mode-appropriate canvas built-ins, and this file's uniforms, varyings, attributes, block members, structs, `#define`s and helper functions |
| inside an `#include "…"` | the sibling `.inc` / `.shader` files, directory by directory |

Canvas built-ins are only offered in `canvas_item` mode (in custom mode `TEXTURE` is an ordinary user
identifier), `VERTEX` only inside `vertex()`, and `TEXTURE`/`PALETTE_OFFSET` only outside it.

### Hovers and navigation

Hovering a directive, built-in, hint, pass field or context facility shows what the compiler does
with it. Hovering something this file declares shows the declaration line. <kbd>Ctrl</kbd>-clicking
an `#include` path opens the file; go-to-definition also works on any name declared in the document.
The outline (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>) lists the programs, variants, uniform
blocks, uniforms, varyings, attributes, helper functions and entry points.

### Diagnostics

Two independent layers:

1. **The compiler itself.** The extension runs `ShaderCompiler <file> --check`, which parses and
   reflects the file and writes nothing, then turns its stderr into squiggles. All three of the
   tool's diagnostic shapes are understood (`<file>:<line>: error: …`, `<file>: error: …` and a bare
   `error: …`), including Windows paths with a drive letter.
2. **A few checks the extension can prove on its own,** so a file still gets feedback with no
   executable around: a missing/duplicate `program`, a missing `void fragment()` (or `void vertex()`
   in custom mode), directives written before `program`, `batched` without `shader_type canvas_item`,
   a `#version` line, a reference to `fragColor`, a stage macro in an illegal preprocessor form, a
   `return;` in a canvas-mode `vertex()`, and an unsupported canvas built-in.
   Every structural check stands down for a file containing `#include`, because the includes are
   expanded textually before parsing and may legitimately carry the program or an entry point.
   Turn the layer off with `deathShader.validate.builtinChecks`.

Diagnostics run on save by default. Set `deathShader.validate.run` to `onType` for live feedback;
a dirty buffer is then written to a temporary copy, which is placed **next to the original** when the
document has `#include` lines (the compiler resolves include paths relative to the input file) and in
the OS temp directory otherwise. Those copies carry a `.vscode-death-shader-tmp.` infix and are
deleted immediately; leftovers from a crashed session are swept up on activation.

> **Line numbers and includes.** The compiler reports lines of the *include-expanded* stream. For a
> file with includes the extension appends a note to the message saying so, rather than pretending
> the line maps cleanly onto the buffer.

### Transform previews

Right-click a `.shader` file, or use the command palette, to open any of the compiler's
inspection-only dumps side by side. None of them writes anything:

| Command | Runs |
| --- | --- |
| Show Reflection Dump | `--check` |
| Show HLSL Transform | `--hlsl` |
| Show Vulkan GLSL Transform | `--vulkan` |
| Show Cg Transform | `--cg` (the PS Vita / sceGxm dialect) |
| Show ESSL 100 Transform | `--essl100-check` |

## Finding the compiler

In order: the `deathShader.compilerPath` setting (absolute, or relative to the workspace folder) →
build outputs in the workspace → `ShaderCompiler` on `PATH`. The build outputs it looks at are

- `Sources/Utilities/ShaderCompiler/{x64,ARM64EC,Win32}/{Release,Debug}/` — where the bundled
  `ShaderCompiler.vcxproj` puts it, and where `GenerateAll.ps1` expects it
- `<build*|cmake-build*>/Sources/Utilities/ShaderCompiler/` — a CMake build tree at the repo root
- `out/build/<preset>/Sources/Utilities/ShaderCompiler/` — the Visual Studio CMake integration

The status-bar item on the right shows which executable is in use, or warns when none was found.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `deathShader.compilerPath` | `""` | Explicit path to the executable; empty means auto-detect |
| `deathShader.validate.enable` | `true` | Report diagnostics at all |
| `deathShader.validate.run` | `onSave` | `onSave` or `onType` |
| `deathShader.validate.delay` | `400` | Debounce in ms for `onType` |
| `deathShader.validate.builtinChecks` | `true` | The executable-free checks listed above |

## Layout

```
package.json                          manifest: language, grammar, snippets, commands, settings
language-configuration.json           comments, brackets, indentation, folding
syntaxes/deathshader.tmLanguage.json  the TextMate grammar
snippets/deathshader.json             file skeletons and directive snippets
src/language.js                       the language vocabulary and its documentation (pure)
src/analysis.js                       text analysis: scanning, cursor context, diagnostics (pure)
src/tool.js                           locating and running ShaderCompiler
src/extension.js                      the editor providers
test/run-tests.js                     tests for the two pure modules
test/run-provider-tests.js            tests for the providers, against a mock vscode API
```

`language.js` and `analysis.js` deliberately import nothing, which is what makes them testable.

## Tests

```
node test/run-tests.js && node test/run-provider-tests.js
```

Both suites also run under `gjs` — they need no dependencies and no Node installation, because the
harness works in any plain JS engine.

`run-tests.js` covers comment stripping, the document scan, cursor-context classification, the three
diagnostic line shapes, and every built-in check — including the cases that must **not** fire (a
`fragColor` inside a comment, `TIME` in custom mode, an early `return` in a custom-mode `fragment()`,
a canvas shader with no `vertex()`, and any file containing an `#include`).

`run-provider-tests.js` drives the real providers against a mock `vscode` module and a mock
`TextDocument`: what completion offers in each cursor context (and what it must *not* offer — no
`VERTEX` in `fragment()`, no canvas built-ins in custom mode, no GLSL `texture()` inside a
`fixed_function` block), the hovers, the outline, `#include` links, go-to-definition, and how a
compiler diagnostic is mapped onto the buffer. The mock is wrapped in a Proxy that throws on any
member it does not define, so a mistyped `vscode.*` API name fails in the test rather than in the
editor.

## Notes

- `.inc` is registered for this language too, since shader includes in this repository use it. If
  that collides with another use of `.inc` in your workspace, remove the extension from the
  `languages` contribution in `package.json`, or override it per workspace with `files.associations`.
- An `.inc` file is treated as an **include fragment**: it is pasted textually into some `.shader`
  file, so it has no `program` directive, no entry points and no `shader_type` of its own. The
  structural checks therefore stand down for it (the checks that do not depend on file structure —
  `#version`, `fragColor`, illegal stage-macro forms — still apply), and because its shader mode is
  unknowable from the file alone, completion offers the canvas built-ins and the sprite contract
  there regardless of mode rather than guessing.
- The extension is marked as not supported in untrusted workspaces: diagnostics and the previews run
  an executable from the workspace.
