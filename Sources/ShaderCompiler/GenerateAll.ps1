# Runs ShaderCompiler.exe over every Sources/Shaders/*.shader and emits the generated C++ headers
# into Sources/Shaders/Generated/<Name>.h.
#
# Namespace convention:
#   Default*.shader (nCine default shader programs) -> nCine::ShadersGen
#   everything else (Jazz2 PrecompiledShader programs) -> Jazz2::ShadersGen
#
# Fails with a non-zero exit code if any shader fails to process.
#
# SPIR-V for the Vulkan backend is embedded when a glslangValidator can be found (VULKAN_SDK, PATH, a
# repo-local build-tree copy, or a Visual Studio-bundled copy) or supplied with -Glslang <path>. glslang
# is a BUILD-TIME-only dependency: when it is unavailable the SPIR-V fields are emitted as nullptr/0 and a
# warning is printed (the headers still build; the Vulkan backend is then not buildable).
#
# -Check: staleness guard. Generates into a temporary directory instead and byte-compares the result
# against the committed Sources/Shaders/Generated headers; exits non-zero (listing the stale files) if
# they differ, without modifying the tree. Run it after editing a .shader (or in CI) to catch a
# forgotten regeneration - the build itself never detects stale committed headers.

param([string]$Glslang = '', [switch]$Check)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$shadersDir = Join-Path (Split-Path -Parent $scriptDir) 'Shaders'
$committedDir = Join-Path $shadersDir 'Generated'
if ($Check) {
    $outDir = Join-Path ([System.IO.Path]::GetTempPath()) ("Jazz2-ShaderCheck-" + [System.IO.Path]::GetRandomFileName())
} else {
    $outDir = $committedDir
}
$tool = Join-Path $scriptDir 'x64\Release\ShaderCompiler.exe'

if (-not (Test-Path $tool)) {
    Write-Host "error: ShaderCompiler.exe not found at '$tool' - build Sources/ShaderCompiler first"
    exit 1
}
if (-not (Test-Path $shadersDir)) {
    Write-Host "error: Shader directory not found at '$shadersDir'"
    exit 1
}

# Locate glslangValidator for offline SPIR-V compilation (build-time only; see the header comment)
function Find-Glslang([string]$override) {
    if ($override -and (Test-Path $override)) { return (Resolve-Path $override).Path }
    if ($env:VULKAN_SDK) {
        foreach ($sub in @('Bin\glslangValidator.exe', 'Bin32\glslangValidator.exe')) {
            $p = Join-Path $env:VULKAN_SDK $sub
            if (Test-Path $p) { return $p }
        }
    }
    $cmd = Get-Command glslangValidator.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Visual Studio-bundled copy (best-effort glob)
    $vs = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\Common7\IDE\Extensions\*\external\glslangValidator.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vs) { return $vs.FullName }
    # Repo-local build-tree copy (may be transient)
    $repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
    $repoCopy = Join-Path $repoRoot '.fake\_legacy\.fake\glsl\glslangValidator.exe'
    if (Test-Path $repoCopy) { return $repoCopy }
    return $null
}

$glslangPath = Find-Glslang $Glslang
$glslangArgs = @()
if ($glslangPath) {
    Write-Host "using glslang: $glslangPath"
    $glslangArgs = @('--glslang', $glslangPath)
} else {
    Write-Host "warning: glslangValidator not found - Vulkan SPIR-V will be omitted (install the Vulkan SDK for the Vulkan backend)"
}

New-Item -ItemType Directory -Force $outDir | Out-Null

# Shared reflection types included by every generated header (and by engine code that consumes reflection)
& $tool --emit-types (Join-Path $outDir 'ShaderCompilerTypes.h')
if ($LASTEXITCODE -ne 0) {
    Write-Host "error: failed to emit ShaderCompilerTypes.h"
    exit 1
}

$shaders = @(Get-ChildItem (Join-Path $shadersDir '*.shader') | Sort-Object Name)
if ($shaders.Count -eq 0) {
    Write-Host "error: No .shader files found in '$shadersDir'"
    exit 1
}

$failed = 0
foreach ($shader in $shaders) {
    $name = $shader.BaseName
    if ($name -like 'Default*') {
        $ns = 'nCine::ShadersGen'
    } else {
        $ns = 'Jazz2::ShadersGen'
    }
    $outPath = Join-Path $outDir ($name + '.h')

    & $tool $shader.FullName -o $outPath -n $ns @glslangArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "error: '$($shader.Name)' failed with exit code $LASTEXITCODE"
        $failed++
    } else {
        Write-Host "ok: $($shader.Name) -> Generated\$name.h [$ns]"
    }
}

if ($failed -gt 0) {
    Write-Host "error: $failed of $($shaders.Count) shader(s) failed"
    exit 1
}

# Emit the umbrella header including every generated program plus per-namespace index arrays,
# so runtime code has a single include and can enumerate the programs.
# A canvas_item file with a "batched" directive carries TWO programs in one header, so the
# program symbols come from the directives, not from the file names.
function Get-ProgramNames($shaderFile) {
    $names = @()
    foreach ($line in Get-Content $shaderFile.FullName) {
        if ($line -cmatch '^\s*(?:program|batched)\s+(\w+)\s*;') {
            $names += $Matches[1]
        }
    }
    return $names
}

