program Blur;

shader_type canvas_item;

uniform vec2 uPixelOffset;
uniform vec2 uDirection;

#if LOW_POWER_GPU
// Low-power-GPU variant (the PS Vita's sceGxm and the OpenGL|ES 2.0 profile): the four outer taps are offset per
// vertex instead of per fragment, so every sample reads its coordinate straight from an interpolant - a read the
// SGX issues before the fragment program even runs, where an offset added in the program makes it a dependent one
varying vec2 vTap1;
varying vec2 vTap2;
varying vec2 vTap3;
varying vec2 vTap4;

void vertex() {
	vec2 off1 = vec2(1.3846153846) * uPixelOffset * uDirection;
	vec2 off2 = vec2(3.2307692308) * uPixelOffset * uDirection;
	vTap1 = UV + off1;
	vTap2 = UV - off1;
	vTap3 = UV + off2;
	vTap4 = UV - off2;
}
#endif

void fragment() {
#if LOW_POWER_GPU
	vec4 color = texture(TEXTURE, UV) * 0.2270270270;
	color += (texture(TEXTURE, vTap1) + texture(TEXTURE, vTap2)) * 0.3162162162;
	color += (texture(TEXTURE, vTap3) + texture(TEXTURE, vTap4)) * 0.0702702703;
#else
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.3846153846) * uPixelOffset * uDirection;
	vec2 off2 = vec2(3.2307692308) * uPixelOffset * uDirection;
	color += texture(TEXTURE, UV) * 0.2270270270;
	color += texture(TEXTURE, UV + off1) * 0.3162162162;
	color += texture(TEXTURE, UV - off1) * 0.3162162162;
	color += texture(TEXTURE, UV + off2) * 0.0702702703;
	color += texture(TEXTURE, UV - off2) * 0.0702702703;
#endif
	COLOR = color;
}
