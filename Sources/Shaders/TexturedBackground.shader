program TexturedBackground;
variant DITHER;
variant KEYED;
// JJ2+ perspective, the shift is the layer pixel at the top left of the view like an ordinary layer
variant PLUS;
variant PLUS + DITHER;
variant PLUS + KEYED;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
// xy: vanishing point and horizon offset from the quad's middle (fraction of it), z: 1 to skip the horizon fade
// (JJ2+ warpHorizon), w: unused (vec4 for the fixed-function rebuild)
uniform vec4 uHorizonOffset;

#include "Include/TexturedBackgroundKey.inc"
#include "Include/TexturedBackgroundDither.inc"

// JJ2+ perspective: a row d pixels from the horizon is 15360 / d texels deep and 60 / d texels per pixel wide;
// the planes would meet 8 rows past the horizon and fade into the horizon color within 141 rows of it
#define PLUS_DEPTH 15360.0
#define PLUS_SCALE 60.0
#define PLUS_GAP 8.0
#define PLUS_FADE_ROWS 141.0
#if LOW_POWER_GPU

// Low-power GPUs: the terms linear across the quad are done per vertex, the fragment only does the curves
// Legacy: row from the horizon towards the top edge, towards the bottom edge, horizontal term
// JJ2+: row from the horizon, the same in fade rows, horizontal term; w: opacity
varying vec4 vWarpTerms;

void vertex() {
#if PLUS
	vec2 pixel = UV * uViewSize - vec2(0.5);
	float row = pixel.y - floor((0.5 + uHorizonOffset.y) * uViewSize.y);
	vWarpTerms = vec4(row, row / PLUS_FADE_ROWS, (pixel.x - (0.5 + uHorizonOffset.x) * uViewSize.x) * (PLUS_SCALE / 256.0), COLOR.a);
#else
	float horizonY = clamp(0.5 + uHorizonOffset.y, 0.0, 1.0);
	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));
	vWarpTerms = vec4((horizonY - UV.y) / max(horizonY, 0.0001), (UV.y - horizonY) / max(1.0 - horizonY, 0.0001),
		(UV.x - 0.5 - uHorizonOffset.x) * correction, COLOR.a);
#endif
}
#endif

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
// JJ2+ perspective in view pixels, the horizon row belongs to the plane below; the texture sits half its size
// below the shift and V runs up the layer. xy: texture position, z: horizon fade
vec3 plusWarp(vec2 uv) {
	vec2 pixel = uv * uViewSize - vec2(0.5);
	float row = pixel.y - floor((0.5 + uHorizonOffset.y) * uViewSize.y);
	float below = step(0.0, row);
	float distance = abs(row) + PLUS_GAP;
	vec2 texel = vec2(
		uShift.x + (pixel.x - (0.5 + uHorizonOffset.x) * uViewSize.x) * PLUS_SCALE / distance,
		uShift.y + 128.0 + (1.0 - 2.0 * below) * PLUS_DEPTH / distance
	);
	float rows = row / PLUS_FADE_ROWS;
	return vec3(texel.x / 256.0, 1.0 - texel.y / 256.0, clamp(1.0 - rows * rows, 0.0, 1.0));
}
#else
// Our own approximation of the original sky: xy: texture position, z: horizon fade
vec3 legacyWarp(vec2 uv) {
	// fromHorizon: 0 at the horizon, 1 at the top/bottom edge; distance: 1.3 at the horizon, 0.3 at the edge
	float horizonY = clamp(0.5 + uHorizonOffset.y, 0.0, 1.0);
	float aboveHorizon = (uv.y <= horizonY ? 1.0 : 0.0);
	float fromHorizon = (aboveHorizon > 0.5 ? (horizonY - uv.y) / max(horizonY, 0.0001) : (uv.y - horizonY) / max(1.0 - horizonY, 0.0001));
	float distance = 1.3 - fromHorizon;
#if SOFTWARE_RENDERER
	// Software renderer: polynomials instead of pow(), no dither sample or star field (too slow on the CPU)
	float horizonDepth = distance;	// Approximates pow(distance, 1.4)
#else
	float horizonDepth = pow(distance, 1.4);
#endif

	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));

	// Above the horizon the texture runs up, below it down
	vec2 texturePos = vec2(
		(uShift.x / 256.0) + (uv.x - 0.5 - uHorizonOffset.x) * (0.5 + (1.5 * horizonDepth)) * correction,
		(uShift.y / 256.0) + (2.0 * aboveHorizon - 1.0) * 0.5 * (1.0 - fromHorizon) * 1.4 * distance
	);

