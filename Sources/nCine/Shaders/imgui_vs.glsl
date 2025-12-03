uniform mat4 uGuiProjection;
uniform float uDepth;

#ifdef WITH_OPENGL2
attribute vec2 aPosition;
attribute vec2 aTexCoords;
attribute vec4 aColor;
varying vec2 vTexCoords;
varying vec4 vColor;
#else
in vec2 aPosition;
in vec2 aTexCoords;
in vec4 aColor;
out vec2 vTexCoords;
out vec4 vColor;
#endif

void main()
{
	gl_Position = uGuiProjection * vec4(aPosition, uDepth, 1.0);
	vTexCoords = aTexCoords;
	vColor = aColor;
}
