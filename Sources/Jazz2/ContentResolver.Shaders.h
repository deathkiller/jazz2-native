#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT

namespace Jazz2::Shaders
{
	constexpr std::uint64_t Version = 8;

	constexpr char LightingVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec4 vTexCoords;
out vec4 vColor;

void main() {
	vec2 aPosition = vec2(0.5 - float(gl_VertexID >> 1), 0.5 - float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = vec4(color.x, color.y, aPosition.x * 2.0, aPosition.y * 2.0);
}
)";

	constexpr char BatchedLightingVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (585) // 64 Kb / 112 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

out vec4 vTexCoords;
out vec4 vColor;

#define i block.instances[gl_VertexID / 6]

void main() {
	vec2 aPosition = vec2(-0.5 + float(((gl_VertexID + 2) / 3) % 2), -0.5 + float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = i.texRect;
	vColor = vec4(i.color.x, i.color.y, aPosition.x * 2.0, aPosition.y * 2.0);
}
)";

	constexpr char LightingFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec4 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

float lightBlend(float t) {
	return t * t * t;
}

void main() {
	vec2 center = vTexCoords.xy;
	float radiusNear = vTexCoords.z;
	float intensity = vColor.r;
	float brightness = vColor.g;

	float dist = length(vColor.zw);
	if (dist > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// TODO: Normal maps
	/*vec4 clrNormal = texture(uTexture, vec2(gl_FragCoord) / ViewSize);
	vec3 normal = normalize(clrNormal.xyz - vec3(0.5, 0.5, 0.5));
	normal.z = -normal.z;

	vec3 lightDir = vec3((center.x - gl_FragCoord.x), (center.y - gl_FragCoord.y), 0);

	// Diffuse lighting
	float diffuseFactor = 1.0 - max(dot(normal, normalize(lightDir)), 0.0);
	diffuseFactor = diffuseFactor * 0.8 + 0.2;*/
	float diffuseFactor = 1.0f;
	
	float strength = diffuseFactor * lightBlend(clamp(1.0 - ((dist - radiusNear) / (1.0 - radiusNear)), 0.0, 1.0));
	fragColor = vec4(strength * intensity, strength * brightness, 0.0, 1.0);
}
)";

	constexpr char BlurFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform vec2 uPixelOffset;
uniform vec2 uDirection;

in vec2 vTexCoords;
out vec4 fragColor;

void main() {
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.3846153846) * uPixelOffset * uDirection;
	vec2 off2 = vec2(3.2307692308) * uPixelOffset * uDirection;
	color += texture(uTexture, vTexCoords) * 0.2270270270;
	color += texture(uTexture, vTexCoords + off1) * 0.3162162162;
	color += texture(uTexture, vTexCoords - off1) * 0.3162162162;
	color += texture(uTexture, vTexCoords + off2) * 0.0702702703;
	color += texture(uTexture, vTexCoords - off2) * 0.0702702703;
	fragColor = color;
}
)";

	constexpr char DownsampleFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform vec2 uPixelOffset;

in vec2 vTexCoords;
out vec4 fragColor;

void main() {
	vec4 color = texture(uTexture, vTexCoords);
	color += texture(uTexture, vTexCoords + vec2(0.0, uPixelOffset.y));
	color += texture(uTexture, vTexCoords + vec2(uPixelOffset.x, 0.0));
	color += texture(uTexture, vTexCoords + uPixelOffset);
	fragColor = vec4(0.25) * color;
}
)";

	constexpr char CombineVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vTexCoords;
out vec2 vViewSize;
out vec2 vViewSizeInv;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vViewSize = spriteSize;
	vViewSizeInv = vec2(1.0) / spriteSize;
}
)";

	constexpr char CombineFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform sampler2D uTextureLighting;
uniform sampler2D uTextureBlurHalf;
uniform sampler2D uTextureBlurQuarter;

uniform vec4 uAmbientColor;
uniform float uTime;

in vec2 vTexCoords;
in vec2 vViewSize;
in vec2 vViewSizeInv;
out vec4 fragColor;

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec2 noiseTexCoords(vec2 position) {
	vec2 seed = position + fract(uTime * 0.01);
	return clamp(position + hash2D(seed) * vViewSizeInv * 1.4, vec2(0.0), vec2(1.0));
}

void main() {
	vec4 blur1 = texture(uTextureBlurHalf, vTexCoords);
	vec4 blur2 = texture(uTextureBlurQuarter, vTexCoords);

	vec4 main = texture(uTexture, vTexCoords);
	vec4 light = texture(uTextureLighting, noiseTexCoords(vTexCoords));

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	fragColor = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(1.0 - light.r));
	fragColor.a = 1.0;
}
)";

	constexpr char CombineWithWaterFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D uTexture;
uniform sampler2D uTextureLighting;
uniform sampler2D uTextureBlurHalf;
uniform sampler2D uTextureBlurQuarter;
uniform sampler2D uTextureNoise;

uniform vec4 uAmbientColor;
uniform float uTime;
uniform vec2 uCameraPos;
uniform float uWaterLevel;

in vec2 vTexCoords;
in vec2 vViewSize;
in vec2 vViewSizeInv;
out vec4 fragColor;

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

void main() {
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

	fragColor = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
	fragColor.a = 1.0;
}
)";

	constexpr char CombineWithWaterLowFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform sampler2D uTextureLighting;
uniform sampler2D uTextureBlurHalf;
uniform sampler2D uTextureBlurQuarter;

uniform vec4 uAmbientColor;
uniform float uTime;
uniform vec2 uCameraPos;
uniform float uWaterLevel;

in vec2 vTexCoords;
in vec2 vViewSize;
in vec2 vViewSizeInv;
out vec4 fragColor;

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec2 noiseTexCoords(vec2 position) {
	vec2 seed = position + fract(uTime * 0.01);
	return clamp(position + hash2D(seed) * vViewSizeInv * 1.4, vec2(0.0), vec2(1.0));
}

void main() {
	vec3 waterColor = vec3(0.4, 0.6, 0.8);

	vec2 uvLocal = vTexCoords;
	vec2 uvWorldCenter = (uCameraPos.xy * vViewSizeInv.xy);
	vec2 uvWorld = uvLocal + uvWorldCenter;

	float isTexelBelow = 1.0 - step(uvLocal.y, uWaterLevel);
	float isTexelAbove = 1.0 - isTexelBelow;

	vec2 uv = clamp(uvLocal + vec2(0.008 * sin(uTime * 16.0 + uvWorld.y * 20.0) * isTexelBelow, 0.0), vec2(0.0), vec2(1.0));
	vec4 main = texture(uTexture, uv);

	// Waves
	float topDist = abs(uvLocal.y - uWaterLevel);
	float topGradient = max(1.0 - topDist, 0.0);
	float isNearTop = 0.2 * topGradient * topGradient;
	float isVeryNearTop = 1.0 - step(vViewSizeInv.y, topDist);
	main.rgb = mix(main.rgb, waterColor, vec3(isTexelBelow * 0.4)) + vec3((isNearTop + 0.2 * isVeryNearTop) * isTexelBelow);

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

	fragColor = mix(mix(
		main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(uAmbientColor.w, 0.35)), 0.0, 1.0))
	), uAmbientColor, vec4(darknessStrength));
	fragColor.a = 1.0;
}
)";

	constexpr char TexturedBackgroundFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D uTexture;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;

in vec2 vTexCoords;
out vec4 fragColor;

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

void main() {
	// Distance to center of screen from top or bottom (1: center of screen, 0: edge of screen)
	float distance = 1.3 - abs(2.0 * vTexCoords.y - 1.0);
	float horizonDepth = pow(distance, 1.4);

	float yShift = (vTexCoords.y > 0.5 ? 1.0 : 0.0);
	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));

	vec2 texturePos = vec2(
		(uShift.x / 256.0) + (vTexCoords.x - 0.5   ) * (0.5 + (1.5 * horizonDepth)) * correction,
		(uShift.y / 256.0) + (vTexCoords.y - yShift) * 1.4 * distance
	);

	vec4 texColor = texture(uTexture, texturePos);

#ifdef DITHER
	texturePos += hash2D(vTexCoords * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(uTexture, texturePos), 0.333);
#endif

	float horizonOpacity = clamp(pow(distance, 1.5) - 0.3, 0.0, 1.0);
	
	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (vTexCoords * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
		
		samplePosition = (vTexCoords * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}

	fragColor = mix(texColor, horizonColorWithStars, horizonOpacity);
	fragColor.a = 1.0;
}
)";

	constexpr char TexturedBackgroundCircleFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D uTexture;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;

in vec2 vTexCoords;
out vec4 fragColor;

#define INV_PI 0.31830988618379067153776752675

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

void main() {
	// Position of pixel on screen (between -1 and 1)
	vec2 targetCoord = vec2(2.0) * vTexCoords - vec2(1.0);

	// Aspect ratio correction, so display circle instead of ellipse
	targetCoord.x *= uViewSize.x / uViewSize.y;

	// Distance to center of screen
	float distance = length(targetCoord);

	// x-coordinate of tunnel
	float xShift = (targetCoord.x == 0.0 ? sign(targetCoord.y) * 0.5 : atan(targetCoord.y, targetCoord.x) * INV_PI);

	vec2 texturePos = vec2(
		(xShift)         * 1.0 + (uShift.x * 0.01),
		(1.0 / distance) * 1.4 + (uShift.y * 0.002)
	);

	vec4 texColor = texture(uTexture, texturePos);

#ifdef DITHER
	texturePos += hash2D(vTexCoords * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(uTexture, texturePos), 0.333);
#endif

	float horizonOpacity = 1.0 - clamp(pow(distance, 1.4) - 0.3, 0.0, 1.0);
	
	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (vTexCoords * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
		
		samplePosition = (vTexCoords * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}

	fragColor = mix(texColor, horizonColorWithStars, horizonOpacity);
	fragColor.a = 1.0;
}
)";

	constexpr char ColorizedFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

void main() {
	vec4 dye = vec4(1.0) + (vColor - vec4(0.5)) * vec4(4.0);
	vec4 original = texture(uTexture, vTexCoords);
	float average = (original.r + original.g + original.b) * 0.5;
	vec4 gray = vec4(average, average, average, original.a);
	fragColor = gray * dye;
}
)";

	constexpr char TintedFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

