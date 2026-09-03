program BackendConditionals;
variant DITHER;

uniform mat4 uProjectionMatrix;

attribute layout(location = 0) vec2 aPosition;

varying vec2 vTexCoords;

uniform sampler2D uTexture : texture_unit(0);
uniform vec4 uTint;

void vertex() {
	vTexCoords = aPosition;
	gl_Position = uProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}

void fragment() {
	vec4 c = texture(uTexture, vTexCoords);

	// Both backend macros in one expression -- the form that replaces a nest of two "#ifndef"s
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING
	if (uTint.w > 0.0) {
		c.rgb = mix(c.rgb, uTint.rgb, uTint.w);
	}
#endif

	// Mixed with a variant define: only the backend macro folds away, and what is left is lowered to
	// "#ifdef DITHER" for the GLSL compiler (the whole block still disappears from the software
	// transpile). The lowering is what makes the bare "DITHER" safe to write here - left as-is it
	// would compile on desktop and break every ES2/ES3/WebGL target
#if DITHER && !SOFTWARE_RENDERER
	c.rgb += vec3(0.01);
#endif

	// Two flags in one expression cannot collapse to "#ifdef", so each is wrapped instead
#if DITHER && MISSING_FLAG
	c.rgb += vec3(0.02);
#endif

	// The "defined(...)" spelling, with an "#else" side
#if defined(SOFTWARE_RENDERER)
	c.rgb = clamp(c.rgb, 0.0, 1.0);
#else
	c.rgb = c.rgb * c.rgb;
#endif

	// A three-way chain -- one directive where a nest of two-way conditionals used to be needed. Every
	// branch condition is an owned macro, so the chain resolves away COMPLETELY for each of the three
	// emissions: only the selected branch's body survives and none of the directives do
#if SOFTWARE_RENDERER
	c.rgb *= 0.5;
#elif LOW_POWER_GPU
	c.rgb *= 0.75;
#else
	c.rgb *= mix(vec3(0.5), vec3(1.0), uTint.w);
#endif

	// A chain that SETTLES before reaching a condition this pass cannot determine: for the software
	// emission the first branch is taken, so "#elif MISSING_FLAG" is dead whatever MISSING_FLAG turns
	// out to be and the chain still resolves away. For every other emission the first branch is false
	// and the outcome does depend on MISSING_FLAG, so there the chain survives for the GLSL compiler --
	// with the opener rewritten to "#if 0", since the macro itself must not reach an emitted source
#if SOFTWARE_RENDERER
	c.rgb += vec3(0.03);
#elif MISSING_FLAG
	c.rgb += vec3(0.04);
#else
	c.rgb += vec3(0.05);
#endif

	COLOR = c;
}
