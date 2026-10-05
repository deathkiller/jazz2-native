uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vTexCoords;
varying vec2 vViewSize;
varying vec2 vViewSizeInv;
#if LOW_POWER_GPU
// The view pixel a fragment covers, which the low-power lighting noise is hashed from (see Combine.shader).
// Interpolated rather than recovered as vTexCoords / vViewSizeInv, which is a division per fragment
varying vec2 vPixelPos;
#endif

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vViewSize = spriteSize;
	vViewSizeInv = vec2(1.0) / spriteSize;
#if LOW_POWER_GPU
	vPixelPos = aPosition * spriteSize;
#endif
}