void main() {
	vec4 original = texture(uTexture, vTexCoords);
	vec3 tinted = mix(original.rgb, vColor.rgb, 0.45);
	fragColor = vec4(tinted.r, tinted.g, tinted.b, original.a * vColor.a);
}
)";

	// Subtle shadow has been added in v2.5.0, previous implementation was:
	//	fragColor = mix(color, vec4(vColor.z, vColor.z, vColor.z, vColor.w), outline - color.a);
	constexpr char OutlineFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value); 
}

void main() {
	vec2 size = vColor.xy;

	float outline = texture(uTexture, vTexCoords + vec2(-size.x, 0)).a;
	outline += texture(uTexture, vTexCoords + vec2(0, size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(size.x, 0)).a;
	outline += texture(uTexture, vTexCoords + vec2(0, -size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(-size.x, size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(size.x, size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(-size.x, -size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(size.x, -size.y)).a;
	outline = aastep(1.0, outline);

	float outline2 = texture(uTexture, vTexCoords + vec2(-2.0 * size.x, 0)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(0, 2.0 * size.y)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(2.0 * size.x, 0)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(0, -2.0 * size.y)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(-2.0 * size.x, 2.0 * size.y)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(2.0 * size.x, 2.0 * size.y)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(-2.0 * size.x, -2.0 * size.y)).a;
	outline2 += texture(uTexture, vTexCoords + vec2(2.0 * size.x, -2.0 * size.y)).a;
	outline2 = aastep(1.0, outline2);

	vec4 color = texture(uTexture, vTexCoords);
	fragColor = mix(color,
		mix(vec4(0.0, 0.0, 0.0, vColor.w * 0.5), vec4(vColor.z, vColor.z, vColor.z, vColor.w), outline),
		max(outline, outline2) - color.a);
}
)";

	constexpr char WhiteMaskFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

void main() {
	vec4 tex = texture(uTexture, vTexCoords);
	float color = min((0.299 * tex.r + 0.587 * tex.g + 0.114 * tex.b) * 6.0f, 1.0f);
	fragColor = vec4(color, color, color, tex.a) * vColor;
}
)";

	constexpr char PartialWhiteMaskFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

void main() {
	vec4 tex = texture(uTexture, vTexCoords);
	float color = min((0.299 * tex.r + 0.587 * tex.g + 0.114 * tex.b) * 2.5f, 1.0f);
	fragColor = vec4(color, color, color, tex.a) * vColor;
}
)";

	constexpr char FrozenMaskFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value); 
}

void main() {
	vec2 size = vColor.xy * vColor.a * 2.0;

	vec4 tex = texture(uTexture, vTexCoords);
	vec4 tex1 = texture(uTexture, vTexCoords + vec2(-size.x, 0));
	vec4 tex2 = texture(uTexture, vTexCoords + vec2(0, size.y));
	vec4 tex3 = texture(uTexture, vTexCoords + vec2(size.x, 0));
	vec4 tex4 = texture(uTexture, vTexCoords + vec2(0, -size.y));

	float outline = tex1.a;
	outline += tex2.a;
	outline += tex3.a;
	outline += tex4.a;
	outline += texture(uTexture, vTexCoords + vec2(-size.x, size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(size.x, size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(-size.x, -size.y)).a;
	outline += texture(uTexture, vTexCoords + vec2(size.x, -size.y)).a;
	outline = aastep(1.0, outline);

	vec4 color = (tex + tex + tex1 + tex2 + tex3 + tex4) / 6.0;
	float grey = min((0.299 * color.r + 0.587 * color.g + 0.114 * color.b) * 2.6f, 1.0f);
	fragColor = mix(tex, vec4(0.2 * grey, 0.2 + grey * 0.62, 0.6 + 0.2 * grey, outline * 0.95), vColor.a);
}
)";

	constexpr char ShieldVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec4 vTexCoords;
out vec4 vColor;
out vec2 vPos;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = color;
	vPos = aPosition * vec2(2.0) - vec2(1.0);
}
)";

	constexpr char BatchedShieldVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (585) // 64 Kb / 112 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

out vec4 vTexCoords;
out vec4 vColor;
out vec2 vPos;

#define i block.instances[gl_VertexID / 6]

void main() {
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = i.texRect;
	vColor = i.color;
	vPos = aPosition * vec2(2.0) - vec2(1.0);
}
)";

	constexpr char ShieldFireFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec4 vTexCoords;
in vec4 vColor;
in vec2 vPos;
out vec4 fragColor;

#define PI 3.1415926

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value); 
}

float triangleWave(float x, float period) {
  float p = x / period;
  float f = fract(p);
  return abs(f - 0.5) * 2.0;
}

void main() {
	vec2 scale = vColor.xy;
	vec2 shift1 = vTexCoords.xy;
	vec2 shift2 = vTexCoords.zw;
	float darkness = vColor.z;
	float alpha = vColor.w;

	float dist = length(vPos);
	if (dist > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	vec3 v = vec3(vPos.x, vPos.y, sqrt(1.0 - vPos.x * vPos.x - vPos.y * vPos.y));
	vec3 n = normalize(v);
	float b = dot(n, vec3(0.0, 0.0, 1.0));
	vec2 q = vec2(0.5 - 0.5 * atan(n.z, n.x) / PI, -acos(vPos.y) / PI);

	float isNearBorder = 1.0 - aastep(0.96, dist);

	float mask1 = texture(uTexture, mod(shift1 + (q * scale), 1.0)).r;
	float maskNormalized1 = max(1.0 - (abs(triangleWave(shift2.y + 0.5, 2.0) - mask1) * 6.0), 0.0);

	float mask2 = texture(uTexture, mod(shift2 + (q * scale), 1.0)).r;
	float maskNormalized2 = max(1.0 - (abs(triangleWave(shift1.x, 1.333) - mask2) * 6.0), 0.0);

	float maskSum = min(maskNormalized1 + maskNormalized2, 1.0);

	fragColor = vec4(mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 1.0, 1.0), max((maskSum * 2.0) - 1.0, 0.0)) * darkness, min(maskSum * b * isNearBorder * 1.3 + 0.1, 1.0) * alpha);
}
)";

	constexpr char ShieldLightningFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec4 vTexCoords;
in vec4 vColor;
in vec2 vPos;
out vec4 fragColor;

#define PI 3.1415926

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value); 
}

float triangleWave(float x, float period) {
  float p = x / period;
  float f = fract(p);
  return abs(f - 0.5) * 2.0;
}

void main() {
	vec2 scale = vColor.xy;
	vec2 shift1 = vTexCoords.xy;
	vec2 shift2 = vTexCoords.zw;
	float darkness = vColor.z;
	float alpha = vColor.w;

	float dist = length(vPos);
	if (dist > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	vec3 v = vec3(vPos.x, vPos.y, sqrt(1.0 - vPos.x * vPos.x - vPos.y * vPos.y));
	vec3 n = normalize(v);
	float b = dot(n, vec3(0.0, 0.0, 1.0));
	vec2 q = vec2(0.5 - 0.5 * atan(n.z, n.x) / PI, -acos(vPos.y) / PI);

	float isNearBorder = 1.0 - aastep(0.96, dist);

	float mask = texture(uTexture, mod(shift1 + (q * scale), 1.0)).r;
	float maskNormalized = max(1.0 - (abs(triangleWave(shift2.y + 0.5, 2.0) - mask) * 8.0), 0.0);

	float isVeryNearBorder = 1.0 - aastep(0.024, abs(dist - 0.94));
	float maskSum = max(maskNormalized, isVeryNearBorder);

	fragColor = vec4(mix(vec3(0.1, 1.0, 0.0), vec3(1.0, 1.0, 1.0), max((maskSum * 2.0) - 1.0, 0.0)) * darkness, min(maskSum * b * isNearBorder * 1.3 + 0.1, 1.0) * alpha);
}
)";

	constexpr char ResizeHQ2xVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vTexCoords0;
out vec4 vTexCoords1;
out vec4 vTexCoords2;
out vec4 vTexCoords3;
out vec4 vTexCoords4;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	
	float x = 0.5 / texRect.x;
	float y = 0.5 / texRect.y;
	vec2 dg1 = vec2( x, y);
	vec2 dg2 = vec2(-x, y);
	vec2 dx = vec2(x, 0.0);
	vec2 dy = vec2(0.0, y);

	vTexCoords0.xy = aPosition;
	vTexCoords1.xy = aPosition - dg1;
	vTexCoords1.zw = aPosition - dy;
	vTexCoords2.xy = aPosition - dg2;
	vTexCoords2.zw = aPosition + dx;
	vTexCoords3.xy = aPosition + dg1;
	vTexCoords3.zw = aPosition + dy;
	vTexCoords4.xy = aPosition + dg2;
	vTexCoords4.zw = aPosition - dx;
}
)";

	constexpr char ResizeHQ2xFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

const float mx = 0.325;
const float k = -0.250;
const float max_w = 0.25;
const float min_w = -0.05;
const float lum_add = 0.25;

uniform sampler2D uTexture;

in vec2 vTexCoords0;
in vec4 vTexCoords1;
in vec4 vTexCoords2;
in vec4 vTexCoords3;
in vec4 vTexCoords4;
out vec4 fragColor;

