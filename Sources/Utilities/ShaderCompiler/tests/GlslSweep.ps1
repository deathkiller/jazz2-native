# Compiles EVERY emitted GL / ESSL 100 stage source of every generated shader variant with
# glslangValidator, in the three profiles the engine injects at runtime (#version 330, #version
# 300 es, #version 100).
#
# This exists because of one asymmetry that is very easy to miss: GLSL ES REJECTS an undefined
# macro in an "#if" expression ("undefined macro in expression not allowed in es profile"), while
# desktop GLSL substitutes 0 for it like C does. HLSL accepts it too, and the Vulkan transform is
# #version 450 DESKTOP GLSL - so a directive written as "#if SOME_VARIANT" passes --hlsl-check and
# --spirv-check while breaking every ES2/ES3/WebGL/Emscripten target. It also catches an "#if" on a
# macro whose body is EMPTY ("#define SLOPE"), which is a "bad expression" even on desktop.
#
# The rule both failures come down to: an "#if" expression that SURVIVES into an emitted source may
# only name macros that are always defined and have a value. Everything else belongs in
# "#ifdef"/"#ifndef", or in "defined(X)" when it has to share a directive with another condition.
#
# Usage: GlslSweep.ps1 [-Glslang <path>] [-GeneratedDir <dir>]
# Exit code 0 = every stage compiled.
param(
    [string] $GeneratedDir,
    [string] $Glslang
)

$ErrorActionPreference = 'Continue'

$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $GeneratedDir) {
    $GeneratedDir = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $testsDir))) 'Shaders\Generated'
}
if (-not (Test-Path $GeneratedDir)) {
    Write-Host "error: generated header directory '$GeneratedDir' not found"
    exit 1
}

if (-not $Glslang) {
    $candidates = @()
    if ($env:VULKAN_SDK) { $candidates += (Join-Path $env:VULKAN_SDK 'Bin\glslangValidator.exe') }
    $onPath = (Get-Command glslangValidator -ErrorAction SilentlyContinue)
    if ($onPath) { $candidates += $onPath.Source }
    # Visual Studio ships one with its CMake/shader tooling extensions
    $vsRoot = 'C:\Program Files\Microsoft Visual Studio'
    if (Test-Path $vsRoot) {
        $candidates += (Get-ChildItem $vsRoot -Recurse -Filter 'glslangValidator.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName)
    }
    $Glslang = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $Glslang) {
    Write-Host 'error: glslangValidator not found - pass -Glslang <path> or set VULKAN_SDK'
    exit 1
}
Write-Host "[GlslSweep] using glslang: $Glslang"

$work = Join-Path $env:TEMP 'jazz2-glslsweep'
if (Test-Path $work) { Get-ChildItem $work -File | Remove-Item -Force -Confirm:$false }
else { New-Item -ItemType Directory -Force $work | Out-Null }

$pass = 0
$fail = 0
$failures = @()

# The per-shader headers carry the GL sources as raw string literals; the aggregates hold bytecode
# or another language, so they are skipped
foreach ($header in Get-ChildItem $GeneratedDir -Filter '*.h' | Where-Object { $_.Name -notmatch 'Generated(Shaders|Effects)\.h$' }) {
    $text = [System.IO.File]::ReadAllText($header.FullName)
    foreach ($m in [regex]::Matches($text, '(?s)char\s+(\w+?)(Vs|Fs)(100)?\[\]\s*=\s*R"__SHDR__\((.*?)\)__SHDR__";')) {
        $symbol = $m.Groups[1].Value + $m.Groups[2].Value + $m.Groups[3].Value
        $stage = if ($m.Groups[2].Value -eq 'Vs') { 'vert' } else { 'frag' }
        $src = $m.Groups[4].Value
        # The "100" sources are the ES2 rewrite (ESSL 100); the others serve desktop GL and GL ES 3.0
        $versions = if ($m.Groups[3].Value -eq '100') { @('#version 100') } else { @('#version 330', '#version 300 es') }
        foreach ($ver in $versions) {
            $path = Join-Path $work ("{0}.{1}.{2}" -f $symbol, ($ver -replace '[^0-9a-z]', ''), $stage)
            [System.IO.File]::WriteAllText($path, ($ver + "`n" + $src))
            $out = (& $Glslang $path 2>&1 | Out-String)
            if ($LASTEXITCODE -eq 0) {
                $pass++
            } else {
                $fail++
                $failures += "  $symbol [$ver] " +
                    (($out -split "`r?`n" | Where-Object { $_ -match 'ERROR' } | Select-Object -First 2) -join ' | ')
            }
        }
    }
}

Write-Host ''
if ($fail -eq 0) {
    Write-Host "[GlslSweep] all $pass stage compilations passed"
    exit 0
}
Write-Host "[GlslSweep] $fail FAILED, $pass passed"
$failures | ForEach-Object { Write-Host $_ }
exit 1
