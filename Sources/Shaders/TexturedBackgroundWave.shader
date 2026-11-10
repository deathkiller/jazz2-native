program TexturedBackgroundWave;
variant DITHER;
variant KEYED;

shader_type canvas_item;
precision highp;

// JJ2+ wave: the texture tiled like the layer, rows moved by the horizontal waves and columns by the vertical ones,
// in whole pixels and by texture position, so the waves follow the texture, not the view

// View size in level pixels
uniform vec2 uViewSize;
// Layer pixel at the top left of the view
uniform vec2 uShift;
// xy: horizontal/vertical wave amplitude in pixels (0 for none), zw: 1 / period in texels
uniform vec4 uWave;
// Horizontal/vertical wave phase, in periods
uniform vec2 uWavePhase;

#include "Include/TexturedBackgroundKey.inc"
#include "Include/TexturedBackgroundDither.inc"

#define TEXTURE_SIZE 256.0
#define TAU 6.28318530718

void fragment() {
	// Opacity of screened or multiplied layers, from the instance color alpha
	float opacity = COLOR.a;
	// Texel this pixel shows, the texture repeats
	vec2 pos = mod(floor(uShift + UV * uViewSize), TEXTURE_SIZE);

	// Rows move by the horizontal wave at their texture row, columns by the vertical one at their column
	vec2 moved = floor(uWave.xy * vec2(cos(TAU * (pos.y * uWave.z + uWavePhase.x)), sin(TAU * (pos.x * uWave.w + uWavePhase.y))));
	vec2 texel = mod(pos + moved, TEXTURE_SIZE);

	// V runs up the layer, sample the texel center
	vec2 texturePos = vec2((texel.x + 0.5) / TEXTURE_SIZE, 1.0 - (texel.y + 0.5) / TEXTURE_SIZE);
	vec3 color = texture(TEXTURE, texturePos).rgb;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	color = reduceColors(color, mod(gl_FragCoord.xy, 4.0));
#endif
#if KEYED
	COLOR = keyedColor(color);
#else
	// Premultiplied for the sprite blend modes
	COLOR = vec4(color * opacity, opacity);
#endif
}
