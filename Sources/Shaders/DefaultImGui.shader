program DefaultImGui;

uniform mat4 uGuiProjection;
uniform float uDepth;

attribute vec2 aPosition;
attribute vec2 aTexCoords;
attribute vec4 aColor;

varying vec2 vTexCoords;
varying vec4 vColor;

void vertex() {
	gl_Position = uGuiProjection * vec4(aPosition, uDepth, 1.0);
	vTexCoords = aTexCoords;
	vColor = aColor;
}

uniform sampler2D uTexture : texture_unit(0);

void fragment() {
	COLOR = vColor * texture(uTexture, vTexCoords);
}
