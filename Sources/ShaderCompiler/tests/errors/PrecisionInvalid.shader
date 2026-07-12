program PrecisionBad;
precision lowp;

void vertex() {
	gl_Position = vec4(0.0);
}

void fragment() {
	COLOR = vec4(1.0);
}
