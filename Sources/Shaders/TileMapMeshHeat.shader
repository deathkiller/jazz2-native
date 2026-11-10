program TileMapMeshHeat;
variant USE_PALETTE;

// Heat effect tiles (see Tiles::LayerTileFlags::Heat): each pixel row is shifted sideways by a 32-row table that scrolls
// up a row every 4 game ticks, so the quad reaches 2 px past the tile on each side. The 12-float vertex adds x and y
// within the tile in pixels (x from -2 to 34) and the texture coordinate step per pixel.

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

varying highp vec2 vTexCoords;
varying vec3 vHeat;
varying vec4 vColor;
varying highp float vPaletteOffset;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// How many rows the table has scrolled, 0 to 31
uniform float uHeatPhase;

// Shift of table row k (0 to 31) in pixels, summed from steps so it needs no array or texture:
// 0, 1, 1, 2, 2, -1, -1, 0, -2, 1, 0, 0, 0, -1, -1, -2, -2, -1, 1, 2, 1, -1, -1, 0, 0, 1, 1, 0, -1, -2, -1, 0
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
	// The tile pixel shown here, empty where the row moved away
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
