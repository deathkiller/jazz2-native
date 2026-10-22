program TexturedBackgroundWave;
variant KEYED;

shader_type canvas_item;
precision highp;

// The wave of JJ2+ (see Tiles::LayerRendererType::Wave): the texture repeated where the tiles of the layer would be, each
// row of it moved left or right by the horizontal waves and each column up or down by the vertical ones, as measured in
// the original - by whole pixels, and by the waves of where the pixel is in the texture before it's moved, so the waves
// follow the texture, not the view

// Size of the view in the pixels of the level
uniform vec2 uViewSize;
// The pixel of the layer at the top left of the view, as its tiles would be drawn there
uniform vec2 uShift;
// xy: how far the horizontal and the vertical waves move the texture, in pixels (0 for no waves), zw: one over the period
// of each, in pixels of the texture
uniform vec4 uWave;
// How far the horizontal and the vertical waves have moved, in periods
uniform vec2 uWavePhase;

#include "Include/TexturedBackgroundKey.inc"

#define TEXTURE_SIZE 256.0
#define TAU 6.28318530718

void fragment() {
	// How opaque a layer drawn screened or multiplied is, the alpha of the instance color (see TileMap::RenderTexturedBackground())
	float opacity = COLOR.a;
	// The pixel of the texture this pixel of the view shows, the texture repeats
	vec2 pos = mod(floor(uShift + UV * uViewSize), TEXTURE_SIZE);

	// A row moves by the horizontal waves at its row of the texture, a column by the vertical ones at its column
	vec2 moved = floor(uWave.xy * vec2(cos(TAU * (pos.y * uWave.z + uWavePhase.x)), sin(TAU * (pos.x * uWave.w + uWavePhase.y))));
	vec2 texel = mod(pos + moved, TEXTURE_SIZE);

	// V runs up the layer in the texture (see TileMap::TexturedBackgroundPass), the middle of the texel is taken
	vec2 texturePos = vec2((texel.x + 0.5) / TEXTURE_SIZE, 1.0 - (texel.y + 0.5) / TEXTURE_SIZE);
#if KEYED
	COLOR = vec4(texture(TEXTURE, texturePos).rgb, keyedOpacity());
#else
	// Premultiplied, which the blending of the sprite modes takes as it is
	COLOR = vec4(texture(TEXTURE, texturePos).rgb * opacity, opacity);
#endif
}
