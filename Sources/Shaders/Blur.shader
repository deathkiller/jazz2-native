program Blur;

shader_type canvas_item;

uniform vec2 uPixelOffset;
uniform vec2 uDirection;

void fragment() {
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.3846153846) * uPixelOffset * uDirection;
	vec2 off2 = vec2(3.2307692308) * uPixelOffset * uDirection;
	color += texture(TEXTURE, UV) * 0.2270270270;
	color += texture(TEXTURE, UV + off1) * 0.3162162162;
	color += texture(TEXTURE, UV - off1) * 0.3162162162;
	color += texture(TEXTURE, UV + off2) * 0.0702702703;
	color += texture(TEXTURE, UV - off2) * 0.0702702703;
	COLOR = color;
}
