program DeadFunctions;

uniform mat4 uProjectionMatrix;
uniform vec2 uOffset;

#ifdef VERTEX_STAGE
in vec2 aPosition;
#endif

varying vec2 vTexCoords;

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

// Used by vertex() only — must survive in the vertex stage, vanish from the fragment stage
vec2 vsHelper(vec2 p) {
	return p + uOffset;
}

// Used by fsHelper (and the unused helper) — must vanish from the vertex stage transitively
float fsInner(float v) {
	return v * v;
}

// Used by fragment() only — must survive in the fragment stage, vanish from the vertex stage
float fsHelper(float v) {
	return fsInner(v) + 1.0;
}

// Referenced by nobody — must vanish from both stages
float unusedHelper(float v) {
	return fsInner(v) - 1.0;
}

void vertex() {
	vTexCoords = vsHelper(aPosition);
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	vec4 c = texture(uTexture, vTexCoords);
	COLOR = vec4(vec3(fsHelper(c.r)), 1.0);
}
