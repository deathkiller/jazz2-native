#ifdef GL_ES
precision mediump float;
#endif

#ifdef WITH_OPENGL2
varying vec4 vColor;
#else
in vec4 vColor;
out vec4 fragColor;
#endif

void main()
{
#ifdef WITH_OPENGL2
	gl_FragColor = vColor;
#else
	fragColor = vColor;
#endif
}
