program StageIfDefined;

#if defined(FRAGMENT_STAGE)
uniform sampler2D uTexture;
#endif

void vertex() {
	gl_Position = vec4(0.0);
}

void fragment() {
	COLOR = vec4(1.0);
}