#if SOFTWARE_RENDERER
	float horizonOpacity = clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.5)
#else
	float horizonOpacity = clamp(pow(distance, 1.5) - 0.3, 0.0, 1.0);
#endif
	return vec3(texturePos, horizonOpacity);
}
#endif

void fragment() {
	// Opacity of screened or multiplied layers, from the instance color alpha
#if LOW_POWER_GPU
	float opacity = vWarpTerms.w;
#else
	float opacity = COLOR.a;
#endif
#if LOW_POWER_GPU
#if PLUS
	// Distance is |row| + PLUS_GAP; from the horizon row down the texture runs down
	float qInv = 1.0 / (abs(vWarpTerms.x) + PLUS_GAP);
	float side = (vWarpTerms.x >= 0.0 ? 1.0 : -1.0);
	vec3 warp = vec3(
		(uShift.x / 256.0) + vWarpTerms.z * qInv,
		1.0 - (uShift.y + 128.0) / 256.0 + side * PLUS_SCALE * qInv,
		clamp(1.0 - vWarpTerms.y * vWarpTerms.y, 0.0, 1.0)
	);
#else
	// m: row from the horizon; d * sqrt(d) is d^1.5 without pow(), blended with d it's within 0.007 of pow(d, 1.4)
	// over [0.3, 1.3]. Above the horizon the texture runs up
	float m = max(vWarpTerms.x, vWarpTerms.y);
	float distance = 1.3 - m;
	float distancePow15 = distance * sqrt(distance);
	float side = (vWarpTerms.x >= 0.0 ? 1.0 : -1.0);
	vec3 warp = vec3(
		(uShift.x / 256.0) + vWarpTerms.z * (0.5 + 1.2 * distancePow15 + 0.3 * distance),
		(uShift.y / 256.0) + side * 0.7 * (1.0 - m) * distance,
		clamp(distancePow15 - 0.3, 0.0, 1.0)
	);
#endif
#else
#if PLUS
	vec3 warp = plusWarp(UV);
#else
	vec3 warp = legacyWarp(UV);
#endif
#endif
	vec2 texturePos = warp.xy;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	vec4 texColor = textureTexel(texturePos);
#else
	vec4 texColor = texture(TEXTURE, texturePos);
#endif
	float horizonOpacity = warp.z;

	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING && !LOW_POWER_GPU
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));

		samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}
#endif

	COLOR = mix(texColor, horizonColorWithStars, horizonOpacity * (1.0 - uHorizonOffset.z));
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
	// Banded rebuild of the warp: no lerp toward a color here, so the tint is a second gradient pass, and no wrap
	// on this target, so bands are cut at texture edges. JJ2+ perspective uses the vertex depth (PVR 1/w, GS Q)
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE wraps the target, so each band is one strip, but the tint stays a second pass (GU_TFX_BLEND weighs
	// by texel); vertices are in screen space, so JJ2+ perspective is drawn in coarse bands
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The RDP wraps in hardware, so each band is one strip, with the tint in the vertex color via TINT_MIX
	// JJ2+ perspective: a 4-vertex band is drawn row by row exactly, so it's cut only at texture and tint edges
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// Same as the rdp block, wrap from the sampler state; colors interpolate in perspective too, so tint bands
	// stay short where the depth changes a lot
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#define WARP_COLOR_IN_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The tint is folded into each band's draw via TINT_MIX instead of a second pass; JJ2+ perspective uses
	// the q of a projective texture coordinate
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}
