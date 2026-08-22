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

void fixed_function(pvr, gu, gs, rdp) {
	// Console fixed-function tier: color = (1/texWidth, 1/texHeight, unused, transition). The GLSL is
	// mix(tex, vec4(0.2*grey, 0.2+0.62*grey, 0.6+0.2*grey, 0.95*outline), transition) - two passes
	// reproduce that mix: the untouched sprite carries the (1-t) side, then an ice silhouette blended
	// on top at alpha 0.95*t carries the ice side. The silhouette is the offset-colour trick (argb rgb
	// zero, so rgb comes from oargb alone while the alpha still modulates the texel alpha - which also
	// stands in for the shader's `outline` term, the sprite's own coverage); the PVR draws that as one
	// specular-enabled pass and the GE as its own GU_TFX_BLEND silhouette, so the ice pass is ONE draw
	// on both. The tone is NOT scaled by t: the pass alpha already applies the transition weighting,
	// and scaling both would darken quadratically.
	//
	// All four consoles share this block because all four are stuck with a CONSTANT ice tone: none can
	// pick a tone per texel (the PVR modulates and adds, the GE has no combiner at all, the GS's
	// only post-texture add is achromatic, and the RDP's combiner has no luminance term - no dot
	// product to weigh the channels with), so the ramp the gx block below uses is out of reach.
	//
	// That constant is the MIDPOINT of the ramp (grey = 0.5), not its bright end. `grey` is luma * 2.6
	// clamped, so it only saturates on sprites brighter than luma 0.385, and the cross blur feeding it
	// pulls in the transparent texels around every edge - taking the high end made the ice read as pale
	// cyan where the shader gives a deep ice blue.
	//
	// The pass alpha is below the shader's 0.95 on purpose. The GLSL `mix` REPLACES the sprite at t = 1,
	// which a silhouette drawn over it cannot do; leaving more of the sprite showing reads as ice with
	// something frozen inside it rather than as a flat painted-on block.
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
	// Same two passes as the shared block above - the untouched sprite carries the (1-t) side of the
	// mix, an ice silhouette at alpha 0.95*t carries the ice side - but the GX does NOT have to
	// settle for a constant ice tone. The GLSL picks it per pixel from `grey` (the texel's luminance
	// amplified 2.6x and saturated): dark texels stay a deep blue, bright ones wash out to pale
	// cyan. LUMA_RAMP is exactly that - the combiner dots the texel against the Rec.601 weights,
	// saturates the scaled result and looks the tone up on a two-endpoint ramp:
	//
	//   grey = 0  ->  vec3(0.2*0, 0.2 + 0.62*0, 0.6 + 0.2*0) = (0.0,  0.2,  0.6)   = ice.color.rgb
	//   grey = 1  ->  vec3(0.2*1, 0.2 + 0.62*1, 0.6 + 0.2*1) = (0.2, 0.82,  0.8)   = ice.offset_color
	//
	// which is linear in grey, so the ramp reproduces the GLSL's tone for EVERY grey, not just the
	// saturated end the flat tone stood for. The texel alpha stands in for the shader's `outline`
	// term (the sprite's own coverage) and the tone is NOT scaled by t - the pass alpha already
	// applies the transition weighting, and scaling both would darken quadratically.
	//
	// The backend still merges the two passes into ONE draw (the premultiplied fold at
	// SubmitMergedSilhouetteOver); the ramp just makes it a six-stage TEV program instead of a
	// two-stage one, which costs nothing per pixel on this hardware.
	// The pass alpha matches the shared block rather than the shader's 0.95, so the frozen effect reads
	// the same across the console tier; only the per-pixel tone differs, which is this block's whole point.
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
