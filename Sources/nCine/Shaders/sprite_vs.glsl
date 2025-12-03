uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef WITH_OPENGL2
uniform mat4 modelMatrix;
uniform vec4 color;
uniform vec4 texRect;
uniform vec2 spriteSize;
attribute vec2 aPosition;
varying vec2 vTexCoords;
varying vec4 vColor;
#else
layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};
out vec2 vTexCoords;
out vec4 vColor;
#endif

void main()
{
#ifndef WITH_OPENGL2
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
#endif
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vColor = color;
}
