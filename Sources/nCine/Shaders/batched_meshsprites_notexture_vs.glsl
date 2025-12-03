uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef WITH_OPENGL2
// OpenGL 2.x: Use simpler batching with per-instance attributes
uniform mat4 modelMatrix;
uniform vec4 color;
uniform vec2 spriteSize;
attribute vec2 aPosition;
attribute float aMeshIndex;
varying vec4 vColor;
#else
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

in vec2 aPosition;
in uint aMeshIndex;
out vec4 vColor;

#define i block.instances[aMeshIndex]
#endif

void main()
{
#ifdef WITH_OPENGL2
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vColor = color;
#else
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vColor = i.color;
#endif
}
