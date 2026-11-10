program SpriteChromaKey;
variant USE_PALETTE;

shader_type canvas_item;
precision highp;

// JJ2+ SPRITE::CHROMAKEY: the sprite shows only where the pixel behind it matches the key color

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// A sharp copy of what is drawn behind the sprite
uniform sampler2D uTextureBehind : texture_unit(2);
// One over the target size - the copy may be smaller but covers the same view
uniform vec2 uTargetSizeInv;
// Key color in rgb, per-channel tolerance in alpha (the copy may have fewer bits than the palette)
uniform vec4 uKeyColor;

void fragment() {
	vec4 src = texture(TEXTURE, UV);
#if USE_PALETTE
	highp float palIndex = floor(PALETTE_OFFSET + 0.5) + floor(src.r * 255.0 + 0.5);
	highp float palX = (mod(palIndex, 256.0) + 0.5) / 256.0;
	highp float palY = (floor(palIndex / 256.0) + 0.5) / 256.0;
	vec4 color = texture(uTexturePalette, vec2(palX, palY));
	color.a *= src.a;
#else
	vec4 color = src;
#endif
#if SOFTWARE_RENDERER
	// The software renderer can't copy what's behind, so draw as usual
	float keyed = 1.0;
#else
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
	vec3 difference = abs(behind - uKeyColor.rgb);
	float keyed = step(max(max(difference.r, difference.g), difference.b), uKeyColor.a);
#endif
	COLOR = vec4(color.rgb, color.a * keyed) * COLOR;
}
