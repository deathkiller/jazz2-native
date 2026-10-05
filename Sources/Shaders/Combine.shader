program Combine;

#include "Include/CombineVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);
uniform sampler2D uTextureBlurHalf : texture_unit(2);
uniform sampler2D uTextureBlurQuarter : texture_unit(3);

uniform vec4 uAmbientColor;
uniform float uTime;

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec2 noiseTexCoords(vec2 position) {
	vec2 seed = position + fract(uTime * 0.01);
	return clamp(position + hash2D(seed) * vViewSizeInv * 1.4, vec2(0.0), vec2(1.0));
}

// Interleaved gradient noise (Jimenez 2014) for the low-power lighting jitter below: two fract() and a dot,
// where hash2D() pays for two sin()
float interleavedGradientNoise(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// The same +-1.4 view pixel jitter of the lightmap lookup as noiseTexCoords(), hashed from the pixel
// position instead, and without the clamp - the lighting buffer is sampled with clamp-to-edge anyway
vec2 lightingJitterLowPower() {
	vec2 seed = vPixelPos + fract(uTime * 7.31) * 512.0;
	vec2 offset = vec2(interleavedGradientNoise(seed), interleavedGradientNoise(seed + vec2(37.0, 17.0))) * 2.0 - 1.0;
	return offset * vViewSizeInv * 1.4;
}

void fragment() {
	vec4 blur1 = texture(uTextureBlurHalf, vTexCoords);
	vec4 blur2 = texture(uTextureBlurQuarter, vTexCoords);

	vec4 main = texture(uTexture, vTexCoords);
#if LOW_POWER_GPU
	// Low-power-GPU variant (the PS Vita's sceGxm and the OpenGL|ES 2.0 profile): the sin()-based hash
	// was nearly half of this full-screen pass on the SGX543 - this saves 1.4 of the 3 ms it cost over a
	// plain copy at 480x272. Any uniformly distributed jitter dithers the lightmap texels equally well.
	vec4 light = texture(uTextureLighting, vTexCoords + lightingJitterLowPower());
#else
	vec4 light = texture(uTextureLighting, noiseTexCoords(vTexCoords));
#endif

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	COLOR = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(1.0 - light.r));
	COLOR.a = 1.0;
}

void fixed_function() {
	// The viewport compositor is the direct-tier CPU-lightmap lighting hook (shared with the software
	// backend's SetPendingSoftwareLighting state) - bound here by name instead of by shader label
	pipeline lighting_combine;
}
