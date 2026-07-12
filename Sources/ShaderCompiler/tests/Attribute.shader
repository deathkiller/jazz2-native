program Attribute;

attribute vec2 aPosition;
attribute layout(location = 3) vec4 aColor;

uniform mat4 uProjectionMatrix;

varying vec4 vColor;

void vertex() {
	vColor = aColor;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	COLOR = vColor;
}
