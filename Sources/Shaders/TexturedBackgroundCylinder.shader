program TexturedBackgroundCylinder;
variant DITHER;
variant KEYED;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
// The cylinder of JJ2+ (see Tiles::LayerRendererType::Cylinder) - xy: the center of its horizon as a fraction of the view,
// z: 1 if it fades into the horizon, w: 1 if it's drawn at half size. It's always drawn in the perspective of JJ2+, the shift
// is the pixel of the layer at the top left of the view, as an ordinary layer would be placed.
uniform vec4 uCylinder;

#include "Include/TexturedBackgroundKey.inc"

// The cylinder of JJ2+, measured in it: the texture once from the top of the view to the bottom (twice at half size), a row
// an angle of half a turn over the view from the horizon, across which the texture is 1/128 of the view's width per pixel
// times 2 + cos of the angle (2 * (1 + 2 cos) at half size). It fades into the horizon color like the sky, to 65 % of it.
#define FADE_ROWS 141.0
#define FADE_STRENGTH 0.65
#define PI 3.14159265359

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

void fragment() {
	// How opaque a layer drawn screened or multiplied is, the alpha of the instance color (see TileMap::RenderTexturedBackground())
	float opacity = COLOR.a;

	// By the pixels of the view as JJ2+ counts them - the texture runs from the middle of the view down wherever the horizon is,
	// the shift moves it along the tube a quarter as fast as an ordinary layer and around it as fast. V runs up the layer (see
	// TileMap::TexturedBackgroundPass).
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
	vec4 texColor = texture(TEXTURE, texturePos);

#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif

	COLOR = mix(texColor, vec4(uHorizonColor.xyz, 1.0), fade * uCylinder.z);
#if KEYED
	COLOR.a = keyedOpacity();
#else
	// Premultiplied, which the blending of the sprite modes takes as it is
	COLOR = vec4(COLOR.rgb * opacity, opacity);
#endif
}

void fixed_function(pvr, gs) {
	// The cylinder is rebuilt in bands on this tier by the include shared with the sky and the tunnel (see
	// TexturedBackground.shader), taken exactly as it is for the same reasons as there: the PVR and the GS have
	// no combiner stage to fold the horizon tint into, so it is the include's own second gradient pass, and the
	// bands are cut into pieces at the texture's edges. It's cut coarsely, every vertex counts on the Dreamcast
	// and the PS2.
#define WARP_LOW_DETAIL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE keeps the second gradient pass but wraps the render target in hardware, so each band is one
	// textured strip (see TexturedBackground.shader). It's cut coarsely, every vertex counts on the PSP.
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The same rebuild with each band as ONE strip, the RDP wrapping texture coordinates in hardware, and the
	// horizon tint riding the band's own strip as a per-vertex colour through the TINT_MIX combiner (see
	// TexturedBackground.shader). It's cut coarsely, every vertex counts on the N64 - a band is a rectangle of
	// 4 vertices, which the RDP draws row by row, each row exactly as it maps.
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_ROW_BANDS
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// The rdp block's rebuild exactly, with the wrap taken from the render target's sampler state
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The same banded rebuild, with the horizon tint folded into the band pieces' own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#include "Include/TexturedBackgroundWarp.inc"
}
