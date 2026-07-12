program ResizeCrtScanlines;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying highp vec2 vPixelCoords;
varying vec2 vTexCoords;
varying highp vec2 vOutputSize;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * spriteSize.xy;
	vTexCoords = aPosition;
	vOutputSize = spriteSize.xy;
}

uniform sampler2D uTexture : texture_unit(0);

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

void fragment() {
	float y = vPixelCoords.y;
	vec2 uv0 = vec2(vTexCoords.x, y / vOutputSize.y);
	vec3 t0 = texture(uTexture, uv0).rgb;
	float ymod = mod(vPixelCoords.y, 3.0);
	if (ymod > 2.0) {
		vec2 uv1 = vec2(vTexCoords.x, (y + 1.0) / vOutputSize.y);
		vec3 t1 = texture(uTexture, uv1).rgb;
		COLOR.rgb = (t0 + t1) * 0.25;
	} else {
		t0 = toYiq(t0);
		t0.r *= 1.1;
		t0 = fromYiq(t0);
		COLOR.rgb = t0;
	}
	COLOR.a = 1.0;
}
