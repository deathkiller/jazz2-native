program TileMapMeshChromaKey;
variant USE_PALETTE;

// Blend modes (see BlendModes.inc) dither and hash by screen position, which needs more than half precision
precision highp;

// Chroma key layer (see Tiles::LayerBlend::ChromaKey), or a blend mode that reads the scene behind it. With the key,
// tiles are drawn only over scene pixels of the key color, like SPRITE::CHROMAKEY of JJ2+. Transparent tile pixels
// draw black there; only where there's no tile does the key stay for the layers in front.

#include "Include/TileMapVs.inc"
#include "Include/BlendModes.inc"

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// A sharp copy of the scene behind the layer
uniform sampler2D uTextureBehind : texture_unit(2);
// One over the target size; the copy may be smaller but covers the same view
uniform vec2 uTargetSizeInv;
// Key color in rgb, per-channel tolerance in alpha (the copy may have fewer bits per channel)
uniform vec4 uKeyColor;
// x: blend mode (see BlendModes.inc), 0 for the key; y: non-zero for ordered-dither opacity
uniform vec2 uBlendMode;

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
	if (uBlendMode.x > 0.5) {
		vec4 tinted = tile * vColor;
		COLOR = blendModeResult(uBlendMode, tinted.rgb, tinted.a, behind, gl_FragCoord.xy);
	} else {
		vec3 difference = abs(behind - uKeyColor.rgb);
		float keyed = step(max(max(difference.r, difference.g), difference.b), uKeyColor.a);
		COLOR = vec4(tile.rgb * tile.a, keyed) * vColor;
	}
}
