// The KEYED variant of a textured background (see Tiles::LayerBlend): drawn only over the pixels of what is behind
// it of the key color, as SPRITE::CHROMAKEY of JJ2+, as opaque as uOpacity - so a layer can be translucent, or show
// through a part of the scene painted in the key color - or mixed with what is behind it by one of the blend modes
#if KEYED
#include "BlendModes.inc"

// A sharp copy of what is behind the layer, without a key or a blend mode it's never read
uniform sampler2D uTextureBehind : texture_unit(2);
// One over the size of the target being drawn into, the copy is smaller or the same size, but covers the same view
uniform vec2 uTargetSizeInv;
// The key color in rgb, and how far each channel of a pixel behind the layer may be from it in alpha - the copy can have
// fewer bits per channel than the palette has - or a negative alpha for no key
uniform vec4 uKeyColor;
// How opaque the layer is where it's shown
uniform float uOpacity;
// The blend mode the color is mixed with what is behind it by in x (see BlendModes.inc), 0 for none, and in y non-zero if its
// opacity is an ordered pattern of whole pixels
uniform vec2 uBlendMode;

vec4 keyedColor(vec3 color) {
#if SOFTWARE_RENDERER
	// The software renderer has no position of a fragment to take, and can't copy what is behind a layer anyway
	return vec4(color, uOpacity);
#else
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
	if (uBlendMode.x > 0.5) {
		return blendModeResult(uBlendMode, color, uOpacity, behind, gl_FragCoord.xy);
	}
	vec3 difference = abs(behind - uKeyColor.rgb);
	// Over the key color, or everywhere without a key
	return vec4(color, uOpacity * max(step(max(max(difference.r, difference.g), difference.b), uKeyColor.a), step(uKeyColor.a, -0.5)));
#endif
}
#endif