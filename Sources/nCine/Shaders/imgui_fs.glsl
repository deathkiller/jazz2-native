#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D uTexture;

#ifdef WITH_OPENGL2
varying vec2 vTexCoords;
varying vec4 vColor;
#else
in vec2 vTexCoords;
in vec4 vColor;
out vec4 fragColor;
#endif

void main()
{
#ifdef WITH_OPENGL2
	gl_FragColor = vColor * texture2D(uTexture, vTexCoords);
#else
	fragColor = vColor * texture(uTexture, vTexCoords);
#endif
}
