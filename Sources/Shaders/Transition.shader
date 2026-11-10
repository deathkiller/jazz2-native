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
	// The iris built from geometry: a 32-segment ring for the soft edge (alpha from vertex colors),
	// then solid black out past the corners
	// Centre and extent come from the sprite axes, so scissor clipping can't distort the circle
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
	// Nothing to darken once the clear circle covers the screen
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

void fixed_function(gx, pica, gu, gs, rdp, legacygl) {
	// Same iris with 64 segments and the soft edge split into 3 bands, so the vertex colors follow ease()
	// Each segment is one 10-vertex strip through all five radii (the PVR's strip is too short for that)
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
	// Nothing to darken once the clear circle covers the screen
	if (outer < corner) {
		// ease() at the band boundaries 1/3 and 2/3
		float ease1 = 2.0 * (1.0 / 3.0) * (1.0 / 3.0);
		float ease2 = 1.0 - 2.0 * (1.0 / 3.0) * (1.0 / 3.0);
		// Not named "step" to avoid the GLSL builtin
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
			// Clear at the inner radius, eased across the edge, solid black outside
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
