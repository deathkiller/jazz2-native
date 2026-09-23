<#
.SYNOPSIS
	Pulls a probe trace out of a raw capture and writes it as a plain CSV.

.DESCRIPTION
	The two probes record the same measurements but not in the same container: this engine writes its rows
	into the game log through LOGI, prefixed and interleaved with everything else the game logs, while the
	original game's AngelScript probe writes them into a jjSTREAM save file (_pt_trace.asdat) with binary
	framing around otherwise plain text. This strips either down to a CSV, so a run of one can be lined up
	against a run of the other.

	The two column sets are close but not identical, and the header written out says which one it is:

		original    scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy,camx,camy,anim,frame
		engine      scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr,camx,camy,anim,frame,tranim,susp

	The original carries `gametick` (its own frame counter, which the engine has no equivalent of), and the
	engine carries five extra flags: whether the player was in control, still animating out of the previous
	move, and crouching - which is what tells a move that did not happen apart from one that happened
	differently - plus the two that pick the rise gravity, `jrel` (jump released) and `spr` (launched by a
	spring). Everything before `sm` is common, and that is what comparisons use.

	`camx`/`camy` are the camera's offset from the player, not its absolute position, so the two sides are
	comparable without either game's view size entering into it. They are appended at the end on both sides
	rather than inserted, because the two column sets already disagree about where `objx` sits and anything
	that reads a fixed index would move under it.

.PARAMETER Path
	The capture to read: a game log (`/log:file:` output) or an `.asdat` from the original game.

.PARAMETER Destination
	Where to write the CSV. Ending it in `.gz` gzips it, which is how the committed traces are stored; see
	TraceIO.ps1. With -Split this is a *directory* instead, and one file per category is written into it.

.PARAMETER Split
	Write one file per scenario category rather than a single CSV - see Categories.ps1 for the categories and
	why they exist. Only the categories the capture actually contains are written, so a filtered run replaces
	just those files and leaves the rest of the committed set alone. That is the point: a full sweep is about
	eighty minutes a side, and one category is about ten.

.PARAMETER Gzip
	With -Split, gzip each category file (`<category>.csv.gz`), which is how the committed traces are stored.

.EXAMPLE
	./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine-60fps.csv

.EXAMPLE
	./ExtractTrace.ps1 -Path ../../../../x64/Debug/Source/_pt_trace.asdat -Destination ../Results/original-70hz.csv

.EXAMPLE
	# One file per category, which is what a run of a single category should be extracted with
	./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine -Split -Gzip
