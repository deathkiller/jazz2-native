program PrecisionHighp;
precision highp;

// A real GLSL global precision statement (with a type) is NOT the two-token directive — it passes through
precision highp float;

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
	COLOR = texture(uTexture, vTexCoords);
}
