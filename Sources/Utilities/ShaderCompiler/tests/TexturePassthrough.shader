program TexturePassthrough;

uniform sampler2D TEXTURE : texture_unit(2);

varying vec2 vTexCoords;

void vertex() {
	vTexCoords = vec2(float(gl_VertexID >> 1), float(gl_VertexID % 2));
	gl_Position = vec4(vTexCoords, 0.0, 1.0);
}

void fragment() {
	COLOR = texture(TEXTURE, vTexCoords);
}
