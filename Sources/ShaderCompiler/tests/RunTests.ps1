# Runs the ShaderCompiler test suite:
#  1. every tests/*.shader --check dump must match tests/expected/<Name>.check.txt byte-for-byte,
#  2. every tests/errors/*.shader must FAIL with the expected "<line>: error:" + message fragment,
#  3. emitted-header shape assertions (flat verbatim entry-body splice - no scope braces, no
#     re-indentation, no custom-mode COLOR init - COLOR as the fragment output, the unnamed
#     base variant (no "_Base" symbol infix, empty ProgramVariant name), compile-time
#     stage-conditional resolution, precision directive, three-token precision passthrough,
#     unused-function elimination, unused-varying trimming (dead-store removal + the demotion
#     fallback with all its disqualifiers), unused-uniform/block elimination,
#     constant folding, the "attribute" keyword, the canvas-mode implicit TEXTURE declaration
#     and the canvas-mode dead COLOR-init elimination with all its disqualifiers).
# Exit code 0 = all passed.

$ErrorActionPreference = 'Stop'

$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$tool = Join-Path (Split-Path -Parent $testsDir) 'x64\Release\ShaderCompiler.exe'
if (-not (Test-Path $tool)) {
    Write-Host "error: ShaderCompiler.exe not found at '$tool' - build Sources/ShaderCompiler first"
    exit 1
}

$failures = 0
$passed = 0

function Assert($condition, $message) {
    if ($condition) {
        $script:passed++
    } else {
        $script:failures++
        Write-Host "FAIL: $message"
    }
}

# --- 1. reflection dumps -------------------------------------------------------------------------

foreach ($shader in Get-ChildItem (Join-Path $testsDir '*.shader')) {
    $expectedPath = Join-Path $testsDir "expected\$($shader.BaseName).check.txt"
    $actual = (& $tool $shader.FullName --check | Out-String) -replace "`r`n", "`n"
    $ok = ($LASTEXITCODE -eq 0) -and (Test-Path $expectedPath) -and
        ($actual -eq (([System.IO.File]::ReadAllText($expectedPath)) -replace "`r`n", "`n"))
    Assert $ok "$($shader.BaseName): --check dump mismatch (exit=$LASTEXITCODE)"
    if ($ok) { Write-Host "ok: $($shader.BaseName)" }
}

# --- 2. expected errors --------------------------------------------------------------------------

$errorCases = @{
    'FragColorInBody' = @(8, 'fragColor')
    'PrecisionInvalid' = @(2, 'unsupported precision')
    'VertexEarlyReturn' = @(9, 'not allowed in vertex()')
    'StageIfDefined' = @(3, 'support only the "#ifdef"/"#ifndef" forms')
}

foreach ($shader in Get-ChildItem (Join-Path $testsDir 'errors\*.shader')) {
    $case = $errorCases[$shader.BaseName]
    if ($null -eq $case) { Write-Host "FAIL: no expectation registered for errors\$($shader.Name)"; $failures++; continue }
    # PowerShell 5.1 wraps native stderr lines in ErrorRecords when redirected; relax the
    # error preference so the expected diagnostic doesn't terminate the runner
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $stderr = (& $tool $shader.FullName --check 2>&1 | Out-String -Width 1024)
    $ErrorActionPreference = $previousPreference
    $stderr = $stderr -replace "\s+", " "
    $ok = ($LASTEXITCODE -ne 0) -and ($stderr.Contains(":$($case[0]): error:")) -and ($stderr.Contains($case[1]))
    Assert $ok "errors\$($shader.BaseName): expected failure at line $($case[0]) mentioning '$($case[1])' (exit=$LASTEXITCODE)"
    if ($ok) { Write-Host "ok: errors\$($shader.BaseName)" }
}

# --- 3. emitted-header shape assertions ----------------------------------------------------------

$tempDir = Join-Path $env:TEMP "ShaderCompilerTests"
New-Item -ItemType Directory -Force $tempDir | Out-Null

