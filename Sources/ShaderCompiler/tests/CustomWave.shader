program CustomWave;
variant DESATURATE;

render_mode blend_premul_alpha;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;
uniform vec2 uPhase;

#ifdef VERTEX_STAGE
layout (location = 0) in vec2 aPosition;
in vec2 aTexCoords;
#endif

varying vec2 vTexCoords;
varying flat highp float vWave;

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);

float luma(vec3 c) {
	return dot(c, vec3(0.299, 0.587, 0.114));
}
#endif

void vertex() {
	vTexCoords = aTexCoords;
	vWave = sin(uPhase.x + aPosition.x * uPhase.y);
	gl_Position = uProjectionMatrix * uViewMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	vec4 c = texture(uTexture, vTexCoords);
#ifdef DESATURATE
	c.rgb = vec3(luma(c.rgb));
#endif
	COLOR = c * (0.75 + 0.25 * vWave);
}
