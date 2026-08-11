uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec4 vTexCoords;
varying vec4 vColor;
#if !SOFTWARE_RENDERER
varying vec2 vPos;
#else
// Software-renderer variant: the CPU fragment path cannot interpolate a per-vertex varying like vPos,
// so the device feeds the fragment an identity-mapped texcoord instead (vTexCoords carries the
// quad-local position in [0,1]^2, see SwDevice's Shield handling) and the real texRect - the animated
// shift values the fragment needs - travels through this per-instance-constant varying.
varying vec4 vShieldRect;
#endif

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = color;
#if !SOFTWARE_RENDERER
	vPos = aPosition * vec2(2.0) - vec2(1.0);
#else
	vShieldRect = texRect;
#endif
}
