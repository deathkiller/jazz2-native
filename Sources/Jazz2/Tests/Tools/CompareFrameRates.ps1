<#
.SYNOPSIS
	Checks that the same build produces the same motion at every frame rate.

.DESCRIPTION
	The movement model is written in per-tick units and scaled onto whatever frame the engine is running
	(see `LegacyFrameRateScale`), so a trajectory is supposed to depend on the *elapsed time* and not on how
	finely that time was sampled. This compares several captures of one build taken at different
	`/max-fps:N` and says whether that holds.

	Nothing needs converting to compare them, which is worth stating because it is not obvious:

	-   `x` and `y` are absolute pixels.
	-   `tick` is already in the original game's tick units on every capture - the probe advances it by
	    `timeMult * (70.021 / 60)`, so one frame is 2.92 ticks at 24 FPS and 0.49 at 144, and a scenario
	    covers the same tick range either way.
	-   `xs`/`ys` are pixels per *60 Hz-equivalent* frame by the engine's own convention, so they are
	    already frame-rate normalised. This is the same reason `CompareTraces.ps1` multiplies by a fixed 60
	    whatever rate the capture was taken at.

	So the comparison is a plain position difference on a shared tick grid. The grid is taken from the
	**coarsest** capture, because that is the one whose rows exist in all of them; the others are linearly
	interpolated onto it. A scenario is only compared over the tick range every capture reached.

	What it cannot tell you: whether all four are equally *right*. They can agree with each other and all
	disagree with the original, which is a model error rather than a sampling one - run `CompareTraces.ps1`
	against the original at each rate for that. The two questions are separate and both are worth asking.

.PARAMETER Traces
	Two or more captures of the same build, each a file or a directory of category files.

.PARAMETER Labels
	Names for them, in the same order. Defaults to the leaf name of each path.

.PARAMETER Tolerance
	Pixels of median per-tick difference above which a scenario is reported. Default 1.

.EXAMPLE
	./CompareFrameRates.ps1 -Traces ../Results/engine,fps60,fps30,fps24 -Labels 144,60,30,24
#>

param(
	[Parameter(Mandatory = $true)][string[]] $Traces,
	[string[]] $Labels,
	[double] $Tolerance = 1.0,
	[int] $Top = 20
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\TraceIO.ps1"

if (-not $Labels -or $Labels.Count -ne $Traces.Count) {
	$Labels = @($Traces | ForEach-Object { Split-Path -Leaf ($_ -replace '\.csv(\.gz)?$', '') })
}

function Read-Rows([string] $path) {
	$files = @()
	if (Test-Path $path -PathType Container) {
		$files = @(Get-ChildItem -Path $path -Filter '*.csv.gz' -File) + @(Get-ChildItem -Path $path -Filter '*.csv' -File)
		$files = @($files | ForEach-Object { $_.FullName } | Sort-Object)
	} else {
		$files = @($path)
	}

	$byScenario = @{}
	foreach ($file in $files) {
		$lines = Read-TraceLines $file
		if ($lines.Count -lt 2) { continue }
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
			})
		}
	}
	return $byScenario
}

# Position at a tick, linearly interpolated between the two rows that straddle it. The rows are already in
# tick order, and a capture's own rows are the only thing its own grid is built from, so the search is a
# plain forward scan carried between calls by the caller's cursor.
function Get-At($rows, [double] $tick, [ref] $cursor) {
	$i = $cursor.Value
	while ($i + 1 -lt $rows.Count -and $rows[$i + 1].Tick -le $tick) { $i++ }
	$cursor.Value = $i
	if ($i + 1 -ge $rows.Count) { return $rows[$rows.Count - 1] }
	$a = $rows[$i]; $b = $rows[$i + 1]
	$span = $b.Tick - $a.Tick
	if ($span -le 0) { return $a }
	$t = ($tick - $a.Tick) / $span
	return [pscustomobject]@{ X = $a.X + ($b.X - $a.X) * $t; Y = $a.Y + ($b.Y - $a.Y) * $t }
}

Write-Output ""
Write-Output "Reading $($Traces.Count) captures..."
$sets = @()
for ($i = 0; $i -lt $Traces.Count; $i++) {
	$rows = Read-Rows $Traces[$i]
	$sets += [pscustomobject]@{ Label = $Labels[$i]; Rows = $rows }
	Write-Output ("  {0,-6} {1,4} scenarios from {2}" -f $Labels[$i], $rows.Count, $Traces[$i])
}

