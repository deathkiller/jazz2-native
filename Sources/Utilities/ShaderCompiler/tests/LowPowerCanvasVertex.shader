program LowPowerCanvasVertex;

shader_type canvas_item;

uniform vec2 uOffset;

#if LOW_POWER_GPU
// A vertex() entry only the low-power emissions (Cg, ESSL 100) get. Every other emission keeps the default
// canvas vertex template - the text it would get with no vertex() at all - so it never declares uOffset in
// its vertex stage, while its fragment stage still reads it in the default branch below
varying vec2 vShifted;

void vertex() {
	vShifted = UV + uOffset;
}
#endif

void fragment() {
#if LOW_POWER_GPU
	vec4 c = texture(TEXTURE, vShifted);
#else
	vec4 c = texture(TEXTURE, UV + uOffset);
#endif
	COLOR = c;
}
