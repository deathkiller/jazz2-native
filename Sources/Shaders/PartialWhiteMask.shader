program PartialWhiteMask;
batched BatchedPartialWhiteMask;
variant USE_PALETTE;

shader_type canvas_item;

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

vec4 maskSample(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
#if USE_PALETTE
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

void fixed_function(pvr, gu, gs, rdp) {
	// Console fixed-function tier: brightened but still shaded (the shader's luma x2.5), which the
	// consoles express in two ways - and these four share the same one. Keep the sprite and lift it by a
	// constant through the post-texture offset colour: on the PVR that is one specular-enabled draw, the
	// GE and the GS reach the identical result because their SubmitQuad expands a pass carrying an
	// offset colour into the modulated sprite plus an additive flat-colour pass over it, and the RDP
	// delivers the add in its combiner's own D term with the constant in the ENV register. The GE and
	// the GS lack the GX's output scale outright; the RDP's second combiner cycle could double like the
	// GX does, but the offset-colour lift reaches the shader's tone just as well and is the one form
	// all four express identically, so it is written once.
	pass p;
	p.color = COLOR;
	p.offset_color = vec3(96.0 / 255.0);
	submit_quad(p);
}
void fixed_function(gx, legacygl) {
	// GX: no post-texture add exists, so the combiner's clamped x2 output scale stands in for the boost
	pass p;
	p.tev = MODULATE_X2;
	p.color = COLOR;
	submit_quad(p);
}
