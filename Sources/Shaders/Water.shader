program Water;
variant LOW;

shader_type canvas_item;
precision highp;

// Water in front of a layer behind the others, drawn mid-scene over a copy of what's behind it, without lighting -
// Combine.shader adds lighting and the darkness above deep water later

// 1 / size of the render target (and of the copy)
uniform vec2 uTargetSizeInv;
// 1 / size of the view, the unit of the wave, ripple and rays
uniform vec2 uViewSizeInv;
uniform float uTime;
uniform vec2 uCameraPos;
uniform float uWaterLevel;
// Colors of the water at its surface and at the bottom of the view
uniform vec4 uWaterColorTop;
uniform vec4 uWaterColorBottom;
// See Include/WaterFs.inc
uniform vec4 uWavePhase;
uniform vec4 uWaterBase;
uniform float uRaysBase;
// Used only by the fixed-function blocks to fade the rays
uniform vec4 uAmbientColor;

uniform sampler2D uTexture : texture_unit(0);
// Not read by LOW
uniform sampler2D uTextureNoise : texture_unit(1);

#include "Include/WaterFs.inc"

void fragment() {
	vec2 uvLocal = gl_FragCoord.xy * uTargetSizeInv;
	float waveHeight = waterWave(waterWavePhases(uvLocal.x));
	float rowSlope = 0.70710678 / uViewSizeInv.y;
	float rippleCycles = uWaterBase.z + uvLocal.y * 3.1830989;
	float isTexelBelow;
	vec2 uv;
#if LOW
	vec3 main = waterSceneLow(uvLocal, waveHeight, rowSlope, uViewSizeInv.y, rippleCycles, isTexelBelow, uv);
#else
#if LOW_POWER_GPU
	float raysLinear = uvLocal.x * (1.4 + 6.0 / (720.0 * uViewSizeInv.x)) + uvLocal.y * (5.0 / (720.0 * uViewSizeInv.y));
	vec2 raysPos = vec2(raysLinear * 0.125 + uRaysBase, uWaterBase.w);
#else
	vec2 raysPos = waterRaysPos(uvLocal, uViewSizeInv);
#endif
	vec3 main = waterScene(uvLocal, waveHeight, rowSlope, uViewSizeInv.y, uvLocal * 0.1 + uWaterBase.xy, rippleCycles, raysPos, isTexelBelow, uv);
#endif
	COLOR = vec4(main, 1.0);
}

// Fixed-function tiers draw only the underwater part over the scene so far
void fixed_function(gx, pica, gu, gs, legacygl) {
#define WATER_NO_DARKNESS
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(rdp) {
#define WATER_STRIP_SEGMENTS 5
#define WATER_STRIPS 2
#define WATER_LITE
#define WATER_NO_DARKNESS
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(pvr) {
#define WATER_STRIP_SEGMENTS 3
#define WATER_STRIPS 9
#define WATER_NO_DARKNESS
#include "Include/CombineWaterFixedFunction.inc"
}
