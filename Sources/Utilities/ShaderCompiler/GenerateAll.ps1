# Thin wrapper around "ShaderCompiler --generate-all", which regenerates every committed artifact in
# Sources/Shaders/Generated: the shared reflection types, one header per Sources/Shaders/*.shader, the
# umbrella ShadersGen.h and the five aggregates (software renderer, Cg for the Vita, and the three
# console fixed-function tables).
#
# The whole flow lives in the tool itself (Main.cpp, --generate-all) so a regeneration is all-or-nothing
# and its file order comes from one byte-wise sort inside the tool instead of the shell's locale-
# sensitive one. This script only locates the executable and forwards its arguments, so it stays usable
# from muscle memory and CI; running the tool directly does exactly the same thing:
#
#   ShaderCompiler --generate-all [--shaders-dir <dir>] [--out-dir <dir>] [--check] [--no-dxbc] [--glslang <path>]
#
# -Glslang <path>  glslangValidator for the offline SPIR-V compilation. Discovered automatically
#                  (VULKAN_SDK, PATH, a Visual Studio-bundled copy, a repo-local build-tree copy) when
#                  not given; without one the SPIR-V fields are emitted as nullptr/0 and a warning is
#                  printed (the headers still build; the Vulkan backend is then not buildable).
# -NoDxbc          Embed the HLSL sources instead of the precompiled DXBC bytecode.
# -Check           Staleness guard: generates into a temporary directory, byte-compares against the
#                  committed headers and exits non-zero (listing them) if any differ, without modifying
#                  the tree. Run it after editing a .shader, or in CI - the build itself never detects
#                  stale committed headers.

param([string]$Glslang = '', [switch]$Check, [switch]$NoDxbc)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcesDir = Split-Path -Parent (Split-Path -Parent $scriptDir)		# .../Sources (the tool lives in Sources/Utilities/ShaderCompiler)
$shadersDir = Join-Path $sourcesDir 'Shaders'
$tool = Join-Path $scriptDir 'x64\Release\ShaderCompiler.exe'

if (-not (Test-Path $tool)) {
    Write-Host "error: ShaderCompiler.exe not found at '$tool' - build Sources/Utilities/ShaderCompiler first"
    exit 1
}
if (-not (Test-Path $shadersDir)) {
    Write-Host "error: Shader directory not found at '$shadersDir'"
    exit 1
}

# Passed explicitly rather than relying on the tool's auto-detection, so the script works from any
# working directory and always targets the shaders next to itself
$toolArgs = @('--generate-all', '--shaders-dir', $shadersDir)
if ($Check) { $toolArgs += '--check' }
if ($NoDxbc) { $toolArgs += '--no-dxbc' }
if ($Glslang) { $toolArgs += @('--glslang', $Glslang) }

& $tool @toolArgs
exit $LASTEXITCODE
