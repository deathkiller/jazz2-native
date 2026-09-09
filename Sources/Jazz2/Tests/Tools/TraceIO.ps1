<#
.SYNOPSIS
	Shared reading and writing of trace files, transparently gzipped.

.DESCRIPTION
	The committed traces are gzipped. Uncompressed they are about 9 MB of CSV between them, which is a lot
	of repository for reference data nobody reads by eye; gzip takes that to well under a megabyte, and
	they are still exact - a trace is only useful if it is the real thing, so nothing is downsampled or
	rounded to save space.

	Compression is decided by the file extension, so `.csv` and `.csv.gz` both work everywhere and a plain
	CSV can still be written when one is wanted for a spreadsheet. Reading also accepts a path written as
	`.csv` when only `.csv.gz` is present, which keeps older commands and notes working unchanged.

	The output is deterministic - .NET's GZipStream writes no timestamp - so regenerating a trace that has
	not changed produces a byte-identical file and no commit. That matters for something committed: a
	container that embedded the time of writing would make every re-capture look like a change.

	Dot-source this from a script in the same directory:

		. "$PSScriptRoot\TraceIO.ps1"
#>

# Resolves a trace path, accepting `.csv` where only `.csv.gz` exists (and the reverse)
function Resolve-TracePath([string] $path) {
	if (Test-Path -LiteralPath $path) {
		return (Resolve-Path -LiteralPath $path).Path
	}
	foreach ($alt in @("$path.gz", ($path -replace '\.gz$', ''))) {
		if ($alt -ne $path -and (Test-Path -LiteralPath $alt)) {
			return (Resolve-Path -LiteralPath $alt).Path
		}
	}
	throw "Trace file not found: '$path' (nor a .gz beside it)"
}

# Reads a trace as one string, gzipped or not. Opened shared, because both games keep their output file
# open for the whole run and looking at a partial trace mid-sweep is the normal case, not the exception.
function Read-TraceText([string] $path) {
	$resolved = Resolve-TracePath $path
	$stream = [System.IO.File]::Open($resolved, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
	try {
		if ([System.IO.Path]::GetExtension($resolved) -eq '.gz') {
			$gzip = [System.IO.Compression.GZipStream]::new($stream, [System.IO.Compression.CompressionMode]::Decompress)
			try {
				$reader = [System.IO.StreamReader]::new($gzip, [System.Text.Encoding]::ASCII)
				try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
			} finally { $gzip.Dispose() }
		}
		$reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII)
		try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
	} finally { $stream.Dispose() }
}

# Reads a trace as an array of lines
function Read-TraceLines([string] $path) {
	return ((Read-TraceText $path) -split '\r?\n' | Where-Object { $_.Length -gt 0 })
}

# Writes lines to a trace, gzipping when the path ends in .gz. The destination is made absolute against
# the shell's location first: .NET resolves a relative path against the *process* working directory, which
# is wherever the host started rather than where the script is being run from.
function Write-TraceLines([string] $path, $lines) {
	$destination = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).Path, $path))
	$destDir = Split-Path -Parent $destination
	if ($destDir -and -not (Test-Path -LiteralPath $destDir)) {
		New-Item -ItemType Directory -Force -Path $destDir | Out-Null
	}

	if ([System.IO.Path]::GetExtension($destination) -ne '.gz') {
		[System.IO.File]::WriteAllLines($destination, $lines)
		return $destination
	}

	$stream = [System.IO.File]::Create($destination)
	try {
		# CompressionLevel is deliberately Optimal rather than Fastest: these are written once and read
		# many times, and the difference on a file of this shape is worth having
		$gzip = [System.IO.Compression.GZipStream]::new($stream, [System.IO.Compression.CompressionLevel]::Optimal)
		try {
			$writer = [System.IO.StreamWriter]::new($gzip, [System.Text.Encoding]::ASCII)
			try {
				foreach ($line in $lines) { $writer.WriteLine($line) }
			} finally { $writer.Dispose() }
		} finally { $gzip.Dispose() }
	} finally { $stream.Dispose() }
	return $destination
}
