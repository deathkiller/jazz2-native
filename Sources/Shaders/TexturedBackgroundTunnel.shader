program TexturedBackgroundTunnel;
variant DITHER;
variant KEYED;
// The tunnel of JJ2+ (SURFACE::FULLSCREEN and the inner surfaces), where the shift is the pixel of the layer at the top left
// of the view, as an ordinary layer would be placed - without it, the tunnel is drawn as it always was
variant PLUS;
variant PLUS + DITHER;
variant PLUS + KEYED;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
uniform vec4 uTunnel;

#include "Include/TexturedBackgroundKey.inc"

#define INV_PI 0.31830988618379067153776752675

// The tunnel of JJ2+, measured in it: the texture once around the center, and once more into the distance each time the
// distance from the center halves, so it never ends. Its spiral turns it by 356 / r^0.15 - 91 pixels of the texture at r
// pixels from the center, it fades into the horizon color from 87 pixels away from the center down to 4 % of it further out.
#define PLUS_SPIRAL_SCALE 356.2
#define PLUS_SPIRAL_POWER -0.15
#define PLUS_SPIRAL_OFFSET -91.0
#define PLUS_FADE_RADIUS 87.0
#define PLUS_FADE_FLOOR 0.0414

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

#if PLUS
// The tunnel of JJ2+, by the pixels of the view as it counts them, the angle clockwise from the right - the shift moves the
// texture around a quarter as fast as an ordinary layer and into the distance as fast. V runs up the layer (see
// TileMap::TexturedBackgroundPass). The spiral's power of the distance comes from the logarithm the depth needs anyway.
// xy the position in the texture, z how far it fades into the color of the horizon
vec3 plusTunnel(vec2 uv) {
	vec2 fromCenter = uv * uViewSize - vec2(0.5) - vec2(uTunnel.x * uViewSize.x, floor(uTunnel.y * uViewSize.y));
	float radius = max(length(fromCenter), 0.5);
	float angle = (fromCenter.x == 0.0 ? sign(fromCenter.y) * 1.5707963 : atan(fromCenter.y, fromCenter.x));
	float logRadius = log2(radius);
	vec2 texel = vec2(
		uShift.x * 0.25 + 64.0 + angle * (128.0 * INV_PI) +
			(uTunnel.w > 0.0 ? PLUS_SPIRAL_SCALE * exp2(PLUS_SPIRAL_POWER * logRadius) + PLUS_SPIRAL_OFFSET : 0.0),
		uShift.y - 256.0 * logRadius
	);
	float fade = PLUS_FADE_RADIUS / radius;
	return vec3(texel.x / 256.0, 1.0 - texel.y / 256.0, clamp(fade * fade - PLUS_FADE_FLOOR, 0.0, 1.0));
}
#else
// The tunnel as it was always drawn: xy the position in the texture, z how far it fades into the color of the horizon
vec3 legacyTunnel(vec2 uv) {
	// Position of pixel relative to the center of the tunnel (between -1 and 1 across the screen for the center in the middle)
	vec2 targetCoord = vec2(2.0) * uv - vec2(2.0) * uTunnel.xy;

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
	return vec3(texturePos, horizonOpacity);
}
#endif

void fragment() {
	// How opaque a layer drawn screened or multiplied is, the alpha of the instance color (see TileMap::RenderTexturedBackground())
	float opacity = COLOR.a;
#if PLUS
	vec3 tunnel = plusTunnel(UV);
#else
	vec3 tunnel = legacyTunnel(UV);
#endif
	vec2 texturePos = tunnel.xy;
	vec4 texColor = texture(TEXTURE, texturePos);

#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif
	float horizonOpacity = tunnel.z;

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
#if KEYED
	COLOR.a = keyedOpacity();
#else
	// Premultiplied, which the blending of the sprite modes takes as it is
	COLOR = vec4(COLOR.rgb * opacity, opacity);
#endif
}

void fixed_function(pvr, gs) {
	// The tunnel keeps the same planar geometry rebuild on this tier (shared
	// with TexturedBackground.shader), with the include taken exactly as it is for the same reasons as
	// there: the PVR and the GS have no combiner stage to fold the horizon tint into, so it is the
	// include's own second gradient pass, and the bands are cut into pieces at the texture's edges. The tunnel
	// of JJ2+ is cut coarsely, every vertex counts on the Dreamcast and the PS2.
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE keeps the second gradient pass but wraps the render target in hardware, so each band is one
	// textured strip (see TexturedBackground.shader). The tunnel of JJ2+ is cut coarsely, every vertex
	// counts on the PSP.
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
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
	// The tunnel of JJ2+ is cut coarsely, every vertex counts on the N64.
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// The rdp block's rebuild exactly, with the wrap taken from the render target's sampler state
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The tunnel borrows the same planar rebuild on this tier as well, with the
	// horizon tint folded into the band pieces' own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}
