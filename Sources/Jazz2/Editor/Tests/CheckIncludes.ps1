<#
	.SYNOPSIS
	Checks that the folders of the level editor include each other only in the allowed directions

	.DESCRIPTION
	Every #include "..." in Sources/Jazz2/Editor is resolved against the file that has it. An include of a file
	outside of the editor (the engine, the game, the formats) is always allowed. Within the editor, the folders are
	layers and a folder includes only folders of lower layers, so they never include each other in a circle:

	  0. Document - the level and its history, it includes only the engine
	  1. Common - value types and helpers shared by the editor, over the document
	  1. UI - the toolkit of the interface, it includes no other folder of the editor
	  2. Files
	  3. Imaging, Input - Input includes only UI, Common and Files
	  4. Events
	  5. Smart
	  6. Runtime
	  7. Tools
	  8. Shell, Session (the parts of EditorSession that keep state of their own) and the files in the root
	     (EditorSession* and the self-test), which include each other

	Folders of the same layer don't include each other. Tests includes whatever it tests, and only the self-test in
	the root includes it. A new folder has to be given a layer here.

	Tools reach the editor only through Tools/ToolHost.h and Tools/EditorContext.h, so no file of Tools includes
	EditorSession.h, and only the files of EditorSession itself include EditorSessionPrivate.h. Session reaches it the
	same way, with host interfaces of its own, only its debug console includes EditorSession.h. These rules have no
	exceptions, $AllowedEdges doesn't apply to them.

	Edges that exist now and break a rule are listed in $AllowedEdges, so the check passes until they're removed.
	An allowed edge that doesn't break a rule anymore is reported as a warning, so it's taken off the list too.

	.PARAMETER ListEdges
	Lists every include between two different folders of the editor, and nothing is checked
#>
param(
	[switch]$ListEdges
)

$ErrorActionPreference = 'Stop'

# Each of these would break a rule above and is to be removed by a later refactor, the check then keeps it away.
# A line is "<file including>|<file included>", both relative to Sources/Jazz2/Editor with forward slashes.
$AllowedEdges = @(
)

$editorRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# Layer of each folder, the root of the editor is the empty name
$layers = @{
	'Document' = 0
	'Common' = 1
	'UI' = 1
	'Files' = 2
	'Imaging' = 3
	'Input' = 3
	'Events' = 4
	'Smart' = 5
	'Runtime' = 6
	'Tools' = 7
	'Shell' = 8
	'Session' = 8
	'' = 8
}
$topLayer = 8

# Path relative to the editor with forward slashes, or $null for a file outside of it
function Get-EditorPath([string]$fullPath) {
	$prefix = $editorRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
		return $null
	}
	return $fullPath.Substring($prefix.Length).Replace('\', '/')
}

# Folder of a file within the editor, empty for a file in its root
function Get-Folder([string]$editorPath) {
	$slash = $editorPath.IndexOf('/')
	if ($slash -lt 0) {
		return ''
	}
	return $editorPath.Substring(0, $slash)
}

# Name of a folder in a message
function Get-FolderName([string]$folder) {
	if ($folder -eq '') {
		return 'the root'
	}
	return $folder
}

# Why an include breaks a rule, or $null if it doesn't
function Get-Violation([string]$from, [string]$to) {
	$fromFolder = Get-Folder $from
	$toFolder = Get-Folder $to
	if ($fromFolder -eq $toFolder) {
		return $null
	}
	if ($fromFolder -eq 'Tests') {
		# The tests reach whatever they test
		return $null
	}
	if ($toFolder -eq 'Tests') {
		if ($fromFolder -eq '') {
			return $null
		}
		return 'only the self-test in the root includes the tests'
	}
	foreach ($folder in @($fromFolder, $toFolder)) {
		if (-not $layers.ContainsKey($folder)) {
			return "$(Get-FolderName $folder) has no layer, it's to be added to the layers of this check"
		}
	}
	if ($fromFolder -eq 'UI') {
		return 'UI includes no other folder of the editor'
	}
	if ($fromFolder -eq 'Input' -and @('UI', 'Common', 'Files') -notcontains $toFolder) {
		return 'Input includes only UI, Common and Files'
	}
	$fromLayer = $layers[$fromFolder]
	$toLayer = $layers[$toFolder]
	if ($toLayer -lt $fromLayer -or ($toLayer -eq $topLayer -and $fromLayer -eq $topLayer)) {
		return $null
	}
	return ('{0} (layer {1}) includes only folders of lower layers, not {2} (layer {3})' -f (Get-FolderName $fromFolder), $fromLayer, (Get-FolderName $toFolder), $toLayer)
}

