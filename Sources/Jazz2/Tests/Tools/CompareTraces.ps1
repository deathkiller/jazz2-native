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
	if (Test-Path $path -PathType Container) {
		# A split capture: one file per category, which is how the committed traces are stored so that adding
		# a scenario re-runs ten minutes rather than eighty. They are read back as one set, because a
		# comparison is over the whole suite regardless of how it was captured - and joined on the scenario
		# NAME, so it does not matter which file a scenario came out of or whether the categories have since
		# been redrawn. See Categories.ps1.
		$files = @(Get-ChildItem -Path $path -File | Where-Object { $_.Name -match '\.csv(\.gz)?$' } | Sort-Object Name)
		if ($files.Count -eq 0) { throw "'$path' is a directory but holds no .csv or .csv.gz files" }
		$lines = [System.Collections.Generic.List[string]]::new()
		foreach ($file in $files) {
			$part = Read-TraceLines $file.FullName
			if ($part.Count -lt 2) { continue }
			# One header for the set, taken from the first file that has one. They are written by the same
			# extractor in the same run, so a mismatch means two captures have been mixed in one directory.
			if ($lines.Count -eq 0) {
				$lines.Add($part[0])
			} elseif ($part[0] -ne $lines[0]) {
				throw "'$($file.Name)' has a different column header than the rest of '$path' - two different captures have been mixed in one directory"
			}
			$lines.AddRange([string[]]$part[1..($part.Count - 1)])
		}
		if ($lines.Count -lt 2) { throw "'$path' holds no rows" }
	} else {
		$lines = Read-TraceLines $path
	}
	if ($lines.Count -lt 2) { throw "'$path' has no rows" }

	# `anim` is located by NAME, not by index. The two sides do not agree on column order - the original
	# carries `gametick` that this engine has no equivalent of, and this engine carries five flags the
	# original does not - so anything that reads a fixed position reads a different column on each side.
	# Captures made before the animation columns existed have no `anim` at all, and get -1 throughout.
	$header = ($lines[0] -split ',')
	$animCol = [Array]::IndexOf($header, 'anim')
	# What is actually *drawn* is the transition when one is playing and the base animation otherwise. Only
	# this engine separates the two; the original logs a single `curAnim` that already is whichever is on
	# screen. Comparing our base state against that undercounts every pose the stop chain and the special
	# moves draw over it - `g_dash_rel` reads 4 poses against the original's 7 that way, and 7 against 7
	# with the transition folded in.
	$tranCol = [Array]::IndexOf($header, 'tranim')

	$byScenario = @{}
	foreach ($line in $lines[1..($lines.Count - 1)]) {
		$f = $line -split ','
		if ($f.Count -lt 6) { continue }
		$name = $f[0]
		if (-not $byScenario.ContainsKey($name)) {
			$byScenario[$name] = [System.Collections.Generic.List[object]]::new()
		}
		$anim = -1
		if ($animCol -ge 0 -and $animCol -lt $f.Count -and $f[$animCol] -ne '') { $anim = [double] $f[$animCol] }
		if ($tranCol -ge 0 -and $tranCol -lt $f.Count -and $f[$tranCol] -ne '' -and $f[$tranCol] -ne '0') {
			$anim = [double] $f[$tranCol]
		}
		$byScenario[$name].Add([pscustomobject]@{
			Tick = [double] $f[1]
			X    = [double] $f[2]
			Y    = [double] $f[3]
			Xs   = [double] $f[4]
			Ys   = [double] $f[5]
			Anim = $anim
		})
	}
	return $byScenario
}

