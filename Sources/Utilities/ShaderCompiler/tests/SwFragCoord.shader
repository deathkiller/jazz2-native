program SwFragCoord;

shader_type canvas_item;

uniform vec2 uTargetSizeInv;

// gl_FragCoord has no counterpart in the software rasterizer's fragment input, so the software transpile
// must decline the shader with a reason instead of emitting C++ that names an undeclared variable; every
// other emission keeps the read
void fragment() {
	COLOR = texture(TEXTURE, gl_FragCoord.xy * uTargetSizeInv);
}
