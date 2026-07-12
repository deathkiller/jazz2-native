program CombineWithWater;
precision highp;

#include "Include/CombineVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);
uniform sampler2D uTextureBlurHalf : texture_unit(2);
uniform sampler2D uTextureBlurQuarter : texture_unit(3);
uniform sampler2D uTextureNoise : texture_unit(4);

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

float wave(float x, float time) {
	float waveOffset = cos((x - time) * 60.0) * 0.004
						+ cos((x - 2.0 * time) * 20.0) * 0.008
						+ sin((x + 2.0 * time) * 35.0) * 0.01
						+ cos((x + 4.0 * time) * 70.0) * 0.001;
	return waveOffset * 0.4;
}

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

// Simplex Noise
vec3 permute(vec3 x) {
	return mod(((x*34.0)+1.0)*x, 289.0);
}

float snoise(vec2 v) {
	const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
	vec2 i = floor(v + dot(v, C.yy));
	vec2 x0 = v - i + dot(i, C.xx);
	vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
	vec4 x12 = x0.xyxy + C.xxzz;
	x12.xy -= i1;
	i = mod(i, 289.0);
	vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0 ));
	vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw, x12.zw)), 0.0);
	m = m * m;
	m = m * m;
	vec3 x = 2.0 * fract(p * C.www) - 1.0;
	vec3 h = abs(x) - 0.5;
	vec3 ox = floor(x + 0.5);
	vec3 a0 = x - ox;
	m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
	vec3 g;
	g.x = a0.x * x0.x + h.x * x0.y;
	g.yz = a0.yz * x12.xz + h.yz * x12.yw;
	return 130.0 * dot(m, g);
}

void fragment() {
	vec3 waterColor = vec3(0.4, 0.6, 0.8);

	vec2 uvLocal = vTexCoords;
	vec2 uvWorldCenter = (uCameraPos.xy * vViewSizeInv.xy);
	vec2 uvWorld = uvLocal + uvWorldCenter;

	float waveHeight = wave(uvWorld.x, uTime);
	float isTexelBelow = aastep(waveHeight, uvLocal.y - uWaterLevel);
	float isTexelAbove = 1.0 - isTexelBelow;

	// Displacement
	vec2 disPos = uvWorld * vec2(0.1) + vec2(mod(uTime * 0.4, 2.0));
	vec2 dis = (texture(uTextureNoise, disPos).xy - vec2(0.5)) * vec2(0.01);

	vec2 uv = clamp(uvLocal + (vec2(0.004 * sin(uTime * 16.0 + uvWorld.y * 20.0), 0.0) + dis) * vec2(isTexelBelow), vec2(0.0), vec2(1.0));
	vec4 main = texture(uTexture, uv);

	// Chromatic Aberration
	float aberration = abs(uvLocal.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main.rgb = mix(main.rgb, waterColor * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));

	// Rays
	vec2 uvNormalized = mod(uvLocal / vViewSizeInv, 720.0) / 720.0;
	float noisePos = uvWorldCenter.x * 6.0 + uvLocal.x * 1.4 + uvWorldCenter.y * 0.5 + (1.0 - uvNormalized.x * 1.2 - uvNormalized.y) * -5.0;
	float rays = snoise(vec2(noisePos, uTime * 5.0 + uvWorldCenter.y)) * 0.55 + 0.3;
	main.rgb += vec3(rays * isTexelBelow * max(1.0 - uvLocal.y * 1.4, 0.0) * 0.6);

	// Waves
	float topDist = abs(uvLocal.y - uWaterLevel - waveHeight);
	float isNearTop = 1.0 - aastep(vViewSizeInv.y * 2.8, topDist);
	float isVeryNearTop = 1.0 - aastep(vViewSizeInv.y * (0.8 - 100.0 * waveHeight), topDist);

	float topColorBlendFac = isNearTop * isTexelBelow * 0.6;
	main.rgb = mix(main.rgb, texture(uTexture, vec2(uvLocal.x,
		(uWaterLevel - uvLocal.y + uWaterLevel) * 0.97 - waveHeight + vViewSizeInv.y
	)).rgb, vec3(topColorBlendFac));
	main.rgb += vec3(0.2 * isVeryNearTop);

	// Lighting
	vec4 blur1 = texture(uTextureBlurHalf, uv);
	vec4 blur2 = texture(uTextureBlurQuarter, uv);
	vec4 light = texture(uTextureLighting, noiseTexCoords(uv));

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
