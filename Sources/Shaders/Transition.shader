program Transition;

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
};

varying vec2 vTexCoords;
varying vec2 vCorrection;
varying float vProgressTime;

void vertex() {
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vCorrection = spriteSize / vec2(max(spriteSize.x, spriteSize.y));
	vProgressTime = color.a;
}

float rand(vec2 xy) {
	return fract(sin(dot(xy.xy, vec2(12.9898,78.233))) * 43758.5453);
}

float ease(float time) {
	time *= 2.0;
	if (time < 1.0)  {
		return 0.5 * time * time;
	}

	time -= 1.0;
	return -0.5 * (time * (time - 2.0) - 1.0);
}

void fragment() {
	vec2 uv = (vTexCoords - vec2(0.5)) * vCorrection;
	float distance = length(uv);

	float progressInner = vProgressTime - 0.22;
	distance = (clamp(distance, progressInner, vProgressTime) - progressInner) / (vProgressTime - progressInner);

	float mixValue = ease(distance);
	float noise = 1.0 + rand(uv) * 0.1;
	COLOR = vec4(0.0, 0.0, 0.0, mixValue * noise);
}

void fixed_function(pvr) {
	// The GLSL effect is a circular iris: black everywhere outside a circle of radius `progress`,
	// clear inside it, with a soft 0.22-wide edge in between (the iso-distance curves above are
	// circles of radius progress * max(spriteSize) about the sprite centre).
	//
	// Without a fragment shader the same shape is built out of geometry instead: a fan of 32
	// segments approximating the soft edge, with the alpha interpolated across it by the vertex
	// colours, then a second fan filling everything from there out past the corners. The centre and
	// extent come from the transformed sprite axes rather than the corner array, so scissor
	// clipping of the quad cannot distort the circle.
	vec2 axisX = quad_axis_x();
	vec2 axisY = quad_axis_y();
	vec2 centre = quad_origin() + 0.5 * (axisX + axisY);
	float extentX = abs(axisX.x) + abs(axisY.x);
	float extentY = abs(axisX.y) + abs(axisY.y);
	float radiusScale = max(extentX, extentY);
	// Far enough that the polygon's flat edges still cover the corners
	float corner = 0.5 * sqrt(extentX * extentX + extentY * extentY) * 1.05;

	float progress = COLOR.a;
	float outer = progress * radiusScale;
	float inner = max((progress - 0.22) * radiusScale, 0.0);
	// Once the iris has swallowed the whole screen there is nothing left to darken
	if (outer < corner) {
		pass p;
		for (int i = 0; i < 32; i++) {
			float a0 = float(i) * (6.28318530718 / 32.0);
			float a1 = float(i + 1) * (6.28318530718 / 32.0);
			vec2 dir0 = vec2(cos(a0), sin(a0));
			vec2 dir1 = vec2(cos(a1), sin(a1));

			// Soft edge: transparent at the inner radius, fully black at the outer one
			if (outer > inner) {
				strip_position(0, centre + inner * dir0);
				strip_position(1, centre + outer * dir0);
				strip_position(2, centre + inner * dir1);
				strip_position(3, centre + outer * dir1);
				strip_color(0, vec4(0.0, 0.0, 0.0, 0.0));
				strip_color(1, vec4(0.0, 0.0, 0.0, 1.0));
				strip_color(2, vec4(0.0, 0.0, 0.0, 0.0));
				strip_color(3, vec4(0.0, 0.0, 0.0, 1.0));
				submit_strip_shaded(p, 4);
			}
			// Solid black from the edge out past the corners
			strip_position(0, centre + outer * dir0);
			strip_position(1, centre + corner * dir0);
			strip_position(2, centre + outer * dir1);
			strip_position(3, centre + corner * dir1);
			strip_color(0, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(1, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(2, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(3, vec4(0.0, 0.0, 0.0, 1.0));
			submit_strip_shaded(p, 4);
		}
	}
}

void fixed_function(gx, psp) {
	// The same iris the PVR block above synthesizes, at the detail the GP and the GE can afford: 64
	// angular segments instead of 32 (a segment's flat chord then misses the true circle by under half a
	// pixel at 640x480, where 32 segments were off by about one and a half), and the soft edge is
	// split into THREE radial bands instead of one so the vertex colours trace the GLSL's `ease()`
	// curve piecewise-linearly rather than as a straight ramp - ease is an S-curve, and a single
	// linear band is off by 0.125 in alpha at its quarter points.
	//
	// Each angular segment is one 10-vertex triangle strip: the vertex pairs walk outward through
	// the five radii (inner, two soft-edge steps, outer, past the corner), which the strip turns
	// into the four quads between them. Both strip scratches hold 16 vertices for exactly this.
	//
	// The iris is pure geometry plus per-vertex colours, so nothing about it depends on a combiner -
	// which is why the two consoles that share nothing else here share this: what the block needs from
	// the hardware is only a long strip, and both take one in a single draw call (on the GE every strip
	// costs a draw call of its own, so the 10-vertex form that walks all five radii is one draw per
	// segment where the PVR's 8-vertex scratch needs two). The PVR keeps its own block above purely
	// because of that scratch limit.
	vec2 axisX = quad_axis_x();
	vec2 axisY = quad_axis_y();
	vec2 centre = quad_origin() + 0.5 * (axisX + axisY);
	float extentX = abs(axisX.x) + abs(axisY.x);
	float extentY = abs(axisX.y) + abs(axisY.y);
	float radiusScale = max(extentX, extentY);
	// Far enough that the polygon's flat edges still cover the corners
	float corner = 0.5 * sqrt(extentX * extentX + extentY * extentY) * 1.05;

	float progress = COLOR.a;
	float outer = progress * radiusScale;
	float inner = max((progress - 0.22) * radiusScale, 0.0);
	// Once the iris has swallowed the whole screen there is nothing left to darken
	if (outer < corner) {
		// ease(t) = 2t^2 for t < 0.5, 1 - 2(1-t)^2 above it (the GLSL's quadratic ease-in-out),
		// sampled at the two interior band boundaries
		float ease1 = 2.0 * (1.0 / 3.0) * (1.0 / 3.0);
		float ease2 = 1.0 - 2.0 * (1.0 / 3.0) * (1.0 / 3.0);
		// Named away from GLSL's step() so a future runtime builtin of that name cannot shadow it
		float edgeStep = (outer - inner) * (1.0 / 3.0);
		float r1 = inner + edgeStep;
		float r2 = inner + 2.0 * edgeStep;

		pass p;
		for (int i = 0; i < 64; i++) {
			float a0 = float(i) * (6.28318530718 / 64.0);
			float a1 = float(i + 1) * (6.28318530718 / 64.0);
			vec2 dir0 = vec2(cos(a0), sin(a0));
			vec2 dir1 = vec2(cos(a1), sin(a1));

			strip_position(0, centre + inner * dir0);
			strip_position(1, centre + inner * dir1);
			strip_position(2, centre + r1 * dir0);
			strip_position(3, centre + r1 * dir1);
			strip_position(4, centre + r2 * dir0);
			strip_position(5, centre + r2 * dir1);
			strip_position(6, centre + outer * dir0);
			strip_position(7, centre + outer * dir1);
			strip_position(8, centre + corner * dir0);
			strip_position(9, centre + corner * dir1);
			// Transparent at the inner radius, eased across the soft edge, then solid black from the
			// outer radius out past the corners
			strip_color(0, vec4(0.0, 0.0, 0.0, 0.0));
			strip_color(1, vec4(0.0, 0.0, 0.0, 0.0));
			strip_color(2, vec4(0.0, 0.0, 0.0, ease1));
			strip_color(3, vec4(0.0, 0.0, 0.0, ease1));
			strip_color(4, vec4(0.0, 0.0, 0.0, ease2));
			strip_color(5, vec4(0.0, 0.0, 0.0, ease2));
			strip_color(6, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(7, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(8, vec4(0.0, 0.0, 0.0, 1.0));
			strip_color(9, vec4(0.0, 0.0, 0.0, 1.0));
			submit_strip_shaded(p, 10);
		}
	}
}
