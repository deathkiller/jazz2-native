uniform sampler2D uTexture : texture_unit(0);

#define PI 3.1415926

float aastep(float threshold, float value) {
	float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
	return smoothstep(threshold - afwidth, threshold + afwidth, value);
}

float triangleWave(float x, float period) {
  float p = x / period;
  float f = fract(p);
  return abs(f - 0.5) * 2.0;
}

void fragment() {
	vec2 scale = vColor.xy;
	vec2 shift1 = vTexCoords.xy;
	vec2 shift2 = vTexCoords.zw;
	float darkness = vColor.z;
	float alpha = vColor.w;

	float dist = length(vPos);
	if (dist > 1.0) {
		COLOR = vec4(0.0, 0.0, 0.0, 0.0);
	} else {
		vec3 v = vec3(vPos.x, vPos.y, sqrt(1.0 - vPos.x * vPos.x - vPos.y * vPos.y));
		vec3 n = normalize(v);
		float b = dot(n, vec3(0.0, 0.0, 1.0));
		vec2 q = vec2(0.5 - 0.5 * atan(n.z, n.x) / PI, -acos(vPos.y) / PI);

		float isNearBorder = 1.0 - aastep(0.96, dist);

		float mask1 = texture(uTexture, mod(shift1 + (q * scale), 1.0)).r;
		float maskNormalized1 = max(1.0 - (abs(triangleWave(shift2.y + 0.5, 2.0) - mask1) * 6.0), 0.0);

		float mask2 = texture(uTexture, mod(shift2 + (q * scale), 1.0)).r;
		float maskNormalized2 = max(1.0 - (abs(triangleWave(shift1.x, 1.333) - mask2) * 6.0), 0.0);

		float maskSum = min(maskNormalized1 + maskNormalized2, 1.0);

		COLOR = vec4(mix(vec3(1.0, 0.3, 0.0), vec3(1.0, 1.0, 1.0), max((maskSum * 2.0) - 1.0, 0.0)) * darkness, min(maskSum * b * isNearBorder * 1.3 + 0.1, 1.0) * alpha);
	}
}
