#pragma once

namespace Jazz2::Shaders
{
	constexpr char LightingVs[] = R"(
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

void main()
{
	vec2 aPosition = vec2(0.5 - float(gl_VertexID >> 1), 0.5 - float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = vec4(color.x, color.y, aPosition.x * 2.0, aPosition.y * 2.0);
}
)";

	constexpr char BatchedLightingVs[] = R"(
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
#ifdef WITH_FIXED_BATCH_SIZE
	Instance[BATCH_SIZE] instances;
#else
	Instance[585] instances;
#endif
} block;

out vec4 vTexCoords;
out vec4 vColor;

#define i block.instances[gl_VertexID / 6]

void main()
{
	vec2 aPosition = vec2(-0.5 + float(((gl_VertexID + 2) / 3) % 2), -0.5 + float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = i.texRect;
	vColor = vec4(i.color.x, i.color.y, aPosition.x * 2.0, aPosition.y * 2.0);
}
)";

	constexpr char LightingFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec2 ViewSize;

uniform sampler2D uTexture; // Normal

in vec4 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

float lightBlend(float t) {
	return t * t;
}

void main() {
	vec2 center = vTexCoords.xy;
	float radiusNear = vTexCoords.z;
	float intensity = vColor.r;
	float brightness = vColor.g;

	float dist = distance(vec2(0.0, 0.0), vec2(vColor.z, vColor.w));
	if (dist > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// TODO
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

	constexpr char BlurFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D uTexture;
uniform vec2 uPixelOffset;
uniform vec2 uDirection;
in vec2 vTexCoords;
out vec4 fragColor;
void main()
{
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

	constexpr char DownsampleFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D uTexture;
uniform vec2 uPixelOffset;
in vec2 vTexCoords;
out vec4 fragColor;
void main()
{
	vec4 color = texture(uTexture, vTexCoords);
	color += texture(uTexture, vTexCoords + vec2(0.0, uPixelOffset.y));
	color += texture(uTexture, vTexCoords + vec2(uPixelOffset.x, 0.0));
	color += texture(uTexture, vTexCoords + uPixelOffset);
	fragColor = vec4(0.25) * color;
}
)";

	constexpr char CombineFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform sampler2D lightTex;
uniform sampler2D blurHalfTex;
uniform sampler2D blurQuarterTex;

uniform float ambientLight;
uniform vec4 darknessColor;

in vec2 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

void main() {
	vec4 blur1 = texture(blurHalfTex, vTexCoords);
	vec4 blur2 = texture(blurQuarterTex, vTexCoords);

	vec4 main = texture(uTexture, vTexCoords);
	vec4 light = texture(lightTex, vTexCoords);

	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);

	fragColor = mix(mix(
		main * (1.0 + light.g),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(ambientLight, 0.35)), 0.0, 1.0))
	), darknessColor, vec4(1.0 - light.r));
	fragColor.a = 1.0;
}
)";

	constexpr char CombineWithWaterFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec2 ViewSize;
uniform vec3 CameraPosition;
uniform float GameTime;

uniform sampler2D uTexture;
uniform sampler2D lightTex;
uniform sampler2D blurHalfTex;
uniform sampler2D blurQuarterTex;
uniform sampler2D displacementTex;

uniform float ambientLight;
uniform vec4 darknessColor;
uniform float waterLevel;