$jazz2Names = @($shaders | Where-Object { $_.BaseName -notlike 'Default*' } | ForEach-Object { Get-ProgramNames $_ })
$ncineNames = @($shaders | Where-Object { $_.BaseName -like 'Default*' } | ForEach-Object { Get-ProgramNames $_ })

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// Generated by GenerateAll.ps1. Do not edit manually.')
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('')
foreach ($name in ($shaders | ForEach-Object { $_.BaseName } | Sort-Object)) {
    [void]$sb.AppendLine("#include `"$name.h`"")
}
[void]$sb.AppendLine('')
# The generated shader data namespaces carry no public API and are excluded from the API
# documentation (Doxygen defines `DOXYGEN_GENERATING_OUTPUT`), keeping this header out of it.
[void]$sb.AppendLine('#ifndef DOXYGEN_GENERATING_OUTPUT')
[void]$sb.AppendLine('namespace Jazz2::ShadersGen')
[void]$sb.AppendLine('{')
[void]$sb.AppendLine("`t// All generated Jazz2 shader programs, sorted by name")
[void]$sb.AppendLine("`tinline constexpr const ShaderCompiler::Program* AllPrograms[] = {")
foreach ($name in $jazz2Names) {
    [void]$sb.AppendLine("`t`t&$name,")
}
[void]$sb.AppendLine("`t};")
[void]$sb.AppendLine('}')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('namespace nCine::ShadersGen')
[void]$sb.AppendLine('{')
[void]$sb.AppendLine("`t// All generated nCine default shader programs, sorted by name")
[void]$sb.AppendLine("`tinline constexpr const ShaderCompiler::Program* AllPrograms[] = {")
foreach ($name in $ncineNames) {
    [void]$sb.AppendLine("`t`t&$name,")
}
[void]$sb.AppendLine("`t};")
[void]$sb.AppendLine('}')
[void]$sb.AppendLine('#endif')

$umbrellaPath = Join-Path $outDir 'ShadersGen.h'
[System.IO.File]::WriteAllText($umbrellaPath, $sb.ToString().Replace("`r`n", "`n").Replace("`n", "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "ok: umbrella -> Generated\ShadersGen.h ($($jazz2Names.Count) Jazz2 + $($ncineNames.Count) nCine programs)"

# Emit the aggregate SwGeneratedShaders.h consumed by the CPU software renderer: the GLSL-to-C++
# transpiler lowers each program variant's fragment stage into a C++ fragment function the software
# device runs for shaders that have no hand-written effect. Shaders outside the supported subset are
# declined and simply omitted (they keep the previous skipped behaviour), so this step never fails.
$swGenPath = Join-Path $outDir 'SwGeneratedShaders.h'
& $tool --emit-sw-generated $swGenPath @($shaders | ForEach-Object { $_.FullName })
if ($LASTEXITCODE -ne 0) {
    Write-Host "error: failed to emit SwGeneratedShaders.h"
    exit 1
}
Write-Host "ok: software fragments -> Generated\SwGeneratedShaders.h"

if ($Check) {
    # Compare the freshly generated headers against the committed ones; any difference means the committed
    # artifacts are stale (or were edited by hand). Missing/extra files count as stale too.
    if (-not $glslangPath) {
        Write-Host "warning: -Check without glslang - committed headers with embedded SPIR-V will be reported stale"
    }
    $stale = @()
    $freshFiles = @(Get-ChildItem (Join-Path $outDir '*.h') | Sort-Object Name)
    foreach ($fresh in $freshFiles) {
        $committed = Join-Path $committedDir $fresh.Name
        if (-not (Test-Path $committed)) {
            $stale += "$($fresh.Name) (missing from Generated)"
        } else {
            $a = [System.IO.File]::ReadAllBytes($fresh.FullName)
            $b = [System.IO.File]::ReadAllBytes($committed)
            if ($a.Length -ne $b.Length -or -not [System.Linq.Enumerable]::SequenceEqual($a, $b)) {
                $stale += $fresh.Name
            }
        }
    }
    $freshNames = @($freshFiles | ForEach-Object { $_.Name })
    foreach ($committed in @(Get-ChildItem (Join-Path $committedDir '*.h'))) {
        if ($freshNames -notcontains $committed.Name) {
            $stale += "$($committed.Name) (committed but no longer generated)"
        }
    }
    Remove-Item -Recurse -Force $outDir
    if ($stale.Count -gt 0) {
        Write-Host "error: $($stale.Count) generated header(s) are STALE - re-run GenerateAll.ps1 and commit:"
        foreach ($s in $stale) { Write-Host "  $s" }
        exit 1
    }
    Write-Host "All $($shaders.Count) shaders verified up to date."
    exit 0
}

Write-Host "All $($shaders.Count) shaders generated successfully."
exit 0
