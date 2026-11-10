program SingleColor;
batched BatchedSingleColor;
variant USE_PALETTE;

shader_type canvas_item;

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

// JJ2+ SPRITE::SINGLECOLOR: the sprite's shape in one color, only texel alpha is used (via the palette when indexed)
void fragment() {
	vec4 src = texture(TEXTURE, UV);
#if USE_PALETTE
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	float alpha = texture(uTexturePalette, vec2(palX, palY)).a * src.a;
#else
	float alpha = src.a;
#endif
	COLOR = vec4(COLOR.r, COLOR.g, COLOR.b, alpha * COLOR.a);
}

void fixed_function() {
	// Black-modulated sprite plus the instance color as offset gives a flat color wherever the texture has alpha
	pass p;
	p.color = vec4(0.0, 0.0, 0.0, COLOR.a);
	p.offset_color = COLOR.rgb;
	submit_quad(p);
}