#>
param(
	[Parameter(Mandatory = $true)][string] $Path,
	[Parameter(Mandatory = $true)][string] $Destination,
	[switch] $Split,
	[switch] $Gzip
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\TraceIO.ps1"
. "$PSScriptRoot\Categories.ps1"

$rows = [System.Collections.Generic.List[string]]::new()
$resolved = Resolve-TracePath $Path
# How many run boundaries were passed; anything after the first is ignored (see below)
$extraRuns = 0

# The header is taken from **the capture itself**, because both probes log their own. It used to be
# hardcoded here, one literal per side, and that is a trap rather than a shortcut: a column added to a probe
# leaves this writing the old names over the new data, and every name after the insertion point then refers
# to the wrong column. It happened - `up`, `fire` and `mod` went into the engine's row and `anim` moved from
# index 22 to 25, so a whole sweep extracted with `anim` pointing at `camy`. Nothing failed. The comparison
# read a column that changes every row, found no pose lasting four ticks, and reported 390 of 424 scenarios
# as having a different animation structure - a number that looks like a finding.
#
# The literals below are only a fallback for a capture too old to carry one, and are the two sets as they
# stood before `up`/`fire`/`mod` were added.
$headerFallbackOriginal = 'scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy,camx,camy,anim,frame'
$headerFallbackEngine = 'scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr,camx,camy,anim,frame,tranim,susp'
$header = $null

if ([System.IO.Path]::GetExtension($resolved) -eq '.asdat') {
	# Read as raw bytes and keep whatever looks like one of our rows - the jjSTREAM framing around them is
	# binary and matches nothing, and the header the script writes into the stream fails the \d+ on tick
	$text = Read-TraceText $resolved
	$headerMatch = [regex]::Match($text, '(?m)^scenario,tick,[^\r\n]+')
	if ($headerMatch.Success) { $header = $headerMatch.Value.Trim() }
	foreach ($m in [regex]::Matches($text, '(?m)^[a-z][a-z0-9_]*,\d+,[^\r\n]+')) {
		$rows.Add($m.Value)
	}
	if (-not $header) { $header = $headerFallbackOriginal }
} else {
	# One row per `[probe]` line. The column header and the "finished" marker the probe also logs are
	# dropped from the rows, the header having been kept aside as the output's own.
	#
	# Only the FIRST run in the log is kept. A log can hold more than one: the game does not exit when the
	# probe is done, and if it is left running, anything that reloads the level builds a fresh probe that
	# starts over from scenario 0. Appending both silently glues the second run's opening scenario onto the
	# end of the first one's - `g_stand` came out with 398 rows instead of 213 and a final position 1152 px
	# away, which reads exactly like a physics bug in the most trivial scenario there is.
	foreach ($line in Read-TraceLines $resolved) {
		$i = $line.IndexOf('[probe] ')
		if ($i -lt 0) { continue }
		$row = $line.Substring($i + 8).Trim()
		if ($row.StartsWith('scenario,')) {
			# The header marks the start of a run, so a second one means the log continues past this run.
			# The first one is this capture's own column set - see the note above the fallbacks.
			if ($rows.Count -gt 0) { $extraRuns++ } elseif (-not $header) { $header = $row }
			continue
		}
		if ($row -eq 'finished') { $extraRuns++; continue }
		if ($extraRuns -gt 0) { continue }
		# Anything under the `[probe] ` prefix that is not shaped like a row is a note, not data. The probe
		# logs its own asides under `[probe-info]` for exactly that reason, but a stray one here would
		# otherwise be appended as a scenario named after whatever the message started with - which survives
		# every downstream check, because a CSV does not care what a name looks like.
		if ($row -notmatch '^[a-z][a-z0-9_]*,\d+,') { continue }
		$rows.Add($row)
	}
	if (-not $header) { $header = $headerFallbackEngine }
}

# A capture whose rows do not have as many fields as its header names is the failure this whole arrangement
# exists to stop, so say so rather than writing it out. Checked against the widest row, because a trailing
# empty field can be lost in the log.
$headerCount = ($header -split ',').Count
$widest = 0
foreach ($row in $rows) { $n = ($row -split ',').Count; if ($n -gt $widest) { $widest = $n } }
if ($widest -ne $headerCount) {
	throw "Header names $headerCount columns but the widest row has $widest. The capture and the header disagree, so every column after the first difference would be read under the wrong name."
}

if ($rows.Count -eq 0) {
	throw "No probe rows found in '$resolved'. A run of this engine needs /physics-probe together with /log:file:, and a build with WITH_PHYSICS_PROBE defined."
}

$scenarios = ($rows | ForEach-Object { ($_ -split ',')[0] } | Select-Object -Unique)

if ($Split) {
	if (-not (Test-Path $Destination)) {
		New-Item -ItemType Directory -Path $Destination -Force | Out-Null
	}
	# Bucket by category in one pass. A scenario's rows are contiguous in the capture, but nothing
	# guarantees that, so this does not assume it.
	$byCategory = @{}
	foreach ($row in $rows) {
		$cat = Get-ScenarioCategory (($row -split ',')[0])
		if (-not $byCategory.ContainsKey($cat)) {
			$byCategory[$cat] = [System.Collections.Generic.List[string]]::new()
		}
		$byCategory[$cat].Add($row)
	}
	# Only the categories this capture actually holds are written. A filtered run therefore replaces just
	# those files and leaves every other category's committed trace untouched, which is the whole reason
	# the split exists.
	#
	# Within a category the new rows are MERGED over the old, scenario by scenario, rather than replacing the
	# file. A filter is often narrower than a category - re-measuring two scenarios of `doublejump`'s fifty is
	# exactly the case the filter exists for - and an overwrite there silently discards the other forty-eight.
	# That is not hypothetical; it happened, and the only reason it was noticed is that the row count in the
	# output looked too small. Merging makes the narrow case behave the way the whole design implies: what you
	# re-ran is replaced, and nothing else is touched.
	$written = 0
	foreach ($cat in ($byCategory.Keys | Sort-Object)) {
		$catOut = [System.Collections.Generic.List[string]]::new()
		$catOut.Add($header)

		$leafExisting = if ($Gzip) { "$cat.csv.gz" } else { "$cat.csv" }
		$existingPath = Join-Path $Destination $leafExisting
		if (Test-Path $existingPath) {
			$fresh = @{}
			foreach ($row in $byCategory[$cat]) { $fresh[($row -split ',')[0]] = $true }
			$previous = Read-TraceLines $existingPath
			if ($previous.Count -ge 2 -and $previous[0] -ne $header) {
				throw "'$existingPath' has a different column header than this capture - merging would mix two incompatible traces. Move it aside, or re-capture the whole category."
			}
			$kept = 0
			foreach ($row in $previous[1..($previous.Count - 1)]) {
				if (-not $fresh.ContainsKey(($row -split ',')[0])) { $catOut.Add($row); $kept++ }
			}
			if ($kept -gt 0) { Write-Output ("{0,-12} kept {1} rows of scenarios this capture did not re-run" -f $cat, $kept) }
		}
		$catOut.AddRange($byCategory[$cat])
		$leaf = if ($Gzip) { "$cat.csv.gz" } else { "$cat.csv" }
		$path = Join-Path $Destination $leaf
		$catNames = ($byCategory[$cat] | ForEach-Object { ($_ -split ',')[0] } | Select-Object -Unique)
		$actual = Write-TraceLines $path $catOut
		Write-Output ("{0,-12} {1,7} rows {2,4} scenarios -> {3}" -f $cat, $byCategory[$cat].Count, $catNames.Count, $actual)
		$written++
	}
	Write-Output "$($rows.Count) rows across $($scenarios.Count) scenarios -> $written categor$(if ($written -eq 1) { 'y' } else { 'ies' }) in $Destination"
	if ($byCategory.ContainsKey('misc')) {
		$miscNames = ($byCategory['misc'] | ForEach-Object { ($_ -split ',')[0] } | Select-Object -Unique)
		Write-Output "Note: $($miscNames.Count) scenario(s) fell through to 'misc' - give them a category in Categories.ps1: $($miscNames -join ', ')"
	}
} else {
	$out = [System.Collections.Generic.List[string]]::new()
	$out.Add($header)
	$out.AddRange($rows)

	$destination = Write-TraceLines $Destination $out
	Write-Output "$($rows.Count) rows across $($scenarios.Count) scenarios -> $destination"
}
if ($extraRuns -gt 1) {
	Write-Output "Note: the log continues past the end of this run - a later probe run was ignored. Only the first is in the output."
}
