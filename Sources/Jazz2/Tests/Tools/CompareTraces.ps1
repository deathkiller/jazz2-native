<#
.SYNOPSIS
	Compares an engine trace against an original-game trace, scenario by scenario.

.DESCRIPTION
	Comparing the two runs instant by instant is useless: the games tick at different rates (70.021 Hz
	against whatever the engine's frame rate is), so a few ticks of offset makes every row disagree even
	when the movement is identical. Comparing only the endpoints is worse - it hides a wrong curve, which
	is exactly how a kick that was three times too fast once passed a distance check.

	So the default check is the **sorted distribution of speeds** each run visits, in px/s so the two tick
	rates are comparable. That catches wrong magnitudes and wrong shapes while ignoring timing offsets. A
	scenario whose speeds are drawn from the same set in the same proportions moved the same way, whenever
	each sample happened to be taken.

	Also reported per scenario, because they are what a human reads first: distance travelled, peak height
	and final resting position, all in pixels and all directly comparable.

	Scenarios missing from one side are listed rather than skipped silently - the ledge climb ones (`lc_*`)
	have no counterpart by design, because the original has no ledge climb.

.PARAMETER Engine
	CSV from this engine, as written by ExtractTrace.ps1.

.PARAMETER Original
	CSV from the original game, as written by ExtractTrace.ps1. With -Baseline, another engine CSV instead.

.PARAMETER Baseline
	Treat -Original as a second *engine* trace rather than an original-game one. This answers a different
	question - "did anything change at all?" - so it converts both sides at 60 Hz instead of converting one
	at the original's 70.021, and it adds a per-tick position comparison, which only means anything when
	both sides tick at the same rate.

.PARAMETER Top
	How many worst-matching scenarios to list in full (default 12).

.EXAMPLE
	./CompareTraces.ps1 -Engine ../Results/engine-60fps.csv -Original ../Results/original-70hz.csv

.EXAMPLE
	./CompareTraces.ps1 -Engine fresh.csv -Original ../Results/engine-60fps.csv.gz -Baseline
#>
param(
	[Parameter(Mandatory = $true)][string] $Engine,
	[Parameter(Mandatory = $true)][string] $Original,
	[switch] $Baseline,
	[int] $Top = 12
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\TraceIO.ps1"

# Speeds are compared in px/s, and the two sides do NOT use the same unit to get there. The original's
# xSpeed/ySpeed are pixels per one of its ticks, and it ticks at a measured 70.021 Hz. The engine's
# `_speed` is pixels per 60 Hz-equivalent frame by its own convention - position advances by
# `speed * timeMult`, and `timeMult` is 1.0 at 60 FPS - so its speeds convert with 60 whatever the frame
# rate actually was. Using one factor for both is worth a flat ~187 px/s of phantom error on every
# scenario that reaches the dash cap, which is most of them.
$engineFrameRate = 60.0
$originalTickRate = if ($Baseline) { $engineFrameRate } else { 70.021 }

function Read-Trace([string] $path) {
	$lines = Read-TraceLines $path
	if ($lines.Count -lt 2) { throw "'$path' has no rows" }

	$byScenario = @{}
	foreach ($line in $lines[1..($lines.Count - 1)]) {
		$f = $line -split ','
		if ($f.Count -lt 6) { continue }
		$name = $f[0]
		if (-not $byScenario.ContainsKey($name)) {
			$byScenario[$name] = [System.Collections.Generic.List[object]]::new()
		}
		$byScenario[$name].Add([pscustomobject]@{
			Tick = [double] $f[1]
			X    = [double] $f[2]
			Y    = [double] $f[3]
			Xs   = [double] $f[4]
			Ys   = [double] $f[5]
		})
	}
	return $byScenario
}

$engineTrace = Read-Trace $Engine
$originalTrace = Read-Trace $Original

$report = [System.Collections.Generic.List[object]]::new()
$onlyEngine = [System.Collections.Generic.List[string]]::new()
$onlyOriginal = [System.Collections.Generic.List[string]]::new()

foreach ($name in ($engineTrace.Keys | Sort-Object)) {
	if (-not $originalTrace.ContainsKey($name)) { $onlyEngine.Add($name); continue }

	$e = $engineTrace[$name]
	$o = $originalTrace[$name]

	# Phase-insensitive: sort each run's speeds and compare the two distributions at matching quantiles,
	# so a scenario that reached the same speeds in the same proportions scores zero however offset it was
	$eSpeeds = ($e | ForEach-Object { [Math]::Sqrt($_.Xs * $_.Xs + $_.Ys * $_.Ys) * $engineFrameRate } | Sort-Object)
	$oSpeeds = ($o | ForEach-Object { [Math]::Sqrt($_.Xs * $_.Xs + $_.Ys * $_.Ys) * $originalTickRate } | Sort-Object)

	$samples = 64
	$diffs = @(0..($samples - 1) | ForEach-Object {
		$q = $_ / [double]($samples - 1)
		$ev = $eSpeeds[[int][Math]::Round($q * ($eSpeeds.Count - 1))]
		$ov = $oSpeeds[[int][Math]::Round($q * ($oSpeeds.Count - 1))]
		[Math]::Abs($ev - $ov)
	})
	$median = ($diffs | Sort-Object)[[int]($samples / 2)]

	$eTravel = ($e | Measure-Object -Property X -Maximum).Maximum - ($e | Measure-Object -Property X -Minimum).Minimum
	$oTravel = ($o | Measure-Object -Property X -Maximum).Maximum - ($o | Measure-Object -Property X -Minimum).Minimum
	$eRise = $e[0].Y - ($e | Measure-Object -Property Y -Minimum).Minimum
	$oRise = $o[0].Y - ($o | Measure-Object -Property Y -Minimum).Minimum

	$row = [ordered]@{
		Scenario     = $name
		SpeedError   = [Math]::Round($median, 1)
		TravelEngine = [Math]::Round($eTravel, 1)
		TravelOrig   = [Math]::Round($oTravel, 1)
		TravelDiff   = [Math]::Round($eTravel - $oTravel, 1)
		RiseEngine   = [Math]::Round($eRise, 1)
		RiseOrig     = [Math]::Round($oRise, 1)
		RiseDiff     = [Math]::Round($eRise - $oRise, 1)
	}

	# Two engine traces tick in step, so the positions can be compared where it actually matters - row by
	# row. Sorted distributions cannot tell a scenario that moved differently from one that moved the same
	# way a few ticks later, and between two builds that distinction is the whole question.
	if ($Baseline) {
		$oByTick = @{}
		foreach ($s in $o) { $oByTick[$s.Tick] = $s }
		$dx = [System.Collections.Generic.List[double]]::new()
		$dy = [System.Collections.Generic.List[double]]::new()
		foreach ($s in $e) {
			$m = $oByTick[$s.Tick]
			if ($null -eq $m) { continue }
			$dx.Add([Math]::Abs($s.X - $m.X))
			$dy.Add([Math]::Abs($s.Y - $m.Y))
		}
		if ($dx.Count -gt 0) {
			$sx = ($dx | Sort-Object); $sy = ($dy | Sort-Object)
			$row.MedianDX = [Math]::Round($sx[[int]($sx.Count / 2)], 3)
			$row.MedianDY = [Math]::Round($sy[[int]($sy.Count / 2)], 3)
			$row.MaxDX = [Math]::Round(($dx | Measure-Object -Maximum).Maximum, 1)
			$row.MaxDY = [Math]::Round(($dy | Measure-Object -Maximum).Maximum, 1)
			$row.TicksMatched = $dx.Count
		}
	}

	$report.Add([pscustomobject] $row)
}

foreach ($name in ($originalTrace.Keys | Sort-Object)) {
	if (-not $engineTrace.ContainsKey($name)) { $onlyOriginal.Add($name) }
}

$compared = $report.Count
$speedErrors = ($report | ForEach-Object { $_.SpeedError } | Sort-Object)
$medianOfMedians = if ($compared -gt 0) { $speedErrors[[int]($compared / 2)] } else { 0 }

Write-Output ""
Write-Output "Compared $compared scenarios present in both traces."
Write-Output "Median speed-distribution error: $medianOfMedians px/s"
Write-Output "Scenarios within 25 px/s: $(($report | Where-Object { $_.SpeedError -le 25 }).Count) / $compared"

if ($Baseline) {
	$posRows = @($report | Where-Object { $null -ne $_.MedianDX })
	if ($posRows.Count -gt 0) {
		$mdx = ($posRows | ForEach-Object { $_.MedianDX } | Sort-Object)[[int]($posRows.Count / 2)]
		$mdy = ($posRows | ForEach-Object { $_.MedianDY } | Sort-Object)[[int]($posRows.Count / 2)]
		Write-Output "Median per-tick position difference: $mdx px in X, $mdy px in Y"
		Write-Output "Scenarios whose median position moved by over 1 px: $(($posRows | Where-Object { $_.MedianDX -gt 1.0 -or $_.MedianDY -gt 1.0 }).Count) / $($posRows.Count)"
	}
}

Write-Output ""
if ($Baseline) {
	Write-Output "Worst $Top by median per-tick position difference:"
	$report | Sort-Object -Property @{ Expression = { [Math]::Max($_.MedianDX, $_.MedianDY) } } -Descending |
		Select-Object -First $Top -Property Scenario, MedianDX, MedianDY, MaxDX, MaxDY, TicksMatched, SpeedError | Format-Table -AutoSize
} else {
	Write-Output "Worst $Top by speed-distribution error:"
	$report | Sort-Object -Property SpeedError -Descending | Select-Object -First $Top | Format-Table -AutoSize
}

if ($onlyEngine.Count -gt 0) {
	Write-Output "Only in the engine trace (the lc_* ones have no counterpart - the original has no ledge climb):"
	Write-Output ("  " + ($onlyEngine -join ' '))
}
if ($onlyOriginal.Count -gt 0) {
	Write-Output "Only in the original trace:"
	Write-Output ("  " + ($onlyOriginal -join ' '))
}
