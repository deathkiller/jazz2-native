program FrozenMask;
batched BatchedFrozenMask;
variant USE_PALETTE;

shader_type canvas_item;

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

vec4 maskSample(vec2 uv) {
	vec4 src = texture(TEXTURE, uv);
#if USE_PALETTE
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

void fixed_function(pvr, pica, gu, gs, rdp, legacygl) {
	// The sprite as is, then an ice silhouette over it at alpha 0.8 * t (t = COLOR.a, the transition)
	// These consoles can't pick a tone per texel, so the ice uses the ramp's midpoint (grey = 0.5)
	// Alpha is below the shader's 0.95 so the sprite stays visible inside the ice
	float t = clamp(COLOR.a, 0.0, 1.0);
	pass sprite;
	submit_quad(sprite);
	if (t > 0.0) {
		pass ice;
		ice.color = vec4(0.0, 0.0, 0.0, 0.8 * t);
		ice.offset_color = vec3(0.1, 0.51, 0.7);
		submit_quad(ice);
	}
}
void fixed_function(gx) {
	// Same two passes, but LUMA_RAMP picks the tone per pixel from luma * 2.6 like the shader:
	// grey 0 -> (0.0, 0.2, 0.6) = color.rgb, grey 1 -> (0.2, 0.82, 0.8) = offset_color
	// Alpha matches the block above so the effect looks the same on all consoles
	pass sprite;
	submit_quad(sprite);
	float t = clamp(COLOR.a, 0.0, 1.0);
	if (t > 0.0) {
		pass ice;
		ice.tev = LUMA_RAMP;
		ice.luma_gain = 2.6;
		ice.color = vec4(0.0, 0.2, 0.6, 0.8 * t);
		ice.offset_color = vec3(0.2, 0.82, 0.8);
		submit_quad(ice);
	}
}
