program CombineWithWater;
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

varying vec2 vTexCoords;
varying vec2 vViewSize;
varying vec2 vViewSizeInv;
#if LOW_POWER_GPU
// The vertex stage of "Include/CombineVs.inc" is spelled out here because the low-power path needs these
// on top of it, and an include cannot add to the vertex() it defines. Everything the fragment stage reads
// through them is a linear function of the screen position, so the interpolators reproduce it exactly
varying vec2 vLightingSeed;
varying vec2 vDisplacementPos;
varying vec2 vRaysPos;
varying vec4 vWavePhases;
varying vec4 vWaterLine;
varying vec2 vConstants;
#endif

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vViewSize = spriteSize;
	vViewSizeInv = vec2(1.0) / spriteSize;
#if LOW_POWER_GPU
	// Each term below is what fragment() would otherwise compute per fragment, split into a part that is
	// constant over the viewport and a part linear in the position. The constant part is wrapped first -
	// into a whole period of what reads it (a texture repeat, a sine cycle) - so the interpolated values
	// stay small, and with them their precision, however long the level runs and wherever the camera is.
	vec2 uvLocal = vTexCoords;
	vec2 uvWorldCenter = uCameraPos * vViewSizeInv;
	const float invTau = 0.15915494;

	// The pixel position plus a per-frame offset seeds the lightmap jitter (see lightingJitterLowPower())
	vLightingSeed = aPosition * spriteSize + fract(uTime * 7.31) * 512.0;

	// The displacement lookup of fragment(), as a texture coordinate - which lets the read be issued before
	// the fragment program runs, instead of as a dependent read
	vec2 displacementBase = uvWorldCenter * 0.1 + mod(uTime * 0.4, 2.0);
	vDisplacementPos = uvLocal * 0.1 + fract(displacementBase);

	// The light rays' noise coordinate (noisePos, and the time as the second axis) in tiles of the noise
	// texture, whose blue channel holds 8 units of tileable noise (see ContentResolver::GetNoiseTexture()).
	// mod(..., 720.0) is dropped - no view this path renders is that large
	float raysLinear = uvLocal.x * 1.4 + uvLocal.x * spriteSize.x * (6.0 / 720.0) + uvLocal.y * spriteSize.y * (5.0 / 720.0);
	float raysBase = uvWorldCenter.x * 6.0 + uvWorldCenter.y * 0.5 - 5.0;
	vRaysPos = vec2(raysLinear * 0.125 + fract(raysBase * 0.125), fract((uTime * 5.0 + uvWorldCenter.y) * 0.125));

	// The four phases of wave(), in cycles, with the cosines turned into sines a quarter cycle on
	vec4 waveScale = vec4(60.0, 20.0, 35.0, 70.0) * invTau;
	vec4 waveBase = vec4((uvWorldCenter.x - uTime) * waveScale.x + 0.25, (uvWorldCenter.x - 2.0 * uTime) * waveScale.y + 0.25,
		(uvWorldCenter.x + 2.0 * uTime) * waveScale.z, (uvWorldCenter.x + 4.0 * uTime) * waveScale.w + 0.25);
	vWavePhases = uvLocal.x * waveScale + fract(waveBase);

	// x: the phase of the underwater ripple, in cycles; y: the row relative to the water line; z: the row
	// the surface reflection is read from; w: the extra darkness above deep water
	float rippleBase = (uTime * 16.0 + uvWorldCenter.y * 20.0) * invTau;
	vWaterLine = vec4(uvLocal.y * 20.0 * invTau + fract(rippleBase), uvLocal.y - uWaterLevel,
		(uWaterLevel - uvLocal.y + uWaterLevel) * 0.97 + vViewSizeInv.y, max(0.4 - uWaterLevel, 0.0));

	// x: the blur blend scale of the final composite; y: the slope of the anti-aliased edges, in rows
	vConstants = vec2(1.0 / sqrt(max(uAmbientColor.w, 0.35)), 0.70710678 * spriteSize.y);
#endif
}

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

