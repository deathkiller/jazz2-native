program UniformTrim;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec2 spriteSize;
};

#ifndef LIGHT_COUNT
	#define LIGHT_COUNT 2
#endif

uniform vec4 uLights[LIGHT_COUNT];

uniform sampler2D uTexture : texture_unit(0);
uniform vec2 uPair, uOther;
uniform float uUnusedBoth;

#define DEAD_SCALE(x) ((x) * 2.0)
#define LIVE_BIAS(x) ((x) + 0.125)

varying vec2 vTexCoords;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);
	position.xy += uLights[0].xy;

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = aPosition;
}

void fragment() {
	COLOR = texture(uTexture, vTexCoords + uPair) * LIVE_BIAS(0.25);
}
