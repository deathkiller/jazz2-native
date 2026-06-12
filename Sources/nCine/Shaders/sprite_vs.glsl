uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	// Flat index into the palette texture (added to the per-pixel index for the palette lookup). Lands in the
	// std140 tail padding after spriteSize, so the block stays 112 bytes. Only read by palette shaders.
	float palOffset;
};

out vec2 vTexCoords;
out vec4 vColor;
out float vPaletteOffset;

void main()
{
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vColor = color;
	vPaletteOffset = palOffset;
}
