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

void fixed_function(pvr, gu, gs) {
	// Textures are grayscale (fonts), so the gain is 1.5 * dye, up to 4.5 - more than a vertex color can carry
	// It's split into whole units drawn as additive passes; the framebuffer saturates the sum like the shader
	vec3 gain = 1.5 * (1.0 + (COLOR.rgb - 0.5) * 4.0);
	float alpha = 1.0 + (COLOR.a - 0.5) * 4.0;
	int passes = clamp(int(ceil(max(0.0, max(max(gain.r, gain.g), gain.b)))), 1, 3);
	for (int i = 0; i < passes; i++) {
		// Pass i carries the part of the multiplier above i, up to one unit
		pass p;
		p.color = vec4(gain - float(i), alpha);
		if (i > 0) {
			p.blend = ADD;
		}
		submit_quad(p);
	}
}
void fixed_function(rdp) {
	// Same split, at most 2 passes: the backend merges them into one draw capped at 1.49 per channel
	// (see MaxCombinerResult in RdpDevice.cpp), so a third pass would never change the result
	vec3 gain = 1.5 * (1.0 + (COLOR.rgb - 0.5) * 4.0);
	float alpha = 1.0 + (COLOR.a - 0.5) * 4.0;
	int passes = clamp(int(ceil(max(0.0, max(max(gain.r, gain.g), gain.b)))), 1, 2);
	for (int i = 0; i < passes; i++) {
		// Pass i carries the part of the multiplier above i, up to one unit
		pass p;
		p.color = vec4(gain - float(i), alpha);
		if (i > 0) {
			p.blend = ADD;
		}
		submit_quad(p);
	}
}
void fixed_function(gx, pica, legacygl) {
	// The gain doesn't fit a vertex color, so it's passed at quarter strength and scaled back up x4
	pass p;
	p.tev = MODULATE_X4;
	p.color = vec4(1.5 * (1.0 + (COLOR.rgb - 0.5) * 4.0) * 0.25, 1.0 + (COLOR.a - 0.5) * 4.0);
	submit_quad(p);
}
