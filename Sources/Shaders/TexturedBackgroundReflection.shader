program TexturedBackgroundReflection;

shader_type canvas_item;
precision highp;

// JJ2+ reflection: below its horizon, mirrors what's behind it above the horizon, like a water plane. The texture on
// that plane holds palette indices (in true color, the brightest channel), and index differences between texels
// ripple the mirrored image. The quad covers the part below the horizon, UV runs over it.

// View size in level pixels, which the copy of what's behind covers
uniform vec2 uViewSize;
// Quad in the view: xy top left, zw size
uniform vec4 uQuad;
// x: horizon row, y: vanishing point column, z: camera height above the plane, w: distortion (0 to 1 for 0 to 255)
// The plane's own horizon is half the height above the drawn one, so a pixel at the horizon is one texel wide
uniform vec4 uHorizon;
// xy: texture position on the plane in texels, zw: 1 / texture size
uniform vec4 uShift;
// Tint color, alpha is its opacity
uniform vec4 uTint;
// 1 to blur the mirrored image horizontally
uniform float uBlur;
// 1 / target size; the copy may be smaller but covers the same view
uniform vec2 uTargetSizeInv;

// A sharp copy of what is drawn behind the layer
uniform sampler2D uTextureBehind : texture_unit(2);

#define FOCAL_LENGTH 330.0
// The texture is twice the size of the plane's units
#define TEXTURE_SCALE 0.5
// JJ2+ ripples on smooth textures are 0.5 to 0.76 of what the index differences give (rule unknown)
#define STRENGTH 0.6

float paletteIndex(vec2 texturePos) {
	vec4 texel = texture(TEXTURE, texturePos * uShift.zw);
	return floor(max(texel.r, max(texel.g, texel.b)) * 255.0 + 0.5);
}

float truncateTowardZero(float value) {
	// Tolerates whole numbers that come out a hair smaller
	return sign(value) * floor(abs(value) + 0.001);
}

void fragment() {
	// View row, rows below the horizon, and column from the vanishing point
	float horizon = uHorizon.x;
	float viewRow = uQuad.y + UV.y * uQuad.w;
	float rows = viewRow - horizon;
	float column = uQuad.x + UV.x * uQuad.z - uHorizon.y;

	// Plane point seen here, rows nearer the horizon see further; the texture runs down the plane towards the camera,
	// so a positive speed moves it towards the horizon
	float planeRows = max(rows, 0.0) + uHorizon.z * 0.5;
	float depth = uHorizon.z / planeRows;
	vec2 texturePos = vec2(column * depth, -depth * FOCAL_LENGTH) * TEXTURE_SCALE + uShift.xy;

	// Ripples in palette indices: vertical is the texel minus the one half a texture further, horizontal is the texel
	// a quarter back and left minus the one half a texture right of it
	float rippleY = paletteIndex(texturePos) - paletteIndex(texturePos + vec2(0.0, 128.0));
	float rippleX = paletteIndex(texturePos + vec2(-64.0, 64.0)) - paletteIndex(texturePos + vec2(64.0, 64.0));
	// One view pixel per 32 indices times distortion and STRENGTH; near the horizon less:
	// 1/2 for rows 7-14, 1/4 for 3-6, 1/8 for 1-2, 1/16 at the horizon
	float row = floor(max(rows, 0.0));
	// Indices and distortion are whole numbers, so the ripple offsets come out exact
	float nearHorizon = 0.0625 * (1.0 + step(1.0, row)) * (1.0 + step(3.0, row)) * (1.0 + step(7.0, row)) * (1.0 + step(15.0, row));
	float scale = floor(uHorizon.w * 255.0 + 0.5) / 32.0 * nearHorizon * STRENGTH;
	// Higher texels sample nearer the horizon (never at or below it) and further left
	float movedY = min(truncateTowardZero(rippleY * scale), max(row - 1.0, 0.0));
	float movedX = truncateTowardZero(rippleX * scale);

	// Mirror at the horizon plus the ripples, in whole target pixels to stay sharp; the first row below the horizon
	// mirrors itself, and the top view row repeats if there are more rows below than above
	vec2 viewToUv = vec2(1.0) / uViewSize;
	float pixelsPerRow = viewToUv.y / uTargetSizeInv.y;
	float pixelsPerColumn = viewToUv.x / uTargetSizeInv.x;
#if SOFTWARE_RENDERER
	// No fragment position here (and no reflections drawn anyway), so the copy is taken to cover the view
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

	// Horizon at the top of the view: nothing to mirror, so black
	color *= step(0.5, horizon);
	COLOR = vec4(mix(color, uTint.rgb, uTint.a), 1.0);
}
