program CanvasInitReturn;

shader_type canvas_item;

void fragment() {
	if (UV.x < 0.5) {
		return;
	}
	COLOR = vec4(1.0, 1.0, 1.0, 1.0);
}