// Low-power lighting jitter, see Combine.shader (the per-frame offset is already in the seed here)
float interleavedGradientNoise(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 lightingJitterLowPower() {
	vec2 offset = vec2(interleavedGradientNoise(vLightingSeed), interleavedGradientNoise(vLightingSeed + vec2(37.0, 17.0))) * 2.0 - 1.0;
	return offset * vViewSizeInv * 1.4;
}

// sin(2 * pi * cycles) as a parabola over the range-reduced phase, refined to within 0.001 of the real
// curve - a few multiply-adds, where sin() is one of the most expensive things a USSE-class part runs
float sinCycles(float cycles) {
	float v = fract(cycles + 0.5) * 2.0 - 1.0;
	float y = 4.0 * v * (1.0 - abs(v));
	return y * (0.775 + 0.225 * abs(y));
}

vec4 sinCycles(vec4 cycles) {
	vec4 v = fract(cycles + 0.5) * 2.0 - 1.0;
	vec4 y = 4.0 * v * (1.0 - abs(v));
	return y * (0.775 + 0.225 * abs(y));
}

void fragment() {
	vec3 waterColor = vec3(0.4, 0.6, 0.8);

	vec2 uvLocal = vTexCoords;
#if LOW_POWER_GPU
	// Low-power-GPU variant (the PS Vita's sceGxm and the OpenGL|ES 2.0 profile). The same effect, but this
	// full-screen pass cost 16 ms over a plain copy on the SGX543 at 480x272 - the frame dropped to 39 fps -
	// where each sin() and cos() costs about 0.6 ms of that, the simplex noise 3.8 ms, and a texture read
	// whose coordinate the program computes cannot be issued before the program runs. This path is 4.7 ms:
	// - the screen-linear terms come from the vertex stage, which makes the displacement and ray reads plain
	//   interpolant reads and leaves the sines a polynomial over an interpolated phase (sinCycles());
	// - the rays are read from tileable noise baked into the noise texture instead of simplex noise;
	// - aastep() takes its width from the row height, which is exactly what its derivatives evaluate to
	//   for a value that changes by one row per row, and ramps linearly over it;
	// - the blur levels and the lightmap are read at the undisplaced position (a few pixels of ripple are
	//   invisible in either), with the cheaper lightmap jitter from Combine.shader.
	float waveHeight = dot(sinCycles(vWavePhases), vec4(0.004, 0.008, 0.01, 0.001) * 0.4);
	float belowWaterLine = vWaterLine.y - waveHeight;
	float aaSlope = vConstants.y;
	float isTexelBelow = clamp(belowWaterLine * aaSlope + 0.5, 0.0, 1.0);
	float isTexelAbove = 1.0 - isTexelBelow;

	// Displacement
	vec2 dis = (texture(uTextureNoise, vDisplacementPos).xy - vec2(0.5)) * vec2(0.01);

	vec2 uv = uvLocal + (vec2(0.004 * sinCycles(vWaterLine.x), 0.0) + dis) * vec2(isTexelBelow);
	vec4 main = texture(uTexture, uv);

	// Chromatic Aberration
	float aberration = abs(uvLocal.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main.rgb = mix(main.rgb, waterColor * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));

	// Rays, (noise + 1) / 2 in the blue channel
	float rays = texture(uTextureNoise, vRaysPos).b * 1.1 - 0.25;
	main.rgb += vec3(rays * isTexelBelow * max(1.0 - uvLocal.y * 1.4, 0.0) * 0.6);

	// Waves
	float topDist = abs(belowWaterLine);
	float isNearTop = clamp(0.5 - (topDist - vViewSizeInv.y * 2.8) * aaSlope, 0.0, 1.0);
	float isVeryNearTop = clamp(0.5 - (topDist - vViewSizeInv.y * (0.8 - 100.0 * waveHeight)) * aaSlope, 0.0, 1.0);

	float topColorBlendFac = isNearTop * isTexelBelow * 0.6;
	main.rgb = mix(main.rgb, texture(uTexture, vec2(uvLocal.x, vWaterLine.z - waveHeight)).rgb, vec3(topColorBlendFac));
	main.rgb += vec3(0.2 * isVeryNearTop);

	// Lighting
	vec4 blur1 = texture(uTextureBlurHalf, uvLocal);
	vec4 blur2 = texture(uTextureBlurQuarter, uvLocal);
	vec4 light = texture(uTextureLighting, uvLocal + lightingJitterLowPower());
#else
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
#endif

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	float darknessStrength = (1.0 - light.r);

	// Darkness above water
#if LOW_POWER_GPU
	darknessStrength = min(1.0, darknessStrength + isTexelAbove * vWaterLine.w);
#else
	if (uWaterLevel < 0.4) {
		float aboveWaterDarkness = isTexelAbove * (0.4 - uWaterLevel);
		darknessStrength = min(1.0, darknessStrength + aboveWaterDarkness);
	}
#endif

#if LOW_POWER_GPU
	COLOR = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) * vConstants.x, 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
#else
	COLOR = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
#endif
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
