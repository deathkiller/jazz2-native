program CanvasInitIfdef;

shader_type canvas_item;

void fragment() {
#ifdef TINT
	COLOR = vec4(1.0, 0.0, 0.0, 1.0);
#endif
	COLOR.a *= 0.5;
}
