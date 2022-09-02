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

uniform vec4 ambientColor;

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
		vec4(clamp((1.0 - light.r) / sqrt(max(ambientColor.w, 0.35)), 0.0, 1.0))
	), ambientColor, vec4(1.0 - light.r));
	fragColor.a = 1.0;
}
)";

	constexpr char CombineWithWaterFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform vec2 ViewSizeInv;
uniform vec2 CameraPosition;
uniform float GameTime;

uniform sampler2D uTexture;
uniform sampler2D lightTex;
uniform sampler2D blurHalfTex;
uniform sampler2D blurQuarterTex;
uniform sampler2D displacementTex;

uniform vec4 ambientColor;
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
	float time = GameTime;

	// TODO: Remove this flip
	vec2 uvLocal = vec2(vTexCoords.x, 1.0 - vTexCoords.y);
	vec2 uvWorldCenter = (CameraPosition.xy * ViewSizeInv.xy);
	vec2 uvWorld = uvLocal + uvWorldCenter;

	float waveHeight = wave(uvWorld.x, time);
	float isTexelBelow = aastep(waveHeight, uvLocal.y - waterLevel);
	float isTexelAbove = 1.0 - isTexelBelow;

	// Displacement
	vec2 disPos = uvWorld * vec2(0.4) + vec2(mod(time * 0.8, 2.0));
	vec2 dis = (texture(displacementTex, disPos).xy - vec2(0.5)) * vec2(0.014);
	
	vec2 uv = uvLocal + dis * vec2(isTexelBelow);
	// TODO: Remove this flip
	uv.y = 1.0 - uv.y;
	vec4 main = texture(uTexture, uv);

	// Chromatic Aberration
	float aberration = abs(uvLocal.x - 0.5) * 0.012;
	float red = texture(uTexture, vec2(uv.x - aberration, uv.y)).r;
	float blue = texture(uTexture, vec2(uv.x + aberration, uv.y)).b;
	main.rgb = mix(main.rgb, waterColor * (0.4 + 1.2 * vec3(red, main.g, blue)), vec3(isTexelBelow * 0.5));
	
	// Rays
	float noisePos = uvWorld.x * 8.0 + uvWorldCenter.y * 0.5 + (1.0 - uvLocal.y - uvLocal.x) * -5.0;
	float rays = perlinNoise2D(vec2(noisePos, time * 10.0 + uvWorldCenter.y)) * 0.5 + 0.4;
	main.rgb += vec3(rays * isTexelBelow * max(1.0 - uvLocal.y * 1.4, 0.0) * 0.6);
	
	// Waves
	float topDist = abs(uvLocal.y - waterLevel - waveHeight);
	float isNearTop = 1.0 - aastep(ViewSizeInv.y * 2.8, topDist);
	float isVeryNearTop = 1.0 - aastep(ViewSizeInv.y * (0.8 - 100.0 * waveHeight), topDist);

	float topColorBlendFac = isNearTop * isTexelBelow * 0.6;
	main.rgb = mix(main.rgb, texture(uTexture, vec2(uvLocal.x,
		(waterLevel - uvLocal.y + waterLevel) * 0.97 + waveHeight - ViewSizeInv.y
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
		vec4(clamp((1.0 - light.r) / sqrt(max(ambientColor.w, 0.35)), 0.0, 1.0))
	), ambientColor, vec4(darknessStrength));
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

	constexpr char Resize3xBrzVs[] = R"(
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

uniform mediump vec2 uTextureSize;

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
out vec4 vTexCoords5;
out vec4 vTexCoords6;
out vec4 vTexCoords7;

void main()
{
	vec2 aPosition = vec2(0.5 - float(gl_VertexID >> 1), 0.5 - float(gl_VertexID % 2));
	vec2 aTexCoords = vec2(1.0 - float(gl_VertexID >> 1), 1.0 - float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	
	float dx = 1.0 / uTextureSize.x;
	float dy = 1.0 / uTextureSize.y;

	vec2 texCoord = vec2(aTexCoords.x * texRect.x + texRect.y, aTexCoords.y * texRect.z + texRect.w) + vec2(0.0000001, 0.0000001);

	vTexCoords0.xy = texCoord;
	vTexCoords1 = texCoord.xxxy + vec4( -dx, 0, dx, -2.0*dy);  // A1 B1 C1
	vTexCoords2 = texCoord.xxxy + vec4( -dx, 0, dx, -dy);      // A  B  C
	vTexCoords3 = texCoord.xxxy + vec4( -dx, 0, dx, 0);        // D  E  F
	vTexCoords4 = texCoord.xxxy + vec4( -dx, 0, dx, dy);       // G  H  I
	vTexCoords5 = texCoord.xxxy + vec4( -dx, 0, dx, 2.0*dy);   // G5 H5 I5
	vTexCoords6 = texCoord.xyyy + vec4(-2.0*dx, -dy, 0, dy);   // A0 D0 G0
	vTexCoords7 = texCoord.xyyy + vec4( 2.0*dx, -dy, 0, dy);   // C4 F4 I4
}
)";

	constexpr char Resize3xBrzFs[] = R"(
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
uniform vec2 uTextureSize;

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
		vec2 f = fract(vTexCoords0.xy*uTextureSize);
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

	constexpr char ResizeMonochromeFs[] = R"(
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;
uniform vec2 uTextureSize;

in vec2 vTexCoords;

out vec4 fragColor;

float dither4x4(vec2 position, float brightness) {
  int x = int(mod(position.x, 4.0));
  int y = int(mod(position.y, 4.0));
  int index = x + y * 4;
  float limit = 0.0;

  if (x < 8) {
	if (index == 0) limit = 0.0625;
	if (index == 1) limit = 0.5625;
	if (index == 2) limit = 0.1875;
	if (index == 3) limit = 0.6875;
	if (index == 4) limit = 0.8125;
	if (index == 5) limit = 0.3125;
	if (index == 6) limit = 0.9375;
	if (index == 7) limit = 0.4375;
	if (index == 8) limit = 0.25;
	if (index == 9) limit = 0.75;
	if (index == 10) limit = 0.125;
	if (index == 11) limit = 0.625;
	if (index == 12) limit = 1.0;
	if (index == 13) limit = 0.5;
	if (index == 14) limit = 0.875;
	if (index == 15) limit = 0.375;
  }

  return brightness + (brightness < limit ? -0.05 : 0.1);
}

void main() {
	vec3 color = texture(uTexture, vTexCoords).rgb;
	float gray = dot(((color - vec3(0.5)) * vec3(1.4, 1.2, 1.0)) + vec3(0.5), vec3(0.3, 0.7, 0.1));
	gray = dither4x4(vTexCoords * uTextureSize, gray);
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
}