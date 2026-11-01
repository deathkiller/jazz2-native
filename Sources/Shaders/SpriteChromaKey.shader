program SpriteChromaKey;
variant USE_PALETTE;

shader_type canvas_item;
precision highp;

// A sprite of SPRITE::CHROMAKEY of JJ2+, drawn as usual but only over the pixels of what is drawn behind it that are of
// the key color (a script draws the teeth of a boss only inside its mouth so)

#if USE_PALETTE
uniform sampler2D uTexturePalette : texture_unit(1);
#endif
// A sharp copy of what is drawn behind the sprite
uniform sampler2D uTextureBehind : texture_unit(2);
// One over the size of the target being drawn into, the copy is smaller or the same size, but covers the same view
uniform vec2 uTargetSizeInv;
// The key color in rgb, and how far each channel of a pixel behind may be from it in alpha - the copy can have fewer bits
// per channel than the palette has
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
	// The software renderer can't copy what is behind a sprite, it's drawn as usual there
	float keyed = 1.0;
#else
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
	vec3 difference = abs(behind - uKeyColor.rgb);
	float keyed = step(max(max(difference.r, difference.g), difference.b), uKeyColor.a);
#endif
	COLOR = vec4(color.rgb, color.a * keyed) * COLOR;
}
