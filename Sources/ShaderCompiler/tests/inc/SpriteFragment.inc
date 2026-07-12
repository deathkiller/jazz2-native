#ifdef FRAGMENT_STAGE
uniform sampler2D uTexture : texture_unit(0);
#endif

void fragment() {
	COLOR = texture(uTexture, vTexCoords) * vColor;
}
