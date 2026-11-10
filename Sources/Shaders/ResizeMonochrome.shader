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

// highp - pixel coordinates of the whole view exceed half precision
varying highp vec2 vPixelCoords;
varying highp vec2 vTexCoords;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPixelCoords = aPosition * texRect.xy;
	vTexCoords = aPosition;
}

uniform sampler2D uTexture : texture_unit(0);

// @p cell is the position in the 4x4 pattern; Bayer threshold is computed, not looked up
// (indexed tables don't compile on GLES 2.0 or PS3)
float dither4x4(vec2 cell, float brightness) {
	vec2 fine = floor(cell);
	vec2 coarse = floor(fine * 0.5);
	float bayerValue = fract(fine.y * 0.5 + fine.x * fine.x * 0.75) + fract(coarse.y * 0.5 + coarse.x * coarse.x * 0.75) * 0.25 + 0.0625;
	return brightness + (brightness < bayerValue ? -0.05 : 0.1);
}

void fragment() {
	vec3 rgb = texture(uTexture, vTexCoords).rgb;
	float gray = dot(((rgb - vec3(0.5)) * vec3(1.4, 1.2, 1.0)) + vec3(0.5), vec3(0.3, 0.7, 0.1));
	gray = dither4x4(mod(vPixelCoords, 4.0), gray);
	float palette = (abs(1.0 - gray) * 0.75) + 0.125;

	if (palette < 0.25) {
		rgb = vec3(0.675, 0.710, 0.420);
	} else if (palette < 0.5) {
		rgb = vec3(0.463, 0.518, 0.283);
	} else if (palette < 0.75) {
		rgb = vec3(0.247, 0.314, 0.247);
	} else {
		rgb = vec3(0.1, 0.134, 0.151);
	}
	COLOR = vec4(rgb, 1.0);
}
