<#
.SYNOPSIS
	The scenario categories the captured traces are split into, and how to map a scenario name to one.

.DESCRIPTION
	A full sweep is about eighty minutes on each side, which is far too slow to iterate against: adding one
	scenario meant regenerating a single file holding all four hundred. The captures are therefore split by
	category, so adding a scenario re-runs and replaces one file of about ten minutes.

	Categories are defined by scenario-name **prefixes**, which is the same thing the probes' `ScenarioFilter`
	takes - so a category's prefix list pasted into either probe runs exactly the scenarios that category's
	file holds. That is the whole point of defining them here rather than by index: the two probes have never
	numbered scenarios alike, so an index-based split would need two definitions that could disagree.

	The list is ORDERED and the first match wins, so a narrower category has to come before the wider one it
	sits inside - `sp_dj` before `sp_`, or every double jump would land in `special`. Sizes are kept under
	roughly fifty scenarios; the two that needed splitting for that reason are `sp_` (91) and nothing else.

	`misc` is the catch-all and has no prefixes of its own. Anything that matches nothing above lands there,
	so a newly added scenario is never silently dropped from the split - it shows up in `misc` until it is
	given a home here.
#>

# Ordered: first match wins
$script:ProbeCategories = @(
	# `sp_dj_a` is the air-control half of the double jump and is a third of it on its own, so it goes first
	# and the rest follows - the two together are what `sp_dj` used to be, at about six minutes each instead
	# of twelve. Same reasoning splits the pinball paddles from the bumpers.
	@{ Name = 'doublejump-air'; Prefixes = @('sp_dj_a') }
	@{ Name = 'doublejump'; Prefixes = @('sp_dj', 'an_dj') }
	@{ Name = 'special';    Prefixes = @('sp_') }
	@{ Name = 'pinball-pad'; Prefixes = @('pb_pad', 'pb_lpad') }
	@{ Name = 'pinball';    Prefixes = @('pb_') }
	@{ Name = 'objects';    Prefixes = @('ob_', 'fc_') }
	@{ Name = 'weapons';    Prefixes = @('wb_') }
	@{ Name = 'apex';       Prefixes = @('ap_') }
	@{ Name = 'enemies';    Prefixes = @('en_') }
	@{ Name = 'air';        Prefixes = @('a_', 'jr_') }
	@{ Name = 'slopes';     Prefixes = @('sl_') }
	@{ Name = 'tube';       Prefixes = @('tb_') }
	@{ Name = 'ceiling';    Prefixes = @('cl_') }
	@{ Name = 'gaps';       Prefixes = @('lh_') }
	@{ Name = 'belts';      Prefixes = @('bl_', 'wd_') }
	@{ Name = 'floatup';    Prefixes = @('fu_') }
	@{ Name = 'animation';  Prefixes = @('an_', 'rt_') }
	@{ Name = 'copter';     Prefixes = @('cp_') }
	@{ Name = 'oneway';     Prefixes = @('ow_') }
	@{ Name = 'ground';     Prefixes = @('g_', 'sd_') }
	@{ Name = 'ledge';      Prefixes = @('lc_') }
	# The level's new section - a warp, two weapon monitors and a morph monitor, none of which `_pt` held
	# until it was extended for them. One category because they share a floor and are cheap.
	@{ Name = 'newsection'; Prefixes = @('wp_', 'pu_', 'mb_') }
	# Its own file because it is its own *level*: `dm_chain` runs in the original game's Diamondus 3 and is
	# guarded off in `_pt`, so a sweep of the test level never produces it and must not appear to delete it.
	@{ Name = 'diam3';      Prefixes = @('dm_') }
	@{ Name = 'misc';       Prefixes = @() }
)

function Get-ProbeCategories {
	<#
	.SYNOPSIS
		The ordered category list.
	#>
	return $script:ProbeCategories
}

function Get-ScenarioCategory {
	<#
	.SYNOPSIS
		The category a scenario name belongs to; 'misc' when it matches nothing.
	#>
	param([Parameter(Mandatory = $true)][string] $Name)

	foreach ($cat in $script:ProbeCategories) {
		foreach ($prefix in $cat.Prefixes) {
			if ($Name.StartsWith($prefix)) { return $cat.Name }
		}
	}
	return 'misc'
}

function Get-CategoryFilter {
	<#
	.SYNOPSIS
		The `ScenarioFilter` string that reproduces one category, to paste into either probe.

	.EXAMPLE
		./Categories.ps1; Get-CategoryFilter objects
		ob_
	#>
	param([Parameter(Mandatory = $true)][string] $Category)

	$cat = $script:ProbeCategories | Where-Object { $_.Name -eq $Category }
	if ($null -eq $cat) {
		throw "Unknown category '$Category'. Known: $(($script:ProbeCategories | ForEach-Object { $_.Name }) -join ', ')"
	}
	if ($cat.Prefixes.Count -eq 0) {
		throw "'$Category' is the catch-all and has no prefixes - it cannot be selected by filter. Give its scenarios a category of their own instead."
	}
	return ($cat.Prefixes -join ',')
}
