program TileMapMeshPalette;

#include "Include/TileMapVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTexturePalette : texture_unit(1);

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
	// Flat palette position = per-instance offset + the per-pixel index (red channel), mapped into the 256x256 texture
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 color = texture(uTexturePalette, vec2(palX, palY));
	COLOR = vec4(color.rgb, color.a * src.a) * vColor;
}

void fixed_function() {
	// Same tile-layer stream as TileMapMesh; the palette remap is carried by the uTexturePalette
	// binding above, not by a separate pipeline stage
	pipeline tile_map_mesh;
}
