program Silhouette;
variant USE_PALETTE;

// Actor coverage behind an x-ray tile layer (see XRayMaskRenderer). Sprites come as world-space quads in the
// tile-layer mesh layout; only premultiplied white alpha is written, so overlaps add up to their union.

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

attribute vec2 aPosition;
attribute vec2 aTexCoords;
attribute vec4 aColor;

// The sprite sheets are large textures, half precision would miss their texels
varying highp vec2 vTexCoords;
varying float vAlpha;
varying highp float vPaletteOffset;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vAlpha = aColor.a;
	// Palette offset rides in the vertex color, sprites of different palettes share a draw
	vPaletteOffset = aColor.r;
}

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
#if USE_PALETTE
	// Transparency of an indexed texel comes from its palette entry
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	float alpha = texture(uTexturePalette, vec2(palX, palY)).a * src.a;
#else
	float alpha = src.a;
#endif
	COLOR = vec4(alpha * vAlpha);
}