in vec2 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

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
	// TODO: Remove this multiply
	float time = GameTime * 0.065;
	vec2 viewSizeInv = (1.0 / ViewSize);

	vec2 uvWorldCenter = (CameraPosition.xy * viewSizeInv.xy);
	vec2 uvWorld = vTexCoords + uvWorldCenter;

	float waveHeight = wave(uvWorld.x, time);
	float isTexelBelow = aastep(waveHeight, vTexCoords.y - waterLevel);
	float isTexelAbove = 1.0 - isTexelBelow;

	// Displacement
	vec2 disPos = uvWorld * vec2(0.4) + vec2(mod(time * 0.8, 2.0));
	vec2 dis = (texture(displacementTex, disPos).xy - vec2(0.5)) * vec2(0.014);
	
	vec2 uv = vTexCoords + dis * vec2(isTexelBelow);
	vec4 main = texture(uTexture, uv);

	// Chromatic Aberration
	float aberration = abs(vTexCoords.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main.rgb = mix(main.rgb, waterColor * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));
	
	// Rays
	float noisePos = uvWorld.x * 8.0 + uvWorldCenter.y * 0.5 + (1.0 - vTexCoords.y - vTexCoords.x) * -5.0;
	float rays = perlinNoise2D(vec2(noisePos, time * 10.0 + uvWorldCenter.y)) * 0.5 + 0.4;
	main.rgb += vec3(rays * isTexelBelow * max(1.0 - vTexCoords.y * 1.4, 0.0) * 0.6);
	
	// Waves
	float topDist = abs(vTexCoords.y - waterLevel - waveHeight);
	float isNearTop = 1.0 - aastep(viewSizeInv.y * 2.8, topDist);
	float isVeryNearTop = 1.0 - aastep(viewSizeInv.y * (0.8 - 100.0 * waveHeight), topDist);

	float topColorBlendFac = isNearTop * isTexelBelow * 0.6;
	main.rgb = mix(main.rgb, texture(uTexture, vec2(vTexCoords.x,
		(waterLevel - vTexCoords.y + waterLevel) * 0.97 + waveHeight - viewSizeInv.y * 1.0
	)).rgb, vec3(topColorBlendFac));
	main.rgb += vec3(0.2 * isVeryNearTop);
	
	// Lighting
	vec4 blur1 = texture(blurHalfTex, uv);
	vec4 blur2 = texture(blurQuarterTex, uv);
	vec4 light = texture(lightTex, uv);
	
	vec4 blur = (blur1 + blur2) * vec4(0.5);

	float gray = dot(blur.rgb, vec3(0.299, 0.587, 0.114));
	blur = vec4(gray, gray, gray, blur.a);
	
	float darknessStrength = (1.0 - light.r);
	
	// Darkness above water
	if (waterLevel < 0.4) {
		float aboveWaterDarkness = isTexelAbove * (0.4 - waterLevel);
		darknessStrength = min(1.0, darknessStrength + aboveWaterDarkness);
	}

	fragColor = mix(mix(
		main * (1.0 + light.g),
		blur,
		vec4(clamp((1.0 - light.r) / sqrt(max(ambientLight, 0.35)), 0.0, 1.0))
	), darknessColor, vec4(darknessStrength));
	fragColor.a = 1.0;
}
)";

	constexpr char TexturedBackgroundFs[] = R"(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D uTexture; // Normal

uniform vec2 ViewSize;
uniform vec2 CameraPosition;

uniform vec3 horizonColor;
uniform vec2 shift;
uniform float parallaxStarsEnabled;

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
	float correction = ((ViewSize.x * 9.0) / (ViewSize.y * 16.0));

	vec2 texturePos = vec2(
		(shift.x / 256.0) + (vTexCoords.x - 0.5   ) * (0.5 + (1.5 * horizonDepth)) * correction,
		(shift.y / 256.0) + (vTexCoords.y - yShift) * 2.0 * distance
	);

	vec4 texColor = texture(uTexture, texturePos);
	float horizonOpacity = clamp(pow(distance, 1.8) - 0.4, 0.0, 1.0);
	
	vec4 horizonColorWithStars = vec4(horizonColor, 1.0);
	if (parallaxStarsEnabled > 0.0) {
		vec2 samplePosition = (vTexCoords * ViewSize / ViewSize.xx) + CameraPosition.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
		
		samplePosition = (vTexCoords * ViewSize / ViewSize.xx) + CameraPosition.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}

	fragColor = mix(texColor, horizonColorWithStars, horizonOpacity);
	fragColor.a = 1.0;
}
)";

	constexpr char TexturedBackgroundCircleFs[] = R"(
#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D uTexture; // Normal

uniform vec2 ViewSize;
uniform vec2 CameraPosition;

uniform vec3 horizonColor;
uniform vec2 shift;
uniform float parallaxStarsEnabled;

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
	targetCoord.x *= ViewSize.x / ViewSize.y;

	// Distance to center of screen
	float distance = length(targetCoord);

	// x-coordinate of tunnel
	float xShift = (targetCoord.x == 0.0 ? sign(targetCoord.y) * 0.5 : atan(targetCoord.y, targetCoord.x) * INV_PI);

	vec2 texturePos = vec2(
		(xShift)         * 1.0 + (shift.x * 0.01),
		(1.0 / distance) * 1.4 + (shift.y * 0.002)
	);

	vec4 texColor = texture(uTexture, texturePos);
	float horizonOpacity = 1.0 - clamp(pow(distance, 1.4) - 0.3, 0.0, 1.0);
	
	vec4 horizonColorWithStars = vec4(horizonColor, 1.0);
	if (parallaxStarsEnabled > 0.0) {
		vec2 samplePosition = (vTexCoords * ViewSize / ViewSize.xx) + CameraPosition.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
		
		samplePosition = (vTexCoords * ViewSize / ViewSize.xx) + CameraPosition.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}

	fragColor = mix(texColor, horizonColorWithStars, horizonOpacity);
	fragColor.a = 1.0;
}
)";

	constexpr char ColorizeFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture; // Normal
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

	constexpr char OutlineFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture; // Normal
in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

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
	outline = min(outline, 1.0);

	vec4 color = texture(uTexture, vTexCoords);
	fragColor = mix(color, vec4(vColor.z, vColor.z, vColor.z, vColor.w), outline - color.a);
}
)";

	constexpr char WhiteMaskFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;

void main() {
	vec4 tex = texture(uTexture, vTexCoords);
	float color = step(0.1, max(max(tex.r, tex.g), tex.b));
	fragColor = vec4(color, color, color, tex.a) * vColor;
}
)";
}