# Why an include breaks a rule without exceptions, or $null if it doesn't
function Get-StrictViolation([string]$from, [string]$to) {
	if ((Get-Folder $from) -eq 'Tools' -and $to -eq 'EditorSession.h') {
		return 'Tools never include EditorSession.h, they use Tools/ToolHost.h or Tools/EditorContext.h'
	}
	if ($to -eq 'EditorSessionPrivate.h' -and $from -notlike 'EditorSession*') {
		return 'only the files of EditorSession include EditorSessionPrivate.h'
	}
	if ((Get-Folder $from) -eq 'Session' -and $to -eq 'EditorSession.h' -and $from -ne 'Session/EditorDebugConsole.cpp') {
		return 'Session reaches the editor through Tools/EditorContext.h and host interfaces of its own, only the debug console includes EditorSession.h'
	}
	return $null
}

$includePattern = [regex]'^\s*#\s*include\s*"([^"]+)"'
$edges = New-Object System.Collections.Generic.List[string]
$sources = Get-ChildItem -Path $editorRoot -Recurse -File -Include '*.h', '*.cpp'
foreach ($source in $sources) {
	$from = Get-EditorPath $source.FullName
	$lineNumber = 0
	foreach ($line in [IO.File]::ReadAllLines($source.FullName)) {
		$lineNumber++
		$match = $includePattern.Match($line)
		if (-not $match.Success) {
			continue
		}
		$target = [IO.Path]::GetFullPath((Join-Path $source.DirectoryName $match.Groups[1].Value))
		if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
			# Found by the include directories, the engine or a library
			continue
		}
		$to = Get-EditorPath $target
		if ($null -eq $to) {
			continue
		}
		$edges.Add("$from|$to|$lineNumber")
	}
}

if ($ListEdges) {
	foreach ($edge in $edges) {
		$parts = $edge.Split('|')
		if ((Get-Folder $parts[0]) -ne (Get-Folder $parts[1])) {
			Write-Output ('{0} -> {1}' -f $parts[0], $parts[1])
		}
	}
	exit 0
}

$failures = 0
$usedAllowed = @{}
foreach ($edge in $edges) {
	$parts = $edge.Split('|')
	$strict = Get-StrictViolation $parts[0] $parts[1]
	if ($null -ne $strict) {
		Write-Output ('{0}({1}): includes {2} - {3}' -f $parts[0], $parts[2], $parts[1], $strict)
		$failures++
		continue
	}
	$violation = Get-Violation $parts[0] $parts[1]
	if ($null -eq $violation) {
		continue
	}
	$key = "$($parts[0])|$($parts[1])"
	if ($AllowedEdges -contains $key) {
		$usedAllowed[$key] = $true
		continue
	}
	Write-Output ('{0}({1}): includes {2} - {3}' -f $parts[0], $parts[2], $parts[1], $violation)
	$failures++
}
foreach ($allowed in $AllowedEdges) {
	if (-not $usedAllowed.ContainsKey($allowed)) {
		# Not a failure, so removing an include never breaks the check, but the exception should go too
		Write-Output ('Warning: allowed include {0} doesn''t break a rule anymore, take it off the list' -f $allowed.Replace('|', ' -> '))
	}
}

if ($failures -gt 0) {
	Write-Output "$failures include problem(s) found in $($sources.Count) files"
	exit 1
}
Write-Output "Includes of $($sources.Count) files of the editor go in the allowed directions ($($AllowedEdges.Count) allowed exceptions)"
exit 0