void main() {
	vec3 c00 = texture(uTexture, vTexCoords1.xy).xyz; 
	vec3 c10 = texture(uTexture, vTexCoords1.zw).xyz; 
	vec3 c20 = texture(uTexture, vTexCoords2.xy).xyz; 
	vec3 c01 = texture(uTexture, vTexCoords4.zw).xyz; 
	vec3 c11 = texture(uTexture, vTexCoords0.xy).xyz; 
	vec3 c21 = texture(uTexture, vTexCoords2.zw).xyz; 
	vec3 c02 = texture(uTexture, vTexCoords4.xy).xyz; 
	vec3 c12 = texture(uTexture, vTexCoords3.zw).xyz; 
	vec3 c22 = texture(uTexture, vTexCoords3.xy).xyz; 
	vec3 dt = vec3(1.0, 1.0, 1.0);

	float md1 = dot(abs(c00 - c22), dt);
	float md2 = dot(abs(c02 - c20), dt);

	highp float w1 = dot(abs(c22 - c11), dt) * md2;
	highp float w2 = dot(abs(c02 - c11), dt) * md1;
	highp float w3 = dot(abs(c00 - c11), dt) * md2;
	highp float w4 = dot(abs(c20 - c11), dt) * md1;

	highp float t1 = w1 + w3;
	highp float t2 = w2 + w4;
	highp float ww = max(t1, t2) + 0.0001;

	highp vec3 cx = (w1 * c00 + w2 * c20 + w3 * c22 + w4 * c02 + ww * c11) / (t1 + t2 + ww);

	highp float lc1 = k / (0.12 * dot(c10 + c12 + cx, dt) + lum_add);
	highp float lc2 = k / (0.12 * dot(c01 + c21 + cx, dt) + lum_add);

	w1 = clamp(lc1 * dot(abs(cx - c10), dt) + mx, min_w, max_w);
	w2 = clamp(lc2 * dot(abs(cx - c21), dt) + mx, min_w, max_w);
	w3 = clamp(lc1 * dot(abs(cx - c12), dt) + mx, min_w, max_w);
	w4 = clamp(lc2 * dot(abs(cx - c01), dt) + mx, min_w, max_w);

	fragColor.xyz = w1 * c10 + w2 * c21 + w3 * c12 + w4 * c01 + (1.0 - w1 - w2 - w3 - w4) * cx;
	fragColor.w = 1.0;
}
)";

	constexpr char Resize3xBrzVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vPixelCoords;
out vec2 vTexCoords0;
out vec4 vTexCoords1;
out vec4 vTexCoords2;
out vec4 vTexCoords3;
out vec4 vTexCoords4;
out vec4 vTexCoords5;
out vec4 vTexCoords6;
out vec4 vTexCoords7;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	
	float dx = 1.0 / texRect.x;
	float dy = 1.0 / texRect.y;

	vec2 texCoord = aPosition + vec2(0.0000001, 0.0000001);

	vPixelCoords = texCoord * texRect.xy;
	vTexCoords0 = texCoord;
	vTexCoords1 = texCoord.xxxy + vec4( -dx, 0, dx, -2.0*dy);  // A1 B1 C1
	vTexCoords2 = texCoord.xxxy + vec4( -dx, 0, dx, -dy);      // A  B  C
	vTexCoords3 = texCoord.xxxy + vec4( -dx, 0, dx, 0);        // D  E  F
	vTexCoords4 = texCoord.xxxy + vec4( -dx, 0, dx, dy);       // G  H  I
	vTexCoords5 = texCoord.xxxy + vec4( -dx, 0, dx, 2.0*dy);   // G5 H5 I5
	vTexCoords6 = texCoord.xyyy + vec4(-2.0*dx, -dy, 0, dy);   // A0 D0 G0
	vTexCoords7 = texCoord.xyyy + vec4( 2.0*dx, -dy, 0, dy);   // C4 F4 I4
}
)";

	constexpr char Resize3xBrzFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

#define BLEND_NONE 0
#define BLEND_NORMAL 1
#define BLEND_DOMINANT 2
#define LUMINANCE_WEIGHT 1.0
#define EQUAL_COLOR_TOLERANCE 30.0/255.0
#define STEEP_DIRECTION_THRESHOLD 2.2
#define DOMINANT_DIRECTION_THRESHOLD 3.6

const float one_third = 1.0 / 3.0;
const float two_third = 2.0 / 3.0;

uniform sampler2D uTexture;

in vec2 vPixelCoords;
in vec2 vTexCoords0;
in vec4 vTexCoords1;
in vec4 vTexCoords2;
in vec4 vTexCoords3;
in vec4 vTexCoords4;
in vec4 vTexCoords5;
in vec4 vTexCoords6;
in vec4 vTexCoords7;
out vec4 fragColor;

float Reduce(vec3 color) {
	return dot(color, vec3(65536.0, 256.0, 1.0));
}

float DistYCbCr(vec3 pixA, vec3 pixB) {
	const vec3 w = vec3(0.2627, 0.6780, 0.0593);
	const float scaleB = 0.5 / (1.0 - w.b);
	const float scaleR = 0.5 / (1.0 - w.r);
	vec3 diff = pixA - pixB;
	float Y = dot(diff, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);

	return sqrt( ((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr) );
}

bool IsPixEqual(vec3 pixA, vec3 pixB) {
	return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);
}

bool IsBlendingNeeded(ivec4 blend) {
	return any(notEqual(blend, ivec4(BLEND_NONE)));
}

void ScalePixel(ivec4 blend, vec3 k[9], inout vec3 dst[9]) {
	float v0 = Reduce(k[0]);
	float v4 = Reduce(k[4]);
	float v5 = Reduce(k[5]);
	float v7 = Reduce(k[7]);
	float v8 = Reduce(k[8]);

	float dist_01_04 = DistYCbCr(k[1], k[4]);
	float dist_03_08 = DistYCbCr(k[3], k[8]);
	bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_01_04 <= dist_03_08) && (v0 != v4) && (v5 != v4);
	bool haveSteepLine   = (STEEP_DIRECTION_THRESHOLD * dist_03_08 <= dist_01_04) && (v0 != v8) && (v7 != v8);
	bool needBlend = (blend[2] != BLEND_NONE);
	bool doLineBlend = (  blend[2] >= BLEND_DOMINANT ||
					   !((blend[1] != BLEND_NONE && !IsPixEqual(k[0], k[4])) ||
						 (blend[3] != BLEND_NONE && !IsPixEqual(k[0], k[8])) ||
						 (IsPixEqual(k[4], k[3]) &&  IsPixEqual(k[3], k[2]) && IsPixEqual(k[2], k[1]) && IsPixEqual(k[1], k[8]) && !IsPixEqual(k[0], k[2]))));

	vec3 blendPix = (DistYCbCr(k[0], k[1]) <= DistYCbCr(k[0], k[3])) ? k[1] : k[3];
	dst[1] = mix(dst[1], blendPix, (needBlend && doLineBlend) ? ((haveSteepLine) ? 0.750 : ((haveShallowLine) ? 0.250 : 0.125)) : 0.000);
	dst[2] = mix(dst[2], blendPix, (needBlend) ? ((doLineBlend) ? ((!haveShallowLine && !haveSteepLine) ? 0.875 : 1.000) : 0.4545939598) : 0.000);
	dst[3] = mix(dst[3], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? 0.750 : ((haveSteepLine) ? 0.250 : 0.125)) : 0.000);
	dst[4] = mix(dst[4], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);
	dst[8] = mix(dst[8], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
}

