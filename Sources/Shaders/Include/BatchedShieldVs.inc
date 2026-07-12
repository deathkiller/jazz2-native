uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

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

#define i block.instances[gl_VertexID / 6]

varying vec4 vTexCoords;
varying vec4 vColor;
varying vec2 vPos;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = i.texRect;
	vColor = i.color;
	vPos = aPosition * vec2(2.0) - vec2(1.0);
}
