// The DITHER variant of a textured background: drawn as the 16-bit renderer of JJ2+ draws it, row by row - the texels of a row
// blended linearly into each other (measured, a large texel ramps evenly into the next one), the rows as they are, in the
// colors of Dither565.inc
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
#include "Dither565.inc"

// Texels down the texture
#define DITHER_TEXTURE_SIZE 256.0

vec4 textureTexel(vec2 texturePos) {
	return texture(TEXTURE, vec2(texturePos.x, (floor(texturePos.y * DITHER_TEXTURE_SIZE) + 0.5) / DITHER_TEXTURE_SIZE));
}
#endif
