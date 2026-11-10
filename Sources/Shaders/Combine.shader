program Combine;
variant DITHER;

#include "Include/CombineVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);
uniform sampler2D uTextureBlurHalf : texture_unit(2);
uniform sampler2D uTextureBlurQuarter : texture_unit(3);

uniform vec4 uAmbientColor;
uniform float uTime;
// Only for the darkness above deep water (see Water.shader), 1 without water
uniform float uWaterLevel;
// See Include/WaterFs.inc
uniform vec4 uWavePhase;
// Used only by the fixed-function blocks
uniform vec2 uCameraPos;

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec2 noiseTexCoords(vec2 position) {
	vec2 seed = position + fract(uTime * 0.01);
	return clamp(position + hash2D(seed) * vViewSizeInv * 1.4, vec2(0.0), vec2(1.0));
}

// Interleaved gradient noise (Jimenez 2014), a cheaper hash without sin()
float interleavedGradientNoise(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Same +-1.4 px lightmap jitter as noiseTexCoords(); no clamp needed, the lightmap clamps to edge
vec2 lightingJitterLowPower() {
	vec2 seed = vPixelPos + fract(uTime * 7.31) * 512.0;
	vec2 offset = vec2(interleavedGradientNoise(seed), interleavedGradientNoise(seed + vec2(37.0, 17.0))) * 2.0 - 1.0;
	return offset * vViewSizeInv * 1.4;
}

#include "Include/WaterFs.inc"

#if DITHER
#include "Include/Dither565.inc"
#endif

void fragment() {
	vec4 blur1 = texture(uTextureBlurHalf, vTexCoords);
	vec4 blur2 = texture(uTextureBlurQuarter, vTexCoords);

	vec4 main = texture(uTexture, vTexCoords);
#if LOW_POWER_GPU
	// Cheaper jitter, the sin()-based hash is too slow on low-power GPUs
	vec4 light = texture(uTextureLighting, vTexCoords + lightingJitterLowPower());
#else
	vec4 light = texture(uTextureLighting, noiseTexCoords(vTexCoords));
#endif

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	float darknessStrength = 1.0 - light.r;
	if (uWaterLevel < 0.4) {
		// Darkness above deep water, following the wave of Water.shader
		float waveHeight = waterWave(waterWavePhases(vTexCoords.x));
		float isTexelAbove = clamp(0.5 - (vTexCoords.y - uWaterLevel - waveHeight) * vViewSize.y * 0.70710678, 0.0, 1.0);
		darknessStrength = min(1.0, darknessStrength + isTexelAbove * (0.4 - uWaterLevel));
	}

	// Brightness also adds white, like JJ2+ brightens dark colors more than a multiply would
	COLOR = mix(mix(
		main * (1.0 + 0.607 * light.g) + (0.2 * light.g + max(light.g - 0.7, 0.0)) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
	COLOR.a = 1.0;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	// Reduce to 16-bit colors like JJ2+ (HUD is drawn later)
	COLOR.rgb = reduceColors(COLOR.rgb, mod(gl_FragCoord.xy, 4.0));
#endif
}

// Bound to the CPU-lightmap pipeline by name; adds only the darkness above deep water (water is in Water.shader)
void fixed_function(gx, pica, gu, gs, legacygl) {
	pipeline lighting_combine;
#define WATER_NO_UNDERWATER
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(rdp) {
	pipeline lighting_combine;
#define WATER_STRIP_SEGMENTS 5
#define WATER_STRIPS 2
#define WATER_LITE
#define WATER_NO_UNDERWATER
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(pvr) {
	pipeline lighting_combine;
#define WATER_STRIP_SEGMENTS 3
#define WATER_STRIPS 9
#define WATER_NO_UNDERWATER
#include "Include/CombineWaterFixedFunction.inc"
}
