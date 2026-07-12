program IncludeShared;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	float palOffset;
};

varying vec2 vTexCoords;
varying vec4 vColor;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vColor = color;
}

#include "inc/SpriteFragment.inc"
