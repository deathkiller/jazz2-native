program FragColorBad;

void vertex() {
	gl_Position = vec4(0.0);
}

void fragment() {
	fragColor = vec4(1.0);
	return;
}
