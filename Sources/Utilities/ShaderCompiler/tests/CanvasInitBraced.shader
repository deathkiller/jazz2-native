program CanvasInitBraced;

shader_type canvas_item;

void fragment() {
	if (UV.x < 0.5) {
		COLOR = vec4(0.0, 0.0, 0.0, 1.0);
	}
}
