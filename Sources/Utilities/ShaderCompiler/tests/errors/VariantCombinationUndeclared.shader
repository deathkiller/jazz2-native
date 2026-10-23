program VariantCombinationUndeclared;
variant PLUS;
variant PLUS + DITHER;

shader_type canvas_item;

void fragment() {
	COLOR = texture(TEXTURE, UV);
}
