program TexturedBackgroundReflection;

shader_type canvas_item;
precision highp;

// A reflection layer (see Tiles::LayerRendererType::Reflection), as TEXTURE::REFLECTION of JJ2+: drawn over the part of
// the view below its horizon, where it shows what is drawn behind it above the horizon mirrored, as a plane of water
// stretching to the horizon would. The texture is laid on that plane, it holds the palette indices of the texture of the
// layer (or the colors of tiles in true color, whose brightest channel stands for the index), and moves what is mirrored
// up or down by how much higher the index of a texel is than that of the texel half of the texture further along the
// plane, and left or right by how much higher the index of the texel a quarter of the texture back and across is than
// that of the one half of the texture across from it, as JJ2+ does - a texture that is the same there doesn't ripple at
// all. The quad covers the part below the horizon, of the whole view or only where the layer is, UV runs over it.

// Size of the view in the pixels of the level, which the copy of what is behind the layer covers
uniform vec2 uViewSize;
// Where the quad is in the view: xy its top left, zw its size
uniform vec4 uQuad;
// x: row of the horizon in the view, y: column of the vanishing point, z: height of the camera above the plane, w: the
// distortion (0 to 1 for 0 to 255) - the horizon of the plane itself is half of the height above the horizon drawn, as
// in JJ2+, so a pixel at the horizon is a pixel of the texture wide
uniform vec4 uHorizon;
// xy: where the texture is on the plane, in its pixels, zw: one over its size
uniform vec4 uShift;
// The color of the tint, alpha its opacity
uniform vec4 uTint;
// 1 to blur what is mirrored horizontally
uniform float uBlur;
// One over the size of the target being drawn into, the copy is smaller or the same size, but covers the same view
uniform vec2 uTargetSizeInv;

// A sharp copy of what is drawn behind the layer
uniform sampler2D uTextureBehind : texture_unit(2);

#define FOCAL_LENGTH 330.0
// The texture is laid on the plane at twice the size of the plane's own units, measured in the original
#define TEXTURE_SCALE 0.5
// JJ2+ moves what is mirrored less than the ripples of the sharp test textures say for its own smooth textures - 0.5 to
// 0.76 of it, measured on four of them (0.58 and 0.50 for the one of primpXmas), the rule behind that wasn't found, so
// the ripples are scaled to look as strong as there
#define STRENGTH 0.6

float paletteIndex(vec2 texturePos) {
	vec4 texel = texture(TEXTURE, texturePos * uShift.zw);
	return floor(max(texel.r, max(texel.g, texel.b)) * 255.0 + 0.5);
}

float truncateTowardZero(float value) {
	// A whole number that comes out a hair smaller is still the same number
	return sign(value) * floor(abs(value) + 0.001);
}

void fragment() {
	// The row and the column of this pixel in the view, the rows below the horizon, and the column relative to the vanishing point
	float horizon = uHorizon.x;
	float viewRow = uQuad.y + UV.y * uQuad.w;
	float rows = viewRow - horizon;
	float column = uQuad.x + UV.x * uQuad.z - uHorizon.y;

	// The point of the plane seen in this pixel, by a camera above it looking at the horizon of the plane - a row nearer
	// the horizon sees a point further away, where the texture is smaller. The texture runs down the plane towards the
	// camera, as in JJ2+, so a texture moving along it by a positive speed moves away towards the horizon
	float planeRows = max(rows, 0.0) + uHorizon.z * 0.5;
	float depth = uHorizon.z / planeRows;
	vec2 texturePos = vec2(column * depth, -depth * FOCAL_LENGTH) * TEXTURE_SCALE + uShift.xy;

	// The ripples, in palette indices, as measured in the original: up and down by the texel against the one half of the
	// texture further along the plane, left and right by the texel a quarter of the texture back and to the left against
	// the one half of the texture to the right of it
	float rippleY = paletteIndex(texturePos) - paletteIndex(texturePos + vec2(0.0, 128.0));
	float rippleX = paletteIndex(texturePos + vec2(-64.0, 64.0)) - paletteIndex(texturePos + vec2(64.0, 64.0));
	// Whole pixels of the view, a row or a column for each 32 indices times the distortion (times STRENGTH), the same on every row but
	// the first 15 below the horizon, where it's halved for each time the rows (the first one counted) halve, as in JJ2+:
	// 1/2 for 7 to 14 rows, 1/4 for 3 to 6, 1/8 for 1 and 2, 1/16 at the horizon
	float row = floor(max(rows, 0.0));
	// The indices and the distortion are whole numbers, as they are in the original, so the ripples are exactly as far
	float nearHorizon = 0.0625 * (1.0 + step(1.0, row)) * (1.0 + step(3.0, row)) * (1.0 + step(7.0, row)) * (1.0 + step(15.0, row));
	float scale = floor(uHorizon.w * 255.0 + 0.5) / 32.0 * nearHorizon * STRENGTH;
	// A higher texel takes what is mirrored from nearer the horizon, but never from the horizon itself or below it, and
	// from further left
	float movedY = min(truncateTowardZero(rippleY * scale), max(row - 1.0, 0.0));
	float movedX = truncateTowardZero(rippleX * scale);

	// This pixel in the copy, then the one mirrored at the horizon moved by the ripples, a whole number of pixels of the
	// target away, so it's as sharp as what it mirrors - the first row below the horizon mirrors itself, as in JJ2+, and
	// where there are more rows below the horizon than above it, the top row of the view repeats
	vec2 viewToUv = vec2(1.0) / uViewSize;
	float pixelsPerRow = viewToUv.y / uTargetSizeInv.y;
	float pixelsPerColumn = viewToUv.x / uTargetSizeInv.x;
#if SOFTWARE_RENDERER
	// The software renderer has no position of a fragment to take, and never draws a reflection anyway (it can't copy what
	// is behind a layer), so the copy is taken to cover the view exactly
	vec2 uv = vec2(uQuad.x + UV.x * uQuad.z, viewRow) * viewToUv;
#else
	vec2 uv = gl_FragCoord.xy * uTargetSizeInv;
#endif
	float mirroredRows = 2.0 * row - movedY;
	vec2 mirroredUv = vec2(uv.x - floor(movedX * pixelsPerColumn + 0.5) * uTargetSizeInv.x,
		uv.y - floor(mirroredRows * pixelsPerRow + 0.5) * uTargetSizeInv.y);
	float topRowUv = uv.y - viewRow * viewToUv.y + 0.5 * uTargetSizeInv.y;
	mirroredUv.y = max(mirroredUv.y, topRowUv);

	vec3 color = texture(uTextureBehind, mirroredUv).rgb;
#if !NO_DYNAMIC_BRANCHING && !LOW_POWER_GPU
	if (uBlur > 0.0) {
		vec2 blurStep = vec2(uTargetSizeInv.x * 1.5, 0.0);
		color = (color * 2.0 + texture(uTextureBehind, mirroredUv - blurStep).rgb * 2.0 + texture(uTextureBehind, mirroredUv + blurStep).rgb * 2.0 +
			texture(uTextureBehind, mirroredUv - blurStep * 2.0).rgb + texture(uTextureBehind, mirroredUv + blurStep * 2.0).rgb) * 0.125;
	}
#endif

	// With the horizon at the top of the view there is nothing to mirror, which is black
	color *= step(0.5, horizon);
	COLOR = vec4(mix(color, uTint.rgb, uTint.a), 1.0);
}
