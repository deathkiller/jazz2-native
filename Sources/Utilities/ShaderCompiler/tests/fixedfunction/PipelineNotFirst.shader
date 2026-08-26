// "pipeline <name>;" has to come FIRST, so a reader meets the stage binding before the passes that
// composite over it. A binding buried after a pass is a hard error rather than a silent reordering.
program PipelineNotFirst;
shader_type canvas_item;

void fragment() {
	COLOR = texture(TEXTURE, UV);
}

void fixed_function(pvr) {
	pass p;
	p.color = COLOR;
	pipeline lighting_combine;
	submit_quad(p);
}
