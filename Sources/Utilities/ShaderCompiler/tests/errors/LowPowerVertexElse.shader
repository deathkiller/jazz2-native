program LowPowerVertexElse;

shader_type canvas_item;

#if !LOW_POWER_GPU
void vertex() {
	VERTEX += vec2(1.0, 0.0);
}
#endif

void fragment() {
	COLOR = texture(TEXTURE, UV);
}
