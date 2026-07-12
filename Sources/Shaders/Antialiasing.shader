program Antialiasing;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

uniform sampler2D uTexture : texture_unit(0);

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vPixelCoords;
varying vec2 vTexSizeInv;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * texRect.xy + 0.5;
	vTexSizeInv = 1.0 / texRect.xy;
}

vec3 cubicHermite(vec3 A, vec3 B, vec3 C, vec3 D, float t) {
	float t2 = t*t;
	float t3 = t*t*t;
	vec3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
	vec3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
	vec3 c = -A/2.0 + C/2.0;
	vec3 d = B;
	return a*t3 + b*t2 + c*t + d;
}

void fragment() {
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

	COLOR = vec4(cubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y), 1.0);
}
