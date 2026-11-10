program TexturedBackgroundTunnel;
variant DITHER;
variant KEYED;
// JJ2+ tunnel, the shift is the layer pixel at the top left of the view like an ordinary layer
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
#include "Include/TexturedBackgroundDither.inc"

#define INV_PI 0.31830988618379067153776752675

// JJ2+ tunnel: the texture goes once around the center and repeats into the distance each time the radius halves;
// the spiral turns it by 356 / r^0.15 - 91 texels at r pixels, the fade starts 87 pixels out and floors at 4 %
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
// JJ2+ tunnel in view pixels, angle clockwise from the right; the shift moves around at 1/4 speed and into the
// distance at full speed, V runs up the layer. xy: texture position, z: horizon fade
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
// Legacy tunnel: xy: texture position, z: horizon fade
vec3 legacyTunnel(vec2 uv) {
	// Position from the tunnel center, -1 to 1 across the screen when centered
	vec2 targetCoord = vec2(2.0) * uv - vec2(2.0) * uTunnel.xy;

	// Aspect correction, a circle instead of an ellipse
	targetCoord.x *= uViewSize.x / uViewSize.y;

	float distance = length(targetCoord);

	// Angle around the center, -1 to 1
	float xShift = (targetCoord.x == 0.0 ? sign(targetCoord.y) * 0.5 : atan(targetCoord.y, targetCoord.x) * INV_PI);

	// The spiral turns a quarter per texture repeat into the distance; not multiplied, the depth is infinite at the center
	float depth = (1.0 / distance) * 1.4;
	vec2 texturePos = vec2(
		(xShift)         * 1.0 + (uShift.x * 0.01) + (uTunnel.w > 0.0 ? depth * 0.5 : 0.0),
		depth                  + (uShift.y * 0.002)
	);

#if SOFTWARE_RENDERER
	// Software renderer: a polynomial instead of pow(), no dither sample or star field (too slow on the CPU)
	float horizonOpacity = 1.0 - clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.4)
#elif LOW_POWER_GPU
	// 0.8 * d^1.5 + 0.2 * d is close to pow(d, 1.4) without pow()
	float horizonOpacity = 1.0 - clamp(0.8 * (distance * sqrt(distance)) + 0.2 * distance - 0.3, 0.0, 1.0);
#else
	float horizonOpacity = 1.0 - clamp(pow(distance, 1.4) - 0.3, 0.0, 1.0);
#endif
	return vec3(texturePos, horizonOpacity);
}
#endif

void fragment() {
	// Opacity of screened or multiplied layers, from the instance color alpha
	float opacity = COLOR.a;
#if PLUS
	vec3 tunnel = plusTunnel(UV);
#else
	vec3 tunnel = legacyTunnel(UV);
#endif
	vec2 texturePos = tunnel.xy;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	vec4 texColor = textureTexel(texturePos);
#else
	vec4 texColor = texture(TEXTURE, texturePos);
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
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE wraps the target, so each band is one strip; the tint stays a second pass, cut coarsely
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The RDP wraps in hardware, so each band is one strip, with the tint in the vertex color via TINT_MIX
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// Same as the rdp block, wrap from the sampler state
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The tint is folded into each band's own draw (see the include)
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_TUNNEL
#include "Include/TexturedBackgroundWarp.inc"
}
