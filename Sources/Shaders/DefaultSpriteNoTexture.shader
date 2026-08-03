program DefaultSpriteNoTexture;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec2 spriteSize;
};

varying vec4 vColor;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vColor = color;
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
