program FrozenMask;
batched BatchedFrozenMask;
variant USE_PALETTE;

shader_type canvas_item;

#ifdef USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

vec4 maskSample(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
#ifdef USE_PALETTE
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 c = texture(uTexturePalette, vec2(palX, palY));
	return vec4(c.rgb, c.a * src.a);
#else
	return src;
#endif
}

void fragment() {
	vec2 size = COLOR.xy * COLOR.a * 2.0;

	vec4 tex = maskSample(UV);
	vec4 tex1 = maskSample(UV + vec2(-size.x, 0));
	vec4 tex2 = maskSample(UV + vec2(0, size.y));
	vec4 tex3 = maskSample(UV + vec2(size.x, 0));
	vec4 tex4 = maskSample(UV + vec2(0, -size.y));

	float outline = tex1.a;
	outline += tex2.a;
	outline += tex3.a;
	outline += tex4.a;
	outline += maskSample(UV + vec2(-size.x, size.y)).a;
	outline += maskSample(UV + vec2(size.x, size.y)).a;
	outline += maskSample(UV + vec2(-size.x, -size.y)).a;
	outline += maskSample(UV + vec2(size.x, -size.y)).a;
	outline = aastep(1.0, outline);

	vec4 color = (tex + tex + tex1 + tex2 + tex3 + tex4) / 6.0;
	float grey = min((0.299 * color.r + 0.587 * color.g + 0.114 * color.b) * 2.6, 1.0);
	COLOR = mix(tex, vec4(0.2 * grey, 0.2 + grey * 0.62, 0.6 + 0.2 * grey, outline * 0.95), COLOR.a);
}
