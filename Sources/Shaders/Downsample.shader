program Downsample;

shader_type canvas_item;

uniform vec2 uPixelOffset;

void fragment() {
	vec4 color = texture(TEXTURE, UV);
	color += texture(TEXTURE, UV + vec2(0.0, uPixelOffset.y));
	color += texture(TEXTURE, UV + vec2(uPixelOffset.x, 0.0));
	color += texture(TEXTURE, UV + uPixelOffset);
	COLOR = vec4(0.25) * color;
}
