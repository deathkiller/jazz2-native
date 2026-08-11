program StageDefine;

// The stage macros are resolved at compile time and never defined in an emitted source, so
// defining one (or undefining it) is an error -- unlike using it in an #if expression, which is
// resolved like the #ifdef form
#define FRAGMENT_STAGE 1

void vertex() {
	gl_Position = vec4(0.0);
}

void fragment() {
	COLOR = vec4(1.0);
}
