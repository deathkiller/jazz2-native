program ConstFold;

uniform mat4 uProjectionMatrix;

#ifdef VERTEX_STAGE
in vec2 aPosition;
in vec2 aTexCoords;
#endif

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

varying vec2 vTexCoords;

// Global-scope declarations must never fold (reflection safety)
const float kGlobal = 1.0 + 2.0;

void vertex() {
	vTexCoords = aTexCoords;
	gl_Position = uProjectionMatrix * vec4(aPosition * (2.0 * 0.5), 0.0, 1.0);
}

void fragment() {
	float a = 2.0 * 3.0 + 1.0;
	int b = 10 / 3;
	int c = 7 % 3 - 9;
	float d = 1 / 255.0;
	float e = a * 2.0 + 1.0;
	int f = 2000000000 + 2000000000;
	vec2 g = vec2(1.0 + 2.0, 3.0);
	float h = 1.0 / 4.0;
	float i2 = 3.0 * 2.0;
	float j = 1.0 / 3.0;
	float k = 1.0 / 8192.0;
	uint m = 2u + 3u;
	vec4 t = texture(uTexture, vTexCoords);
	COLOR = t * vec4(a + d + e + h + i2 + j + k + kGlobal + float(b + c + f) + g.x + float(m), 0.0, 1.0 - 0.5, 1.0);
}
