#pragma once

namespace Jazz2::Shaders
{
	constexpr uint64_t Version = 4;

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

mat4 bayerIndex = mat4(
	vec4(0.0/16.0, 12.0/16.0, 3.0/16.0, 15.0/16.0),
	vec4(8.0/16.0, 4.0/16.0, 11.0/16.0, 7.0/16.0),
	vec4(2.0/16.0, 14.0/16.0, 1.0/16.0, 13.0/16.0),
	vec4(10.0/16.0, 6.0/16.0, 9.0/16.0, 5.0/16.0));

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
precision mediump float;
#endif

mat4 bayerIndex = mat4(
	vec4(0.0/16.0, 12.0/16.0, 3.0/16.0, 15.0/16.0),
	vec4(8.0/16.0, 4.0/16.0, 11.0/16.0, 7.0/16.0),
	vec4(2.0/16.0, 14.0/16.0, 1.0/16.0, 13.0/16.0),
	vec4(10.0/16.0, 6.0/16.0, 9.0/16.0, 5.0/16.0));

uniform sampler2D uTexture;
uniform sampler2D uTextureLighting;
uniform sampler2D uTextureBlurHalf;
uniform sampler2D uTextureBlurQuarter;
uniform sampler2D uTextureDisplacement;

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

// Perlin noise
vec4 permute(vec4 x) {
	return mod(34.0 * (x * x) + x, 289.0);
}

vec2 fade(vec2 t) {
	return 6.0*(t * t * t * t * t)-15.0*(t * t * t * t)+10.0*(t * t * t);
}

float perlinNoise2D(vec2 P) {
	vec4 Pi = floor(P.xyxy) + vec4(0.0, 0.0, 1.0, 1.0);
	vec4 Pf = fract(P.xyxy) - vec4(0.0, 0.0, 1.0, 1.0);
	vec4 ix = Pi.xzxz;
	vec4 iy = Pi.yyww;
	vec4 fx = Pf.xzxz;
	vec4 fy = Pf.yyww;
	vec4 i = permute(permute(ix) + iy);
	vec4 gx = fract(i/41.0)*2.0-1.0;
	vec4 gy = abs(gx)-0.5;
	vec4 tx = floor(gx+0.5);
	gx = gx-tx;
	vec2 g00 = vec2(gx.x, gy.x);
	vec2 g10 = vec2(gx.y, gy.y);
	vec2 g01 = vec2(gx.z, gy.z);
	vec2 g11 = vec2(gx.w, gy.w);
	vec4 norm = 1.79284291400159 - 0.85373472095314 * vec4(dot(g00,g00),dot(g01,g01),dot(g10,g10),dot(g11,g11));
	g00 *= norm.x;
	g01 *= norm.y;
	g10 *= norm.z;
	g11 *= norm.w;
	float n00 = dot(g00, vec2(fx.x,fy.x));
	float n10 = dot(g10, vec2(fx.y,fy.y));
	float n01 = dot(g01, vec2(fx.z,fy.z));
	float n11 = dot(g11, vec2(fx.w,fy.w));
	vec2 fade_xy = fade(Pf.xy);
	vec2 n_x = mix(vec2(n00, n01), vec2(n10, n11), fade_xy.x);
	float n_xy = mix(n_x.x, n_x.y, fade_xy.y);
	return 2.3 * n_xy;
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
	vec2 dis = (texture(uTextureDisplacement, disPos).xy - vec2(0.5)) * vec2(0.01);
	
	vec2 uv = clamp(uvLocal + (vec2(0.004 * sin(uTime * 16.0 + uvWorld.y * 20.0), 0.0) + dis) * vec2(isTexelBelow), vec2(0.0), vec2(1.0));
	vec4 main = texture(uTexture, uv);

	// Chromatic Aberration
	float aberration = abs(uvLocal.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main.rgb = mix(main.rgb, waterColor * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));
	
	// Rays
	float noisePos = uvWorld.x * 8.0 + uvWorldCenter.y * 0.5 + (1.0 - uvLocal.y - uvLocal.x) * -5.0;
	float rays = perlinNoise2D(vec2(noisePos, uTime * 10.0 + uvWorldCenter.y)) * 0.5 + 0.4;
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
	float horizonDepth = pow(distance, 2.0);

	float yShift = (vTexCoords.y > 0.5 ? 1.0 : 0.0);
	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));

	vec2 texturePos = vec2(
		(uShift.x / 256.0) + (vTexCoords.x - 0.5   ) * (0.5 + (1.5 * horizonDepth)) * correction,
		(uShift.y / 256.0) + (vTexCoords.y - yShift) * 2.0 * distance
	);

	vec4 texColor = texture(uTexture, texturePos);
	float horizonOpacity = clamp(pow(distance, 1.8) - 0.4, 0.0, 1.0);
	
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