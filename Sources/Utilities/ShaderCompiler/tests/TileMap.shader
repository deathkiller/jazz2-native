program TileMap;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	float palOffset;
};

#ifdef VERTEX_STAGE
in vec2 aPosition;
in vec2 aTexCoords;
in vec4 aColor;
#endif

#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

varying vec2 vTexCoords;
varying vec4 vColor;
varying highp float vPaletteOffset;

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vColor = aColor * color;
	vPaletteOffset = palOffset;
}

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
	// Flat palette position = per-instance offset + the per-pixel index (red channel), mapped into the 256x256 texture
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 color = texture(uTexturePalette, vec2(palX, palY));
	COLOR = vec4(color.rgb, color.a * src.a) * vColor;
}
