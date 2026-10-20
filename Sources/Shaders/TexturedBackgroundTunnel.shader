program TexturedBackgroundTunnel;
variant DITHER;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
uniform vec4 uTunnel;

#define INV_PI 0.31830988618379067153776752675

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec3 voronoi(in vec2 p) {
	vec2 n = floor(p);
	vec2 f = fract(p);

	vec2 mg, mr;

	float md = 8.0;
	for (int j = -1; j <= 1; ++j) {
		for (int i = -1; i <= 1; ++i) {
			vec2 g = vec2(float(i), float(j));
			vec2 o = hash2D(n + g);

			vec2 r = g + o - f;
			float d = dot(r, r);

			if (d < md) {
				md = d;
				mr = r;
				mg = g;
			}
		}
	}
	return vec3(md, mr);
}

float addStarField(vec2 samplePosition, float threshold) {
	vec3 starValue = voronoi(samplePosition);
	if (starValue.x < threshold) {
		float power = 1.0 - (starValue.x / threshold);
		return min(power * power * power, 0.5);
	}
	return 0.0;
}

void fragment() {
	// Position of pixel relative to the center of the tunnel (between -1 and 1 across the screen for the center in the middle)
	vec2 targetCoord = vec2(2.0) * UV - vec2(2.0) * uTunnel.xy;

	// Aspect ratio correction, so display circle instead of ellipse
	targetCoord.x *= uViewSize.x / uViewSize.y;

	// Distance to center of the tunnel
	float distance = length(targetCoord);

	// x-coordinate of tunnel
	float xShift = (targetCoord.x == 0.0 ? sign(targetCoord.y) * 0.5 : atan(targetCoord.y, targetCoord.x) * INV_PI);

	// A spiral turns further around the deeper it goes, by a quarter of a turn each time the texture repeats into the
	// distance; chosen rather than multiplied, the depth is infinite in the very center
	float depth = (1.0 / distance) * 1.4;
	vec2 texturePos = vec2(
		(xShift)         * 1.0 + (uShift.x * 0.01) + (uTunnel.w > 0.0 ? depth * 0.5 : 0.0),
		depth                  + (uShift.y * 0.002)
	);

	vec4 texColor = texture(TEXTURE, texturePos);

#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif

#if SOFTWARE_RENDERER
	// Software-renderer variant: the tunnel keeps its atan()-based warp geometry and the horizon
	// tint, but the per-pixel pow() curve, the dithering second texture sample and the voronoi
	// "star field" (dozens of sin() per pixel when uHorizonColor.w > 0) are far too slow on the
	// CPU, so the pow() is approximated polynomially and dither/stars are dropped entirely.
	float horizonOpacity = 1.0 - clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.4)
#elif LOW_POWER_GPU
	// See TexturedBackground.shader: sqrt() is one instruction where pow() is two transcendentals, and
	// 0.8 * d^1.5 + 0.2 * d tracks pow(d, 1.4) closely enough that the horizon keeps its shape
	float horizonOpacity = 1.0 - clamp(0.8 * (distance * sqrt(distance)) + 0.2 * distance - 0.3, 0.0, 1.0);
#else
	float horizonOpacity = 1.0 - clamp(pow(distance, 1.4) - 0.3, 0.0, 1.0);
#endif

	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING && !LOW_POWER_GPU
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));

		samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}
#endif

	// Without the fade, the circles go on into the center
	COLOR = mix(texColor, horizonColorWithStars, horizonOpacity * uTunnel.z);
	COLOR.a = 1.0;
}

void fixed_function(pvr, gs) {
	// The tunnel keeps the same planar geometry rebuild on this tier (shared
	// with TexturedBackground.shader), with the include taken exactly as it is for the same reasons as
	// there: the PVR and the GS have no combiner stage to fold the horizon tint into, so it is the
	// include's own second gradient pass, and the bands are cut into pieces at the texture's edges.
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE keeps the second gradient pass but wraps the render target in hardware, so each band is one
	// textured strip (see TexturedBackground.shader)
#define WARP_TEXTURE_REPEAT
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The same rebuild, but each band goes out as ONE strip: the RDP wraps texture coordinates in
	// hardware (a mask on the tile descriptor), and so does the PICA200 (the render target is a power of
	// two), so the band's unwrapped U span is sampled straight from the 256-texel row window instead of
	// being cut into up to eight pieces at the texture's edges. That is ~130 strips a frame down to ~64,
	// on CPUs where every strip costs more than the GPU's time to draw it. The horizon tint rides the
	// band's own strip as a per-vertex colour through the TINT_MIX combiner, exactly like the GX block:
	// one strip per band instead of a textured one and a gradient one, and no combiner change between them.
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
	// The tunnel borrows the same planar rebuild on this tier as well, with the
	// horizon tint folded into the band pieces' own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#include "Include/TexturedBackgroundWarp.inc"
}
