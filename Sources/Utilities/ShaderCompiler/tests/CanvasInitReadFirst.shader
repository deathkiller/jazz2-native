program CanvasInitReadFirst;

shader_type canvas_item;

void fragment() {
	vec4 dye = COLOR * 2.0;
	COLOR = texture(TEXTURE, UV) * dye;
}