function Get-Source($headerText, $symbol) {
    $m = [regex]::Match($headerText, "(?s)inline constexpr char $symbol\[\] =\s*R`"__SHDR__\((.*?)\)__SHDR__`";")
    if (-not $m.Success) { return $null }
    return $m.Groups[1].Value
}
function Emit($name) {
    $out = Join-Path $tempDir "$name.h"
    & $tool (Join-Path $testsDir "$name.shader") -o $out | Out-Null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ([System.IO.File]::ReadAllText($out)) -replace "`r`n", "`n"
}

# EarlyReturn: the custom-mode body becomes main() VERBATIM (no scope braces, no extra tab,
# no "COLOR = vec4(1.0);" init - COLOR is undefined until written), COLOR is the fragment
# output variable itself, and the early return inside fragment() is legal; the base variant
# is unnamed - no "_Base" symbol infix, and the ProgramVariant initializer's name is ""
$h = Emit 'EarlyReturn'
$fs = Get-Source $h 'EarlyReturn_Fs'
Assert ($null -ne $fs) 'EarlyReturn: FS source missing'
Assert ($fs.Contains("out vec4 COLOR;")) 'EarlyReturn: FS does not declare COLOR as the output'
Assert ($fs.Contains("void main() {`n	if (vTexCoords.x < 0.5) {`n		COLOR = vec4(0.0);`n		return;`n	}`n	COLOR = texture(uTexture, vTexCoords);`n}")) 'EarlyReturn: flat verbatim main() shape (no scope block, no epilogue)'
Assert (-not $fs.Contains('COLOR = vec4(1.0);')) 'EarlyReturn: custom-mode COLOR init still emitted'
Assert (-not $fs.Contains('void fragment(')) 'EarlyReturn: fragment() wrapper still emitted'
Assert (-not $h.Contains('fragColor')) 'EarlyReturn: fragColor appears in the emitted header'
Assert (-not $h.Contains('VERTEX_STAGE') -and -not $h.Contains('FRAGMENT_STAGE')) 'EarlyReturn: stage macros leaked into the emitted header'
Assert (-not $h.Contains('_Base')) 'EarlyReturn: "_Base" symbol infix still emitted for the unnamed base variant'
Assert ($h.Contains('{ "", "", EarlyReturn_Vs, EarlyReturn_Fs,')) 'EarlyReturn: base ProgramVariant initializer does not carry the empty name'

# PrecisionHighp: directive selects the GL_ES prologue, three-token statement passes through
$h = Emit 'PrecisionHighp'
$fs = Get-Source $h 'PrecisionHighp_Fs'
$vs = Get-Source $h 'PrecisionHighp_Vs'
Assert ($fs.Contains("#ifdef GL_ES`nprecision highp float;`n#endif")) 'PrecisionHighp: FS prologue is not highp'
Assert (-not $fs.Contains('precision mediump float;')) 'PrecisionHighp: FS still contains a mediump prologue'
Assert (([regex]::Matches($fs, [regex]::Escape('precision highp float;'))).Count -eq 2) 'PrecisionHighp: three-token passthrough missing from FS globals'
Assert ($vs.Contains("precision highp float;")) 'PrecisionHighp: three-token passthrough missing from VS globals'

# DeadFunctions: FS-only helpers vanish from the VS (transitively), VS-only helper vanishes from
# the FS, unused helper vanishes from both, used helpers survive; stage-guarded declarations are
# resolved at compile time (attribute only in VS, sampler only in FS, no stage macros anywhere)
$h = Emit 'DeadFunctions'
$vs = Get-Source $h 'DeadFunctions_Vs'
$fs = Get-Source $h 'DeadFunctions_Fs'
Assert ($vs.Contains('vec2 vsHelper(vec2 p)')) 'DeadFunctions: vsHelper missing from VS'
Assert (-not $vs.Contains('fsHelper')) 'DeadFunctions: fsHelper not eliminated from VS'
Assert (-not $vs.Contains('fsInner')) 'DeadFunctions: fsInner not eliminated from VS (transitive)'
Assert (-not $vs.Contains('unusedHelper')) 'DeadFunctions: unusedHelper not eliminated from VS'
Assert ($fs.Contains('float fsHelper(float v)')) 'DeadFunctions: fsHelper missing from FS'
Assert ($fs.Contains('float fsInner(float v)')) 'DeadFunctions: fsInner missing from FS'
Assert (-not $fs.Contains('unusedHelper')) 'DeadFunctions: unusedHelper not eliminated from FS'
Assert (-not $fs.Contains('vsHelper')) 'DeadFunctions: vsHelper not eliminated from FS'
Assert ($vs.Contains('in vec2 aPosition;')) 'DeadFunctions: VS lost its stage-guarded attribute'
Assert (-not $fs.Contains('aPosition')) 'DeadFunctions: VS-guarded attribute leaked into FS'
Assert ($fs.Contains('uniform sampler2D uTexture;')) 'DeadFunctions: FS lost its stage-guarded sampler'
Assert (-not $vs.Contains('uniform sampler2D')) 'DeadFunctions: FS-guarded sampler leaked into VS'
Assert (-not $h.Contains('VERTEX_STAGE') -and -not $h.Contains('FRAGMENT_STAGE')) 'DeadFunctions: stage macros leaked into the emitted header'

# CanvasVertex: canvas vertex() body spliced VERBATIM (source indentation, no scope block)
# between the template prologue locals and the epilogue; the FS defaults COLOR (the output
# variable) to the instance color, with the body following directly; the named WAVE variant
# keeps its "_WAVE" symbol infix while the base variant carries none
$h = Emit 'CanvasVertex'
$vs = Get-Source $h 'CanvasVertex_Vs'
$fs = Get-Source $h 'CanvasVertex_Fs'
Assert (-not $vs.Contains('void vertex(')) 'CanvasVertex: vertex() wrapper still emitted'
Assert ($vs.Contains("	highp float PALETTE_OFFSET = palOffset;`n`n#ifdef WAVE`n	VERTEX += vec2(0.0, sin(UV.x * 6.28318) * uAmplitude);`n#endif`n	vOrigin = VERTEX;`n`n	gl_Position = ")) 'CanvasVertex: verbatim vertex body between prologue and epilogue'
Assert ($fs.Contains('out vec4 COLOR;')) 'CanvasVertex: FS does not declare COLOR as the output'
Assert ($fs.Contains("void main() {`n	COLOR = vColor;`n	vec4 c = texture(uTexture, vTexCoords);")) 'CanvasVertex: FS default is not the instance color (or the body is not verbatim after it)'
Assert (-not $h.Contains('fragColor')) 'CanvasVertex: fragColor appears in the emitted header'
Assert ($null -ne (Get-Source $h 'CanvasVertex_WAVE_Vs') -and $null -ne (Get-Source $h 'CanvasVertex_WAVE_Fs')) 'CanvasVertex: named WAVE variant lost its symbol infix'
Assert ($h.Contains('{ "WAVE", "WAVE", CanvasVertex_WAVE_Vs, CanvasVertex_WAVE_Fs,')) 'CanvasVertex: named WAVE ProgramVariant initializer changed'
Assert (-not $h.Contains('_Base')) 'CanvasVertex: "_Base" symbol infix still emitted for the unnamed base variant'
# Unused-varying trimming: the template vPaletteOffset is never read here — its FS "in" is
# removed and, because its only VS occurrence is the pure epilogue store, the VS declaration
# AND the store are removed outright (dead-store removal, no demotion); the read varying
# vOrigin keeps its flat/highp qualifiers in both stages
Assert (-not $fs.Contains('vPaletteOffset')) 'CanvasVertex: unused template vPaletteOffset not trimmed from FS'
Assert (-not $vs.Contains('vPaletteOffset')) 'CanvasVertex: template vPaletteOffset declaration/store not fully removed from VS'
Assert ($vs.Contains("	vColor = COLOR;`n}")) 'CanvasVertex: epilogue shape after the vPaletteOffset store removal changed'
Assert ($vs.Contains('flat out highp vec2 vOrigin;')) 'CanvasVertex: read varying vOrigin lost its VS qualifiers'
Assert ($fs.Contains('flat in highp vec2 vOrigin;')) 'CanvasVertex: read varying vOrigin lost its FS declaration'

# StageGuards: compile-time resolution of #ifdef/#ifndef VERTEX_STAGE|FRAGMENT_STAGE (+#else),
# nested in both directions; unknown conditionals (#ifdef TINT) pass through textually
$h = Emit 'StageGuards'
$vs = Get-Source $h 'StageGuards_Vs'
$fs = Get-Source $h 'StageGuards_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'StageGuards: sources missing'
Assert ($vs.Contains("in vec2 aPosition;`nin vec2 aTexCoords;")) 'StageGuards: VS lost its attributes'
Assert ($vs.Contains('uniform vec2 uVertexOnly;')) 'StageGuards: VS lost the #ifndef FRAGMENT_STAGE branch'
Assert (-not $vs.Contains('uniform sampler2D uTexture;')) 'StageGuards: #else branch leaked into VS'
Assert ($vs.Contains("#ifdef TINT`nuniform vec2 uTintShift;`n#endif")) 'StageGuards: unknown conditional inside a VS stage guard not preserved'
Assert ($vs.Contains("#ifdef TINT`nuniform float uTintStrength;`n#endif")) 'StageGuards: stage guard inside #ifdef TINT not resolved in VS'
Assert (-not $vs.Contains('uTintColor')) 'StageGuards: FS-only declaration inside #ifdef TINT leaked into VS'
Assert ($fs.Contains('uniform sampler2D uTexture;')) 'StageGuards: FS lost the #else branch'
Assert (-not $fs.Contains('uVertexOnly')) 'StageGuards: #ifndef FRAGMENT_STAGE branch leaked into FS'
Assert ($fs.Contains("#ifdef TINT`nuniform vec4 uTintColor;`nuniform float uTintStrength;`n#endif")) 'StageGuards: stage guard inside #ifdef TINT not resolved in FS'
Assert (-not $fs.Contains('uTintShift')) 'StageGuards: VS-only block leaked into FS'
Assert (-not $fs.Contains('aPosition') -and -not $fs.Contains('aTexCoords')) 'StageGuards: attributes leaked into FS'
Assert (-not $h.Contains('VERTEX_STAGE') -and -not $h.Contains('FRAGMENT_STAGE')) 'StageGuards: stage macros leaked into the emitted header'

# VaryingTrim: unread user varyings are removed from the FS; in the VS every occurrence of
# each unread name is a provably-pure dead store (standalone statement, pure RHS), so the
# declarations AND the stores are removed outright (dead-store removal — the 3.7-B demotion
# is only the fallback, exercised by VaryingDemote below). Covers the plain store, the
# flat/highp declaration, the stage-guarded VS-only "out" with no FS "in" at all, and
# swizzled targets with whitelisted constructor/builtin calls (commas inside call argument
# lists are allowed); read varyings survive untouched
$h = Emit 'VaryingTrim'
$vs = Get-Source $h 'VaryingTrim_Vs'
$fs = Get-Source $h 'VaryingTrim_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'VaryingTrim: sources missing'
Assert ($vs.Contains('out vec2 vUsed;')) 'VaryingTrim: read varying vUsed lost its VS out'
Assert ($fs.Contains('in vec2 vUsed;')) 'VaryingTrim: read varying vUsed lost its FS in'
Assert ($vs.Contains('	vUsed = aTexCoords;')) 'VaryingTrim: the read varying lost its VS store'
Assert (-not $vs.Contains('vUnused') -and -not $fs.Contains('vUnused')) 'VaryingTrim: vUnused declaration/store not fully removed'
Assert (-not $vs.Contains('vFlat') -and -not $fs.Contains('vFlat')) 'VaryingTrim: vFlat declaration/store not fully removed (flat/highp handling)'
Assert (-not $vs.Contains('flat ')) 'VaryingTrim: a flat qualifier survived'
Assert (-not $vs.Contains('vGuardedOnly')) 'VaryingTrim: stage-guarded VS-only out (or its store) not fully removed'
Assert (-not $vs.Contains('vSwizzled')) 'VaryingTrim: swizzled-target stores with whitelisted calls not removed'
Assert ($vs.Contains("void main()`n{`n	vUsed = aTexCoords;`n	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);`n}")) 'VaryingTrim: VS main() shape after dead-store removal changed'

# VaryingDemote: the demotion FALLBACK — each disqualifier keeps the 3.7-B behavior for the
# whole name (declaration demoted to a plain global, ALL stores kept — no partial removal):
# a user-function call in the RHS (vCallRhs), a store as an unbraced "else" body (vElseBody),
# a name READ elsewhere in the VS (vReadBack), and a nested assignment in the RHS (vNested,
# whose "(t = 2.0)" side effect must survive). The read varying vUsed is untouched
$h = Emit 'VaryingDemote'
$vs = Get-Source $h 'VaryingDemote_Vs'
$fs = Get-Source $h 'VaryingDemote_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'VaryingDemote: sources missing'
Assert ($vs.Contains('out vec2 vUsed;')) 'VaryingDemote: read varying vUsed lost its VS out'
Assert ($fs.Contains('in vec2 vUsed;')) 'VaryingDemote: read varying vUsed lost its FS in'
Assert ($vs.Contains("`nfloat vCallRhs;")) 'VaryingDemote: user-call RHS did not fall back to demotion'
Assert ($vs.Contains('	vCallRhs = shade(aPosition.x);')) 'VaryingDemote: user-call store was removed'
Assert ($vs.Contains("`nfloat vElseBody;")) 'VaryingDemote: unbraced-else store did not fall back to demotion'
Assert ($vs.Contains('	else vElseBody = aPosition.y;')) 'VaryingDemote: unbraced-else store was removed'
Assert ($vs.Contains("`nfloat vReadBack;")) 'VaryingDemote: VS-read name did not fall back to demotion'
Assert ($vs.Contains('	vReadBack = aPosition.x;')) 'VaryingDemote: store of a VS-read name was removed'
Assert ($vs.Contains("`nfloat vNested;")) 'VaryingDemote: nested-assignment RHS did not fall back to demotion'
Assert ($vs.Contains('	vNested = (t = 2.0);')) 'VaryingDemote: nested-assignment store was removed (side effect lost)'
Assert (-not $fs.Contains('vCallRhs') -and -not $fs.Contains('vElseBody') -and -not $fs.Contains('vReadBack') -and -not $fs.Contains('vNested')) 'VaryingDemote: unread FS ins not removed'
Assert (-not $vs.Contains('out float')) 'VaryingDemote: a demoted declaration kept its out qualifier'

# ConstFold: literal-only subexpressions collapse with exact GLSL semantics — int/float never
# mix, int division truncates, overflow skips, floats fold only when "%.9g" round-trips and is
# not longer than the original, ".0" is appended, constructor arguments fold individually, and
# global-scope declarations never fold (reflection safety)
$h = Emit 'ConstFold'
$vs = Get-Source $h 'ConstFold_Vs'
$fs = Get-Source $h 'ConstFold_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'ConstFold: sources missing'
Assert ($fs.Contains('float a = 7.0;')) 'ConstFold: float chain 2.0 * 3.0 + 1.0 not folded'
Assert ($fs.Contains('int b = 3;')) 'ConstFold: int division 10 / 3 not folded (truncation)'
Assert ($fs.Contains('int c = -8;')) 'ConstFold: 7 % 3 - 9 not folded'
Assert ($fs.Contains('float d = 1 / 255.0;')) 'ConstFold: mixed int/float expression was folded'
Assert ($fs.Contains('float e = a * 2.0 + 1.0;')) 'ConstFold: identifier-containing expression was folded'
Assert ($fs.Contains('int f = 2000000000 + 2000000000;')) 'ConstFold: int overflow was folded'
Assert ($fs.Contains('vec2 g = vec2(3.0, 3.0);')) 'ConstFold: constructor argument not folded'
Assert ($fs.Contains('float h = 0.25;')) 'ConstFold: 1.0 / 4.0 not folded (%.9g formatting)'
Assert ($fs.Contains('float i2 = 6.0;')) 'ConstFold: ".0" not appended to a whole-number float'
Assert ($fs.Contains('float j = 1.0 / 3.0;')) 'ConstFold: non-round-trippable float was folded'
Assert ($fs.Contains('float k = 1.0 / 8192.0;')) 'ConstFold: longer-than-original fold was applied'
Assert ($fs.Contains('uint m = 2u + 3u;')) 'ConstFold: unsigned literals were folded'
Assert ($fs.Contains(', 0.0, 0.5, 1.0);')) 'ConstFold: 1.0 - 0.5 not folded inside constructor arguments'
Assert ($fs.Contains('const float kGlobal = 1.0 + 2.0;')) 'ConstFold: global-scope declaration was folded (reflection hazard)'
Assert ($vs.Contains('aPosition * 1.0')) 'ConstFold: parenthesized (2.0 * 0.5) not folded in VS'

# Attribute: the "attribute" keyword emits a vertex-stage-only "in" global; a leading
# layout(...) qualifier stays in front of the "in" keyword; nothing is emitted in the FS
$h = Emit 'Attribute'
$vs = Get-Source $h 'Attribute_Vs'
$fs = Get-Source $h 'Attribute_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'Attribute: sources missing'
Assert ($vs.Contains("in vec2 aPosition;")) 'Attribute: VS lost the plain attribute'
Assert ($vs.Contains("layout(location = 3) in vec4 aColor;")) 'Attribute: VS lost the layout-qualified attribute (layout must precede "in")'
Assert (-not $fs.Contains('aPosition') -and -not $fs.Contains('aColor')) 'Attribute: attributes leaked into FS'

# ImplicitTexture: a canvas document referencing TEXTURE without declaring uTexture gets
# "uniform sampler2D uTexture;" auto-declared in the FS globals (texture unit 0 is asserted
# by the check dump); the template VS carries no user globals, so no sampler appears there
$h = Emit 'ImplicitTexture'
$vs = Get-Source $h 'ImplicitTexture_Vs'
$fs = Get-Source $h 'ImplicitTexture_Fs'
$bfs = Get-Source $h 'BatchedImplicitTexture_Fs'
Assert ($null -ne $vs -and $null -ne $fs -and $null -ne $bfs) 'ImplicitTexture: sources missing'
Assert (([regex]::Matches($fs, [regex]::Escape('uniform sampler2D uTexture;'))).Count -eq 1) 'ImplicitTexture: FS must auto-declare uTexture exactly once'
Assert (([regex]::Matches($bfs, [regex]::Escape('uniform sampler2D uTexture;'))).Count -eq 1) 'ImplicitTexture: batched FS must auto-declare uTexture exactly once'
Assert (-not $vs.Contains('uniform sampler2D')) 'ImplicitTexture: sampler leaked into the template VS'
Assert ($fs.Contains('texture(uTexture, vTexCoords + uPixelOffset)')) 'ImplicitTexture: TEXTURE/UV not substituted in the body'
# The body is "COLOR = texture(...) * COLOR;" - the right-hand side reads the default back,
# so the dead-init elimination must NOT fire here
Assert ($fs.Contains("void main() {`n	COLOR = vColor;")) 'ImplicitTexture: COLOR init dropped although the assignment RHS reads COLOR'

# CanvasTinted: an explicit uTexture declaration wins over the implicit one - the FS carries
# exactly one declaration, nothing is auto-added
$h = Emit 'CanvasTinted'
$fs = Get-Source $h 'Tinted_Fs'
Assert (([regex]::Matches($fs, [regex]::Escape('uniform sampler2D uTexture;'))).Count -eq 1) 'CanvasTinted: explicit uTexture declaration must win (exactly one declaration)'

# UniformTrim: unused-uniform/block elimination (loose uniforms, dead defines, the standalone
# "#ifndef X/#define X/#endif" fallback trio and the reflection-preservation rule):
#  - the FS-only sampler + the multi-name "uniform vec2 uPair, uOther;" drop from the VS
#    (uPair is read by the FS, so the FS keeps the WHOLE multi-name declaration - conservative)
#  - the VS-only matrices, InstanceBlock, uLights and its LIGHT_COUNT fallback trio drop from
#    the FS (the trio dies as a unit once uLights is gone - no empty guard survives)
#  - uUnusedBoth is referenced in NEITHER stage and must stay in BOTH (reflection preservation;
#    the check dump above already asserted it still reflects)
#  - the dead function-like DEAD_SCALE define drops from both stages, the live LIVE_BIAS define
#    stays in the FS (and drops from the VS, where nothing references it)
$h = Emit 'UniformTrim'
$vs = Get-Source $h 'UniformTrim_Vs'
$fs = Get-Source $h 'UniformTrim_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'UniformTrim: sources missing'
Assert ($vs.Contains('uniform mat4 uProjectionMatrix;')) 'UniformTrim: VS lost uProjectionMatrix'
Assert ($vs.Contains('layout (std140) uniform InstanceBlock')) 'UniformTrim: VS lost InstanceBlock'
Assert ($vs.Contains("#ifndef LIGHT_COUNT`n	#define LIGHT_COUNT 2`n#endif")) 'UniformTrim: VS lost the LIGHT_COUNT fallback trio (uLights is alive there)'
Assert ($vs.Contains('uniform vec4 uLights[LIGHT_COUNT];')) 'UniformTrim: VS lost uLights'
Assert (-not $vs.Contains('uniform sampler2D')) 'UniformTrim: FS-only sampler not eliminated from VS'
Assert (-not $vs.Contains('uPair')) 'UniformTrim: FS-only multi-name declaration not eliminated from VS'
Assert (-not $vs.Contains('DEAD_SCALE') -and -not $vs.Contains('LIVE_BIAS')) 'UniformTrim: dead defines not eliminated from VS'
Assert (-not $fs.Contains('uProjectionMatrix') -and -not $fs.Contains('uViewMatrix')) 'UniformTrim: VS-only matrices not eliminated from FS'
Assert (-not $fs.Contains('InstanceBlock') -and -not $fs.Contains('modelMatrix')) 'UniformTrim: VS-only block not eliminated from FS'
Assert (-not $fs.Contains('uLights') -and -not $fs.Contains('LIGHT_COUNT')) 'UniformTrim: uLights + its fallback trio not eliminated from FS'
Assert ($fs.Contains('uniform sampler2D uTexture;')) 'UniformTrim: FS lost its sampler'
Assert ($fs.Contains('uniform vec2 uPair, uOther;')) 'UniformTrim: FS lost the multi-name declaration (conservative keep when ANY name is read)'
Assert ($vs.Contains('uniform float uUnusedBoth;')) 'UniformTrim: uUnusedBoth eliminated from VS (must stay - unused in BOTH stages)'
Assert ($fs.Contains('uniform float uUnusedBoth;')) 'UniformTrim: uUnusedBoth eliminated from FS (must stay - unused in BOTH stages)'
Assert (-not $fs.Contains('DEAD_SCALE')) 'UniformTrim: dead function-like define not eliminated from FS'
Assert ($fs.Contains('#define LIVE_BIAS(x) ((x) + 0.125)')) 'UniformTrim: live function-like define eliminated from FS'

# UniformTrimBatched: the batched-sprite shape (shared globals, no stage guards) regains the
# minimal per-stage declaration sets automatically - the FS drops the "#define i" chain, then
# InstancesBlock (with the in-block BATCH_SIZE fallback trio), then "struct Instance" and the
# matrices; the VS keeps all of them (and its BATCH_SIZE trio) and drops only the FS sampler
$h = Emit 'UniformTrimBatched'
$vs = Get-Source $h 'UniformTrimBatched_Vs'
$fs = Get-Source $h 'UniformTrimBatched_Fs'
Assert ($null -ne $vs -and $null -ne $fs) 'UniformTrimBatched: sources missing'
Assert ($vs.Contains('struct Instance')) 'UniformTrimBatched: VS lost struct Instance'
Assert ($vs.Contains('layout (std140) uniform InstancesBlock')) 'UniformTrimBatched: VS lost InstancesBlock'
Assert ($vs.Contains("#ifndef BATCH_SIZE`n	#define BATCH_SIZE (682) // 64 Kb / 96 b`n#endif")) 'UniformTrimBatched: VS lost the BATCH_SIZE fallback trio (its block survives there)'
Assert ($vs.Contains('#define i block.instances[gl_VertexID / 6]')) 'UniformTrimBatched: VS lost the "#define i" accessor'
Assert (-not $vs.Contains('uniform sampler2D')) 'UniformTrimBatched: FS-only sampler not eliminated from VS'
Assert (-not $fs.Contains('#define i')) 'UniformTrimBatched: dead "#define i" not eliminated from FS'
Assert (-not $fs.Contains('InstancesBlock') -and -not $fs.Contains('instances')) 'UniformTrimBatched: VS-only InstancesBlock not eliminated from FS'
Assert (-not $fs.Contains('BATCH_SIZE')) 'UniformTrimBatched: in-block BATCH_SIZE fallback trio survived the FS block removal'
Assert (-not $fs.Contains('struct Instance') -and -not $fs.Contains('modelMatrix')) 'UniformTrimBatched: dead struct Instance not eliminated from FS'
Assert (-not $fs.Contains('uProjectionMatrix') -and -not $fs.Contains('uViewMatrix')) 'UniformTrimBatched: VS-only matrices not eliminated from FS'
Assert ($fs.Contains('uniform sampler2D uTexture;')) 'UniformTrimBatched: FS lost its sampler'

# CanvasInit*: dead canvas COLOR-init elimination. The "COLOR = vColor;" default is dropped
# IFF the body's FIRST whole-identifier COLOR occurrence is an unconditional (preprocessor
# depth 0), top-level (brace depth 0) FULL plain assignment whose right-hand side does not
# read COLOR back, with no "return" before it. CanvasInitDead qualifies (init absent, body
# follows main() directly; the unread vColor varying is then trimmed too); every disqualifier
# keeps the init: read-before-write, partial write ("COLOR.a ="), compound op ("COLOR *="),
# first write inside #ifdef, first write inside an if block, "return" before the first write,
# comparison "COLOR ==" as the first occurrence
$h = Emit 'CanvasInitDead'
$fs = Get-Source $h 'CanvasInitDead_Fs'
Assert ($null -ne $fs) 'CanvasInitDead: FS source missing'
Assert (-not $fs.Contains('COLOR = vColor;')) 'CanvasInitDead: dead COLOR init still emitted'
Assert ($fs.Contains("void main() {`n	vec4 c = texture(uTexture, vTexCoords);")) 'CanvasInitDead: body does not follow main() directly'
Assert (-not $fs.Contains('vColor')) 'CanvasInitDead: unread vColor varying not trimmed after the init removal'

foreach ($name in @('CanvasInitReadFirst', 'CanvasInitPartialWrite', 'CanvasInitCompound', 'CanvasInitIfdef', 'CanvasInitBraced', 'CanvasInitReturn', 'CanvasInitCompare')) {
    $h = Emit $name
    $fs = Get-Source $h "${name}_Fs"
    Assert ($null -ne $fs -and $fs.Contains("void main() {`n	COLOR = vColor;")) "${name}: disqualified body lost the COLOR init"
}

# TexturePassthrough: custom mode never substitutes TEXTURE and never auto-declares -
# TEXTURE stays an ordinary user identifier (here a sampler, unit from the texture_unit hint)
$h = Emit 'TexturePassthrough'
$fs = Get-Source $h 'TexturePassthrough_Fs'
Assert ($fs.Contains('uniform sampler2D TEXTURE;')) 'TexturePassthrough: user TEXTURE sampler declaration missing from FS'
Assert ($fs.Contains('texture(TEXTURE, vTexCoords)')) 'TexturePassthrough: TEXTURE was substituted in custom mode'
Assert (-not $h.Contains('uTexture')) 'TexturePassthrough: uTexture auto-declaration leaked into custom mode'

# --- 4. ESSL 100 (OpenGL ES 2.0) target -----------------------------------------------------------
# The --essl100-check transform dump: attribute/varying split per stage, texture->texture2D,
# the "out vec4 COLOR;" -> gl_FragColor retarget (incl. early "return;"), the unconditional
# precision prologue, layout()/flat stripping, and the slice-2 deferral of gl_VertexID / std140-UBO
# programs (fixtures live in tests/essl100/, outside the section-1 golden-dump scan).

function Essl100($path) {
    (& $tool $path --essl100-check | Out-String -Width 4096) -replace "`r`n", "`n"
}
function Get-Essl100Stage($dump, $stage) {
    # Extracts one stage section of a single-variant program dump ('vertex' or 'fragment')
    if ($stage -eq 'vertex') {
        return [regex]::Match($dump, "(?s)--- vertex \(essl100\) ---\n(.*?)\n--- fragment").Groups[1].Value
    }
    return [regex]::Match($dump, "(?s)--- fragment \(essl100\) ---\n(.*)").Groups[1].Value
}

# Textured: non-batched textured shader - the attribute/varying split per stage, the layout()
# qualifier stripped to a plain attribute, texture()->texture2D(), the gl_FragColor epilogue and
# the unconditional mediump precision prologue
$d = Essl100 (Join-Path $testsDir 'essl100\Textured.shader')
$vs = Get-Essl100Stage $d 'vertex'
$fs = Get-Essl100Stage $d 'fragment'
Assert ($vs.Contains('attribute vec2 aPosition;')) 'Essl100 Textured: VS "in" not turned into "attribute"'
Assert ($vs.Contains('attribute vec2 aTexCoords;') -and -not $vs.Contains('layout')) 'Essl100 Textured: layout-qualified attribute not stripped to a plain attribute'
Assert ($vs.Contains('varying vec2 vTexCoords;')) 'Essl100 Textured: VS "out" not turned into "varying"'
Assert ($vs.Contains('uniform mat4 uProjectionMatrix;')) 'Essl100 Textured: VS uniform changed'
Assert ($fs.Contains('precision mediump float;') -and -not $fs.Contains('#ifdef GL_ES')) 'Essl100 Textured: FS precision prologue not made unconditional'
Assert ($fs.Contains('varying vec2 vTexCoords;')) 'Essl100 Textured: FS "in" not turned into "varying"'
Assert (-not $fs.Contains('out vec4 COLOR;')) 'Essl100 Textured: FS "out vec4 COLOR;" not removed'
Assert ($fs.Contains('vec4 COLOR;')) 'Essl100 Textured: FS local COLOR not declared in main()'
Assert ($fs.Contains('texture2D(uTexture, vTexCoords)')) 'Essl100 Textured: texture() not rewritten to texture2D()'
Assert ($fs.Contains('gl_FragColor = COLOR;')) 'Essl100 Textured: gl_FragColor epilogue missing'

# VaryingEarlyReturn: the highp precision directive, a highp varying preserved through both stages,
# the early "return;" retargeted, and textureLod()->texture2DLod()
$d = Essl100 (Join-Path $testsDir 'essl100\VaryingEarlyReturn.shader')
$vs = Get-Essl100Stage $d 'vertex'
$fs = Get-Essl100Stage $d 'fragment'
Assert ($vs.Contains('attribute vec2 aPosition;')) 'Essl100 Varying: VS attribute missing'
Assert ($vs.Contains('varying highp vec2 vTexCoords;')) 'Essl100 Varying: VS highp varying not preserved'
Assert ($fs.Contains('precision highp float;')) 'Essl100 Varying: highp precision directive not honored'
Assert ($fs.Contains('varying highp vec2 vTexCoords;')) 'Essl100 Varying: FS highp varying not preserved'
Assert ($fs.Contains('gl_FragColor = COLOR; return;')) 'Essl100 Varying: early "return;" not retargeted to gl_FragColor'
Assert ($fs.Contains('texture2DLod(uTexture, vTexCoords, 0.0)')) 'Essl100 Varying: textureLod() not rewritten to texture2DLod()'
Assert ((([regex]::Matches($fs, [regex]::Escape('gl_FragColor = COLOR;'))).Count) -ge 2) 'Essl100 Varying: expected both the early-return and the final gl_FragColor writes'

# BatchedSprites: a std140-UBO + gl_VertexID batched program defers its VS to slice 2 (std140 cited
# first, being the earliest offending line) while its FRAGMENT stage still transforms cleanly
$d = Essl100 (Join-Path $testsDir 'BatchedSprites.shader')
$vs = Get-Essl100Stage $d 'vertex'
$fs = Get-Essl100Stage $d 'fragment'
Assert ($vs.Contains('unsupported in ES2') -and $vs.Contains('std140')) 'Essl100 BatchedSprites: VS not deferred with a std140 diagnostic'
Assert ($fs.Contains('texture2D(uTexture, vTexCoords)')) 'Essl100 BatchedSprites: FS texture() not rewritten'
Assert ($fs.Contains('gl_FragColor = COLOR;') -and -not $fs.Contains('out vec4 COLOR;')) 'Essl100 BatchedSprites: FS COLOR not retargeted to gl_FragColor'

# TexturePassthrough: a gl_VertexID-only (no UBO) program defers its VS citing gl_VertexID; the
# fragment stage transforms regardless of the deferred vertex stage
$d = Essl100 (Join-Path $testsDir 'TexturePassthrough.shader')
$vs = Get-Essl100Stage $d 'vertex'
$fs = Get-Essl100Stage $d 'fragment'
Assert ($vs.Contains('unsupported in ES2') -and $vs.Contains('gl_VertexID')) 'Essl100 TexturePassthrough: VS not deferred citing gl_VertexID'
Assert ($fs.Contains('texture2D(TEXTURE, vTexCoords)') -and $fs.Contains('gl_FragColor = COLOR;')) 'Essl100 TexturePassthrough: FS not transformed'

Write-Host ''
if ($failures -eq 0) {
    Write-Host "All tests passed ($passed assertions)."
    exit 0
} else {
    Write-Host "$failures FAILURE(S), $passed passed."
    exit 1
}
