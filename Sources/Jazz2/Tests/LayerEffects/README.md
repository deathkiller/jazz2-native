# Jazz² Resurrection - LayerFX

A single-player level built around the special tile layers: x-ray (`LayerRendererType::XRay`) and blur-behind with
displacement, reflection and blur (`LayerRendererType::BlurBehind`), see `Sources/Jazz2/Tiles/LayerTypes.h`. It is a
cardboard diorama: a countryside of torn kraft board and painted card that turns into the backlot of a film studio
and ends on its sound stage. Most parts have more than one way through, and every effect is used where it belongs in
the level rather than on its own.

## Layout

| Path | What it is |
|---|---|
| `resurrection-layerfx.j2l` | The level, in the game's own format (version 1). |
| `resurrection-layerfx.j2t` | Its tileset, painted procedurally, 32-bit (about 9.6 MB, mostly the facades and the animation frames of the effects). |
| `LevelGen.Core.cs` | Tiles, the image encoding and the writers of both files. |
| `LevelGen.Terrain.cs` | The solid foreground: a height map, platforms and holes painted as one cardboard picture and then cut into tiles, so neighbouring tiles always connect and the collision masks are the shapes themselves. |
| `LevelGen.Art.cs` | The cardboard look (`Cardboard()`, `Layered()`), the ways the facades are seen through, water, glass and the background. |
| `LevelGen.Props.cs` | The props. |
| `LevelGen.Level.cs` | The layout of the level and its events. |
| `Generate.ps1` | Builds the level and installs it into a game directory. |

## Playing it

1. Install the level: either run `Generate.ps1`, which installs into `x64\Debug` by default (`-GameDir` for another
   one), or copy `resurrection-layerfx.j2t` into `Content\Tilesets\` and `resurrection-layerfx.j2l` into
   `Content\Episodes\unknown\` of the game directory.
2. Choose it in *Play Custom Levels*, or start a Debug build with `/level unknown/resurrection-layerfx`.

The effects need a backend of the post-processing tier (`RHI_CAP_POSTPROCESSING`, e.g. OpenGL, Direct3D 11 or
Vulkan); elsewhere the level is shown as it would look without them. `Generate.ps1 -SpawnX <column>` starts the
player at another column, which is also the way to look at a section in a screenshot, since a capture can't move the
player.

## The level

400 columns. Falling out of the bottom of the level kills, so the pits are real; there are four checkpoints.

| Columns | Section | Effects and challenges |
|---|---|---|
| 0-32 | Meadow | Rolling S-curved hills, a magnifying glass on the first hilltop. Bushes cut themselves out around whoever is behind them (x-ray, empty mask frame). A spring up to a route over the clouds, which goes on to the waterfall; a burrow under the meadow. |
| 33-56 | Lake under a jetty | The water reflects what is above it, bent by animated ripples and blurred with depth, and fades into its sloping banks (blur-behind). A gap in the jetty. |
| 57-95 | Hill and waterfall | An S-curve up to a plateau with a sloped ledge over it. The waterfall shows only its outline over whoever is behind it (x-ray, 16 frames); a cave with a 1-up is hidden behind it. Its pool reflects like the lake. |
| 96-140 | Box canyon | Pits between islands of cardboard boxes, planks over the widest that collapse under you, thumbtacks where a careless jump lands. |
| 141-171 | Misty marsh | A reflection that fades into blur with depth, under a mist; stepping planks over it. |
| 172-181 | Studio gate | A green screen between the gate's posts: whoever walks behind it is keyed out to an outer-space backdrop (x-ray). |
| 182-270 | Backlot street | Facades in front of the street, each seen through its own way over whoever is behind it (x-ray): the theater is a paper screen lit from behind with their shadow on it, and its marquee bulbs chase; the detective agency glows under a blacklight; the photo studio turns into its negative; the stage's hangar into its blueprint. A raised plaza with curved ramps carries the diner. A glass-block wall bends what is behind it, frosted windows blur it, puddles reflect, a magnifying glass magnifies (displacement), heat shimmers over the diner's chimney, and the shade of its awning falls only on what is under it. Road works with boxes to climb over spilled thumbtacks, roofs to run across, and a sewer under the street with steam rising out of its manhole and a wall of boxes to shoot through to a secret. |
| 271-399 | Sound stage | A riser with curved ramps, a cardboard castle set (an archway under it, towers and battlements on it), hanging clouds over the wave tank up to a second catwalk, planks hanging over the wind machine, stars and a moon on wires, a balcony by the exit. Spotlights color whoever stands in them (x-ray, a single frame), smoke machines shimmer the air (displacement and blur), the glossy floor reflects, whoever walks behind the aquarium is seen through its water (animated displacement), the wave tank's water rises and falls with its reflection, a wind machine blows you back towards thumbtacks (the air it blows rippling), a reducing glass shrinks what is behind it, and frosted panes of all shapes and tints (a screen, a pane clear at its top and frosted at its bottom, an arch, a porthole, a diamond) blur it. A catwalk over the stage; boxes up to the riser with the exit. |
## How the special tiles are encoded

- **X-ray**: the last frame of an animated tile is shown in the silhouettes of the actors behind the layer, the
  frames before it animate as usual elsewhere. A single frame is shown only in the silhouettes.
- **Blur behind**: red and green move what is behind the layer horizontally and vertically (128 leaves it in place,
  every step away from it is a quarter of a pixel), blue mirrors it, and alpha blurs it. Alpha 0 is a pixel that does
  nothing at all, so a pixel that only moves or mirrors has alpha 1. `LevelGen.Fx()` encodes one.

## Changing it

Edit the generator, then run `Generate.ps1 -UpdateCommitted` to rebuild both files here as well (`-Preview <png>`
also saves the tileset sheet). The palette is taken from the committed tileset (`-PaletteSource` takes another
tileset in the game's own format, for example `Cache\Tilesets\castle1.j2t` of a game directory) - the level's
tileset replaces the sprite palette while it runs, so it has to be the game's. The output is the same on every run.

Things to keep in mind:

- Everything that repeats in the art repeats every 64 or 128 pixels, so the tiles repeat as well and are stored
  once. Anything that doesn't (noise over the whole level, a texture in level coordinates) makes every tile unique
  and the tileset many times larger.
- A layer with a depth below 50 is drawn in front of the sprite layer, so decoration behind it needs more than 50.
- Only one animated tile fits a tile of a layer, so two animated props on the same layer can't overlap.
- The level start is placed last, so no other event on its tile can replace it.
