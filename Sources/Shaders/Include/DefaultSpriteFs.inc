uniform sampler2D uTexture : texture_unit(0);

void fragment() {
	COLOR = texture(uTexture, vTexCoords) * vColor;
}
