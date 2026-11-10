program LightingMesh;

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
attribute vec4 aColor;

varying vec4 vTexCoords;
varying vec4 vColor;

void vertex() {
	// All lights of a viewport in one vertex stream, corners in world space (see LightingRenderer::OnDraw).
	// aTexCoords = near radius and ring radius (0 for a point light), in units of the far radius.
	// The light centre isn't in the stream, normal mapping would need it added.
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = vec4(0.0, 0.0, aTexCoords.x, aTexCoords.y);
	vColor = aColor;
}

#include "Include/LightingFs.inc"
