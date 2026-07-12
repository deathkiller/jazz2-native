program CanvasInitDead;

shader_type canvas_item;

void fragment() {
	vec4 c = texture(TEXTURE, UV);
	COLOR = vec4(c.rgb, 1.0);
}
