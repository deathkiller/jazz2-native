program DefaultMeshSprite;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

attribute vec2 aPosition;
attribute vec2 aTexCoords;

varying vec2 vTexCoords;
varying vec4 vColor;

void vertex() {
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aTexCoords.x * texRect.x + texRect.y, aTexCoords.y * texRect.z + texRect.w);
	vColor = color;
}

#include "Include/DefaultSpriteFs.inc"

void fixed_function() {
	// The weapon wheel: a vertex-fed textured line strip. The consoles consume the vertex stream in
	// their line-strip pipeline stage (PVR expands segments into thin quads - no line primitive on
	// the TA; GX draws native lines); only the LineStrip primitive form is drawn.
	pipeline line_strip_mesh;
}