void main() {
	vec2 f = fract(vPixelCoords);
	vec3 src[25];

	src[21] = texture(uTexture, vTexCoords1.xw).rgb;
	src[22] = texture(uTexture, vTexCoords1.yw).rgb;
	src[23] = texture(uTexture, vTexCoords1.zw).rgb;
	src[ 6] = texture(uTexture, vTexCoords2.xw).rgb;
	src[ 7] = texture(uTexture, vTexCoords2.yw).rgb;
	src[ 8] = texture(uTexture, vTexCoords2.zw).rgb;
	src[ 5] = texture(uTexture, vTexCoords3.xw).rgb;
	src[ 0] = texture(uTexture, vTexCoords3.yw).rgb;
	src[ 1] = texture(uTexture, vTexCoords3.zw).rgb;
	src[ 4] = texture(uTexture, vTexCoords4.xw).rgb;
	src[ 3] = texture(uTexture, vTexCoords4.yw).rgb;
	src[ 2] = texture(uTexture, vTexCoords4.zw).rgb;
	src[15] = texture(uTexture, vTexCoords5.xw).rgb;
	src[14] = texture(uTexture, vTexCoords5.yw).rgb;
	src[13] = texture(uTexture, vTexCoords5.zw).rgb;
	src[19] = texture(uTexture, vTexCoords6.xy).rgb;
	src[18] = texture(uTexture, vTexCoords6.xz).rgb;
	src[17] = texture(uTexture, vTexCoords6.xw).rgb;
	src[ 9] = texture(uTexture, vTexCoords7.xy).rgb;
	src[10] = texture(uTexture, vTexCoords7.xz).rgb;
	src[11] = texture(uTexture, vTexCoords7.xw).rgb;

	float v[9];
	v[0] = Reduce(src[0]);
	v[1] = Reduce(src[1]);
	v[2] = Reduce(src[2]);
	v[3] = Reduce(src[3]);
	v[4] = Reduce(src[4]);
	v[5] = Reduce(src[5]);
	v[6] = Reduce(src[6]);
	v[7] = Reduce(src[7]);
	v[8] = Reduce(src[8]);

	ivec4 blendResult = ivec4(BLEND_NONE);

	if (!((v[0] == v[1] && v[3] == v[2]) || (v[0] == v[3] && v[1] == v[2]))) {
		float dist_03_01 = DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + DistYCbCr(src[14], src[ 2]) + DistYCbCr(src[ 2], src[10]) + (4.0 * DistYCbCr(src[ 3], src[ 1]));
		float dist_00_02 = DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[ 3], src[13]) + DistYCbCr(src[ 7], src[ 1]) + DistYCbCr(src[ 1], src[11]) + (4.0 * DistYCbCr(src[ 0], src[ 2]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_03_01) < dist_00_02;
		blendResult[2] = ((dist_03_01 < dist_00_02) && (v[0] != v[1]) && (v[0] != v[3])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((v[5] == v[0] && v[4] == v[3]) || (v[5] == v[4] && v[0] == v[3]))) {
		float dist_04_00 = DistYCbCr(src[17]  , src[ 5]) + DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[15], src[ 3]) + DistYCbCr(src[ 3], src[ 1]) + (4.0 * DistYCbCr(src[ 4], src[ 0]));
		float dist_05_03 = DistYCbCr(src[18]  , src[ 4]) + DistYCbCr(src[ 4], src[14]) + DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + (4.0 * DistYCbCr(src[ 5], src[ 3]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_05_03) < dist_04_00;
		blendResult[3] = ((dist_04_00 > dist_05_03) && (v[0] != v[5]) && (v[0] != v[3])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((v[7] == v[8] && v[0] == v[1]) || (v[7] == v[0] && v[8] == v[1]))) {
		float dist_00_08 = DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[ 7], src[23]) + DistYCbCr(src[ 3], src[ 1]) + DistYCbCr(src[ 1], src[ 9]) + (4.0 * DistYCbCr(src[ 0], src[ 8]));
		float dist_07_01 = DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + DistYCbCr(src[22], src[ 8]) + DistYCbCr(src[ 8], src[10]) + (4.0 * DistYCbCr(src[ 7], src[ 1]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_07_01) < dist_00_08;
		blendResult[1] = ((dist_00_08 > dist_07_01) && (v[0] != v[7]) && (v[0] != v[1])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((v[6] == v[7] && v[5] == v[0]) || (v[6] == v[5] && v[7] == v[0]))) {
		float dist_05_07 = DistYCbCr(src[18], src[ 6]) + DistYCbCr(src[ 6], src[22]) + DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + (4.0 * DistYCbCr(src[ 5], src[ 7]));
		float dist_06_00 = DistYCbCr(src[19], src[ 5]) + DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[21], src[ 7]) + DistYCbCr(src[ 7], src[ 1]) + (4.0 * DistYCbCr(src[ 6], src[ 0]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_05_07) < dist_06_00;
		blendResult[0] = ((dist_05_07 < dist_06_00) && (v[0] != v[5]) && (v[0] != v[7])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	vec3 dst[9];
	dst[0] = src[0];
	dst[1] = src[0];
	dst[2] = src[0];
	dst[3] = src[0];
	dst[4] = src[0];
	dst[5] = src[0];
	dst[6] = src[0];
	dst[7] = src[0];
	dst[8] = src[0];

	// Scale pixel
	if (IsBlendingNeeded(blendResult)) {
		vec3 k[9];
		vec3 tempDst8;
		vec3 tempDst7;

		k[8] = src[8];
		k[7] = src[7];
		k[6] = src[6];
		k[5] = src[5];
		k[4] = src[4];
		k[3] = src[3];
		k[2] = src[2];
		k[1] = src[1];
		k[0] = src[0];
		ScalePixel(blendResult.xyzw, k, dst);

		k[8] = src[6];
		k[7] = src[5];
		k[6] = src[4];
		k[5] = src[3];
		k[4] = src[2];
		k[3] = src[1];
		k[2] = src[8];
		k[1] = src[7];
		tempDst8 = dst[8];
		tempDst7 = dst[7];
		dst[8] = dst[6];
		dst[7] = dst[5];
		dst[6] = dst[4];
		dst[5] = dst[3];
		dst[4] = dst[2];
		dst[3] = dst[1];
		dst[2] = tempDst8;
		dst[1] = tempDst7;
		ScalePixel(blendResult.wxyz, k, dst);

		k[8] = src[4];
		k[7] = src[3];
		k[6] = src[2];
		k[5] = src[1];
		k[4] = src[8];
		k[3] = src[7];
		k[2] = src[6];
		k[1] = src[5];
		tempDst8 = dst[8];
		tempDst7 = dst[7];
		dst[8] = dst[6];
		dst[7] = dst[5];
		dst[6] = dst[4];
		dst[5] = dst[3];
		dst[4] = dst[2];
		dst[3] = dst[1];
		dst[2] = tempDst8;
		dst[1] = tempDst7;
		ScalePixel(blendResult.zwxy, k, dst);

		k[8] = src[2];
		k[7] = src[1];
		k[6] = src[8];
		k[5] = src[7];
		k[4] = src[6];
		k[3] = src[5];
		k[2] = src[4];
		k[1] = src[3];
		tempDst8 = dst[8];
		tempDst7 = dst[7];
		dst[8] = dst[6];
		dst[7] = dst[5];
		dst[6] = dst[4];
		dst[5] = dst[3];
		dst[4] = dst[2];
		dst[3] = dst[1];
		dst[2] = tempDst8;
		dst[1] = tempDst7;
		ScalePixel(blendResult.yzwx, k, dst);

		tempDst8 = dst[8];
		tempDst7 = dst[7];
		dst[8] = dst[6];
		dst[7] = dst[5];
		dst[6] = dst[4];
		dst[5] = dst[3];
		dst[4] = dst[2];
		dst[3] = dst[1];
		dst[2] = tempDst8;
		dst[1] = tempDst7;
	}

	vec3 res = mix(mix(dst[6], mix(dst[7], dst[8], step(two_third, f.x)), step(one_third, f.x)),
						   mix(mix(dst[5], mix(dst[0], dst[1], step(two_third, f.x)), step(one_third, f.x)),
							   mix(dst[4], mix(dst[3], dst[2], step(two_third, f.x)), step(one_third, f.x)), step(two_third, f.y)),
																											 step(one_third, f.y));
	fragColor = vec4(res, 1.0);
}
)";

	constexpr char ResizeSabrVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vTexSize;
out vec2 tc;
out vec4 xyp_1_2_3;
out vec4 xyp_5_10_15;
out vec4 xyp_6_7_8;
out vec4 xyp_9_14_9;
out vec4 xyp_11_12_13;
out vec4 xyp_16_17_18;
out vec4 xyp_21_22_23;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;

	vTexSize = texRect.xy;
	tc = aPosition * vec2(1.0004, 1.0);

	float x = 1.0 / vTexSize.x;
	float y = 1.0 / vTexSize.y;

	xyp_1_2_3    = tc.xxxy + vec4(      -x, 0.0,   x, -2.0 * y);
	xyp_6_7_8    = tc.xxxy + vec4(      -x, 0.0,   x,       -y);
	xyp_11_12_13 = tc.xxxy + vec4(      -x, 0.0,   x,      0.0);
	xyp_16_17_18 = tc.xxxy + vec4(      -x, 0.0,   x,        y);
	xyp_21_22_23 = tc.xxxy + vec4(      -x, 0.0,   x,  2.0 * y);
	xyp_5_10_15  = tc.xyyy + vec4(-2.0 * x,  -y, 0.0,        y);
	xyp_9_14_9   = tc.xyyy + vec4( 2.0 * x,  -y, 0.0,        y);
}
)";

	constexpr char ResizeSabrFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vTexSize;
in vec2 tc;
in vec4 xyp_1_2_3;
in vec4 xyp_5_10_15;
in vec4 xyp_6_7_8;
in vec4 xyp_9_14_9;
in vec4 xyp_11_12_13;
in vec4 xyp_16_17_18;
in vec4 xyp_21_22_23;
out vec4 fragColor;

const vec4 Ai  = vec4( 1.0, -1.0, -1.0,  1.0);
const vec4 B45 = vec4( 1.0,  1.0, -1.0, -1.0);
const vec4 C45 = vec4( 1.5,  0.5, -0.5,  0.5);
const vec4 B30 = vec4( 0.5,  2.0, -0.5, -2.0);
const vec4 C30 = vec4( 1.0,  1.0, -0.5,  0.0);
const vec4 B60 = vec4( 2.0,  0.5, -2.0, -0.5);
const vec4 C60 = vec4( 2.0,  0.0, -1.0,  0.5);

const vec4 M45 = vec4(0.4, 0.4, 0.4, 0.4);
const vec4 M30 = vec4(0.2, 0.4, 0.2, 0.4);
const vec4 M60 = M30.yxwz;
const vec4 Mshift = vec4(0.2);

const float coef = 2.0;
const vec4 threshold = vec4(0.32);
const vec3 lum = vec3(0.21, 0.72, 0.07);

bvec4 _and_(bvec4 A, bvec4 B) {
	return bvec4(A.x && B.x, A.y && B.y, A.z && B.z, A.w && B.w);
}

bvec4 _or_(bvec4 A, bvec4 B) {
	return bvec4(A.x || B.x, A.y || B.y, A.z || B.z, A.w || B.w);
}

vec4 lum_to(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
	return vec4(dot(lum, v0), dot(lum, v1), dot(lum, v2), dot(lum, v3));
}

vec4 lum_df(vec4 A, vec4 B) {
	return abs(A - B);
}

bvec4 lum_eq(vec4 A, vec4 B) {
	return lessThan(lum_df(A, B), threshold);
}

vec4 lum_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return lum_df(a, b) + lum_df(a, c) + lum_df(d, e) + lum_df(d, f) + 4.0 * lum_df(g, h);
}

float c_df(vec3 c1, vec3 c2) {
	vec3 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}

void main() {
	// Get mask values by performing texture lookup with the uniform sampler
	vec3 P1  = texture(uTexture, xyp_1_2_3.xw   ).rgb;
	vec3 P2  = texture(uTexture, xyp_1_2_3.yw   ).rgb;
	vec3 P3  = texture(uTexture, xyp_1_2_3.zw   ).rgb;
	
	vec3 P6  = texture(uTexture, xyp_6_7_8.xw   ).rgb;
	vec3 P7  = texture(uTexture, xyp_6_7_8.yw   ).rgb;
	vec3 P8  = texture(uTexture, xyp_6_7_8.zw   ).rgb;
	
	vec3 P11 = texture(uTexture, xyp_11_12_13.xw).rgb;
	vec3 P12 = texture(uTexture, xyp_11_12_13.yw).rgb;
	vec3 P13 = texture(uTexture, xyp_11_12_13.zw).rgb;
	
	vec3 P16 = texture(uTexture, xyp_16_17_18.xw).rgb;
	vec3 P17 = texture(uTexture, xyp_16_17_18.yw).rgb;
	vec3 P18 = texture(uTexture, xyp_16_17_18.zw).rgb;
	
	vec3 P21 = texture(uTexture, xyp_21_22_23.xw).rgb;
	vec3 P22 = texture(uTexture, xyp_21_22_23.yw).rgb;
	vec3 P23 = texture(uTexture, xyp_21_22_23.zw).rgb;
	
	vec3 P5  = texture(uTexture, xyp_5_10_15.xy ).rgb;
	vec3 P10 = texture(uTexture, xyp_5_10_15.xz ).rgb;
	vec3 P15 = texture(uTexture, xyp_5_10_15.xw ).rgb;
	
	vec3 P9  = texture(uTexture, xyp_9_14_9.xy  ).rgb;
	vec3 P14 = texture(uTexture, xyp_9_14_9.xz  ).rgb;
	vec3 P19 = texture(uTexture, xyp_9_14_9.xw  ).rgb;
	
	// Store luminance values of each point in groups of 4
	// so that we may operate on all four corners at once
	vec4 p7  = lum_to(P7,  P11, P17, P13);
	vec4 p8  = lum_to(P8,  P6,  P16, P18);
	vec4 p11 = p7.yzwx;                      // P11, P17, P13, P7
	vec4 p12 = lum_to(P12, P12, P12, P12);
	vec4 p13 = p7.wxyz;                      // P13, P7,  P11, P17
	vec4 p14 = lum_to(P14, P2,  P10, P22);
	vec4 p16 = p8.zwxy;                      // P16, P18, P8,  P6
	vec4 p17 = p7.zwxy;                      // P17, P13, P7,  P11
	vec4 p18 = p8.wxyz;                      // P18, P8,  P6,  P16
	vec4 p19 = lum_to(P19, P3,  P5,  P21);
	vec4 p22 = p14.wxyz;                     // P22, P14, P2,  P10
	vec4 p23 = lum_to(P23, P9,  P1,  P15);
	
	// Scale current texel coordinate to [0..1]
	vec2 fp = fract(tc * vTexSize);
	
	// Determine amount of "smoothing" or mixing that could be done on texel corners
	vec4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);
	vec4 ma30 = smoothstep(C30 - M30, C30 + M30, Ai * fp.y + B30 * fp.x);
	vec4 ma60 = smoothstep(C60 - M60, C60 + M60, Ai * fp.y + B60 * fp.x);
	vec4 marn = smoothstep(C45 - M45 + Mshift, C45 + M45 + Mshift, Ai * fp.y + B45 * fp.x);
	
	// Perform edge weight calculations
	vec4 e45   = lum_wd(p12, p8, p16, p18, p22, p14, p17, p13);
	vec4 econt = lum_wd(p17, p11, p23, p13, p7, p19, p12, p18);
	vec4 e30   = lum_df(p13, p16);
	vec4 e60   = lum_df(p8, p17);
	
	// Calculate rule results for interpolation
	bvec4 r45_1   = _and_(notEqual(p12, p13), notEqual(p12, p17));
	bvec4 r45_2   = _and_(not(lum_eq(p13, p7)), not(lum_eq(p13, p8)));
	bvec4 r45_3   = _and_(not(lum_eq(p17, p11)), not(lum_eq(p17, p16)));
	bvec4 r45_4_1 = _and_(not(lum_eq(p13, p14)), not(lum_eq(p13, p19)));
	bvec4 r45_4_2 = _and_(not(lum_eq(p17, p22)), not(lum_eq(p17, p23)));
	bvec4 r45_4   = _and_(lum_eq(p12, p18), _or_(r45_4_1, r45_4_2));
	bvec4 r45_5   = _or_(lum_eq(p12, p16), lum_eq(p12, p8));
	bvec4 r45     = _and_(r45_1, _or_(_or_(_or_(r45_2, r45_3), r45_4), r45_5));
	bvec4 r30 = _and_(notEqual(p12, p16), notEqual(p11, p16));
	bvec4 r60 = _and_(notEqual(p12, p8), notEqual(p7, p8));
	
	// Combine rules with edge weights
	bvec4 edr45 = _and_(lessThan(e45, econt), r45);
	bvec4 edrrn = lessThanEqual(e45, econt);
	bvec4 edr30 = _and_(lessThanEqual(coef * e30, e60), r30);
	bvec4 edr60 = _and_(lessThanEqual(coef * e60, e30), r60);
	
	// Finalize interpolation rules and cast to float (0.0 for false, 1.0 for true)
	vec4 final45 = vec4(_and_(_and_(not(edr30), not(edr60)), edr45));
	vec4 final30 = vec4(_and_(_and_(edr45, not(edr60)), edr30));
	vec4 final60 = vec4(_and_(_and_(edr45, not(edr30)), edr60));
	vec4 final36 = vec4(_and_(_and_(edr60, edr30), edr45));
	vec4 finalrn = vec4(_and_(not(edr45), edrrn));
	
	// Determine the color to mix with for each corner
	vec4 px = step(lum_df(p12, p17), lum_df(p12, p13));
	
	// Determine the mix amounts by combining the final rule result and corresponding
	// mix amount for the rule in each corner
	vec4 mac = final36 * max(ma30, ma60) + final30 * ma30 + final60 * ma60 + final45 * ma45 + finalrn * marn;

	vec3 res1 = P12;
	res1 = mix(res1, mix(P13, P17, px.x), mac.x);
	res1 = mix(res1, mix(P7, P13, px.y), mac.y);
	res1 = mix(res1, mix(P11, P7, px.z), mac.z);
	res1 = mix(res1, mix(P17, P11, px.w), mac.w);
	
	vec3 res2 = P12;
	res2 = mix(res2, mix(P17, P11, px.w), mac.w);
	res2 = mix(res2, mix(P11, P7, px.z), mac.z);
	res2 = mix(res2, mix(P7, P13, px.y), mac.y);
	res2 = mix(res2, mix(P13, P17, px.x), mac.x);
	
	fragColor = vec4(mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2))), 1.0);
}
)";

	constexpr char ResizeCleanEdgeVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 v_px;
