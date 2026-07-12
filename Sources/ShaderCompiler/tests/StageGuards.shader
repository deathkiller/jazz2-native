program StageGuards;
variant TINT;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
in vec2 aTexCoords;
#endif

#ifndef FRAGMENT_STAGE
uniform vec2 uVertexOnly;
#else
uniform sampler2D uTexture : texture_unit(0);
#endif

varying vec2 vTexCoords;

// Stage guard nested inside an unknown conditional -- still resolved in place
#ifdef TINT
#ifdef FRAGMENT_STAGE
uniform vec4 uTintColor;
#endif
uniform float uTintStrength;
#endif

// Unknown conditional nested inside a stage guard -- kept/dropped with the block
#ifdef VERTEX_STAGE
#ifdef TINT
uniform vec2 uTintShift;
#endif
#endif

void vertex() {
	vTexCoords = aTexCoords;
#ifdef TINT
	gl_Position = uProjectionMatrix * vec4(aPosition + uTintShift, 0.0, 1.0);
#else
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
#endif
}

void fragment() {
	vec4 c = texture(uTexture, vTexCoords);
#ifdef TINT
	c = mix(c, uTintColor, uTintStrength);
#endif
	COLOR = c;
}
