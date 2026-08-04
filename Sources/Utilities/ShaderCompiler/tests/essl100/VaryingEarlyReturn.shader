program Essl100Varying;

precision highp;

attribute vec2 aPosition;

uniform mat4 uProjectionMatrix;
uniform sampler2D uTexture : texture_unit(0);

varying highp vec2 vTexCoords;

void vertex() {
	vTexCoords = aPosition;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	if (vTexCoords.x < 0.5) {
		COLOR = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	COLOR = textureLod(uTexture, vTexCoords, 0.0);
}
