program Silhouette;
variant USE_PALETTE;

// Every global reaches both stages, InstanceBlock included, and a named uniform block whose members
// disagree on precision between them does not link - so the fragment prologue keeps the vertex default
precision highp;

// The coverage of the actors behind an x-ray tile layer (see Rendering::XRayMaskRenderer): every sprite
// arrives as one quad of a mesh in the 8-float layout of the tile-layer meshes, already in world space, and
// only its alpha is written - premultiplied white, so overlapping sprites add up to their union.

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

varying vec2 vTexCoords;
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
	// Sprites of different palettes share a draw, so the palette offset rides in the vertex color
	vPaletteOffset = aColor.r;
}

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
#if USE_PALETTE
	// An indexed texel is transparent where its palette entry is, which the index alone doesn't tell
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	float alpha = texture(uTexturePalette, vec2(palX, palY)).a * src.a;
#else
	float alpha = src.a;
#endif
	COLOR = vec4(alpha * vAlpha);
}
