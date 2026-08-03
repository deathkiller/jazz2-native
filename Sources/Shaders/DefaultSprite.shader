program DefaultSprite;
batched DefaultBatchedSprites;

shader_type canvas_item;

void fragment() {
	COLOR = texture(TEXTURE, UV) * COLOR;
}

void fixed_function() {
	// Console fixed-function tier: a plain modulated sprite (shared by the batched twin)
	pass p;
	p.color = COLOR;
	submit_quad(p);
}
