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
	// All lights of a viewport arrive as one vertex stream instead of one instanced quad each: the corners are
	// already in world space, the light's own parameters ride along per vertex (see LightingRenderer::OnDraw).
	// The fragment stage is shared with the single-light program, so the varyings it reads are filled the same
	// way - the light centre it would need for normal mapping is not in the stream and would have to be added.
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = vec4(0.0, 0.0, aTexCoords.x, 0.0);
	vColor = aColor;
}

#include "Include/LightingFs.inc"
