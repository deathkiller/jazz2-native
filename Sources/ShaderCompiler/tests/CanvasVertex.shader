program CanvasVertex;
variant WAVE;

shader_type canvas_item;
render_mode blend_add;

uniform sampler2D uTexture : texture_unit(0);
uniform float uAmplitude : hint_range(0.0, 8.0);

varying flat highp vec2 vOrigin;

void vertex() {
#ifdef WAVE
	VERTEX += vec2(0.0, sin(UV.x * 6.28318) * uAmplitude);
#endif
	vOrigin = VERTEX;
}

void fragment() {
	vec4 c = texture(TEXTURE, UV);
#ifdef WAVE
	c.rgb *= 0.5 + 0.5 * vOrigin.x;
#endif
	COLOR = c * COLOR;
}