out vec2 size;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;

	size = texRect.xy + 0.0001;
	v_px = aPosition.xy * size;
}
)";

	constexpr char ResizeCleanEdgeFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 v_px;
in vec2 size;
out vec4 fragColor;

#define SLOPE 
#define CLEANUP

vec3 highestColor = vec3(1.,1.,1.);
float similarThreshold = 0.0;
float lineWidth = 1.0;

const float scale = 4.0;
const mat3 yuv_matrix = mat3(vec3(0.299, 0.587, 0.114), vec3(-0.169, -0.331, 0.5), vec3(0.5, -0.419, -0.081));

vec3 yuv(vec3 col){
	mat3 yuv = transpose(yuv_matrix);
	return yuv * col;
}

bool similar(vec4 col1, vec4 col2){
	return (col1.a == 0. && col2.a == 0.) || distance(col1, col2) <= similarThreshold;
}

bool similar3(vec4 col1, vec4 col2, vec4 col3){
	return similar(col1, col2) && similar(col2, col3);
}

bool similar4(vec4 col1, vec4 col2, vec4 col3, vec4 col4){
	return similar(col1, col2) && similar(col2, col3) && similar(col3, col4);
}

bool similar5(vec4 col1, vec4 col2, vec4 col3, vec4 col4, vec4 col5){
	return similar(col1, col2) && similar(col2, col3) && similar(col3, col4) && similar(col4, col5);
}

bool higher(vec4 thisCol, vec4 otherCol){
	if(similar(thisCol, otherCol)) return false;
	if(thisCol.a == otherCol.a){
//		return yuv(thisCol.rgb).x > yuv(otherCol.rgb).x;
//		return distance(yuv(thisCol.rgb), yuv(highestColor)) < distance(yuv(otherCol.rgb), yuv(highestColor));
		return distance(thisCol.rgb, highestColor) < distance(otherCol.rgb, highestColor);
	} else {
		return thisCol.a > otherCol.a;
	}
}

vec4 higherCol(vec4 thisCol, vec4 otherCol){
	return higher(thisCol, otherCol) ? thisCol : otherCol;
}

float cd(vec4 col1, vec4 col2){
	return distance(col1.rgba, col2.rgba);
}

float distToLine(vec2 testPt, vec2 pt1, vec2 pt2, vec2 dir){
  vec2 lineDir = pt2 - pt1;
  vec2 perpDir = vec2(lineDir.y, -lineDir.x);
  vec2 dirToPt1 = pt1 - testPt;
  return (dot(perpDir, dir) > 0.0 ? 1.0 : -1.0) * (dot(normalize(perpDir), dirToPt1));
}

