program CombineWithWater;
variant DITHER;
precision highp;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);
uniform sampler2D uTextureBlurHalf : texture_unit(2);
uniform sampler2D uTextureBlurQuarter : texture_unit(3);
uniform sampler2D uTextureNoise : texture_unit(4);

uniform vec4 uAmbientColor;
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

varying vec2 vTexCoords;
varying vec2 vViewSize;
varying vec2 vViewSizeInv;
#if LOW_POWER_GPU
// Linear terms computed per vertex so texture reads aren't dependent
varying vec2 vLightingSeed;
varying vec2 vDisplacementPos;
varying vec2 vRaysPos;
varying vec4 vWavePhases;
varying vec2 vConstants;
varying float vRipple;
#endif

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vViewSize = spriteSize;
	vViewSizeInv = vec2(1.0) / spriteSize;
#if LOW_POWER_GPU
	vec2 uvLocal = vTexCoords;
	vLightingSeed = aPosition * spriteSize + fract(uTime * 7.31) * 512.0;
	vDisplacementPos = uvLocal * 0.1 + uWaterBase.xy;
	// Rays come from the blue channel of the noise texture
	float raysLinear = uvLocal.x * (1.4 + spriteSize.x * (6.0 / 720.0)) + uvLocal.y * spriteSize.y * (5.0 / 720.0);
	vRaysPos = vec2(raysLinear * 0.125 + uRaysBase, uWaterBase.w);
	vWavePhases = uWavePhase + uvLocal.x * vec4(9.5492966, 3.1830989, 5.5704230, 11.140846);
	vRipple = uWaterBase.z + uvLocal.y * 3.1830989;
	// x: blur blend scale, y: edge anti-aliasing slope in rows
	vConstants = vec2(1.0 / sqrt(max(uAmbientColor.w, 0.35)), 0.70710678 * spriteSize.y);
#endif
}

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
	vec2 offset = vec2(interleavedGradientNoise(vLightingSeed), interleavedGradientNoise(vLightingSeed + vec2(37.0, 17.0))) * 2.0 - 1.0;
	return offset * vViewSizeInv * 1.4;
}

#include "Include/WaterFs.inc"

#if DITHER
#include "Include/Dither565.inc"
#endif

void fragment() {
	vec2 uvLocal = vTexCoords;
	float isTexelBelow;
	vec2 uv;
#if LOW_POWER_GPU
	vec3 main = waterScene(uvLocal, waterWave(vWavePhases), vConstants.y, vViewSizeInv.y, vDisplacementPos, vRipple, vRaysPos, isTexelBelow, uv);
	// Blur and lightmap are read undisplaced, the ripple isn't visible in them
	vec4 blur1 = texture(uTextureBlurHalf, uvLocal);
	vec4 blur2 = texture(uTextureBlurQuarter, uvLocal);
	vec4 light = texture(uTextureLighting, uvLocal + lightingJitterLowPower());
	float blurScale = vConstants.x;
#else
	vec3 main = waterScene(uvLocal, waterWave(waterWavePhases(uvLocal.x)), 0.70710678 * vViewSize.y, vViewSizeInv.y,
		uvLocal * 0.1 + uWaterBase.xy, uWaterBase.z + uvLocal.y * 3.1830989, waterRaysPos(uvLocal, vViewSizeInv), isTexelBelow, uv);
	vec4 blur1 = texture(uTextureBlurHalf, uv);
	vec4 blur2 = texture(uTextureBlurQuarter, uv);
	vec4 light = texture(uTextureLighting, noiseTexCoords(uv));
	float blurScale = 1.0 / sqrt(max(uAmbientColor.w, 0.35));
#endif

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	// Includes darkness above deep water
	float darknessStrength = min(1.0, (1.0 - light.r) + (1.0 - isTexelBelow) * max(0.4 - uWaterLevel, 0.0));

	COLOR = mix(mix(
		vec4(main, 1.0) * (1.0 + 0.607 * light.g) + (0.2 * light.g + max(light.g - 0.7, 0.0)) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) * blurScale, 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
	COLOR.a = 1.0;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	// Reduce to 16-bit colors like JJ2+ (HUD is drawn later)
	COLOR.rgb = reduceColors(COLOR.rgb, mod(gl_FragCoord.xy, 4.0));
#endif
}

void fixed_function(gx, pica, gu, gs, legacygl) {
	// Water built from geometry over the composited scene
	pipeline lighting_combine;
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(rdp) {
	// Lite variant with fewer triangles, they are expensive to set up here
	pipeline lighting_combine;
#define WATER_STRIP_SEGMENTS 5
#define WATER_STRIPS 2
#define WATER_LITE
#include "Include/CombineWaterFixedFunction.inc"
}

void fixed_function(pvr) {
	// Shorter strips to fit the 8-vertex buffer
	pipeline lighting_combine;
#define WATER_STRIP_SEGMENTS 3
#define WATER_STRIPS 9
#include "Include/CombineWaterFixedFunction.inc"
}
