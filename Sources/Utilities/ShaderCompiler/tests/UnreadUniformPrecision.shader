program UnreadUniformPrecision;

uniform mat4 uProjectionMatrix;
// Read by neither stage, so it stays declared in both - the vertex declaration has to take the fragment precision
uniform vec2 uUnread;
// Read by the vertex stage only, so it is trimmed from the fragment stage and keeps its own precision
uniform float uScale;

#ifdef VERTEX_STAGE
in vec2 aPosition;
#endif

varying vec2 vTexCoords;

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

void vertex() {
	vTexCoords = aPosition;
	gl_Position = uProjectionMatrix * vec4(aPosition * uScale, 0.0, 1.0);
}

void fragment() {
	COLOR = texture(uTexture, vTexCoords);
}
