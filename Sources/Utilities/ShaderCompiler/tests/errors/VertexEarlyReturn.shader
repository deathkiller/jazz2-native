program VertexEarlyReturn;

shader_type canvas_item;

uniform sampler2D uTexture : texture_unit(0);

void vertex() {
	if (VERTEX.x < 0.0) {
		return;
	}
	VERTEX += vec2(1.0, 0.0);
}

void fragment() {
	COLOR = texture(TEXTURE, UV) * COLOR;
}
