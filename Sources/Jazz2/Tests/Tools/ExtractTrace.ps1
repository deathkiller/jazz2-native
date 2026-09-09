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

		original    scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy
		engine      scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr

	The original carries `gametick` (its own frame counter, which the engine has no equivalent of), and the
	engine carries five extra flags: whether the player was in control, still animating out of the previous
	move, and crouching - which is what tells a move that did not happen apart from one that happened
	differently - plus the two that pick the rise gravity, `jrel` (jump released) and `spr` (launched by a
	spring). Everything before `sm` is common, and that is what comparisons use.

.PARAMETER Path
	The capture to read: a game log (`/log:file:` output) or an `.asdat` from the original game.

.PARAMETER Destination
	Where to write the CSV. Ending it in `.gz` gzips it, which is how the committed traces are stored; see
	TraceIO.ps1.

.EXAMPLE
	./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine-60fps.csv

.EXAMPLE
	./ExtractTrace.ps1 -Path ../../../../x64/Debug/Source/_pt_trace.asdat -Destination ../Results/original-70hz.csv
#>
param(
	[Parameter(Mandatory = $true)][string] $Path,
	[Parameter(Mandatory = $true)][string] $Destination
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\TraceIO.ps1"

$rows = [System.Collections.Generic.List[string]]::new()
$resolved = Resolve-TracePath $Path
# How many run boundaries were passed; anything after the first is ignored (see below)
$extraRuns = 0

if ([System.IO.Path]::GetExtension($resolved) -eq '.asdat') {
	$header = 'scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy'
	# Read as raw bytes and keep whatever looks like one of our rows - the jjSTREAM framing around them is
	# binary and matches nothing, and the header the script writes into the stream fails the \d+ on tick
	$text = Read-TraceText $resolved
	foreach ($m in [regex]::Matches($text, '(?m)^[a-z][a-z0-9_]*,\d+,[^\r\n]+')) {
		$rows.Add($m.Value)
	}
} else {
	$header = 'scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr'
	# One row per `[probe]` line. The column header and the "finished" marker the probe also logs are
	# dropped, so the header below is the only one in the output.
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
			# The header marks the start of a run, so a second one means the log continues past this run
			if ($rows.Count -gt 0) { $extraRuns++ }
			continue
		}
		if ($row -eq 'finished') { $extraRuns++; continue }
		if ($extraRuns -gt 0) { continue }
		$rows.Add($row)
	}
}

if ($rows.Count -eq 0) {
	throw "No probe rows found in '$resolved'. A run of this engine needs /physics-probe together with /log:file:, and a build with WITH_PHYSICS_PROBE defined."
}

$out = [System.Collections.Generic.List[string]]::new()
$out.Add($header)
$out.AddRange($rows)

$destination = Write-TraceLines $Destination $out

$scenarios = ($rows | ForEach-Object { ($_ -split ',')[0] } | Select-Object -Unique)
Write-Output "$($rows.Count) rows across $($scenarios.Count) scenarios -> $destination"
if ($extraRuns -gt 1) {
	Write-Output "Note: the log continues past the end of this run - a later probe run was ignored. Only the first is in the output."
}
