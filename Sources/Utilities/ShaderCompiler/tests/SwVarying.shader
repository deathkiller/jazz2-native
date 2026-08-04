program SwVarying;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec4 vTexCoords;
varying vec4 vColor;
#ifndef SOFTWARE_RENDERER
varying vec2 vPos;
#else
// Software-only constant varying: the SW transpiler reads it from the instance block; every
// other backend's emission must not contain it (nor the SOFTWARE_RENDERER macro itself)
varying vec4 vRect;
#endif

uniform sampler2D uTexture : texture_unit(0);

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = texRect;
	vColor = color;
#ifndef SOFTWARE_RENDERER
	vPos = aPosition * vec2(2.0) - vec2(1.0);
#else
	vRect = texRect;
#endif
}

void fragment() {
#ifndef SOFTWARE_RENDERER
	vec2 shift = vTexCoords.xy;
	vec2 pos = vPos;
#else
	vec2 shift = vRect.xy;
	vec2 pos = vTexCoords.xy * vec2(2.0) - vec2(1.0);
#endif
	COLOR = texture(uTexture, shift + pos * 0.5) * vColor;
}
