program SingleColor;
batched BatchedSingleColor;
variant USE_PALETTE;

shader_type canvas_item;

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

// The shape of the sprite in a single color, as SPRITE::SINGLECOLOR of JJ2+ - only the alpha of the texel is taken,
// looking the palette index up in the palette texture when the sprite is indexed (an index is transparent where its
// palette entry is)
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
	// Console fixed-function tier: the black-modulated sprite with the instance colour added on top of it is that
	// colour wherever the texture has alpha (oargb+specular on the PVR, the silhouette TEV preset on the GX), the
	// same as WhiteMask reaches its flat mask
	pass p;
	p.color = vec4(0.0, 0.0, 0.0, COLOR.a);
	p.offset_color = COLOR.rgb;
	submit_quad(p);
}
