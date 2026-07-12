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