# How many runs of a constant animation a scenario passes through. The two games number their animations
# differently, so the ids themselves are not comparable across a run of each - but "how many poses, held for
# how long" is, and that is what catches a pose this engine shows and the original does not (or holds three
# times as long). Between two *engine* traces the ids are comparable and per-tick equality is used instead.
function Get-AnimSegments($rows) {
	$segments = [System.Collections.Generic.List[object]]::new()
	$prev = $null
	$len = 0
	foreach ($r in $rows) {
		if ($r.Anim -ne $prev) {
			if ($null -ne $prev) { $segments.Add([pscustomobject]@{ Anim = $prev; Ticks = $len }) }
			$prev = $r.Anim
			$len = 1
		} else { $len++ }
	}
	if ($null -ne $prev) { $segments.Add([pscustomobject]@{ Anim = $prev; Ticks = $len }) }
	# Anything shorter than four ticks is dropped across the two games, because their traces do not have the
	# same number of rows - 686 against 800 for the same scenario - so a pose that lasts a tick or two can
	# land in one sampling and fall between rows in the other. Four ticks is where the plain scenarios start
	# agreeing exactly (`g_walk` 1/1, `g_dash` 3/3, `a_jump` 7/7) instead of differing by sampling alone.
	#
	# Dropping one has to **coalesce** what it sat between, or the count is inflated on whichever side had
	# the short segment: a rise/fall cycle that flickers for two ticks at the apex reads as three poses where
	# the other side reads one. The original has more such flickers simply by being sampled 14% finer, so
	# without this the metric reports the sampling rate as a difference - it put the float-up columns at 75
	# poses against 46 when both alternate the same two.
	$kept = [System.Collections.Generic.List[object]]::new()
	foreach ($s in $segments) {
		if ($s.Ticks -lt 4) { continue }
		if ($kept.Count -gt 0 -and $kept[$kept.Count - 1].Anim -eq $s.Anim) {
			$kept[$kept.Count - 1].Ticks += $s.Ticks
			continue
		}
		$kept.Add([pscustomobject]@{ Anim = $s.Anim; Ticks = $s.Ticks })
	}
	return $kept
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

	# The animation, which nothing compared until a rev-up launch was found showing the skid pose for its
	# whole length with every position and speed still matching. A trajectory check cannot see that: the
	# stop chain is drawn *over* the base animation and moves the player not at all.
	$eHasAnim = ($e[0].Anim -ge 0)
	$oHasAnim = ($o[0].Anim -ge 0)
	if ($eHasAnim) {
		$row.AnimSegE = (Get-AnimSegments $e).Count
		$row.AnimSegO = if ($oHasAnim) { (Get-AnimSegments $o).Count } else { $null }
	}

	# Two engine traces tick in step, so the positions can be compared where it actually matters - row by
	# row. Sorted distributions cannot tell a scenario that moved differently from one that moved the same
	# way a few ticks later, and between two builds that distinction is the whole question.
	if ($Baseline) {
		$oByTick = @{}
		foreach ($s in $o) { $oByTick[$s.Tick] = $s }
		$dx = [System.Collections.Generic.List[double]]::new()
		$dy = [System.Collections.Generic.List[double]]::new()
		$animMatched = 0
		$animSame = 0
		foreach ($s in $e) {
			$m = $oByTick[$s.Tick]
			if ($null -eq $m) { continue }
			$dx.Add([Math]::Abs($s.X - $m.X))
			$dy.Add([Math]::Abs($s.Y - $m.Y))
			# Only between two engine traces are the ids themselves comparable, and then a straight per-tick
			# equality is the sharpest regression check there is - it catches a pose that moved by one frame
			if ($eHasAnim -and $oHasAnim) {
				$animMatched++
				if ($s.Anim -eq $m.Anim) { $animSame++ }
			}
		}
		if ($dx.Count -gt 0) {
			$sx = ($dx | Sort-Object); $sy = ($dy | Sort-Object)
			$row.MedianDX = [Math]::Round($sx[[int]($sx.Count / 2)], 3)
			$row.MedianDY = [Math]::Round($sy[[int]($sy.Count / 2)], 3)
			$row.MaxDX = [Math]::Round(($dx | Measure-Object -Maximum).Maximum, 1)
			$row.MaxDY = [Math]::Round(($dy | Measure-Object -Maximum).Maximum, 1)
			$row.TicksMatched = $dx.Count
		}
		if ($animMatched -gt 0) {
			$row.AnimSame = [Math]::Round(100.0 * $animSame / $animMatched, 1)
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
	$animRows = @($report | Where-Object { $null -ne $_.AnimSame })
	if ($animRows.Count -gt 0) {
		$sa = ($animRows | ForEach-Object { $_.AnimSame } | Sort-Object)
		Write-Output "Median per-tick animation agreement: $($sa[[int]($animRows.Count / 2)])%"
		Write-Output "Scenarios whose animation agrees on under 90% of ticks: $(($animRows | Where-Object { $_.AnimSame -lt 90 }).Count) / $($animRows.Count)"
	}
} else {
	# Across the two games the ids are unrelated, so only the *shape* is comparable - how many poses the
	# scenario passes through. A run that shows twice as many has a pose the original does not.
	$segRows = @($report | Where-Object { $null -ne $_.AnimSegO })
	if ($segRows.Count -gt 0) {
		$off = @($segRows | Where-Object { [Math]::Abs($_.AnimSegE - $_.AnimSegO) -gt [Math]::Max(2, 0.25 * $_.AnimSegO) })
		Write-Output "Scenarios whose animation-segment count differs by over 25%: $($off.Count) / $($segRows.Count)"
		if ($off.Count -gt 0) {
			Write-Output "  $((($off | Sort-Object -Property @{ Expression = { [Math]::Abs($_.AnimSegE - $_.AnimSegO) } } -Descending | Select-Object -First 12 | ForEach-Object { "$($_.Scenario) $($_.AnimSegE)/$($_.AnimSegO)" }) -join '  '))"
		}
	} else {
		Write-Output "Animation not compared: one of the traces has no `anim` column (captures made before it was added)."
	}
}

Write-Output ""
if ($Baseline) {
	Write-Output "Worst $Top by median per-tick position difference:"
	$report | Sort-Object -Property @{ Expression = { [Math]::Max($_.MedianDX, $_.MedianDY) } } -Descending |
		Select-Object -First $Top -Property Scenario, MedianDX, MedianDY, MaxDX, MaxDY, TicksMatched, SpeedError, AnimSame | Format-Table -AutoSize

	$animRows = @($report | Where-Object { $null -ne $_.AnimSame })
	if ($animRows.Count -gt 0) {
		Write-Output "Worst $Top by animation agreement:"
		$animRows | Sort-Object -Property AnimSame |
			Select-Object -First $Top -Property Scenario, AnimSame, MedianDX, MedianDY, SpeedError | Format-Table -AutoSize
	}
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
