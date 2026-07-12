uniform sampler2D uTexture : texture_unit(0);

float lightBlend(float t) {
	return t * t * t;
}

void fragment() {
	vec2 center = vTexCoords.xy;
	float radiusNear = vTexCoords.z;
	float intensity = vColor.r;
	float brightness = vColor.g;

	float dist = length(vColor.zw);
	if (dist > 1.0) {
		COLOR = vec4(0.0, 0.0, 0.0, 1.0);
	} else {
		// TODO: Normal maps
		/*vec4 clrNormal = texture(uTexture, vec2(gl_FragCoord) / ViewSize);
		vec3 normal = normalize(clrNormal.xyz - vec3(0.5, 0.5, 0.5));
		normal.z = -normal.z;

		vec3 lightDir = vec3((center.x - gl_FragCoord.x), (center.y - gl_FragCoord.y), 0);

		// Diffuse lighting
		float diffuseFactor = 1.0 - max(dot(normal, normalize(lightDir)), 0.0);
		diffuseFactor = diffuseFactor * 0.8 + 0.2;*/
		float diffuseFactor = 1.0f;

		float strength = diffuseFactor * lightBlend(clamp(1.0 - ((dist - radiusNear) / (1.0 - radiusNear)), 0.0, 1.0));
		COLOR = vec4(strength * intensity, strength * brightness, 0.0, 1.0);
	}
}