vec4 sliceDist(vec2 point, vec2 mainDir, vec2 pointDir, vec4 ub, vec4 u, vec4 uf, vec4 uff, vec4 b, vec4 c, vec4 f, vec4 ff, vec4 db, vec4 d, vec4 df, vec4 dff, vec4 ddb, vec4 dd, vec4 ddf){
	//clamped range prevents inacccurate identity (no change) result, feel free to disable if necessary
	#ifdef SLOPE
	float minWidth = 0.45;
	float maxWidth = 1.142;
	#else
	float minWidth = 0.0;
	float maxWidth = 1.4;
	#endif
	float _lineWidth = max(minWidth, min(maxWidth, lineWidth));
	point = mainDir * (point - 0.5) + 0.5; //flip point
	
	//edge detection
	float distAgainst = 4.0*cd(f,d) + cd(uf,c) + cd(c,db) + cd(ff,df) + cd(df,dd);
	float distTowards = 4.0*cd(c,df) + cd(u,f) + cd(f,dff) + cd(b,d) + cd(d,ddf);
	bool shouldSlice = 
	  (distAgainst < distTowards)
	  || (distAgainst < distTowards + 0.001) && !higher(c, f); //equivalent edges edge case
	if(similar4(f, d, b, u) && similar4(uf, df, db, ub) && !similar(c, f)){ //checkerboard edge case
		shouldSlice = false;
	}
	if(!shouldSlice) return vec4(-1.0);
	
	//only applicable for very large lineWidth (>1.3)
//	if(similar3(c, f, df)){ //don't make slice for same color
//		return vec4(-1.0);
//	}
	float dist = 1.0;
	bool flip = false;
	vec2 center = vec2(0.5,0.5);
	
	#ifdef SLOPE
	if(similar3(f, d, db) && !similar3(f, d, b) && !similar(uf, db)){ //lower shallow 2:1 slant
		if(similar(c, df) && higher(c, f)){ //single pixel wide diagonal, dont flip
			
		} else {
			//priority edge cases
			if(higher(c, f)){
				flip = true; 
			}
			if(similar(u, f) && !similar(c, df) && !higher(c, u)){
				flip = true; 
			}
		}
		
		if(flip){
			dist = _lineWidth-distToLine(point, center+vec2(1.5, -1.0)*pointDir, center+vec2(-0.5, 0.0)*pointDir, -pointDir); //midpoints of neighbor two-pixel groupings
		} else {
			dist = distToLine(point, center+vec2(1.5, 0.0)*pointDir, center+vec2(-0.5, 1.0)*pointDir, pointDir); //midpoints of neighbor two-pixel groupings
		}
		
		//cleanup slant transitions
		#ifdef CLEANUP
		if(!flip && similar(c, uf) && !(similar3(c, uf, uff) && !similar3(c, uf, ff) && !similar(d, uff))){ //shallow
			float dist2 = distToLine(point, center+vec2(2.0, -1.0)*pointDir, center+vec2(-0.0, 1.0)*pointDir, pointDir); 
			dist = min(dist, dist2);
		}
		#endif
		
		dist -= (_lineWidth/2.0);
		return dist <= 0.0 ? ((cd(c,f) <= cd(c,d)) ? f : d) : vec4(-1.0);
	} else if(similar3(uf, f, d) && !similar3(u, f, d) && !similar(uf, db)){ //forward steep 2:1 slant
		if(similar(c, df) && higher(c, d)){ //single pixel wide diagonal, dont flip
			
		} else {
			//priority edge cases
			if(higher(c, d)){ 
				flip = true; 
			}
			if(similar(b, d) && !similar(c, df) && !higher(c, d)){
				flip = true; 
			}
		}
		
		if(flip){
			dist = _lineWidth-distToLine(point, center+vec2(0.0, -0.5)*pointDir, center+vec2(-1.0, 1.5)*pointDir, -pointDir); //midpoints of neighbor two-pixel groupings
		} else {
			dist = distToLine(point, center+vec2(1.0, -0.5)*pointDir, center+vec2(0.0, 1.5)*pointDir, pointDir); //midpoints of neighbor two-pixel groupings
		}
		
		//cleanup slant transitions
		#ifdef CLEANUP
		if(!flip && similar(c, db) && !(similar3(c, db, ddb) && !similar3(c, db, dd) && !similar(f, ddb))){ //steep
			float dist2 = distToLine(point, center+vec2(1.0, 0.0)*pointDir, center+vec2(-1.0, 2.0)*pointDir, pointDir); 
			dist = min(dist, dist2);
		}
		#endif
		
		dist -= (_lineWidth/2.0);
		return dist <= 0.0 ? ((cd(c,f) <= cd(c,d)) ? f : d) : vec4(-1.0);
	} else 
	#endif
	if(similar(f, d)) { //45 diagonal
		if(similar(c, df) && higher(c, f)){ //single pixel diagonal along neighbors, dont flip
			if(!similar(c, dd) && !similar(c, ff)){ //line against triple color stripe edge case
				flip = true; 
			}
		} else {
			//priority edge cases
			if(higher(c, f)){
				flip = true; 
			}
			if(!similar(c, b) && similar4(b, f, d, u)){
				flip = true;
			}
		}
		//single pixel 2:1 slope, dont flip
		if((( (similar(f, db) && similar3(u, f, df)) || (similar(uf, d) && similar3(b, d, df)) ) && !similar(c, df))){
			flip = true;
		} 
		
		if(flip){
			dist = _lineWidth-distToLine(point, center+vec2(1.0, -1.0)*pointDir, center+vec2(-1.0, 1.0)*pointDir, -pointDir); //midpoints of own diagonal pixels
		} else {
			dist = distToLine(point, center+vec2(1.0, 0.0)*pointDir, center+vec2(0.0, 1.0)*pointDir, pointDir); //midpoints of corner neighbor pixels
		}
		
		//cleanup slant transitions
		#ifdef SLOPE
		#ifdef CLEANUP
		if(!flip && similar3(c, uf, uff) && !similar3(c, uf, ff) && !similar(d, uff)){ //shallow
			float dist2 = distToLine(point, center+vec2(1.5, 0.0)*pointDir, center+vec2(-0.5, 1.0)*pointDir, pointDir); 
			dist = max(dist, dist2);
		} 
		
		if(!flip && similar3(ddb, db, c) && !similar3(dd, db, c) && !similar(ddb, f)){ //steep
			float dist2 = distToLine(point, center+vec2(1.0, -0.5)*pointDir, center+vec2(0.0, 1.5)*pointDir, pointDir); 
			dist = max(dist, dist2);
		}
		#endif
		#endif
		
		dist -= (_lineWidth/2.0);
		return dist <= 0.0 ? ((cd(c,f) <= cd(c,d)) ? f : d) : vec4(-1.0);
	} 
	#ifdef SLOPE
	else if(similar3(ff, df, d) && !similar3(ff, df, c) && !similar(uff, d)){ //far corner of shallow slant 
		
		if(similar(f, dff) && higher(f, ff)){ //single pixel wide diagonal, dont flip
			
		} else {
			//priority edge cases
			if(higher(f, ff)){ 
				flip = true; 
			}
			if(similar(uf, ff) && !similar(f, dff) && !higher(f, uf)){
				flip = true; 
			}
		}
		if(flip){
			dist = _lineWidth-distToLine(point, center+vec2(1.5+1.0, -1.0)*pointDir, center+vec2(-0.5+1.0, 0.0)*pointDir, -pointDir); //midpoints of neighbor two-pixel groupings
		} else {
			dist = distToLine(point, center+vec2(1.5+1.0, 0.0)*pointDir, center+vec2(-0.5+1.0, 1.0)*pointDir, pointDir); //midpoints of neighbor two-pixel groupings
		}
		
		dist -= (_lineWidth/2.0);
		return dist <= 0.0 ? ((cd(f,ff) <= cd(f,df)) ? ff : df) : vec4(-1.0);
	} else if(similar3(f, df, dd) && !similar3(c, df, dd) && !similar(f, ddb)){ //far corner of steep slant
		if(similar(d, ddf) && higher(d, dd)){ //single pixel wide diagonal, dont flip
			
		} else {
			//priority edge cases
			if(higher(d, dd)){ 
				flip = true; 
			}
			if(similar(db, dd) && !similar(d, ddf) && !higher(d, dd)){
				flip = true; 
			}
//			if(!higher(d, dd)){
//				return vec4(1.0);
//				flip = true; 
//			}
		}
		
		if(flip){
			dist = _lineWidth-distToLine(point, center+vec2(0.0, -0.5+1.0)*pointDir, center+vec2(-1.0, 1.5+1.0)*pointDir, -pointDir); //midpoints of neighbor two-pixel groupings
		} else {
			dist = distToLine(point, center+vec2(1.0, -0.5+1.0)*pointDir, center+vec2(0.0, 1.5+1.0)*pointDir, pointDir); //midpoints of neighbor two-pixel groupings
		}
		dist -= (_lineWidth/2.0);
		return dist <= 0.0 ? ((cd(d,df) <= cd(d,dd)) ? df : dd) : vec4(-1.0);
	}
	#endif
	return vec4(-1.0);
}

void main() {
	vec2 local = fract(v_px);
	vec2 px = ceil(v_px);
	
	vec2 pointDir = round(local)*2.0-1.0;
	
	//neighbor pixels
	//Up, Down, Forward, and Back
	//relative to quadrant of current location within pixel
	
	vec4 uub = texture(uTexture, (px+vec2(-1.0,-2.0)*pointDir)/size);
	vec4 uu  = texture(uTexture, (px+vec2( 0.0,-2.0)*pointDir)/size);
	vec4 uuf = texture(uTexture, (px+vec2( 1.0,-2.0)*pointDir)/size);
	
	vec4 ubb = texture(uTexture, (px+vec2(-2.0,-2.0)*pointDir)/size);
	vec4 ub  = texture(uTexture, (px+vec2(-1.0,-1.0)*pointDir)/size);
	vec4 u   = texture(uTexture, (px+vec2( 0.0,-1.0)*pointDir)/size);
	vec4 uf  = texture(uTexture, (px+vec2( 1.0,-1.0)*pointDir)/size);
	vec4 uff = texture(uTexture, (px+vec2( 2.0,-1.0)*pointDir)/size);
	
	vec4 bb  = texture(uTexture, (px+vec2(-2.0, 0.0)*pointDir)/size);
	vec4 b   = texture(uTexture, (px+vec2(-1.0, 0.0)*pointDir)/size);
	vec4 c   = texture(uTexture, (px+vec2( 0.0, 0.0)*pointDir)/size);
	vec4 f   = texture(uTexture, (px+vec2( 1.0, 0.0)*pointDir)/size);
	vec4 ff  = texture(uTexture, (px+vec2( 2.0, 0.0)*pointDir)/size);
	
	vec4 dbb = texture(uTexture, (px+vec2(-2.0, 1.0)*pointDir)/size);
	vec4 db  = texture(uTexture, (px+vec2(-1.0, 1.0)*pointDir)/size);
	vec4 d   = texture(uTexture, (px+vec2( 0.0, 1.0)*pointDir)/size);
	vec4 df  = texture(uTexture, (px+vec2( 1.0, 1.0)*pointDir)/size);
	vec4 dff = texture(uTexture, (px+vec2( 2.0, 1.0)*pointDir)/size);
	
	vec4 ddb = texture(uTexture, (px+vec2(-1.0, 2.0)*pointDir)/size);
	vec4 dd  = texture(uTexture, (px+vec2( 0.0, 2.0)*pointDir)/size);
	vec4 ddf = texture(uTexture, (px+vec2( 1.0, 2.0)*pointDir)/size);
	
	vec4 col = c;
	
	//c_orner, b_ack, and u_p slices
	// (slices from neighbor pixels will only ever reach these 3 quadrants
	vec4 c_col = sliceDist(local, vec2( 1.0, 1.0), pointDir, ub, u, uf, uff, b, c, f, ff, db, d, df, dff, ddb, dd, ddf);
	vec4 b_col = sliceDist(local, vec2(-1.0, 1.0), pointDir, uf, u, ub, ubb, f, c, b, bb, df, d, db, dbb, ddf, dd, ddb);
	vec4 u_col = sliceDist(local, vec2( 1.0,-1.0), pointDir, db, d, df, dff, b, c, f, ff, ub, u, uf, uff, uub, uu, uuf);
	
	if(c_col.r >= 0.0){
		col = c_col;
	}
	if(b_col.r >= 0.0){
		col = b_col;
	}
	if(u_col.r >= 0.0){
		col = u_col;
	}
	
	fragColor = col;
}
)";

	constexpr char ResizeCrtScanlinesVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out highp vec2 vPixelCoords;
out vec2 vTexCoords;
out highp vec2 vOutputSize;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * spriteSize.xy;
	vTexCoords = aPosition;
	vOutputSize = spriteSize.xy;
}
)";

	constexpr char ResizeCrtScanlinesFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in highp vec2 vPixelCoords;
in vec2 vTexCoords;
in highp vec2 vOutputSize;
out vec4 fragColor;

vec3 toYiq(vec3 value) {
	const mat3 yiqmat = mat3(
		0.2989, 0.5870, 0.1140,
		0.5959, -0.2744, -0.3216,
		0.2115, -0.5229, 0.3114);
	return value * yiqmat;
}

