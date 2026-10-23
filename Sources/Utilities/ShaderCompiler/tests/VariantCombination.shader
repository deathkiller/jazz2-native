program Combined;
variant DITHER;
variant KEYED;
variant PLUS;
variant PLUS + DITHER;
variant PLUS + KEYED;

shader_type canvas_item;

uniform vec2 uShift;
#if PLUS
uniform float uPlusScale;
#endif
#if KEYED
uniform float uOpacity;
#endif

// Only the DITHER variants call it, so the other variants don't carry it
vec4 ditherTap(vec2 uv) {
	return texture(TEXTURE, uv + vec2(0.001));
}

void fragment() {
	vec2 uv = UV + uShift;
#if PLUS
	uv *= uPlusScale;
#endif
	vec4 texColor = texture(TEXTURE, uv);
#if DITHER
	texColor = mix(texColor, ditherTap(uv), 0.333);
#endif
#if KEYED
	COLOR = vec4(texColor.rgb, uOpacity);
#else
	COLOR = texColor * COLOR.a;
#endif
}

void fixed_function(pvr) {
	pass quad;
#ifdef PLUS
#ifdef DITHER
	quad.blend = ALPHA;
#endif
#endif
	submit_quad(quad);
}
