program LowPowerVarying;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;
uniform float uTime;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vTexCoords;
#if LOW_POWER_GPU
// Extra interpolant of the low-power path, which moves a screen-linear term from the fragment stage to
// the vertex stage. Only the Cg and ESSL 100 emissions may contain it (nor the macro itself), and uTime,
// which only that branch of the vertex stage reads, must not be declared in any other vertex stage
varying vec2 vPhase;
#endif

uniform sampler2D uTexture : texture_unit(0);

float sinCycles(float c) {
	float v = fract(c + 0.5) * 2.0 - 1.0;
	return 4.0 * v * (1.0 - abs(v));
}

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
#if LOW_POWER_GPU
	vPhase = vec2(vTexCoords.y * 3.0 + uTime, 0.0);
#endif
}

void fragment() {
#if LOW_POWER_GPU
	float offset = 0.01 * sinCycles(vPhase.x);
#else
	float offset = 0.01 * sin((vTexCoords.y * 3.0 + uTime) * 6.2831853);
#endif
	COLOR = texture(uTexture, vTexCoords + vec2(offset, 0.0));
}
