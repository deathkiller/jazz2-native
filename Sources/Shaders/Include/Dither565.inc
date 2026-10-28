// The colors of the 16-bit renderer of JJ2+: 5, 6 and 5 bits per channel with an ordered dither of 4x4 pixels, so
// gradients show steps and grain
#if !SOFTWARE_RENDERER && !LOW_POWER_GPU
// Steps of each channel
#define DITHER_LEVELS vec3(31.0, 63.0, 31.0)

float bayer2(vec2 pixel) {
	pixel = floor(pixel);
	return fract(pixel.x * 0.5 + pixel.y * pixel.y * 0.75);
}

// From 0 to 15/16 in a 4x4 pattern of the pixels
float bayer4(vec2 pixel) {
	return bayer2(pixel * 0.5) * 0.25 + bayer2(pixel);
}

vec3 reduceColors(vec3 color, vec2 pixel) {
	return floor(color * DITHER_LEVELS + vec3(bayer4(pixel))) / DITHER_LEVELS;
}
#endif
