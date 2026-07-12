program DefaultMeshSpriteNoTexture;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec2 spriteSize;
};

attribute vec2 aPosition;

varying vec4 vColor;

void vertex() {
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vColor = color;
}

#include "Include/DefaultSpriteNoTextureFs.inc"
