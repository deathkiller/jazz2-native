program Transition;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vTexCoords;
varying vec2 vCorrection;
varying float vProgressTime;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vCorrection = spriteSize / vec2(max(spriteSize.x, spriteSize.y));
	vProgressTime = color.a;
}

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

void fragment() {
	vec2 uv = (vTexCoords - vec2(0.5)) * vCorrection;
	float distance = length(uv);

	float progressInner = vProgressTime - 0.22;
	distance = (clamp(distance, progressInner, vProgressTime) - progressInner) / (vProgressTime - progressInner);

	float mixValue = ease(distance);
	float noise = 1.0 + rand(uv) * 0.1;
	COLOR = vec4(0.0, 0.0, 0.0, mixValue * noise);
}
