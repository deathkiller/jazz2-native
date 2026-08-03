program DefaultBatchedSpritesNoTexture;

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

#define i block.instances[gl_VertexID / 6]

varying vec4 vColor;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vColor = i.color;
}

#include "Include/DefaultSpriteNoTextureFs.inc"

void fixed_function() {
	// Console fixed-function tier: a plain solid-colour quad - the dispatch already knows the program
	// has no texture, so the same modulate pass degrades to the flat vertex colour. Stated here rather
	// than in the shared no-texture fragment include, because the MeshSprite programs also include
	// that file and stay on the handwritten vertex-fed tier.
	pass p;
	p.color = COLOR;
	submit_quad(p);
}
