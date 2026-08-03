program Outline;
batched BatchedOutline;

shader_type canvas_item;

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

void fragment() {
	vec2 size = COLOR.xy;

	float outline = texture(TEXTURE, UV + vec2(-size.x, 0)).a;
	outline += texture(TEXTURE, UV + vec2(0, size.y)).a;
	outline += texture(TEXTURE, UV + vec2(size.x, 0)).a;
	outline += texture(TEXTURE, UV + vec2(0, -size.y)).a;
	outline += texture(TEXTURE, UV + vec2(-size.x, size.y)).a;
	outline += texture(TEXTURE, UV + vec2(size.x, size.y)).a;
	outline += texture(TEXTURE, UV + vec2(-size.x, -size.y)).a;
	outline += texture(TEXTURE, UV + vec2(size.x, -size.y)).a;
	outline = aastep(1.0, outline);

	float outline2 = texture(TEXTURE, UV + vec2(-2.0 * size.x, 0)).a;
	outline2 += texture(TEXTURE, UV + vec2(0, 2.0 * size.y)).a;
	outline2 += texture(TEXTURE, UV + vec2(2.0 * size.x, 0)).a;
	outline2 += texture(TEXTURE, UV + vec2(0, -2.0 * size.y)).a;
	outline2 += texture(TEXTURE, UV + vec2(-2.0 * size.x, 2.0 * size.y)).a;
	outline2 += texture(TEXTURE, UV + vec2(2.0 * size.x, 2.0 * size.y)).a;
	outline2 += texture(TEXTURE, UV + vec2(-2.0 * size.x, -2.0 * size.y)).a;
	outline2 += texture(TEXTURE, UV + vec2(2.0 * size.x, -2.0 * size.y)).a;
	outline2 = aastep(1.0, outline2);

	vec4 color = texture(TEXTURE, UV);
	COLOR = mix(color,
		mix(vec4(0.0, 0.0, 0.0, COLOR.w * 0.5), vec4(COLOR.z, COLOR.z, COLOR.z, COLOR.w), outline),
		max(outline, outline2) - color.a);
}

void fixed_function() {
	// Console fixed-function tier: color = (1/texWidth, 1/texHeight, outline grey, alpha). The shader
	// finds the border by summing eight neighbour taps, drawn here instead as eight silhouettes offset
	// by one texel (a black sprite lifted to the outline colour), covered by the sprite itself. (The
	// shader's dimmer second ring at two texels is dropped - it costs another eight quads and barely
	// registers at these resolutions.)
	if (COLOR.a > 0.0 && has_texel_size()) {
		vec2 step = texel_size();
		for (int oy = -1; oy <= 1; oy++) {
			for (int ox = -1; ox <= 1; ox++) {
				if (ox != 0 || oy != 0) {
					pass p;
					p.color = vec4(0.0, 0.0, 0.0, COLOR.a);
					p.offset_color = vec3(COLOR.b);
					p.screen_offset = step * vec2(float(ox), float(oy));
					submit_quad(p);
				}
			}
		}
	}
	pass sprite;
	submit_quad(sprite);
}
