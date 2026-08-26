// A fixed_function block that binds a backend pipeline stage AND describes passes: the stage keeps the
// part it cannot delegate (a per-vertex or per-texel loop over an engine buffer) and the passes carry
// the per-draw policy, run after it. Also covers uniform_float() and the SamplesTexture analysis - an
// effect that only submits SHADED strips never samples the bound texture, so its entry must not claim
// the requirement even though the program declares a sampler for its fragment stage.
program PipelinePasses;
shader_type canvas_item;

uniform float uLevel;

void fragment() {
	COLOR = texture(TEXTURE, UV);
}

void fixed_function(pvr) {
	pipeline lighting_combine;

	vec2 origin = quad_origin();
	vec2 axisX = quad_axis_x();
	vec2 axisY = quad_axis_y();
	float level = uniform_float(uLevel);
	vec2 split = origin + level * axisY;

	pass p;
	p.blend = ALPHA;
	strip_position(0, origin);
	strip_position(1, origin + axisX);
	strip_position(2, split);
	strip_position(3, split + axisX);
	strip_color(0, vec4(0.0, 0.0, 0.0, 1.0));
	strip_color(1, vec4(0.0, 0.0, 0.0, 1.0));
	strip_color(2, vec4(0.0, 0.0, 0.0, 1.0));
	strip_color(3, vec4(0.0, 0.0, 0.0, 1.0));
	submit_strip_shaded(p, 4);
}
