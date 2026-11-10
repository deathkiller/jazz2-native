program TexturedBackgroundCylinder;
variant DITHER;
variant KEYED;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
// JJ2+ cylinder - xy: horizon center as a fraction of the view, z: 1 to fade into the horizon, w: 1 for half size
// The shift is the layer pixel at the top left of the view like an ordinary layer
uniform vec4 uCylinder;

#include "Include/TexturedBackgroundKey.inc"
#include "Include/TexturedBackgroundDither.inc"

// The texture spans the view height once (twice at half size), rows span half a turn from the horizon, and a pixel
// is 1/128 of the view width times 2 + cos (2 * (1 + 2 cos) at half size); it fades like the sky, but only to 65 %
#define FADE_ROWS 141.0
#define FADE_STRENGTH 0.65
#define PI 3.14159265359

void fragment() {
	// Opacity of screened or multiplied layers, from the instance color alpha
	float opacity = COLOR.a;

	// In view pixels; the texture runs down from the view's middle wherever the horizon is, the shift moves it along
	// the tube at 1/4 speed and around it at full speed, V runs up the layer
	vec2 pixel = UV * uViewSize - vec2(0.5);
	float row = pixel.y - floor(uCylinder.y * uViewSize.y);
	float rowCos = cos(PI * row / uViewSize.y);
	float halfSize = step(0.5, uCylinder.w);
	vec2 texel = vec2(
		uShift.x * 0.25 + (pixel.x - uCylinder.x * uViewSize.x) * mix(128.0 * (2.0 + rowCos), 256.0 * (1.0 + 2.0 * rowCos), halfSize) / uViewSize.x,
		uShift.y + 128.0 + 128.0 * halfSize + (pixel.y - 0.5 * uViewSize.y) * (256.0 + 256.0 * halfSize) / uViewSize.y
	);
	float rows = row / FADE_ROWS;
	float fade = clamp(1.0 - rows * rows, 0.0, 1.0) * FADE_STRENGTH;

	vec2 texturePos = vec2(texel.x / 256.0, 1.0 - texel.y / 256.0);
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	vec4 texColor = textureTexel(texturePos);
#else
	vec4 texColor = texture(TEXTURE, texturePos);
#endif

	COLOR = mix(texColor, vec4(uHorizonColor.xyz, 1.0), fade * uCylinder.z);
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	COLOR.rgb = reduceColors(COLOR.rgb, mod(gl_FragCoord.xy, 4.0));
#endif
#if KEYED
	COLOR = keyedColor(COLOR.rgb);
#else
	// Premultiplied for the sprite blend modes
	COLOR = vec4(COLOR.rgb * opacity, opacity);
#endif
}

void fixed_function(pvr, gs) {
	// Same banded rebuild as TexturedBackground.shader, cut coarsely
#define WARP_LOW_DETAIL
#define WARP_CYLINDER
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE wraps the target, so each band is one strip; the tint stays a second pass, cut coarsely
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#define WARP_CYLINDER
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// One strip per band with the tint in the vertex color; a 4-vertex band is drawn row by row exactly
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_ROW_BANDS
#define WARP_CYLINDER
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// Same as the rdp block, wrap from the sampler state
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_CYLINDER
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The tint is folded into each band's own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_CYLINDER
#include "Include/TexturedBackgroundWarp.inc"
}
