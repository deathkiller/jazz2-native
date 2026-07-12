program OutlinePalette;
batched BatchedOutlinePalette;

shader_type canvas_item;

uniform sampler2D uTexturePalette : texture_unit(1);

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

vec4 palette(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 c = texture(uTexturePalette, vec2(palX, palY));
	return vec4(c.rgb, c.a * src.a);
}

// A texel's real opacity comes from its palette entry's alpha (index 0 is transparent), not the raw texture alpha:
// an R8 index texture reports .a == 1 everywhere, which would make the whole sprite read as opaque and the outline
// would fill its entire bounding box. Resolve through the palette here too (matches palette()'s alpha).
float alphaAt(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	return texture(uTexturePalette, vec2(palX, palY)).a * src.a;
}

void fragment() {
	vec2 size = COLOR.xy;

	float outline = alphaAt(UV + vec2(-size.x, 0));
	outline += alphaAt(UV + vec2(0, size.y));
	outline += alphaAt(UV + vec2(size.x, 0));
	outline += alphaAt(UV + vec2(0, -size.y));
	outline += alphaAt(UV + vec2(-size.x, size.y));
	outline += alphaAt(UV + vec2(size.x, size.y));
	outline += alphaAt(UV + vec2(-size.x, -size.y));
	outline += alphaAt(UV + vec2(size.x, -size.y));
	outline = aastep(1.0, outline);

	float outline2 = alphaAt(UV + vec2(-2.0 * size.x, 0));
	outline2 += alphaAt(UV + vec2(0, 2.0 * size.y));
	outline2 += alphaAt(UV + vec2(2.0 * size.x, 0));
	outline2 += alphaAt(UV + vec2(0, -2.0 * size.y));
	outline2 += alphaAt(UV + vec2(-2.0 * size.x, 2.0 * size.y));
	outline2 += alphaAt(UV + vec2(2.0 * size.x, 2.0 * size.y));
	outline2 += alphaAt(UV + vec2(-2.0 * size.x, -2.0 * size.y));
	outline2 += alphaAt(UV + vec2(2.0 * size.x, -2.0 * size.y));
	outline2 = aastep(1.0, outline2);

	vec4 color = palette(UV);
	COLOR = mix(color,
		mix(vec4(0.0, 0.0, 0.0, COLOR.w * 0.5), vec4(COLOR.z, COLOR.z, COLOR.z, COLOR.w), outline),
		max(outline, outline2) - color.a);
}
