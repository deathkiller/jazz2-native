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

void fixed_function(pvr, gx, pica, gu, gs, rdp, legacygl) {
	// The lightmap half of the composite stays in the backend stage: converting the compositor's
	// half-resolution float lightmap into a factor texture is a per-texel loop over the whole map every
	// frame, and its store format (ARGB4444 in video memory, tiled RGBA8, IA16, an attenuation-only I8)
	// is backend business. See LightingCombine.h for the one part of it that is not.
	pipeline lighting_combine;

	// The water half is not: it is two screen-aligned quads per viewport per frame, and every number in
	// it comes from the fragment stage above. Keeping it here is what stops the two from drifting - the
	// tint and the 0.4 threshold below used to be transcribed into all six console backends by hand.
	//
	// This is water at the fidelity this tier can afford: a flat tint band instead of the displacement,
	// chromatic aberration, light rays and wavy surface of the GLSL, which all need per-pixel work. The
	// LOW-quality variant carries the same block - there is nothing left to reduce.
	if (has_uniform(uWaterLevel)) {
		// The Combine draw's quad IS the viewport, so the sprite axes are its rectangle in raster space:
		// the origin is the top-left corner and axis_y runs down to the bottom edge, which is the same
		// direction uWaterLevel measures in (0 at the top of the view, 1 at the bottom).
		vec2 origin = quad_origin();
		vec2 axisX = quad_axis_x();
		vec2 axisY = quad_axis_y();
		float level = uniform_float(uWaterLevel);
		vec2 waterline = origin + level * axisY;

		pass p;
		p.blend = ALPHA;

		// Underwater: the GLSL's mix(main, vec3(0.4, 0.6, 0.8), 0.5 * isTexelBelow) collapses to a
		// constant band over everything below the waterline. Skipped when the waterline is already at or
		// below the bottom edge - the compositor queues this draw for the whole viewport, not per band.
		if (level < 1.0) {
			strip_position(0, waterline);
			strip_position(1, waterline + axisX);
			strip_position(2, origin + axisY);
			strip_position(3, origin + axisY + axisX);
			strip_color(0, vec4(0.4, 0.6, 0.8, 0.4));
			strip_color(1, vec4(0.4, 0.6, 0.8, 0.4));
			strip_color(2, vec4(0.4, 0.6, 0.8, 0.4));
			strip_color(3, vec4(0.4, 0.6, 0.8, 0.4));
			submit_strip_shaded(p, 4);
		}

		// Above deep water: the GLSL adds (0.4 - uWaterLevel) of extra darkness toward the ambient
		// colour once the waterline is in the top 40% of the view, so the world above deep water dims
		vec4 ambient = uniform_vec4(uAmbientColor);
		if (level < 0.4 && level > 0.0) {
			vec4 above = vec4(ambient.r, ambient.g, ambient.b, 0.4 - level);
			strip_position(0, origin);
			strip_position(1, origin + axisX);
			strip_position(2, waterline);
			strip_position(3, waterline + axisX);
			strip_color(0, above);
			strip_color(1, above);
			strip_color(2, above);
			strip_color(3, above);
			submit_strip_shaded(p, 4);
		}
	}
}
