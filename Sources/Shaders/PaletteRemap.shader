program PaletteRemap;
batched BatchedPaletteRemap;

shader_type canvas_item;

uniform sampler2D uTexturePalette : texture_unit(1);

void fragment() {
	vec4 src = texture(TEXTURE, UV);
	// Instance offset + pixel index (red channel), mapped into the 256x256 palette texture
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 color = texture(uTexturePalette, vec2(palX, palY));
	COLOR = vec4(color.rgb, color.a * src.a) * COLOR;
}

void fixed_function() {
	// Plain modulated sprite - the remap is texture state chosen by the dispatch
	pass p;
	p.color = COLOR;
	submit_quad(p);
}
