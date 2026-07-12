program CanvasInitCompare;

shader_type canvas_item;

void fragment() {
	if (COLOR == vec4(1.0, 1.0, 1.0, 1.0)) {
		discard;
	}
	COLOR = vec4(0.5, 0.5, 0.5, 1.0);
}
