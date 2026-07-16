program WhiteMask;
batched BatchedWhiteMask;
variant USE_PALETTE;

shader_type canvas_item;

#ifdef USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

// Resolves the diffuse color, looking the palette index up in the palette texture when the sprite is indexed
vec4 maskSample(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
#ifdef USE_PALETTE
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 c = texture(uTexturePalette, vec2(palX, palY));
	return vec4(c.rgb, c.a * src.a);
#else
	return src;
#endif
}

void fragment() {
	vec4 tex = maskSample(UV);
	float color = min((0.299 * tex.r + 0.587 * tex.g + 0.114 * tex.b) * 6.0, 1.0);
	COLOR = vec4(color, color, color, tex.a) * COLOR;
}