vec3 fromYiq(vec3 value) {
	const mat3 rgbmat = mat3(
		1.0, 0.956, 0.6210,
		1.0, -0.2720, -0.6474,
		1.0, -1.1060, 1.7046);
	return value * rgbmat;
}

void main() {
	float y = vPixelCoords.y;
	vec2 uv0 = vec2(vTexCoords.x, y / vOutputSize.y);
	vec3 t0 = texture(uTexture, uv0).rgb;
	float ymod = mod(vPixelCoords.y, 3.0);
	if (ymod > 2.0) {
		vec2 uv1 = vec2(vTexCoords.x, (y + 1.0) / vOutputSize.y);
		vec3 t1 = texture(uTexture, uv1).rgb;
		fragColor.rgb = (t0 + t1) * 0.25;
	} else {
		t0 = toYiq(t0);
		t0.r *= 1.1;
		t0 = fromYiq(t0);
		fragColor.rgb = t0;
	}
	fragColor.a = 1.0;
}
)";

	constexpr char ResizeCrtVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vTexCoords;
out vec2 vTexSize;
out vec2 vViewSize;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = aPosition;
	vTexSize = texRect.xy;
	vViewSize = spriteSize;
}
)";

	constexpr char ResizeCrtShadowMaskFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

#define hardScan -6.0 /*-8.0*/
#define hardPix -3.0
#define warpX 0.031
#define warpY 0.041
#define maskDark 0.55
#define maskLight 1.5
#define scaleInLinearGamma 1
#define brightboost 1.0
#define hardBloomScan -2.0
#define hardBloomPix -1.5
#define bloomAmount 1.0/12.0 /*1.0/16.0*/
#define shape 2.0

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec2 vTexSize;
in vec2 vViewSize;
out vec4 fragColor;

#ifdef SIMPLE_LINEAR_GAMMA
	float ToLinear1(float c) {
		return c;
	}
	vec3 ToLinear(vec3 c) {
		return c;
	}
	vec3 ToSrgb(vec3 c) {
		return pow(c, 1.0 / 2.2);
	}
#else
	float ToLinear1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);
	}
	vec3 ToLinear(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));
	}
	float ToSrgb1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<0.0031308?c*12.92:1.055*pow(c,0.41666)-0.055);
	}
	vec3 ToSrgb(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToSrgb1(c.r),ToSrgb1(c.g),ToSrgb1(c.b));
	}
#endif

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos, vec2 off, vec2 texture_size) {
	pos=(floor(pos*texture_size.xy+off)+vec2(0.5,0.5))/texture_size.xy;
#ifdef SIMPLE_LINEAR_GAMMA
	return ToLinear(vec3(brightboost) * pow(texture(uTexture,pos.xy).rgb, 2.2));
#else
	return ToLinear(vec3(brightboost) * texture(uTexture,pos.xy).rgb);
#endif
}

// Distance in emulated pixels to nearest texel
vec2 Dist(vec2 pos, vec2 texture_size) {
	pos=pos*texture_size.xy;
	return -((pos-floor(pos))-vec2(0.5, 0.5));
}

// 1D Gaussian
float Gaus(float pos,float scale) {
	return exp2(scale*pow(abs(pos),shape));
}

// 3-tap Gaussian filter along horz line
vec3 Horz3(vec2 pos, float off, vec2 texture_size) {
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	// Return filtered sample.
	return (b*wb+c*wc+d*wd)/(wb+wc+wd);
}
  
// 5-tap Gaussian filter along horz line
vec3 Horz5(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 2.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wa=Gaus(dst-2.0,scale);
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	float we=Gaus(dst+2.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);
}

// 7-tap Gaussian filter along horz line
vec3 Horz7(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-3.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 c=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 f=Fetch(pos,vec2( 2.0,off),texture_size);
	vec3 g=Fetch(pos,vec2( 3.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardBloomPix;
	float wa=Gaus(dst-3.0,scale);
	float wb=Gaus(dst-2.0,scale);
	float wc=Gaus(dst-1.0,scale);
	float wd=Gaus(dst+0.0,scale);
	float we=Gaus(dst+1.0,scale);
	float wf=Gaus(dst+2.0,scale);
	float wg=Gaus(dst+3.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we+f*wf+g*wg)/(wa+wb+wc+wd+we+wf+wg);
}

// Return scanline weight
float Scan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardScan);
}
  
  // Return scanline weight for bloom
float BloomScan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardBloomScan);
}

// Allow nearest three lines to effect pixel
vec3 Tri(vec2 pos, vec2 texture_size){
	vec3 a=Horz3(pos,-1.0, texture_size);
	vec3 b=Horz5(pos, 0.0, texture_size);
	vec3 c=Horz3(pos, 1.0, texture_size);
	float wa=Scan(pos,-1.0, texture_size);
	float wb=Scan(pos, 0.0, texture_size);
	float wc=Scan(pos, 1.0, texture_size);
	return a*wa+b*wb+c*wc;
}
  
// Small bloom
vec3 Bloom(vec2 pos, vec2 texture_size) {
	vec3 a=Horz5(pos,-2.0, texture_size);
	vec3 b=Horz7(pos,-1.0, texture_size);
	vec3 c=Horz7(pos, 0.0, texture_size);
	vec3 d=Horz7(pos, 1.0, texture_size);
	vec3 e=Horz5(pos, 2.0, texture_size);
	float wa=BloomScan(pos,-2.0, texture_size);
	float wb=BloomScan(pos,-1.0, texture_size);
	float wc=BloomScan(pos, 0.0, texture_size);
	float wd=BloomScan(pos, 1.0, texture_size);
	float we=BloomScan(pos, 2.0, texture_size);
	return a*wa+b*wb+c*wc+d*wd+e*we;
}

// Distortion of scanlines, and end of screen alpha
vec2 Warp(vec2 pos) {
	pos=pos*2.0-1.0;    
	pos*=vec2(1.0+(pos.y*pos.y)*warpX,1.0+(pos.x*pos.x)*warpY);
	return pos*0.5+0.5;
}

// Shadow mask 
vec3 Mask(vec2 pos) {
	vec3 mask = vec3(maskDark,maskDark,maskDark);

	pos.xy=floor(pos.xy*vec2(1.0,0.5));
	pos.x+=pos.y*3.0;
	pos.x=fract(pos.x/6.0);

	if(pos.x<0.333)mask.r=maskLight;
	else if(pos.x<0.666)mask.g=maskLight;
	else mask.b=maskLight;

	return mask;
}    

vec4 crt_lottes(vec2 texture_size, vec2 video_size, vec2 output_size, vec2 tex) {
	vec2 pos=Warp(tex.xy*(texture_size.xy/video_size.xy))*(video_size.xy/texture_size.xy);	
	vec3 outColor = Tri(pos, texture_size);

	outColor.rgb+=Bloom(pos, texture_size)*bloomAmount;
	outColor.rgb*=Mask(floor(tex.xy*(texture_size.xy/video_size.xy)*output_size.xy)+vec2(0.5,0.5));

	return vec4(ToSrgb(outColor.rgb),1.0);
}

void main() {
	fragColor = crt_lottes(vTexSize, vTexSize, vViewSize, vTexCoords);
}
)";

	constexpr char ResizeCrtApertureGrilleFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

#define hardScan -8.0
#define hardPix -3.0
#define warpX 0.0155
#define warpY 0.0205
#define maskDark 0.7
#define maskLight 1.5
#define scaleInLinearGamma 1
#define brightboost 1.1
#define hardBloomScan -2.0
#define hardBloomPix -1.5
#define bloomAmount 1.0/16.0
#define shape 2.0

uniform sampler2D uTexture;

in vec2 vTexCoords;
in vec2 vTexSize;
in vec2 vViewSize;
out vec4 fragColor;

#ifdef SIMPLE_LINEAR_GAMMA
	float ToLinear1(float c) {
		return c;
	}
	vec3 ToLinear(vec3 c) {
		return c;
	}
	vec3 ToSrgb(vec3 c) {
		return pow(c, 1.0 / 2.2);
	}
#else
	float ToLinear1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);
	}
	vec3 ToLinear(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));
	}
	float ToSrgb1(float c) {
		if (scaleInLinearGamma==0) return c;
		return(c<0.0031308?c*12.92:1.055*pow(c,0.41666)-0.055);
	}
	vec3 ToSrgb(vec3 c) {
		if (scaleInLinearGamma==0) return c;
		return vec3(ToSrgb1(c.r),ToSrgb1(c.g),ToSrgb1(c.b));
	}
#endif

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos, vec2 off, vec2 texture_size) {
	pos=(floor(pos*texture_size.xy+off)+vec2(0.5,0.5))/texture_size.xy;
#ifdef SIMPLE_LINEAR_GAMMA
	return ToLinear(vec3(brightboost) * pow(texture(uTexture,pos.xy).rgb, 2.2));
#else
	return ToLinear(vec3(brightboost) * texture(uTexture,pos.xy).rgb);
#endif
}

// Distance in emulated pixels to nearest texel
vec2 Dist(vec2 pos, vec2 texture_size) {
	pos=pos*texture_size.xy;
	return -((pos-floor(pos))-vec2(0.5, 0.5));
}

// 1D Gaussian
float Gaus(float pos,float scale) {
	return exp2(scale*pow(abs(pos),shape));
}

// 3-tap Gaussian filter along horz line
vec3 Horz3(vec2 pos, float off, vec2 texture_size) {
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	// Return filtered sample.
	return (b*wb+c*wc+d*wd)/(wb+wc+wd);
}
  
// 5-tap Gaussian filter along horz line
vec3 Horz5(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 c=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 2.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardPix;
	float wa=Gaus(dst-2.0,scale);
	float wb=Gaus(dst-1.0,scale);
	float wc=Gaus(dst+0.0,scale);
	float wd=Gaus(dst+1.0,scale);
	float we=Gaus(dst+2.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);
}

