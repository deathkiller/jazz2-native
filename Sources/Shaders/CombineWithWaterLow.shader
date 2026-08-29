program CombineWithWaterLow;

#include "Include/CombineVs.inc"

uniform sampler2D uTexture : texture_unit(0);
uniform sampler2D uTextureLighting : texture_unit(1);

uniform vec4 uAmbientColor;
uniform float uTime;
uniform vec2 uCameraPos;
uniform float uWaterLevel;

void fragment() {
	vec3 waterColor = vec3(0.4, 0.6, 0.8);

	vec2 uvLocal = vTexCoords;
	vec2 uvWorldCenter = (uCameraPos.xy * vViewSizeInv.xy);
	vec2 uvWorld = uvLocal + uvWorldCenter;
	// One moving wave restores the water silhouette without the high-quality shader's noise field.
	float waveHeight = sin((uvWorld.x - uTime) * 60.0) * 0.007;
	float waterSurface = uWaterLevel + waveHeight;

	float isTexelBelow = 1.0 - step(uvLocal.y, waterSurface);
	float isTexelAbove = 1.0 - isTexelBelow;

	vec2 uv = clamp(uvLocal + vec2(0.008 * sin(uTime * 16.0 + uvWorld.y * 20.0) * isTexelBelow, 0.0), vec2(0.0), vec2(1.0));
	vec4 main = texture(uTexture, uv);

	// Waves
	float topDist = abs(uvLocal.y - waterSurface);
	float topGradient = max(1.0 - topDist, 0.0);
	float isNearTop = 0.2 * topGradient * topGradient;
	float isVeryNearTop = 1.0 - step(vViewSizeInv.y, topDist);
	main.rgb = mix(main.rgb, waterColor, vec3(isTexelBelow * 0.4)) + vec3((isNearTop + 0.2 * isVeryNearTop) * isTexelBelow);

	// Lighting
	vec4 light = texture(uTextureLighting, uv);

	float darknessStrength = (1.0 - light.r);

	// Darkness above water
	if (uWaterLevel < 0.4) {
		float aboveWaterDarkness = isTexelAbove * (0.4 - uWaterLevel);
		darknessStrength = min(1.0, darknessStrength + aboveWaterDarkness);
	}

	COLOR = mix(main * (1.0 + light.g) + max(light.g - 0.7, 0.0) * vec4(1.0), uAmbientColor, vec4(darknessStrength));
	COLOR.a = 1.0;
}

void fixed_function() {
	// The low-quality water compositor shares the direct-tier base composite too
	pipeline lighting_combine;
}
