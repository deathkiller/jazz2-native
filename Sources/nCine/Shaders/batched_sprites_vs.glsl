uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef WITH_OPENGL2
// OpenGL 2.x: Use simpler batching with per-instance attributes
uniform mat4 modelMatrix;
uniform vec4 color;
uniform vec4 texRect;
uniform vec2 spriteSize;
attribute vec2 aPosition;
attribute float aInstanceIndex;
varying vec2 vTexCoords;
varying vec4 vColor;
#else
struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (585) // 64 Kb / 112 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

out vec2 vTexCoords;
out vec4 vColor;

#define i block.instances[gl_VertexID / 6]
#endif

void main()
{
#ifdef WITH_OPENGL2
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vColor = color;
#else
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = vec2(aPosition.x * i.texRect.x + i.texRect.y, aPosition.y * i.texRect.z + i.texRect.w);
	vColor = i.color;
#endif
}