// 7-tap Gaussian filter along horz line
vec3 Horz7(vec2 pos, float off, vec2 texture_size) {
	vec3 a=Fetch(pos,vec2(-3.0,off),texture_size);
	vec3 b=Fetch(pos,vec2(-2.0,off),texture_size);
	vec3 c=Fetch(pos,vec2(-1.0,off),texture_size);
	vec3 d=Fetch(pos,vec2( 0.0,off),texture_size);
	vec3 e=Fetch(pos,vec2( 1.0,off),texture_size);
	vec3 f=Fetch(pos,vec2( 2.0,off),texture_size);
	vec3 g=Fetch(pos,vec2( 3.0,off),texture_size);
	float dst=Dist(pos, texture_size).x;
	// Convert distance to weight.
	float scale=hardBloomPix;
	float wa=Gaus(dst-3.0,scale);
	float wb=Gaus(dst-2.0,scale);
	float wc=Gaus(dst-1.0,scale);
	float wd=Gaus(dst+0.0,scale);
	float we=Gaus(dst+1.0,scale);
	float wf=Gaus(dst+2.0,scale);
	float wg=Gaus(dst+3.0,scale);
	// Return filtered sample.
	return (a*wa+b*wb+c*wc+d*wd+e*we+f*wf+g*wg)/(wa+wb+wc+wd+we+wf+wg);
}

// Return scanline weight
float Scan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardScan);
}
  
  // Return scanline weight for bloom
float BloomScan(vec2 pos,float off, vec2 texture_size) {
	float dst=Dist(pos, texture_size).y;
	return Gaus(dst+off,hardBloomScan);
}

// Allow nearest three lines to effect pixel
vec3 Tri(vec2 pos, vec2 texture_size){
	vec3 a=Horz3(pos,-1.0, texture_size);
	vec3 b=Horz5(pos, 0.0, texture_size);
	vec3 c=Horz3(pos, 1.0, texture_size);
	float wa=Scan(pos,-1.0, texture_size);
	float wb=Scan(pos, 0.0, texture_size);
	float wc=Scan(pos, 1.0, texture_size);
	return a*wa+b*wb+c*wc;
}
  
// Small bloom
vec3 Bloom(vec2 pos, vec2 texture_size) {
	vec3 a=Horz5(pos,-2.0, texture_size);
	vec3 b=Horz7(pos,-1.0, texture_size);
	vec3 c=Horz7(pos, 0.0, texture_size);
	vec3 d=Horz7(pos, 1.0, texture_size);
	vec3 e=Horz5(pos, 2.0, texture_size);
	float wa=BloomScan(pos,-2.0, texture_size);
	float wb=BloomScan(pos,-1.0, texture_size);
	float wc=BloomScan(pos, 0.0, texture_size);
	float wd=BloomScan(pos, 1.0, texture_size);
	float we=BloomScan(pos, 2.0, texture_size);
	return a*wa+b*wb+c*wc+d*wd+e*we;
}

// Distortion of scanlines, and end of screen alpha
vec2 Warp(vec2 pos) {
	pos=pos*2.0-1.0;    
	pos*=vec2(1.0+(pos.y*pos.y)*warpX,1.0+(pos.x*pos.x)*warpY);
	return pos*0.5+0.5;
}

// Shadow mask 
vec3 Mask(vec2 pos) {
	vec3 mask = vec3(maskDark,maskDark,maskDark);

	pos.x=fract(pos.x/3.0);

	if(pos.x<0.333)mask.r=maskLight;
	else if(pos.x<0.666)mask.g=maskLight;
	else mask.b=maskLight;

	return mask;
}    

vec4 crt_lottes(vec2 texture_size, vec2 video_size, vec2 output_size, vec2 tex) {
	vec2 pos=Warp(tex.xy*(texture_size.xy/video_size.xy))*(video_size.xy/texture_size.xy);
	vec3 outColor = Tri(pos, texture_size);

	outColor.rgb+=Bloom(pos, texture_size)*bloomAmount;
	outColor.rgb*=Mask(floor(tex.xy*(texture_size.xy/video_size.xy)*output_size.xy)+vec2(0.5,0.5));

	return vec4(ToSrgb(outColor.rgb),1.0);
}

void main() {
	fragColor = crt_lottes(vTexSize, vTexSize, vViewSize, vTexCoords);
}
)";

	constexpr char ResizeMonochromeVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vPixelCoords;
out vec2 vTexCoords;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * texRect.xy;
	vTexCoords = aPosition;
}
)";

	constexpr char ResizeMonochromeFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

mat4 bayerIndex = mat4(
	vec4(0.0625, 0.5625, 0.1875, 0.6875),
	vec4(0.8125, 0.3125, 0.9375, 0.4375),
	vec4(0.25, 0.75, 0.125, 0.625),
	vec4(1.0, 0.5, 0.875, 0.375));

uniform sampler2D uTexture;

in vec2 vPixelCoords;
in vec2 vTexCoords;
out vec4 fragColor;

float dither4x4(vec2 position, float brightness) {
	float bayerValue = bayerIndex[int(position.x) % 4][int(position.y) % 4];
	return brightness + (brightness < bayerValue ? -0.05 : 0.1);
}

void main() {
	vec3 color = texture(uTexture, vTexCoords).rgb;
	float gray = dot(((color - vec3(0.5)) * vec3(1.4, 1.2, 1.0)) + vec3(0.5), vec3(0.3, 0.7, 0.1));
	gray = dither4x4(vPixelCoords, gray);
	float palette = (abs(1.0 - gray) * 0.75) + 0.125;

	if (palette < 0.25) {
		color = vec3(0.675, 0.710, 0.420);
	} else if (palette < 0.5) {
		color = vec3(0.463, 0.518, 0.283);
	} else if (palette < 0.75) {
		color = vec3(0.247, 0.314, 0.247);
	} else {
		color = vec3(0.1, 0.134, 0.151);
	}
	fragColor = vec4(color, 1.0);
}
)";

	constexpr char AntialiasingVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vPixelCoords;
out vec2 vTexSizeInv;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * texRect.xy + 0.5;
	vTexSizeInv = 1.0 / texRect.xy;
}
)";

	constexpr char AntialiasingFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

in vec2 vPixelCoords;
in vec2 vTexSizeInv;
out vec4 fragColor;

vec3 cubicHermite(vec3 A, vec3 B, vec3 C, vec3 D, float t) {
	float t2 = t*t;
	float t3 = t*t*t;
	vec3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
	vec3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
	vec3 c = -A/2.0 + C/2.0;
	vec3 d = B;
	return a*t3 + b*t2 + c*t + d;
}

void main() {
	vec2 frac = fract(vPixelCoords);
	vec2 pixel = floor(vPixelCoords) * vTexSizeInv - vec2(vTexSizeInv * 0.5);
	vec2 vTexSizeInv2 = 2.0 * vTexSizeInv;

	vec3 C00 = texture(uTexture, pixel + vec2(-vTexSizeInv.x, -vTexSizeInv.y)).rgb;
	vec3 C10 = texture(uTexture, pixel + vec2(0.0, -vTexSizeInv.y)).rgb;
	vec3 C20 = texture(uTexture, pixel + vec2(vTexSizeInv.x, -vTexSizeInv.y)).rgb;
	vec3 C30 = texture(uTexture, pixel + vec2(vTexSizeInv2.x, -vTexSizeInv.y)).rgb;

	vec3 C01 = texture(uTexture, pixel + vec2(-vTexSizeInv.x, 0.0)).rgb;
	vec3 C11 = texture(uTexture, pixel + vec2(0.0, 0.0)).rgb;
	vec3 C21 = texture(uTexture, pixel + vec2(vTexSizeInv.x, 0.0)).rgb;
	vec3 C31 = texture(uTexture, pixel + vec2(vTexSizeInv2.x, 0.0)).rgb;    

	vec3 C02 = texture(uTexture, pixel + vec2(-vTexSizeInv.x , vTexSizeInv.y)).rgb;
	vec3 C12 = texture(uTexture, pixel + vec2(0.0, vTexSizeInv.y)).rgb;
	vec3 C22 = texture(uTexture, pixel + vec2(vTexSizeInv.x , vTexSizeInv.y)).rgb;
	vec3 C32 = texture(uTexture, pixel + vec2(vTexSizeInv2.x, vTexSizeInv.y)).rgb;

	vec3 C03 = texture(uTexture, pixel + vec2(-vTexSizeInv.x, vTexSizeInv2.y)).rgb;
	vec3 C13 = texture(uTexture, pixel + vec2(0.0, vTexSizeInv2.y)).rgb;
	vec3 C23 = texture(uTexture, pixel + vec2(vTexSizeInv.x, vTexSizeInv2.y)).rgb;
	vec3 C33 = texture(uTexture, pixel + vec2(vTexSizeInv2.x, vTexSizeInv2.y)).rgb;

	vec3 CP0X = cubicHermite(C00, C10, C20, C30, frac.x);
	vec3 CP1X = cubicHermite(C01, C11, C21, C31, frac.x);
	vec3 CP2X = cubicHermite(C02, C12, C22, C32, frac.x);
	vec3 CP3X = cubicHermite(C03, C13, C23, C33, frac.x);

	fragColor = vec4(cubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y), 1.0);
}
)";

	constexpr char TransitionVs[] = "#line " DEATH_LINE_STRING "\n" R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

out vec2 vTexCoords;
out vec2 vCorrection;
out float vProgressTime;

void main() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vCorrection = spriteSize / vec2(max(spriteSize.x, spriteSize.y));
	vProgressTime = color.a;
}
)";

	constexpr char TransitionFs[] = "#line " DEATH_LINE_STRING "\n" R"(
#ifdef GL_ES
precision mediump float;
#endif

in vec2 vTexCoords;
in vec2 vCorrection;
in float vProgressTime;
out vec4 fragColor;

float rand(vec2 xy) {
	return fract(sin(dot(xy.xy, vec2(12.9898,78.233))) * 43758.5453);
}

float ease(float time) {
	time *= 2.0;
	if (time < 1.0)  {
		return 0.5 * time * time;
	}

	time -= 1.0;
	return -0.5 * (time * (time - 2.0) - 1.0);
}

void main() {
	vec2 uv = (vTexCoords - vec2(0.5)) * vCorrection;
	float distance = length(uv);

	float progressInner = vProgressTime - 0.22;
	distance = (clamp(distance, progressInner, vProgressTime) - progressInner) / (vProgressTime - progressInner);

	float mixValue = ease(distance);
	float noise = 1.0 + rand(uv) * 0.1;
	fragColor = vec4(0.0, 0.0, 0.0, mixValue * noise);
}
)";
}

#endif