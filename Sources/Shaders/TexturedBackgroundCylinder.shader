program TexturedBackgroundCylinder;
variant DITHER;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
// The cylinder of JJ2+ (see Tiles::LayerRendererType::Cylinder) - xy: the center of its horizon as a fraction of the view,
// z: 1 if it fades into the horizon, w: 1 if it's drawn at half size
uniform vec4 uCylinder;

// How far up or down from the horizon the fade reaches, as an angle around the axis of the tube, and how strong it is there
#define FADE_ANGLE 0.58
#define FADE_STRENGTH 0.75

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

void fragment() {
	// The inside of a horizontal tube, seen from its axis looking at the far wall: a row of the view is an angle around the axis,
	// up or down from the horizon, a column a distance along it. The wall is as far away everywhere, but the rows further from
	// the horizon see it at a steeper angle, so the texture gets larger towards the top and the bottom of the view.
	// Half size sees more of the tube, more curved, with the texture four times across the view instead of once.
	float fov = (uCylinder.w > 0.0 ? 2.0 : 1.0);
	float repeats = (uCylinder.w > 0.0 ? 4.0 : 1.0);

	// Position of the pixel relative to the center of the horizon, in pixels of the view - right of it, and up from it
	float along = (UV.x - uCylinder.x) * uViewSize.x;
	float up = (uCylinder.y - UV.y) * uViewSize.y;

	// The angle around the axis, the edge of the view at the horizon in the center is at atan(fov)
	float slope = up * fov / (0.5 * uViewSize.y);
	float angle = atan(slope, 1.0);
	float cosAngle = inversesqrt(1.0 + slope * slope);

	// The texture as square at the horizon as it is, along the tube and around it (V runs up the layer), the shift moves
	// along it and turns it
	vec2 texturePos = vec2(
		repeats * along * cosAngle / uViewSize.x + (uShift.x / 256.0),
		repeats * angle * uViewSize.y / (2.0 * fov * uViewSize.x) - (uShift.y / 256.0)
	);

	vec4 texColor = texture(TEXTURE, texturePos);

#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif

	// Fainter than the sky, only the far wall fades into the color of the horizon, never fully
	float fadeOpacity = clamp(1.0 - (angle * angle) / (FADE_ANGLE * FADE_ANGLE), 0.0, 1.0) * FADE_STRENGTH;

	COLOR = mix(texColor, vec4(uHorizonColor.xyz, 1.0), fadeOpacity * uCylinder.z);
	COLOR.a = 1.0;
}

void fixed_function(pvr, gs) {
	// The cylinder keeps the planar geometry rebuild of the sky on this tier, as the tunnel does (see
	// TexturedBackground.shader), with the include taken exactly as it is for the same reasons as there:
	// the PVR and the GS have no combiner stage to fold the horizon tint into, so it is the include's own
	// second gradient pass, and the bands are cut into pieces at the texture's edges.
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE keeps the second gradient pass but wraps the render target in hardware, so each band is one
	// textured strip (see TexturedBackground.shader)
#define WARP_TEXTURE_REPEAT
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The same rebuild with each band as ONE strip, the RDP wrapping texture coordinates in hardware, and the
	// horizon tint riding the band's own strip as a per-vertex colour through the TINT_MIX combiner (see
	// TexturedBackground.shader)
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// The rdp block's rebuild exactly, with the wrap taken from the render target's sampler state
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The cylinder borrows the same planar rebuild on this tier as well, with the horizon tint folded into
	// the band pieces' own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#include "Include/TexturedBackgroundWarp.inc"
}
