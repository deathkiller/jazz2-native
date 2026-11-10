program BackgroundDither;

shader_type canvas_item;
precision highp;

// Redraws what is behind the sprite layer in JJ2+ 16-bit renderer colors (see Dither565.inc);
// layers in front and sprites stay as they are

// One over the target size - the copy may be smaller but covers the same view
uniform vec2 uTargetSizeInv;
// A sharp copy of what is drawn behind the sprite layer
uniform sampler2D uTextureBehind : texture_unit(2);

#include "Include/Dither565.inc"

void fragment() {
	vec3 behind = texture(uTextureBehind, gl_FragCoord.xy * uTargetSizeInv).rgb;
#if !SOFTWARE_RENDERER && !LOW_POWER_GPU
	behind = reduceColors(behind, mod(gl_FragCoord.xy, 4.0));
#endif
	COLOR = vec4(behind, 1.0);
}
