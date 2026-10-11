// Console rebuild of the underwater effect out of geometry, shared by every void fixed_function block of
// CombineWithWater.shader and CombineWithWaterLow.shader.
//
// The lightmap half of the composite stays in the backend stage: converting the compositor's
// half-resolution float lightmap into a factor texture is a per-texel loop over the whole map every
// frame, and its store format (ARGB4444 in video memory, tiled RGBA8, IA16, an attenuation-only I8)
// is backend business. See LightingCombine.h for the one part of it that is not.
//
// The water half is not: every number in it comes from the fragment stages, and keeping it here is what
// stops the two from drifting - the tint and the 0.4 threshold used to be transcribed into all six
// console backends by hand. The scene was rasterized straight into the screen buffer on this tier, so
// there is no texture of it to displace, to split into colour channels or to reflect, and everything
// left is what can be drawn OVER it with per-vertex colours:
// - the tint, whose top edge is the GLSL's wave() - the same four sines, as a parabola over the
//   range-reduced phase like sinCycles(), so no libm sin() is called on consoles where it costs
//   microseconds - with a bright line along the surface and a glow that fades with depth. Both mix
//   toward white instead of adding, exactly like the software backend's per-row water (see
//   SwDevice.cpp), so a vertex's colour and alpha stay one blend;
// - the light rays of the High variant, as slanted shafts anchored to the world that sway and flicker,
//   fading in below the surface and out where the GLSL's max(1.0 - uvLocal.y * 1.4, 0.0) does - linear
//   in the row, so the rasterizer's interpolation reproduces it exactly;
// - the extra darkness above deep water, whose lower edge follows the same wave.
//
// Only a thin band on either side of the surface - as deep as the wave is high - follows the wave; the
// rest is full-width quads. Tall, narrow triangles are what a span-walking rasterizer pays for per row:
// on the RDP, a band of 28 columns reaching a third of the way down the view cost 2 ms a frame.
//
// A block may define WATER_STRIP_SEGMENTS and WATER_STRIPS before including this file: each wavy band
// goes out as WATER_STRIPS strips of WATER_STRIP_SEGMENTS quads, and a strip holds
// 2 * (WATER_STRIP_SEGMENTS + 1) vertices, which has to fit the strip builder. WATER_LITE is for a backend
// that pays for every triangle: it leaves out the light rays, draws everything below the wavy band as one
// gradient quad, and keeps only the wave's long 20-cycle term (at the height of the two big ones together),
// because the short terms would alias into a jagged edge at the few columns such a backend can afford.

#ifndef WATER_STRIP_SEGMENTS
#define WATER_STRIP_SEGMENTS 7
#endif
#ifndef WATER_STRIPS
#define WATER_STRIPS 4
#endif

pipeline lighting_combine;

