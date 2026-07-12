program Tinted;
batched BatchedTinted;
variant USE_PALETTE;

shader_type canvas_item;

#ifdef USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

void fragment() {
#ifdef USE_PALETTE
	vec4 src = texture(TEXTURE, UV);
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 original = texture(uTexturePalette, vec2(palX, palY));
	original.a *= src.a;
#else
	vec4 original = texture(TEXTURE, UV);
#endif
	vec3 tinted = mix(original.rgb, COLOR.rgb, 0.45);
	COLOR = vec4(tinted.r, tinted.g, tinted.b, original.a * COLOR.a);
}
