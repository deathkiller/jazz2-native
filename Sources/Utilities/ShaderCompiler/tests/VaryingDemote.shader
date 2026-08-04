program VaryingDemote;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
in vec2 aTexCoords;
#endif

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

varying vec2 vUsed;
varying float vCallRhs;
varying float vElseBody;
varying float vReadBack;
varying float vNested;

float shade(float v) {
	return v * 0.5;
}

void vertex() {
	float t = 0.0;
	vUsed = aTexCoords;
	vCallRhs = shade(aPosition.x);
	if (aPosition.x > 0.5) t = 1.0;
	else vElseBody = aPosition.y;
	vReadBack = aPosition.x;
	t += vReadBack;
	vNested = (t = 2.0);
	gl_Position = uProjectionMatrix * vec4(aPosition + vec2(t * 0.0), 0.0, 1.0);
}

void fragment() {
	COLOR = texture(uTexture, vUsed);
}
