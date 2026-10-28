program BackgroundDither;

shader_type canvas_item;
precision highp;

// What is drawn behind the sprite layer in the colors of the 16-bit renderer of JJ2+ (see Dither565.inc), the layers in
// front of it and the sprites stay as they are: the quad covers the view and replaces it with the copy of it taken there

// One over the size of the target being drawn into, the copy is smaller or the same size, but covers the same view
uniform vec2 uTargetSizeInv;
// A sharp copy of what is drawn behind the sprite layer
uniform sampler2D uTextureBehind : texture_unit(2);

#include "Include/Dither565.inc"

void fragment() {
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
#if !SOFTWARE_RENDERER && !LOW_POWER_GPU
	behind = reduceColors(behind, gl_FragCoord.xy);
#endif
	COLOR = vec4(behind, 1.0);
}
