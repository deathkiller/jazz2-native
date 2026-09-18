<#
.SYNOPSIS
	Compares the camera columns of an engine trace against an original-game trace, scenario by scenario.

.DESCRIPTION
	`CompareTraces.ps1` compares the sorted speed distribution, which is the right metric for movement and
	says nothing at all about the camera - a camera that leads three times too far moves the player exactly
	as fast. This prints `camx`/`camy` side by side at chosen ticks instead, which is how the camera model
	in `Docs/MovementAccuracyReference.dox` was read.

	**The two sides do not log the same convention.** The original's camx/camy are the view's TOP-LEFT
	corner - read off `g_stand`, where a resting player gives exactly -400.000,-225.000 on its 800x450
	subscreen - while this engine logs the CENTRE (`LevelHandler::GetCameraPos`). Half the subscreen is
	added to the original's figures here so the two are the same quantity: how far the camera leads the
	player. Forget it and every original row looks like a camera lagging by a third of a screen.

	Both columns are logged on every scenario, so the whole matrix is camera coverage: the horizontal pan at
	both speeds is in `g_walk`/`g_dash`, what a released direction does is in `g_dash_rel` against
	`g_dash_relrun`, the vertical behaviour through a jump is in `a_jump`, and a move that carries speed the
	player never asked for is in `sp_spaz_side_rel`.

.PARAMETER Engine
	Engine-side CSV, from ExtractTrace.ps1.

.PARAMETER Original
	Original-side CSV, from ExtractTrace.ps1.

.PARAMETER Scenarios
	Scenario names to print. They are joined by name, never by index - the two probes agree on names only.

.PARAMETER Ticks
	Which ticks to sample. Both probes count in the original's 70.021 Hz ticks, so the same number is the
	same real time on both sides.

.EXAMPLE
	./CompareCam.ps1 -Engine ../Results/engine-60fps.csv.gz -Original ../Results/original-70hz.csv.gz `
		-Scenarios g_walk,g_dash,a_jump,sp_spaz_side_rel
#>
param(
	[Parameter(Mandatory = $true)][string] $Engine,
	[Parameter(Mandatory = $true)][string] $Original,
	[Parameter(Mandatory = $true)][string[]] $Scenarios,
	[int[]] $Ticks = @(0, 10, 20, 30, 45, 60, 90, 120, 160, 200)
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\TraceIO.ps1"

# Half of the original's subscreen, i.e. its corner-to-centre shift
$HalfW = 400.0
$HalfH = 225.0

function Read-CameraRows([string] $path, [string] $scenario) {
	$rows = @{}
	$col = $null
	foreach ($line in Read-TraceLines $path) {
		# Columns are resolved by NAME from the header, never by position. The two sides disagree about where
		# `objx` sits, and counting from the end broke the moment `anim`/`frame` were appended after the camera
		# - a fixed index works on one side, or until the next column is added, and then silently reads the
		# wrong number rather than failing.
		if ($col -eq $null) {
			if (-not $line.StartsWith('scenario,')) { continue }
			$names = $line -split ','
			$col = @{}
			for ($i = 0; $i -lt $names.Length; $i++) { $col[$names[$i]] = $i }
			continue
		}
		if (-not $line.StartsWith("$scenario,")) { continue }
		$f = $line -split ','
		$rows[[int]$f[$col['tick']]] = [pscustomobject]@{
			Xs   = [double]$f[$col['xs']]
			CamX = [double]$f[$col['camx']]
			CamY = [double]$f[$col['camy']]
		}
	}
	return $rows
}

foreach ($s in $Scenarios) {
	$e = Read-CameraRows $Engine $s
	$o = Read-CameraRows $Original $s
	if ($e.Count -eq 0 -or $o.Count -eq 0) {
		Write-Output "=== $s  (engine $($e.Count) rows, original $($o.Count) rows) - SKIPPED"
		continue
	}
	Write-Output "=== $s"
	Write-Output "  tick |     engine   xs     camx    camy |   original   xs     camx    camy"
	foreach ($t in $Ticks) {
		if (-not $e.ContainsKey($t) -or -not $o.ContainsKey($t)) { continue }
		$er = $e[$t]
		$or = $o[$t]
		Write-Output ("  {0,4} | {1,12:N4} {2,8:N2} {3,7:N2} | {4,10:N4} {5,8:N2} {6,7:N2}" -f `
			$t, $er.Xs, $er.CamX, $er.CamY, $or.Xs, ($or.CamX + $HalfW), ($or.CamY + $HalfH))
	}
}
