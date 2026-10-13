param(
	# Where the game runs from - the level and its tileset are installed into its Content directory
	[string]$GameDir = (Join-Path $PSScriptRoot '..\..\..\..\x64\Debug'),
	# The tile column the player starts at
	[int]$SpawnX = 3,
	# A tileset in the game's own format whose palette the level's tileset takes (it replaces the sprite palette while
	# the level runs, so it must be the game's palette) - the committed tileset by default
	[string]$PaletteSource = (Join-Path $PSScriptRoot 'resurrection-layerfx.j2t'),
	# Also rewrites the committed level and tileset next to this script
	[switch]$UpdateCommitted,
	# Also saves a preview of the tileset sheet to the given PNG
	[string]$Preview
)
# Builds the "resurrection-layerfx" level and its tileset (see README.md) and installs them into the game's Content
# directory
$ErrorActionPreference = 'Stop'
$sources = @('LevelGen.Core.cs', 'LevelGen.Terrain.cs', 'LevelGen.Art.cs', 'LevelGen.Props.cs', 'LevelGen.Level.cs') | ForEach-Object { Join-Path $PSScriptRoot $_ }
Add-Type -Path $sources -ReferencedAssemblies System.Drawing, System.IO.Compression

$tilesetDir = Join-Path $GameDir 'Content\Tilesets'
$levelDir = Join-Path $GameDir 'Content\Episodes\unknown'
New-Item -ItemType Directory -Force $tilesetDir, $levelDir | Out-Null

$palette = [LevelGen]::ReadPaletteFrom((Resolve-Path $PaletteSource).Path)
$tilesetPath = Join-Path $tilesetDir 'resurrection-layerfx.j2t'
$levelPath = Join-Path $levelDir 'resurrection-layerfx.j2l'
[LevelGen]::Generate($palette, $tilesetPath, $levelPath, 'resurrection-layerfx', $SpawnX)
if ($UpdateCommitted) {
	Copy-Item $tilesetPath, $levelPath $PSScriptRoot -Force
}
if ($Preview) {
	[LevelGen]::SavePreview($Preview)
}
