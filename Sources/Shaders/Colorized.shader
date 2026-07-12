program Colorized;
batched BatchedColorized;

shader_type canvas_item;

void fragment() {
	vec4 dye = vec4(1.0) + (COLOR - vec4(0.5)) * vec4(4.0);
	vec4 original = texture(TEXTURE, UV);
	float average = (original.r + original.g + original.b) * 0.5;
	vec4 gray = vec4(average, average, average, original.a);
	COLOR = gray * dye;
}
