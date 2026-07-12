program CanvasInitCompound;

shader_type canvas_item;

void fragment() {
	COLOR *= texture(TEXTURE, UV);
}
