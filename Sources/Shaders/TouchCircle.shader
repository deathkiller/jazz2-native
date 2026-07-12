program TouchCircle;

// The InstanceBlock is deliberately shared by both stages - the fragment stage reads color/texRect directly
layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

varying vec2 vPos;

void vertex() {
	vec2 aPosition = vec2(float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPos = aPosition - vec2(0.5, 0.5);
}

void fragment() {
	float dist = length(vPos);
	float softness = texRect.y;
	float outerAlpha = 1.0 - smoothstep(0.5 - softness, 0.5, dist);
	float innerRadius = texRect.x * 0.5;
	float innerAlpha = (innerRadius > 0.0) ? smoothstep(innerRadius - softness, innerRadius, dist) : 1.0;
	COLOR = vec4(color.rgb, color.a * outerAlpha * innerAlpha);
}
