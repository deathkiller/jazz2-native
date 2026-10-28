// The blend modes of JJ2+ a layer is drawn with over what is behind it (see Tiles::LayerBlend), those that take its color: the
// value of the mode, the color of the layer and the color behind it. Darken, lighten, hue, saturation, color, luminosity,
// dissolve, overlay, hard light, soft light, difference, dodge, burn and exclusion, as the image editors have them

float blendLuminosity(vec3 c) {
	return dot(c, vec3(0.3, 0.59, 0.11));
}

vec3 blendSetLuminosity(vec3 c, float l) {
	c += vec3(l - blendLuminosity(c));
	float lum = blendLuminosity(c);
	float lo = min(min(c.r, c.g), c.b);
	float hi = max(max(c.r, c.g), c.b);
	if (lo < 0.0) {
		c = vec3(lum) + (c - vec3(lum)) * lum / max(lum - lo, 0.0001);
	}
	if (hi > 1.0) {
		c = vec3(lum) + (c - vec3(lum)) * (1.0 - lum) / max(hi - lum, 0.0001);
	}
	return c;
}

float blendSaturation(vec3 c) {
	return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

vec3 blendSetSaturation(vec3 c, float s) {
	float lo = min(min(c.r, c.g), c.b);
	float hi = max(max(c.r, c.g), c.b);
	return (hi > lo ? (c - vec3(lo)) * s / (hi - lo) : vec3(0.0));
}

vec3 blendHardLight(vec3 s, vec3 d) {
	vec3 multiply = 2.0 * s * d;
	vec3 screen = vec3(1.0) - 2.0 * (vec3(1.0) - s) * (vec3(1.0) - d);
	return mix(multiply, screen, step(vec3(0.5), s));
}

vec3 blendModeColor(float mode, vec3 s, vec3 d) {
	if (mode < 3.5) {
		return vec3(1.0) - (vec3(1.0) - s) * (vec3(1.0) - d);
	} else if (mode < 4.5) {
		return s * d;
	} else if (mode < 5.5) {
		return min(s, d);
	} else if (mode < 6.5) {
		return max(s, d);
	} else if (mode < 7.5) {
		return blendSetLuminosity(blendSetSaturation(s, blendSaturation(d)), blendLuminosity(d));
	} else if (mode < 8.5) {
		return blendSetLuminosity(blendSetSaturation(d, blendSaturation(s)), blendLuminosity(d));
	} else if (mode < 9.5) {
		return blendSetLuminosity(s, blendLuminosity(d));
	} else if (mode < 10.5) {
		return blendSetLuminosity(d, blendLuminosity(s));
	} else if (mode < 11.5) {
		return s;
	} else if (mode < 12.5) {
		return blendHardLight(d, s);
	} else if (mode < 13.5) {
		return blendHardLight(s, d);
	} else if (mode < 14.5) {
		vec3 dark = d - (vec3(1.0) - 2.0 * s) * d * (vec3(1.0) - d);
		vec3 lightD = mix(((16.0 * d - vec3(12.0)) * d + vec3(4.0)) * d, sqrt(d), step(vec3(0.25), d));
		vec3 light = d + (2.0 * s - vec3(1.0)) * (lightD - d);
		return mix(dark, light, step(vec3(0.5), s));
	} else if (mode < 15.5) {
		return abs(s - d);
	} else if (mode < 16.5) {
		return min(d / max(vec3(1.0) - s, vec3(0.0001)), vec3(1.0));
	} else if (mode < 17.5) {
		return vec3(1.0) - min((vec3(1.0) - d) / max(s, vec3(0.0001)), vec3(1.0));
	} else {
		return s + d - 2.0 * s * d;
	}
}

// How opaque a pixel of the layer is, a dissolve draws a pixel fully or not at all as many times as it's opaque
float blendModeAlpha(float mode, float alpha, vec2 pixel) {
	if (mode > 10.5 && mode < 11.5) {
		float noise = fract(sin(dot(floor(pixel), vec2(12.9898, 78.233))) * 43758.5453);
		return step(noise, alpha);
	}
	return alpha;
}

// From 0 to 15/16 in a 4x4 pattern of the pixels
float blendBayer4(vec2 pixel) {
	pixel = floor(pixel);
	vec2 coarsePixel = floor(pixel * 0.5);
	float coarse = fract(coarsePixel.x * 0.5 + coarsePixel.y * coarsePixel.y * 0.75);
	float fine = fract(pixel.x * 0.5 + pixel.y * pixel.y * 0.75);
	return coarse * 0.25 + fine;
}

// What the pixel of the layer is drawn with: screened and multiplied premultiplied by how opaque it is, which the fixed blending
// takes, any other mode the color the mode makes with what is behind it, mixed over it by alpha. With mode.y non-zero a pixel is
// drawn fully or not at all, as many of them as the layer is opaque in an ordered pattern, as the 16-bit renderer of JJ2+ draws
// a translucent blend
vec4 blendModeResult(vec2 mode, vec3 s, float a, vec3 d, vec2 pixel) {
	a = blendModeAlpha(mode.x, a, pixel);
	if (mode.y > 0.5) {
		a = step(blendBayer4(pixel) + 1.0 / 32.0, a);
	}
	if (mode.x < 4.5) {
		return vec4(s * a, a);
	}
	return vec4(blendModeColor(mode.x, s, d), a);
}