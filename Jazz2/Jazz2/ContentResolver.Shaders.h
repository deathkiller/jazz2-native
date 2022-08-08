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
uniform vec2 ViewSize;

uniform sampler2D uTexture;
uniform sampler2D lightTex;
uniform sampler2D blurHalfTex;
uniform sampler2D blurQuarterTex;

uniform float ambientLight;
uniform vec4 darknessColor;

in vec2 vTexCoords;
in vec4 vColor;

out vec4 fragColor;

float lightBlend(float t) {
	return t * t;
}

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

	vec2 texturePos = vec2(
		(shift.x / 256.0) + (vTexCoords.x - 0.5   ) * (0.5 + (1.5 * horizonDepth)),
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
}