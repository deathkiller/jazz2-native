program TouchCircle;

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
// Passed on so the fragment stage needn't read InstanceBlock at vertex precision
varying vec4 vColor;
varying vec2 vRadii;

void vertex() {
	vec2 aPosition = vec2(float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);
	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vPos = aPosition - vec2(0.5, 0.5);
	vColor = color;
	vRadii = texRect.xy;
}

void fragment() {
	float dist = length(vPos);
	float softness = vRadii.y;
	float outerAlpha = 1.0 - smoothstep(0.5 - softness, 0.5, dist);
	float innerRadius = vRadii.x * 0.5;
	float innerAlpha = (innerRadius > 0.0) ? smoothstep(innerRadius - softness, innerRadius, dist) : 1.0;
	COLOR = vec4(vColor.rgb, vColor.a * outerAlpha * innerAlpha);
}
