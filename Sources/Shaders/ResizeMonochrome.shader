program ResizeMonochrome;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vPixelCoords;
varying vec2 vTexCoords;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * texRect.xy;
	vTexCoords = aPosition;
}

mat4 bayerIndex = mat4(
	vec4(0.0625, 0.5625, 0.1875, 0.6875),
	vec4(0.8125, 0.3125, 0.9375, 0.4375),
	vec4(0.25, 0.75, 0.125, 0.625),
	vec4(1.0, 0.5, 0.875, 0.375));

uniform sampler2D uTexture : texture_unit(0);

float dither4x4(vec2 position, float brightness) {
	float bayerValue = bayerIndex[int(position.x) % 4][int(position.y) % 4];
	return brightness + (brightness < bayerValue ? -0.05 : 0.1);
}

void fragment() {
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
	COLOR = vec4(color, 1.0);
}
