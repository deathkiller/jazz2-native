program ImplicitTexture;
batched BatchedImplicitTexture;

shader_type canvas_item;

uniform vec2 uPixelOffset;

void fragment() {
	COLOR = texture(TEXTURE, UV + uPixelOffset) * COLOR;
}
