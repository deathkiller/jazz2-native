program ResizeHQ2x;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vTexCoords0;
varying vec4 vTexCoords1;
varying vec4 vTexCoords2;
varying vec4 vTexCoords3;
varying vec4 vTexCoords4;

void vertex() {
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

const float mx = 0.325;
const float k = -0.250;
const float max_w = 0.25;
const float min_w = -0.05;
const float lum_add = 0.25;

uniform sampler2D uTexture : texture_unit(0);

void fragment() {
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

	COLOR.xyz = w1 * c10 + w2 * c21 + w3 * c12 + w4 * c01 + (1.0 - w1 - w2 - w3 - w4) * cx;
	COLOR.w = 1.0;
}
