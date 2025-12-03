uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

#ifdef WITH_OPENGL2
uniform mat4 modelMatrix;
uniform vec4 color;
uniform vec2 spriteSize;
attribute vec2 aPosition;
varying vec4 vColor;
#else
layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec2 spriteSize;
};
in vec2 aPosition;
out vec4 vColor;
#endif

void main()
{
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vColor = color;
}
