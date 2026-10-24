program TileMapMeshChromaKey;
variant USE_PALETTE;

// Every global reaches both stages, InstanceBlock included, and a named uniform block whose members
// disagree on precision between them does not link - so the fragment prologue keeps the vertex default
precision highp;

// A chroma key layer (see Tiles::LayerRendererType::ChromaKey) in the tile-layer mesh: the tiles are drawn as usual,
// but only over the pixels of the scene behind them that are of the key color, as SPRITE::CHROMAKEY of JJ2+ - so the
// layer shows through a part of the scene painted in that color, a window of a single color in the tiles in front
// of it. A transparent pixel of a tile is black there, as measured in JJ2+ - only where there's no tile at all does the
// key stay for the layers in front

#include "Include/TileMapVs.inc"

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// A sharp copy of the scene behind the layer
uniform sampler2D uTextureBehind : texture_unit(2);
// One over the size of the target being drawn into. The copy is smaller or the same size, but it covers the same view
uniform vec2 uTargetSizeInv;
// The key color in rgb, and how far each channel of a pixel of the scene may be from it in alpha - the copy can have
// fewer bits per channel than the palette has
uniform vec4 uKeyColor;

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
#if USE_PALETTE
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 paletteColor = texture(uTexturePalette, vec2(palX, palY));
	vec4 tile = vec4(paletteColor.rgb, paletteColor.a * src.a);
#else
	vec4 tile = src;
#endif
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
	vec3 difference = abs(behind - uKeyColor.rgb);
	float keyed = step(max(max(difference.r, difference.g), difference.b), uKeyColor.a);
	COLOR = vec4(tile.rgb * tile.a, keyed) * vColor;
}
