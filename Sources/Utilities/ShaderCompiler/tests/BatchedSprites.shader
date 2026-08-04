program BatchedSprites;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef VERTEX_STAGE
struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	// Flat index into the palette texture; lands in the std140 tail padding, so the stride stays 112 bytes
	float palOffset;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (585) // 64 Kb / 112 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

#define i block.instances[gl_VertexID / 6]
#endif

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

varying vec2 vTexCoords;
varying vec4 vColor;
varying highp float vPaletteOffset;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = vec2(aPosition.x * i.texRect.x + i.texRect.y, aPosition.y * i.texRect.z + i.texRect.w);
	vColor = i.color;
	vPaletteOffset = i.palOffset;
}

void fragment() {
	COLOR = texture(uTexture, vTexCoords) * vColor;
}
