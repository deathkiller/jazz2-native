program EarlyReturn;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
#endif

varying vec2 vTexCoords;

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

void vertex() {
	vTexCoords = aPosition;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	if (vTexCoords.x < 0.5) {
		COLOR = vec4(0.0);
		return;
	}
	COLOR = texture(uTexture, vTexCoords);
}
