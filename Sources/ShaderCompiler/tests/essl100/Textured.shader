program Essl100Textured;

attribute vec2 aPosition;
attribute layout(location = 1) vec2 aTexCoords;

uniform mat4 uProjectionMatrix;
uniform sampler2D uTexture : texture_unit(0);

varying vec2 vTexCoords;

void vertex() {
	vTexCoords = aTexCoords;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	COLOR = texture(uTexture, vTexCoords);
}
