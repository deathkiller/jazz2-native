uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef WITH_OPENGL2
uniform mat4 modelMatrix;
uniform vec4 color;
uniform vec4 texRect;
uniform vec2 spriteSize;
attribute vec2 aPosition;
attribute vec2 aTexCoords;
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
in vec2 aPosition;
in vec2 aTexCoords;
out vec2 vTexCoords;
out vec4 vColor;
#endif

void main()
{
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aTexCoords.x * texRect.x + texRect.y, aTexCoords.y * texRect.z + texRect.w);
	vColor = color;
}
