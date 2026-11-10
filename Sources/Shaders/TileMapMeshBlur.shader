program TileMapMeshBlur;
variant USE_PALETTE;

// Blur-behind layer (see Tiles::LayerRendererType::BlurBehind): tiles aren't drawn, they control the scene behind them.
// Red/green displace it by up to 32 layer pixels (128 = none), blue mirrors it vertically, alpha blurs it; alpha 0 does
// nothing. The 9-float vertex adds the reflection's mirror line in position coordinates (see TileMap::AppendBlurTileQuad()).

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

varying highp vec2 vTexCoords;
varying vec4 vColor;
varying highp float vPaletteOffset;
#if LOW_POWER_GPU
// Screen scale and mirror offset, computed per vertex
varying highp vec4 vScale;
#else
varying highp vec2 vPos;
varying highp float vAxisY;
#endif

uniform sampler2D uTexture : texture_unit(0);
#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// Blurred scene behind the layer (sharp if blur is off) and a sharp copy, bound only if the level reflects or displaces
uniform sampler2D uTextureBehind : texture_unit(2);
uniform sampler2D uTextureSharp : texture_unit(3);
// One over the target size; the textures behind may be smaller but cover the same view
uniform highp vec2 uTargetSizeInv;

void vertex() {
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * vec4(aPosition.x, aPosition.y, 0.0, 1.0);
	vTexCoords = aTexCoords;
	vColor = aColor * color;
	vPaletteOffset = palOffset;
#if LOW_POWER_GPU
	// Per-pixel derivatives are costly here, so the layer's screen scale comes from the matrices
	// Targets are always stored top-down, so only the magnitude is needed
	highp vec4 unitX = uProjectionMatrix * (uViewMatrix * (modelMatrix * vec4(1.0, 0.0, 0.0, 0.0)));
	highp vec4 unitY = uProjectionMatrix * (uViewMatrix * (modelMatrix * vec4(0.0, 1.0, 0.0, 0.0)));
	highp vec2 pixelsPerUnit = abs(vec2(unitX.x, unitY.y)) * 0.5 / uTargetSizeInv;
	// xy: one displacement step, z: distance to the mirrored point (both in target units), w: 1 / 8 px edge fade
	vScale = vec4(pixelsPerUnit * 0.25 * uTargetSizeInv, 2.0 * (aAxisY - aPosition.y) * pixelsPerUnit.y * uTargetSizeInv.y,
		0.125 / uTargetSizeInv.y);
#else
	vPos = aPosition;
	vAxisY = aAxisY;
#endif
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
#if LOW_POWER_GPU
	// Same as below with the scale from the vertex stage; 8-bit channels times 255 are close enough to skip rounding
	float on = step(0.5 / 255.0, tile.a);
	float blurAmount = clamp(tile.a * (255.0 / 254.0) - (1.0 / 254.0), 0.0, 1.0);
	vec2 displacement = (tile.rg * 255.0 - 128.0) * on;
	float displaced = step(0.5, max(abs(displacement.x), abs(displacement.y)));
	float strength = vColor.a * on;
	highp vec2 uv = gl_FragCoord.xy * uTargetSizeInv + displacement * (strength * vScale.xy);
	highp vec2 reflectionUv = vec2(uv.x, uv.y + vScale.z);
	float reflectionAmount = tile.b * on * clamp(min(reflectionUv.y, 1.0 - reflectionUv.y) * vScale.w, 0.0, 1.0);
#else
	// Alpha 0 turns a texel off whatever its color (often black, the largest displacement); above that it's the blur
	float on = step(0.5 / 255.0, tile.a);
	float blurAmount = clamp((tile.a * 255.0 - 1.0) / 254.0, 0.0, 1.0);
	float reflectionAmount = tile.b * on;
	vec2 displacement = (floor(tile.rg * 255.0 + 0.5) - 128.0) * on;
	float displaced = step(0.5, max(abs(displacement.x), abs(displacement.y)));
	// Tile alpha times layer alpha, the strength of the whole effect
	float strength = vColor.a * on;

	// Layer units per pixel, with the view scale (supersampling) and the y direction, which differs per backend
	highp vec2 unitsPerPixel = vec2(dFdx(vPos.x), dFdy(vPos.y));
	unitsPerPixel.x = (abs(unitsPerPixel.x) < 0.000001 ? 1.0 : unitsPerPixel.x);
	unitsPerPixel.y = (abs(unitsPerPixel.y) < 0.000001 ? 1.0 : unitsPerPixel.y);
	// One displacement step is a quarter of a layer pixel, reflection included
	vec2 offset = displacement * strength * 0.25 / unitsPerPixel;
	highp vec2 uv = (gl_FragCoord.xy + offset) * uTargetSizeInv;
	// The mirrored point
	highp vec2 reflectionUv = vec2(gl_FragCoord.x + offset.x, gl_FragCoord.y + 2.0 * (vAxisY - vPos.y) / unitsPerPixel.y + offset.y) * uTargetSizeInv;
	// Fade out near the target edge instead of smearing it
	reflectionAmount *= clamp(min(reflectionUv.y, 1.0 - reflectionUv.y) / (8.0 * uTargetSizeInv.y), 0.0, 1.0);
#endif

	vec3 sharp = texture(uTextureSharp, uv).rgb;
	vec3 blurred = texture(uTextureBehind, uv).rgb;
	// The reflection is blurred too, so an alpha gradient softens it
	vec3 reflected = mix(texture(uTextureSharp, reflectionUv).rgb, texture(uTextureBehind, reflectionUv).rgb, blurAmount);
	// Displaced scene, blur and reflection as one blend over the scene; where nothing moves, the scene itself is the sharp part
	float sharpAmount = (1.0 - blurAmount) * (1.0 - reflectionAmount);
	float alpha = 1.0 - sharpAmount * (1.0 - displaced);
	vec3 shown = sharp * (sharpAmount * displaced) + blurred * (blurAmount * (1.0 - reflectionAmount)) + reflected * reflectionAmount;
	COLOR = vec4(shown / max(alpha, 0.0001), alpha * strength);
}
