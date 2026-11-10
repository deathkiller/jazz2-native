// The underwater look shared by CombineWithWater.shader, CombineWithWaterLow.shader and Water.shader, and the wave
// Combine.shader darkens above. A helper is only kept by the shaders that call it, so the includer declares only what
// those read: uWaterLevel, uWavePhase, uWaterColorTop/uWaterColorBottom, uTexture, uTextureNoise, uCameraPos and uTime.
// The phases and offsets are wrapped into their periods on the CPU (see Rendering::PlayerViewport::GetWaterView()).

// sin(2 * pi * cycles) as a parabola over the range-reduced phase, within 0.001 of the real curve
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

// The phases of the four sines of the wave at a column of the view, in cycles
vec4 waterWavePhases(float x) {
	return uWavePhase + x * vec4(9.5492966, 3.1830989, 5.5704230, 11.140846);
}

// The height of the wave, in the fraction of the view
float waterWave(vec4 phases) {
	return dot(sinCycles(phases), vec4(0.0016, 0.0032, 0.004, 0.0004));
}

// From the color at the surface to the one at the bottom of the view
vec3 waterColorAt(float y) {
	return mix(uWaterColorTop.rgb, uWaterColorBottom.rgb, clamp((y - uWaterLevel) / max(1.0 - uWaterLevel, 0.05), 0.0, 1.0));
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

// The displacement, the white noise of the 64x64 noise texture eased between its texels - read linearly it bent
// the scene at each of them, seams 10/64 of the view apart
vec2 waterDisplacement(vec2 pos) {
	vec2 p = pos * 64.0 + 0.5;
	vec2 i = floor(p);
	vec2 f = p - i;
	f = f * f * (3.0 - 2.0 * f);
	return (texture(uTextureNoise, (i + f - 0.5) * 0.015625).xy - vec2(0.5)) * vec2(0.01);
}

// The coordinates of the simplex noise of the light rays, anchored to the world
vec2 waterRaysPos(vec2 uvLocal, vec2 viewSizeInv) {
	vec2 uvWorldCenter = uCameraPos * viewSizeInv;
	vec2 pixel = uvLocal / viewSizeInv;
	return vec2(uvWorldCenter.x * 6.0 + uvWorldCenter.y * 0.5 + uvLocal.x * 1.4 + pixel.x * (6.0 / 720.0) + pixel.y * (5.0 / 720.0) - 5.0,
		uTime * 5.0 + uvWorldCenter.y);
}

// The scene in uTexture under the water at a point of the view: displaced, tinted and lit by the rays below the wave,
// with the bright line along the surface and its reflection. rowSlope is 0.7071 rows of the view per unit, the width
// of the anti-aliased edges, rowSize one row. The rays are read at raysPos from the simplex noise, from the blue
// channel of uTextureNoise on a low-power GPU. Also returns how much the point is below the surface and where the
// scene was read.
vec3 waterScene(vec2 uvLocal, float waveHeight, float rowSlope, float rowSize, vec2 displacementPos, float rippleCycles,
	vec2 raysPos, out float isTexelBelow, out vec2 uv) {
	float belowWaterLine = uvLocal.y - uWaterLevel - waveHeight;
	isTexelBelow = clamp(belowWaterLine * rowSlope + 0.5, 0.0, 1.0);

	uv = clamp(uvLocal + (vec2(0.004 * sinCycles(rippleCycles), 0.0) + waterDisplacement(displacementPos)) * vec2(isTexelBelow), vec2(0.0), vec2(1.0));
	vec3 main = texture(uTexture, uv).rgb;

	// Chromatic aberration
	float aberration = abs(uvLocal.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main = mix(main, waterColorAt(uvLocal.y) * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));

#if LOW_POWER_GPU
	float rays = texture(uTextureNoise, raysPos).b * 1.1 - 0.25;
#else
	float rays = snoise(raysPos) * 0.55 + 0.3;
#endif
	main += vec3(rays * isTexelBelow * max(1.0 - uvLocal.y * 1.4, 0.0) * 0.6);

	float topDist = abs(belowWaterLine);
	float isNearTop = clamp(0.5 - (topDist - rowSize * 2.8) * rowSlope, 0.0, 1.0);
	float isVeryNearTop = clamp(0.5 - (topDist - rowSize * (0.8 - 100.0 * waveHeight)) * rowSlope, 0.0, 1.0);
	main = mix(main, texture(uTexture, vec2(uvLocal.x, (uWaterLevel - uvLocal.y + uWaterLevel) * 0.97 - waveHeight + rowSize)).rgb,
		vec3(isNearTop * isTexelBelow * 0.6));
	return main + vec3(0.2 * isVeryNearTop);
}

// The low quality one: no noise, a stronger ripple, a flat tint and a glow down from the surface
vec3 waterSceneLow(vec2 uvLocal, float waveHeight, float rowSlope, float rowSize, float rippleCycles, out float isTexelBelow, out vec2 uv) {
	float belowWaterLine = uvLocal.y - uWaterLevel - waveHeight;
	isTexelBelow = clamp(belowWaterLine * rowSlope + 0.5, 0.0, 1.0);

	uv = clamp(uvLocal + vec2(0.008 * sinCycles(rippleCycles) * isTexelBelow, 0.0), vec2(0.0), vec2(1.0));
	vec3 main = texture(uTexture, uv).rgb;

	float topDist = abs(belowWaterLine);
	float topGradient = max(1.0 - topDist, 0.0);
	float isNearTop = 0.2 * topGradient * topGradient;
	float isVeryNearTop = clamp(0.5 - (topDist - rowSize) * rowSlope, 0.0, 1.0);
	return mix(main, waterColorAt(uvLocal.y), vec3(isTexelBelow * 0.4)) + vec3((isNearTop + 0.2 * isVeryNearTop) * isTexelBelow);
}
