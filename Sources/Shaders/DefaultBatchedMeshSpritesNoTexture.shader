program DefaultBatchedMeshSpritesNoTexture;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec2 spriteSize;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (682) // 64 Kb / 96 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

attribute vec2 aPosition;
attribute uint aMeshIndex;

#define i block.instances[aMeshIndex]

varying vec4 vColor;

void vertex() {
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vColor = i.color;
}

#include "Include/DefaultSpriteNoTextureFs.inc"
