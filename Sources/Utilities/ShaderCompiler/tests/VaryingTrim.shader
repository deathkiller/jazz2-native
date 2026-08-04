program VaryingTrim;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
in vec2 aTexCoords;
out vec2 vGuardedOnly;
#endif

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

varying vec2 vUsed;
varying float vUnused;
varying flat highp vec2 vFlat;
varying vec4 vSwizzled;

void vertex() {
	vUsed = aTexCoords;
	vUnused = aPosition.x;
	vFlat = aPosition;
	vGuardedOnly = aPosition + aTexCoords;
	vSwizzled.xy = vec2(min(aPosition.x, 1.0), 0.5);
	vSwizzled.zw = aTexCoords;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	COLOR = texture(uTexture, vUsed);
}
