program PartialWhiteMask;
batched BatchedPartialWhiteMask;
variant USE_PALETTE;

shader_type canvas_item;

#ifdef USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

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
	float color = min((0.299 * tex.r + 0.587 * tex.g + 0.114 * tex.b) * 2.5, 1.0);
	COLOR = vec4(color, color, color, tex.a) * COLOR;
}

void fixed_function(pvr, psp, gs) {
	// Console fixed-function tier: brightened but still shaded (the shader's luma x2.5), which the
	// consoles express in two ways - and these three share the same one. Keep the sprite and lift it by a
	// constant through the post-texture offset colour: on the PVR that is one specular-enabled draw, and
	// the GE and the GS reach the identical result because their SubmitQuad expands a pass carrying an
	// offset colour into the modulated sprite plus an additive flat-colour pass over it. None of them has
	// the GX's output scale, so none can use the gx block below; the offset-colour lift is what all three
	// are left with, so it is written once.
	pass p;
	p.color = COLOR;
	p.offset_color = vec3(96.0 / 255.0);
	submit_quad(p);
}
void fixed_function(gx) {
	// GX: no post-texture add exists, so the combiner's clamped x2 output scale stands in for the boost
	pass p;
	p.tev = MODULATE_X2;
	p.color = COLOR;
	submit_quad(p);
}
