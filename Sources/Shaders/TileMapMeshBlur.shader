program TileMapMeshBlur;
variant USE_PALETTE;

// Every global reaches both stages, InstanceBlock included, and a named uniform block whose members
// disagree on precision between them does not link - so the fragment prologue keeps the vertex default
precision highp;

// A blur-behind layer (see Tiles::LayerRendererType::BlurBehind) in the tile-layer mesh: the tiles themselves are
// never drawn, they only say what of the scene behind them is shown in their place - red and green move it by up to
// 32 of the layer's pixels either way (128 doesn't), blue says how much of it is mirrored vertically, and alpha how
// blurred what is shown is, the reflection included. A texel of no alpha does nothing at all. The 9-float vertex is
// the tile-layer mesh layout with the mirror line of the reflection the tile belongs to added, in the same
// coordinates as the position (see TileMap::AppendBlurTileQuad()).

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
attribute float aAxisY;

varying vec2 vTexCoords;
varying vec4 vColor;
varying highp float vPaletteOffset;
varying highp vec2 vPos;
varying highp float vAxisY;

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// The blurred scene behind the layer (a sharp copy of it where the blur effects are off) and a sharp copy of it, which
// exists if a tile of the level reflects or displaces (the blur stands in for it otherwise, where it's never used)
uniform sampler2D uTextureBehind : texture_unit(2);
uniform sampler2D uTextureSharp : texture_unit(3);
// One over the size of the target being drawn into. The textures behind are smaller or the same size, but they
// cover the same view
uniform vec2 uTargetSizeInv;

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vColor = aColor * color;
	vPaletteOffset = palOffset;
	vPos = aPosition;
	vAxisY = aAxisY;
}

void fragment() {
	vec4 src = texture(uTexture, vTexCoords);
#if USE_PALETTE
	highp float palIndex = floor(vPaletteOffset + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 paletteColor = texture(uTexturePalette, vec2(palX, palY));
	vec4 tile = vec4(paletteColor.rgb, paletteColor.a * src.a);
#else
	vec4 tile = src;
#endif
	// Alpha switches a texel on, so one with none does nothing whatever its colour - which image editors and palette
	// index 0 leave black, the largest displacement there is. Above that, it's the blur
	float on = step(0.5 / 255.0, tile.a);
	float blurAmount = clamp((tile.a * 255.0 - 1.0) / 254.0, 0.0, 1.0);
	float reflectionAmount = tile.b * on;
	vec2 displacement = (floor(tile.rg * 255.0 + 0.5) - 128.0) * on;
	float displaced = step(0.5, max(abs(displacement.x), abs(displacement.y)));
	// vColor.a is the tile's own alpha times the alpha of the layer's color, how strong the whole effect is
	float strength = vColor.a * on;

	// The derivatives are the layer's units per pixel along the window's axes, so they carry both the scale (the view
	// can be supersampled) and the direction of y, which differs between backends
	highp vec2 unitsPerPixel = vec2(dFdx(vPos.x), dFdy(vPos.y));
	unitsPerPixel.x = (abs(unitsPerPixel.x) < 0.000001 ? 1.0 : unitsPerPixel.x);
	unitsPerPixel.y = (abs(unitsPerPixel.y) < 0.000001 ? 1.0 : unitsPerPixel.y);
	// A step of displacement is a quarter of the layer's pixel, applied to the reflection as well
	vec2 offset = displacement * strength * 0.25 / unitsPerPixel;
	vec2 uv = (gl_FragCoord.xy + offset) * uTargetSizeInv;
	// The mirrored point in window pixels
	vec2 reflectionUv = vec2(gl_FragCoord.x + offset.x, gl_FragCoord.y + 2.0 * (vAxisY - vPos.y) / unitsPerPixel.y + offset.y) * uTargetSizeInv;
	// Fades out over the last few pixels of the target instead of smearing its edge where the mirrored point leaves it
	reflectionAmount *= clamp(min(reflectionUv.y, 1.0 - reflectionUv.y) / (8.0 * uTargetSizeInv.y), 0.0, 1.0);

	vec3 sharp = texture(uTextureSharp, uv).rgb;
	vec3 blurred = texture(uTextureBehind, uv).rgb;
	// The reflection is blurred as well, so a gradient in alpha can soften it gradually
	vec3 reflected = mix(texture(uTextureSharp, reflectionUv).rgb, texture(uTextureBehind, reflectionUv).rgb, blurAmount);
	// The scene - moved if displaced - mixed with its blur, and that with the reflection, as one blend over the scene.
	// Where nothing moves, the sharp part is left to the scene itself, so the sharp copy is only needed for the texels
	// that displace or reflect
	float sharpAmount = (1.0 - blurAmount) * (1.0 - reflectionAmount);
	float alpha = 1.0 - sharpAmount * (1.0 - displaced);
	vec3 color = sharp * (sharpAmount * displaced) + blurred * (blurAmount * (1.0 - reflectionAmount)) + reflected * reflectionAmount;
	COLOR = vec4(color / max(alpha, 0.0001), alpha * strength);
}
