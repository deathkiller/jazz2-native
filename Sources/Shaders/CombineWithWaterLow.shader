program CombineWithWaterLow;

#include "Include/CombineVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);
uniform sampler2D uTextureBlurHalf : texture_unit(2);
uniform sampler2D uTextureBlurQuarter : texture_unit(3);

uniform vec4 uAmbientColor;
uniform float uTime;
uniform vec2 uCameraPos;
uniform float uWaterLevel;

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec2 noiseTexCoords(vec2 position) {
	vec2 seed = position + fract(uTime * 0.01);
	return clamp(position + hash2D(seed) * vViewSizeInv * 1.4, vec2(0.0), vec2(1.0));
}

// Low-power lighting jitter, see Combine.shader
float interleavedGradientNoise(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 lightingJitterLowPower() {
	vec2 seed = vPixelPos + fract(uTime * 7.31) * 512.0;
	vec2 offset = vec2(interleavedGradientNoise(seed), interleavedGradientNoise(seed + vec2(37.0, 17.0))) * 2.0 - 1.0;
	return offset * vViewSizeInv * 1.4;
}

// sin(2 * pi * cycles) as a parabola over the range-reduced phase, refined to within 0.001 of the real
// curve - a few multiply-adds, where sin() is one of the most expensive things a USSE-class part runs
float sinCycles(float cycles) {
	float v = fract(cycles + 0.5) * 2.0 - 1.0;
	float y = 4.0 * v * (1.0 - abs(v));
	return y * (0.775 + 0.225 * abs(y));
}

void fragment() {
	vec3 waterColor = vec3(0.4, 0.6, 0.8);

	vec2 uvLocal = vTexCoords;
	vec2 uvWorldCenter = (uCameraPos.xy * vViewSizeInv.xy);
	vec2 uvWorld = uvLocal + uvWorldCenter;

	float isTexelBelow = 1.0 - step(uvLocal.y, uWaterLevel);
	float isTexelAbove = 1.0 - isTexelBelow;

#if LOW_POWER_GPU
	// Low-power-GPU variant (the PS Vita's sceGxm and the OpenGL|ES 2.0 profile): sin() as a polynomial,
	// and no clamp - the view is sampled with clamp-to-edge anyway
	vec2 uv = uvLocal + vec2(0.008 * sinCycles((uTime * 16.0 + uvWorld.y * 20.0) * 0.15915494) * isTexelBelow, 0.0);
#else
	vec2 uv = clamp(uvLocal + vec2(0.008 * sin(uTime * 16.0 + uvWorld.y * 20.0) * isTexelBelow, 0.0), vec2(0.0), vec2(1.0));
#endif
	vec4 main = texture(uTexture, uv);

	// Waves
	float topDist = abs(uvLocal.y - uWaterLevel);
	float topGradient = max(1.0 - topDist, 0.0);
	float isNearTop = 0.2 * topGradient * topGradient;
	float isVeryNearTop = 1.0 - step(vViewSizeInv.y, topDist);
	main.rgb = mix(main.rgb, waterColor, vec3(isTexelBelow * 0.4)) + vec3((isNearTop + 0.2 * isVeryNearTop) * isTexelBelow);

	// Lighting
#if LOW_POWER_GPU
	// The blurred levels and the lightmap are read at the undisplaced position: a few pixels of ripple are
	// invisible in either, and a coordinate taken straight from an interpolant lets the blur reads be issued
	// before the program even runs, rather than as dependent reads. With the lightmap jitter from
	// Combine.shader and the sine above, this pass costs 3.2 ms over a plain copy on the SGX543 at 480x272
	// instead of 5.3.
	vec4 blur1 = texture(uTextureBlurHalf, uvLocal);
	vec4 blur2 = texture(uTextureBlurQuarter, uvLocal);
	vec4 light = texture(uTextureLighting, uvLocal + lightingJitterLowPower());
#else
	vec4 blur1 = texture(uTextureBlurHalf, uv);
	vec4 blur2 = texture(uTextureBlurQuarter, uv);
	vec4 light = texture(uTextureLighting, noiseTexCoords(uv));
#endif

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	float darknessStrength = (1.0 - light.r);

	// Darkness above water
	if (uWaterLevel < 0.4) {
		float aboveWaterDarkness = isTexelAbove * (0.4 - uWaterLevel);
		darknessStrength = min(1.0, darknessStrength + aboveWaterDarkness);
	}

	COLOR = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
	COLOR.a = 1.0;
}

void fixed_function(gx, pica, gu, gs, legacygl) {
	// Water at the fidelity this tier can afford, rebuilt out of geometry over the composited scene - the
	// HIGH-quality variant carries the same blocks (see the include)
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(rdp) {
	// The light variant with 10 columns along the wave: every triangle costs this CPU ~25 us to set up
	// for the RSP, against a few for a whole full-screen rectangle (see the include)
#define WATER_STRIP_SEGMENTS 5
#define WATER_STRIPS 2
#define WATER_LITE
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(pvr) {
	// The same water, cut into the shorter strips the PVR's 8-vertex scratch holds
#define WATER_STRIP_SEGMENTS 3
#define WATER_STRIPS 9
#include "Include/CombineWaterFixedFunction.inc"
}
