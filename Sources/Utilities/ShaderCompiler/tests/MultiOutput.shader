program MultiOutput;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
#endif

varying vec2 vTexCoords;

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
out vec4 NORMAL;
#endif

void vertex() {
	vTexCoords = aPosition;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	vec4 c = texture(uTexture, vTexCoords);
	NORMAL = vec4(vTexCoords, 0.0, 1.0);
	COLOR = c;
}
