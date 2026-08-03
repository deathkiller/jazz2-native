program Colorized;
batched BatchedColorized;

shader_type canvas_item;

void fragment() {
	vec4 dye = vec4(1.0) + (COLOR - vec4(0.5)) * vec4(4.0);
	vec4 original = texture(TEXTURE, UV);
	float average = (original.r + original.g + original.b) * 0.5;
	vec4 gray = vec4(average, average, average, original.a);
	COLOR = gray * dye;
}

void fixed_function(pvr, psp) {
	// Console fixed-function tier: gray = (r + g + b) * 0.5 and COLOR = gray * dye with
	// dye = 1 + (color - 0.5) * 4. The textures this runs on are grayscale (fonts), so r = g = b and
	// that "average" is really a 1.5x brightening; the product reaches 4.5 for a fully bright tint.
	//
	// This is the NO-COMBINER tier, which is why both of these consoles run the same code: a vertex
	// colour cannot carry a multiplier above 1.0 on either, and neither has an output scale to make up
	// the difference (the PVR always modulates; the GE's five texture functions combine one texel with
	// the fragment colour and nothing else). Neither workaround alone is right - folding the excess into
	// the offset colour adds a constant, which lifts a glyph's dark texels as much as its bright ones
	// and blows the antialiased edges out, while simply clamping the multiplier leaves bright tints
	// looking washed out. So the multiplier is split into whole units drawn as successive additive
	// passes - the sum stays proportional to the texel, and the framebuffer saturates it exactly where
	// the shader's own clamp would. Only the GX escapes the split, in its own block below.
	vec3 gain = 1.5 * (1.0 + (COLOR.rgb - 0.5) * 4.0);
	float alpha = 1.0 + (COLOR.a - 0.5) * 4.0;
	int passes = clamp(int(ceil(max(0.0, max(max(gain.r, gain.g), gain.b)))), 1, 3);
	for (int i = 0; i < passes; i++) {
		// Pass i carries whatever of the multiplier is left above i, clamped to one unit
		pass p;
		p.color = vec4(gain - float(i), alpha);
		if (i > 0) {
			p.blend = ADD;
		}
		submit_quad(p);
	}
}
void fixed_function(gx) {
	// GX: the dye exceeds 1.0 for any tint brighter than neutral, which a vertex colour cannot carry,
	// so both the gray gain and the dye are folded in at a quarter strength and the TEV stage scales
	// the result back up (x4 output scale)
	pass p;
	p.tev = MODULATE_X4;
	p.color = vec4(1.5 * (1.0 + (COLOR.rgb - 0.5) * 4.0) * 0.25, 1.0 + (COLOR.a - 0.5) * 4.0);
	submit_quad(p);
}