# Only scenarios every capture holds can be compared
$common = @($sets[0].Rows.Keys)
foreach ($s in $sets[1..($sets.Count - 1)]) { $common = @($common | Where-Object { $s.Rows.ContainsKey($_) }) }
$common = @($common | Sort-Object)

$report = [System.Collections.Generic.List[object]]::new()
foreach ($name in $common) {
	# The reference is the capture with the FEWEST rows for this scenario - the coarsest sampling, whose
	# ticks the others can all be interpolated onto without inventing detail
	$ref = $sets[0]
	foreach ($s in $sets) { if ($s.Rows[$name].Count -lt $ref.Rows[$name].Count) { $ref = $s } }
	$refRows = $ref.Rows[$name]

	# ...and only over the span they all reached, since a scenario can end early in one capture
	$lastTick = ($sets | ForEach-Object { $_.Rows[$name][$_.Rows[$name].Count - 1].Tick } | Measure-Object -Minimum).Minimum

	$row = [ordered]@{ Scenario = $name; RefRate = $ref.Label }
	$worstMedian = 0.0
	$worstMax = 0.0
	foreach ($s in $sets) {
		if ($s.Label -eq $ref.Label) { continue }
		$cursor = 0
		$refCursor = 0
		$diffs = [System.Collections.Generic.List[double]]::new()
		$maxDiff = 0.0
		foreach ($r in $refRows) {
			if ($r.Tick -gt $lastTick) { break }
			$a = Get-At $refRows $r.Tick ([ref]$refCursor)
			$b = Get-At $s.Rows[$name] $r.Tick ([ref]$cursor)
			$d = [Math]::Sqrt((($a.X - $b.X) * ($a.X - $b.X)) + (($a.Y - $b.Y) * ($a.Y - $b.Y)))
			$diffs.Add($d)
			if ($d -gt $maxDiff) { $maxDiff = $d }
		}
		$median = if ($diffs.Count -gt 0) { ($diffs | Sort-Object)[[int]($diffs.Count / 2)] } else { 0 }
		$row["d$($s.Label)"] = [Math]::Round($median, 2)
		if ($median -gt $worstMedian) { $worstMedian = $median }
		if ($maxDiff -gt $worstMax) { $worstMax = $maxDiff }
	}
	$row['WorstMedian'] = [Math]::Round($worstMedian, 2)
	$row['WorstMax'] = [Math]::Round($worstMax, 1)

	# Travel and rise per capture, which is what the reference page quotes and what a reader will compare
	$travels = @()
	$rises = @()
	foreach ($s in $sets) {
		$rs = $s.Rows[$name]
		$travels += [Math]::Round((($rs | Measure-Object -Property X -Maximum).Maximum - ($rs | Measure-Object -Property X -Minimum).Minimum), 1)
		$rises += [Math]::Round(($rs[0].Y - ($rs | Measure-Object -Property Y -Minimum).Minimum), 1)
	}
	$row['TravelSpread'] = [Math]::Round((($travels | Measure-Object -Maximum).Maximum - ($travels | Measure-Object -Minimum).Minimum), 1)
	$row['RiseSpread'] = [Math]::Round((($rises | Measure-Object -Maximum).Maximum - ($rises | Measure-Object -Minimum).Minimum), 1)

	$report.Add([pscustomobject] $row)
}

# Every count below is wrapped in @() before .Count: in Windows PowerShell a pipeline that yields nothing
# returns $null rather than an empty array, and `$null.Count` prints as a blank field instead of 0 - so the
# one line you most want to read, "nothing exceeded the tolerance", is the one that silently disappears.
$within = @($report | Where-Object { $_.WorstMedian -le $Tolerance }).Count
$medians = ($report | ForEach-Object { $_.WorstMedian } | Sort-Object)
$overall = if ($report.Count -gt 0) { $medians[[int]($report.Count / 2)] } else { 0 }

Write-Output ""
Write-Output "Compared $($report.Count) scenarios present in all $($sets.Count) captures."
Write-Output "Median across scenarios of the worst per-rate position difference: $overall px"
Write-Output "Scenarios agreeing within $Tolerance px: $within / $($report.Count)"
Write-Output "Scenarios whose travel spread exceeds 10 px: $(@($report | Where-Object { $_.TravelSpread -gt 10 }).Count) / $($report.Count)"
Write-Output "Scenarios whose rise spread exceeds 10 px: $(@($report | Where-Object { $_.RiseSpread -gt 10 }).Count) / $($report.Count)"

Write-Output ""
Write-Output "Worst $Top by cross-rate position difference:"
$report | Sort-Object -Property WorstMedian -Descending | Select-Object -First $Top | Format-Table -AutoSize
