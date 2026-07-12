program TexturedBackground;
variant DITHER;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;

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
	// Distance to center of screen from top or bottom (1: center of screen, 0: edge of screen)
	float distance = 1.3 - abs(2.0 * UV.y - 1.0);
	float horizonDepth = pow(distance, 1.4);

	float yShift = (UV.y > 0.5 ? 1.0 : 0.0);
	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));

	vec2 texturePos = vec2(
		(uShift.x / 256.0) + (UV.x - 0.5   ) * (0.5 + (1.5 * horizonDepth)) * correction,
		(uShift.y / 256.0) + (UV.y - yShift) * 1.4 * distance
	);

	vec4 texColor = texture(TEXTURE, texturePos);

#ifdef DITHER
	texturePos += hash2D(UV * uViewSize + (uCameraPos + uShift) * 0.001).xy * 8.0 / uViewSize;
	texColor = mix(texColor, texture(TEXTURE, texturePos), 0.333);
#endif

	float horizonOpacity = clamp(pow(distance, 1.5) - 0.3, 0.0, 1.0);

	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));

		samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}

	COLOR = mix(texColor, horizonColorWithStars, horizonOpacity);
	COLOR.a = 1.0;
}
