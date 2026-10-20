program TileMapMeshHeat;
variant USE_PALETTE;

// Every global reaches both stages, InstanceBlock included, and a named uniform block whose members
// disagree on precision between them does not link - so the fragment prologue keeps the vertex default
precision highp;

// The tiles of the heat effect (the tile type of JJ2+, see Tiles::LayerTileFlags::Heat): every row of pixels of a tile is
// drawn moved left or right by a few pixels, by a fixed table of 32 rows that scrolls up a row every 4 game ticks, as
// measured in the original - each row moves into the tiles next to it, so the quad reaches 2 pixels past the tile on
// either side. The 12-float vertex is the tile-layer mesh layout with the position in the tile added: x and y in its
// pixels (x from -2 to 34), and how far the texture coordinates move for a pixel to the right.

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
attribute vec4 aHeat;
attribute vec4 aColor;

varying vec2 vTexCoords;
varying vec3 vHeat;
varying vec4 vColor;
varying highp float vPaletteOffset;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// How many rows the table has scrolled, 0 to 31
uniform float uHeatPhase;

// How far the row k of the table (0 to 31) is taken from the right, in pixels: the table as the sum of its steps, so it
// needs neither an array nor a texture - 0, 1, 1, 2, 2, -1, -1, 0, -2, 1, 0, 0, 0, -1, -1, -2, -2, -1, 1, 2, 1, -1, -1,
// 0, 0, 1, 1, 0, -1, -2, -1, 0
float heatShift(float k) {
	vec4 row = vec4(k);
	return dot(vec4(1.0, 1.0, -3.0, 1.0), step(vec4(1.0, 3.0, 5.0, 7.0), row)) +
		dot(vec4(-2.0, 3.0, -1.0, -1.0), step(vec4(8.0, 9.0, 10.0, 13.0), row)) +
		dot(vec4(-1.0, 1.0, 2.0, 1.0), step(vec4(15.0, 17.0, 18.0, 19.0), row)) +
		dot(vec4(-1.0, -2.0, 1.0, 1.0), step(vec4(20.0, 21.0, 23.0, 25.0), row)) +
		dot(vec4(-1.0, -1.0, -1.0, 1.0), step(vec4(27.0, 28.0, 29.0, 30.0), row)) +
		step(31.0, k);
}

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vHeat = aHeat.xyz;
	vColor = aColor * color;
	vPaletteOffset = palOffset;
}

void fragment() {
	float shift = heatShift(mod(floor(vHeat.y) + uHeatPhase, 32.0));
	// The pixel of the tile this one shows, nothing where the row moved away from it
	float source = vHeat.x + shift;
	float inside = step(0.0, source) * step(source, 31.999);

	vec4 src = texture(uTexture, vec2(vTexCoords.x + shift * vHeat.z, vTexCoords.y));
#if USE_PALETTE
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 palColor = texture(uTexturePalette, vec2(palX, palY));
	src = vec4(palColor.rgb, palColor.a * src.a);
#endif
	COLOR = vec4(src.rgb, src.a * inside) * vColor;
}
