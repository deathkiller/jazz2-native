program TexturedBackgroundCircle;
variant DITHER;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;

#define INV_PI 0.31830988618379067153776752675

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec3 voronoi(in vec2 p) {
	vec2 n = floor(p);
	vec2 f = fract(p);

	vec2 mg, mr;

	float md = 8.0;
	for (int j = -1; j <= 1; ++j) {
		for (int i = -1; i <= 1; ++i) {
			vec2 g = vec2(float(i), float(j));
			vec2 o = hash2D(n + g);

			vec2 r = g + o - f;
			float d = dot(r, r);

			if (d < md) {
				md = d;
				mr = r;
				mg = g;
			}
		}
	}
	return vec3(md, mr);
}

float addStarField(vec2 samplePosition, float threshold) {
	vec3 starValue = voronoi(samplePosition);
	if (starValue.x < threshold) {
		float power = 1.0 - (starValue.x / threshold);
		return min(power * power * power, 0.5);
	}
	return 0.0;
}

void fragment() {
	// Position of pixel on screen (between -1 and 1)
	vec2 targetCoord = vec2(2.0) * UV - vec2(1.0);

	// Aspect ratio correction, so display circle instead of ellipse
	targetCoord.x *= uViewSize.x / uViewSize.y;

	// Distance to center of screen
	float distance = length(targetCoord);

	// x-coordinate of tunnel
	float xShift = (targetCoord.x == 0.0 ? sign(targetCoord.y) * 0.5 : atan(targetCoord.y, targetCoord.x) * INV_PI);

	vec2 texturePos = vec2(
		(xShift)         * 1.0 + (uShift.x * 0.01),
		(1.0 / distance) * 1.4 + (uShift.y * 0.002)
	);

	vec4 texColor = texture(TEXTURE, texturePos);

#ifdef DITHER
#ifndef SOFTWARE_RENDERER
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif
#endif

#ifndef SOFTWARE_RENDERER
	float horizonOpacity = 1.0 - clamp(pow(distance, 1.4) - 0.3, 0.0, 1.0);
#else
	// Software-renderer variant: the tunnel keeps its atan()-based warp geometry and the horizon
	// tint, but the per-pixel pow() curve, the dithering second texture sample and the voronoi
	// "star field" (dozens of sin() per pixel when uHorizonColor.w > 0) are far too slow on the
	// CPU, so the pow() is approximated polynomially and dither/stars are dropped entirely.
	float horizonOpacity = 1.0 - clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.4)
#endif

	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
#ifndef SOFTWARE_RENDERER
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));

		samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}
#endif

	COLOR = mix(texColor, horizonColorWithStars, horizonOpacity);
	COLOR.a = 1.0;
}