if (has_uniform(uWaterLevel)) {
	// The Combine draw's quad IS the viewport, so the sprite axes are its rectangle in raster space:
	// the origin is the top-left corner and axis_y runs down to the bottom edge, which is the same
	// direction uWaterLevel measures in (0 at the top of the view, 1 at the bottom).
	vec2 origin = quad_origin();
	float viewW = quad_axis_x().x;
	float viewH = quad_axis_y().y;
	float leftX = origin.x;
	float rightX = origin.x + viewW;
	float topY = origin.y;
	float bottomY = origin.y + viewH;
	float invViewH = 1.0 / viewH;
	float level = uniform_float(uWaterLevel);
	float waterY = topY + level * viewH;
	float time = uniform_float(uTime);
	vec2 camera = uniform_vec2(uCameraPos);
	vec4 ambient = uniform_vec4(uAmbientColor);

	pass p;
	p.blend = ALPHA;

	// wave() in cycles: its four phases at the left edge (wrapped, so they stay precise however long the
	// level runs and wherever the camera is) and their advance across the view, the cosines turned into
	// sines a quarter cycle on; and its amplitudes, 0.4 * (0.004, 0.008, 0.01, 0.001), in rows of this view.
	// The surface never leaves waterY +- waveMax.
	vec4 waveScale = vec4(60.0, 20.0, 35.0, 70.0) * 0.15915494;
	float worldX = camera.x / viewW;
	vec4 waveBase = vec4((worldX - time) * waveScale.x + 0.25, (worldX - 2.0 * time) * waveScale.y + 0.25,
		(worldX + 2.0 * time) * waveScale.z, (worldX + 4.0 * time) * waveScale.w + 0.25);
	waveBase = waveBase - floor(waveBase);
#ifdef WATER_LITE
	vec4 waveAmp = vec4(0.0, 0.0072, 0.0, 0.0) * viewH;
	float waveMax = 0.0072 * viewH;
#else
	vec4 waveAmp = vec4(0.0016, 0.0032, 0.004, 0.0004) * viewH;
	float waveMax = 0.0092 * viewH;
#endif
	float segmentCount = float(WATER_STRIPS * WATER_STRIP_SEGMENTS);

	// The wavy bands are skipped once the surface is entirely outside the view
	bool wavy = (waterY + waveMax > topY && waterY - waveMax < bottomY);

	// Above deep water: the GLSL adds (0.4 - uWaterLevel) of extra darkness toward the ambient colour once
	// the waterline is in the top 40% of the view, so the world above deep water dims - a flat quad down to
	// the crests, then a wavy band down to the surface the tint starts at. WATER_LITE ends the quad at the
	// undisturbed waterline instead: it is drawn before the tint, so where a crest rises above that line
	// the surface's bright edge goes over the darkness, and the troughs leave a sliver of a row or two.
	if (level < 0.4 && level > 0.0) {
		vec4 above = vec4(ambient.r, ambient.g, ambient.b, 0.4 - level);
#ifdef WATER_LITE
		float crestY = clamp(waterY, topY, bottomY);
#else
		float crestY = clamp(waterY - waveMax, topY, bottomY);
#endif
		strip_position(0, vec2(leftX, topY));
		strip_position(1, vec2(rightX, topY));
		strip_position(2, vec2(leftX, crestY));
		strip_position(3, vec2(rightX, crestY));
		strip_color(0, above);
		strip_color(1, above);
		strip_color(2, above);
		strip_color(3, above);
		submit_strip_shaded(p, 4);
#ifndef WATER_LITE
		if (wavy) {
			for (int s = 0; s < WATER_STRIPS; s++) {
				for (int j = 0; j <= WATER_STRIP_SEGMENTS; j++) {
					float u = float(s * WATER_STRIP_SEGMENTS + j) / segmentCount;
					vec4 ph = waveBase + waveScale * u + 0.5;
					ph = (ph - floor(ph)) * 2.0 - 1.0;
					vec4 sy = 4.0 * ph * (1.0 - vec4(abs(ph.x), abs(ph.y), abs(ph.z), abs(ph.w)));
					sy = sy * (0.775 + 0.225 * vec4(abs(sy.x), abs(sy.y), abs(sy.z), abs(sy.w))) * waveAmp;
					float x = leftX + u * viewW;
					strip_position(2 * j, vec2(x, crestY));
					strip_position(2 * j + 1, vec2(x, clamp(waterY + sy.x + sy.y + sy.z + sy.w, topY, bottomY)));
					strip_color(2 * j, above);
					strip_color(2 * j + 1, above);
				}
				submit_strip_shaded(p, 2 * WATER_STRIP_SEGMENTS + 2);
			}
		}
#endif
	}

	if (level < 1.0) {
		// Underwater: mix(main, (0.4, 0.6, 0.8), 0.4) plus the glow g toward white - out = main * 0.6 * (1 - g) +
		// (waterColor * 0.4 * (1 - g) + g), which as one blend is alpha 0.4 + 0.6 * g and a colour that is a
		// convex combination of the water colour and white. g is half of the Low variant's
		// 0.2 * topGradient^2 (the light rays carry the rest) plus the line at the surface, which fades over
		// the wavy band - thinner in the troughs, wider on the crests. Each flat row is clamped into the view
		// and its glow taken at its clamped depth, so a band cut off by the top edge keeps the colour the edge
		// actually has there; the surface row takes the glow of the undisturbed waterline, and a few rows of
		// wave make no visible difference to it.
		float lineY = waterY + waveMax + max(viewH * 0.008, 1.5);
		float y0 = clamp(waterY, topY, bottomY);
		float y1 = clamp(lineY, topY, bottomY);
		float y2 = clamp(waterY + viewH * (1.0 / 3.0), topY, bottomY);
		float y3 = clamp(waterY + viewH * (2.0 / 3.0), topY, bottomY);
		float t0 = max(1.0 - (y0 - waterY) * invViewH, 0.0);
		float t1 = max(1.0 - (y1 - waterY) * invViewH, 0.0);
		float t2 = max(1.0 - (y2 - waterY) * invViewH, 0.0);
		float t3 = max(1.0 - (y3 - waterY) * invViewH, 0.0);
		float t4 = max(1.0 - (bottomY - waterY) * invViewH, 0.0);
		float g0 = 0.1 * t0 * t0 + (waterY > topY ? 0.3 : 0.0);
		float g1 = 0.1 * t1 * t1;
		float g2 = 0.1 * t2 * t2;
		float g3 = 0.1 * t3 * t3;
		float g4 = 0.1 * t4 * t4;
		float a0 = 0.4 + 0.6 * g0;
		float a1 = 0.4 + 0.6 * g1;
		float a2 = 0.4 + 0.6 * g2;
		float a3 = 0.4 + 0.6 * g3;
		float a4 = 0.4 + 0.6 * g4;
		vec4 c0 = vec4((vec3(0.4, 0.6, 0.8) * (0.4 * (1.0 - g0)) + g0) * (1.0 / a0), a0);
		vec4 c1 = vec4((vec3(0.4, 0.6, 0.8) * (0.4 * (1.0 - g1)) + g1) * (1.0 / a1), a1);
		vec4 c2 = vec4((vec3(0.4, 0.6, 0.8) * (0.4 * (1.0 - g2)) + g2) * (1.0 / a2), a2);
		vec4 c3 = vec4((vec3(0.4, 0.6, 0.8) * (0.4 * (1.0 - g3)) + g3) * (1.0 / a3), a3);
		vec4 c4 = vec4((vec3(0.4, 0.6, 0.8) * (0.4 * (1.0 - g4)) + g4) * (1.0 / a4), a4);

		if (wavy) {
			for (int s = 0; s < WATER_STRIPS; s++) {
				for (int j = 0; j <= WATER_STRIP_SEGMENTS; j++) {
					float u = float(s * WATER_STRIP_SEGMENTS + j) / segmentCount;
					vec4 ph = waveBase + waveScale * u + 0.5;
					ph = (ph - floor(ph)) * 2.0 - 1.0;
					vec4 sy = 4.0 * ph * (1.0 - vec4(abs(ph.x), abs(ph.y), abs(ph.z), abs(ph.w)));
					sy = sy * (0.775 + 0.225 * vec4(abs(sy.x), abs(sy.y), abs(sy.z), abs(sy.w))) * waveAmp;
					float x = leftX + u * viewW;
					strip_position(2 * j, vec2(x, clamp(waterY + sy.x + sy.y + sy.z + sy.w, topY, bottomY)));
					strip_position(2 * j + 1, vec2(x, y1));
					strip_color(2 * j, c0);
					strip_color(2 * j + 1, c1);
				}
				submit_strip_shaded(p, 2 * WATER_STRIP_SEGMENTS + 2);
			}
		}

		// The rest of the view below the wavy band, or all of it when the surface is above the top edge (the
		// first rows then collapse into it), down to the bottom edge
#ifdef WATER_LITE
		strip_position(0, vec2(leftX, y1));
		strip_position(1, vec2(rightX, y1));
		strip_position(2, vec2(leftX, bottomY));
		strip_position(3, vec2(rightX, bottomY));
		strip_color(0, c1);
		strip_color(1, c1);
		strip_color(2, c4);
		strip_color(3, c4);
		submit_strip_shaded(p, 4);
#else
		strip_position(0, vec2(leftX, y1));
		strip_position(1, vec2(rightX, y1));
		strip_position(2, vec2(leftX, y2));
		strip_position(3, vec2(rightX, y2));
		strip_position(4, vec2(leftX, y3));
		strip_position(5, vec2(rightX, y3));
		strip_position(6, vec2(leftX, bottomY));
		strip_position(7, vec2(rightX, bottomY));
		strip_color(0, c1);
		strip_color(1, c1);
		strip_color(2, c2);
		strip_color(3, c2);
		strip_color(4, c3);
		strip_color(5, c3);
		strip_color(6, c4);
		strip_color(7, c4);
		submit_strip_shaded(p, 8);
#endif

		// Light rays: the GLSL's simplex noise is ~70 rows wide across the view and leans 0.62 columns left
		// per row down; here it is a shaft every 0.3 view widths, each with its own width, sway and flicker
		// from a golden-ratio hash of its index, mixed toward white like the glow. A shaft is two bands of
		// three columns (transparent edges, the peak in the middle): fading in below the surface, then out
		// toward the GLSL's cut-off row. They are faded by the ambient level, since this tier lays them over
		// the lighting instead of under it.
		float rayEnd = topY + viewH * (1.0 / 1.4);
		float fadeIn = viewH * 0.06;
		float yA = max(waterY, topY);
		float yB = max(waterY + fadeIn, topY);
		float rayStrength = 0.25 * clamp(ambient.w, 0.0, 1.0);
#ifndef WATER_LITE
		if (yB < rayEnd && rayStrength > 0.01) {
			float spacing = viewW * 0.3;
			float rayPos = (camera.x - 0.5 * viewW) / spacing;
			float firstRay = floor(rayPos) - 1.0;
			float lean = -0.62;
			float envelopeB = max(1.0 - 1.4 * (yB - topY) * invViewH, 0.0);
			// Already faded in when the surface is far enough above the top edge
			float envelopeA = max(1.0 - 1.4 * (yA - topY) * invViewH, 0.0) * clamp((yA - waterY) / fadeIn, 0.0, 1.0);
			for (int i = 0; i < 8; i++) {
				float k = firstRay + float(i);
				float h1 = k * 0.618034;
				h1 = h1 - floor(h1);
				float h2 = k * 0.414214 + 0.5;
				h2 = h2 - floor(h2);

				// sinCycles() of the sway and of the flicker
				float sw = time * (0.8 + 0.5 * h2) + h1 + 0.5;
				sw = (sw - floor(sw)) * 2.0 - 1.0;
				sw = 4.0 * sw * (1.0 - abs(sw));
				sw = sw * (0.775 + 0.225 * abs(sw));
				float fl = time * (1.6 + 1.2 * h1) + h2 + 0.5;
				fl = (fl - floor(fl)) * 2.0 - 1.0;
				fl = 4.0 * fl * (1.0 - abs(fl));
				fl = fl * (0.775 + 0.225 * abs(fl));

				float centerX = leftX + (k - rayPos) * spacing + 0.5 * h2 * spacing + 0.12 * spacing * sw;
				float halfW = spacing * (0.1 + 0.12 * h1);
				float spread = 0.6 * halfW * invViewH;
				float intensity = rayStrength * (0.6 + 0.4 * fl);
				float xA = centerX + lean * (yA - topY);
				float xB = centerX + lean * (yB - topY);
				float xC = centerX + lean * (rayEnd - topY);
				float wA = halfW + spread * (yA - topY);
				float wB = halfW + spread * (yB - topY);
				float wC = halfW + spread * (rayEnd - topY);
				// Shafts that miss the view entirely, at their top and at their (leaned) bottom, are skipped
				bool missesRight = (xA - wA > rightX && xC - wC > rightX);
				bool missesLeft = (xA + wA < leftX && xC + wC < leftX);
				if (!missesRight && !missesLeft) {
					vec4 edge = vec4(1.0, 1.0, 1.0, 0.0);
					if (yB > yA) {
						strip_position(0, vec2(xA - wA, yA));
						strip_position(1, vec2(xB - wB, yB));
						strip_position(2, vec2(xA, yA));
						strip_position(3, vec2(xB, yB));
						strip_position(4, vec2(xA + wA, yA));
						strip_position(5, vec2(xB + wB, yB));
						strip_color(0, edge);
						strip_color(1, edge);
						strip_color(2, vec4(1.0, 1.0, 1.0, intensity * envelopeA));
						strip_color(3, vec4(1.0, 1.0, 1.0, intensity * envelopeB));
						strip_color(4, edge);
						strip_color(5, edge);
						submit_strip_shaded(p, 6);
					}
					strip_position(0, vec2(xB - wB, yB));
					strip_position(1, vec2(xC - wC, rayEnd));
					strip_position(2, vec2(xB, yB));
					strip_position(3, vec2(xC, rayEnd));
					strip_position(4, vec2(xB + wB, yB));
					strip_position(5, vec2(xC + wC, rayEnd));
					strip_color(0, edge);
					strip_color(1, edge);
					strip_color(2, vec4(1.0, 1.0, 1.0, intensity * envelopeB));
					strip_color(3, edge);
					strip_color(4, edge);
					strip_color(5, edge);
					submit_strip_shaded(p, 6);
				}
			}
		}
#endif
	}
}
