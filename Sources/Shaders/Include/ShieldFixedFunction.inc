// Console rebuild of the shield sphere out of geometry, shared by the void fixed_function blocks of
// Include/ShieldFireFs.inc and Include/ShieldLightningFs.inc (SHIELD_LIGHTNING picks the latter's look).
//
// The instance texRect of a shield does not map the quad onto its texture - it carries the animated
// shifts of the fragment stage's noise lookups - so the plain sprite quad of this tier would only show a
// box of stretched noise. What is drawn instead is a disc of concentric rings with per-vertex colours,
// laid over the quad's own axes (an ellipse where the quad is not square, exactly like vPos), ending at
// 0.96, where the GLSL's isNearBorder cuts the sphere off. The lightning shield's thin white rim
// (isVeryNearBorder) is a ring of its own.
//
// With SHIELD_NOISE the rings are only a faint base, and the flames are the noise texture itself, drawn
// over every ring once per noise lookup of the fragment stage - at the same coordinates, shift + q * scale
// with q the sphere mapping the GLSL computes per pixel, here per vertex. The streaks of the GLSL are a
// band of the noise around a threshold, which no texture environment can cut out of a texel, so the band
// is baked into the ALPHA of the noise textures (the fragment stage reads only their red channel) at a
// fixed threshold, and each lookup is the silhouette of that alpha: an orange (or green) pass, then a white
// one at about half of it for the hot core. The flat colour of a textured strip cannot fade toward the
// edge like the GLSL's facing term b does, so the fade steps from ring to ring instead. The texture has
// to repeat (Player::DrawShield sets that), as the coordinates run over several tiles.
//
// Without SHIELD_NOISE the rings carry the look alone: each boundary takes roughly the AVERAGE of the
// GLSL's output at its radius - alpha 0.1 + 1.3 * mask * b with the sphere's facing term b = sqrt(1 - r^2),
// the colour lifted toward white where the mask peaks - and the noise streaks become a flicker of the
// middle boundaries around the circle, driven by the same texRect shifts so it flows with them.
//
// A block may define SHIELD_STRIP_SEGMENTS and SHIELD_STRIPS before including this file: each ring goes
// out as SHIELD_STRIPS strips of SHIELD_STRIP_SEGMENTS quads, and a strip holds
// 2 * (SHIELD_STRIP_SEGMENTS + 1) vertices, which has to fit the strip builder. SHIELD_LITE is for a
// backend that pays for every triangle: a single ring from the centre to the edge, flickering at the edge.

#ifndef SHIELD_STRIP_SEGMENTS
#define SHIELD_STRIP_SEGMENTS 7
#endif
#ifndef SHIELD_STRIPS
#define SHIELD_STRIPS 3
#endif

#ifdef SHIELD_LITE
#define SHIELD_RINGS 1
#else
#ifdef SHIELD_LIGHTNING
#define SHIELD_RINGS 4
#else
#define SHIELD_RINGS 3
#endif
#endif

// The shaded base, then one layer per noise lookup: the fire shield has two of them, the lightning one
#ifdef SHIELD_NOISE
#ifdef SHIELD_LIGHTNING
#define SHIELD_LAYERS 2
#else
#define SHIELD_LAYERS 3
#endif
#else
#define SHIELD_LAYERS 1
#endif

// Instance color = (scaleX, scaleY, darkness, alpha) - the scales size the noise lookups
float darkness = COLOR.b;
float alpha = COLOR.a;
#ifdef SHIELD_LIGHTNING
vec3 baseColor = vec3(0.1, 1.0, 0.0);
#else
vec3 baseColor = vec3(1.0, 0.3, 0.0);
#endif

vec2 halfX = 0.5 * quad_axis_x();
vec2 halfY = 0.5 * quad_axis_y();
vec2 centre = quad_origin() + halfX + halfY;
vec4 shift = quad_tex_rect();

#ifdef SHIELD_NOISE
// Both lookups' shifts without their whole tiles - the texture repeats, and they grow without bound with
// the elapsed frames - and two more tiles on V, which keeps it positive (q.y runs from -1 to 0)
vec2 noiseScale = vec2(COLOR.r, COLOR.g);
vec4 noiseShift = shift - floor(shift) + vec4(0.0, 2.0, 0.0, 2.0);
#ifdef SHIELD_LIGHTNING
vec3 glowColor = vec3(0.2, 1.0, 0.1) * darkness;
float coreAlpha = 0.43;
#else
vec3 glowColor = vec3(1.0, 0.3, 0.0) * darkness;
float coreAlpha = 0.55;
#endif
#endif

// The ring vertices walk around the circle by rotating one direction, so no sin()/cos() is called per vertex
float segmentCount = float(SHIELD_STRIPS * SHIELD_STRIP_SEGMENTS);
float stepCos = cos(6.28318530718 / segmentCount);
float stepSin = sin(6.28318530718 / segmentCount);

pass p;
for (int ring = 0; ring < SHIELD_RINGS; ring++) {
	vec2 stripDir = vec2(1.0, 0.0);
	vec2 dir = stripDir;
	for (int s = 0; s < SHIELD_STRIPS; s++) {
		for (int layer = 0; layer < SHIELD_LAYERS; layer++) {
			dir = stripDir;
			for (int j = 0; j <= SHIELD_STRIP_SEGMENTS; j++) {
#ifndef SHIELD_NOISE
				// Two waves per flicker, in whole cycles around the circle so the last vertex meets the first,
				// as a parabola over the range-reduced phase (see CombineWaterFixedFunction.inc)
				float u = float(s * SHIELD_STRIP_SEGMENTS + j) / segmentCount;
				vec4 ph = vec4(u * 2.0 + shift.x * 2.0, u * 3.0 - shift.y * 2.0, u * 3.0 + shift.z, u * 2.0 - shift.w * 2.0);
				ph = (ph - floor(ph)) * 2.0 - 1.0;
				vec4 wave = 4.0 * ph * (1.0 - vec4(abs(ph.x), abs(ph.y), abs(ph.z), abs(ph.w)));
				float flickerInner = clamp(0.5 + 0.3 * (wave.x + wave.y), 0.0, 1.0);
				float flickerOuter = clamp(0.5 + 0.3 * (wave.z + wave.w), 0.0, 1.0);
#endif

				// Even vertices on the inner boundary of the ring, odd ones on its outer boundary
				for (int e = 0; e < 2; e++) {
					int boundary = ring + e;
					float radius = 0.0;
					float white = 0.0;
					float a = 0.0;
#ifdef SHIELD_LITE
#ifdef SHIELD_LIGHTNING
					if (boundary == 0) {
						white = 0.1;
						a = 0.22;
					} else {
						radius = 0.96;
						white = 0.6 + 0.4 * flickerInner;
						a = 0.25 + 0.2 * flickerOuter;
					}
#else
					if (boundary == 0) {
						white = 0.15;
						a = 0.38;
					} else {
						radius = 0.96;
						white = 0.3 * flickerInner;
						a = 0.15 + 0.15 * flickerOuter;
					}
#endif
#else
#ifdef SHIELD_LIGHTNING
#ifdef SHIELD_NOISE
					if (boundary == 0) {
						white = 0.1;
						a = 0.1;
					} else if (boundary == 1) {
						radius = 0.5;
						white = 0.1;
						a = 0.1;
					} else if (boundary == 2) {
						radius = 0.88;
						white = 0.15;
						a = 0.11;
					} else if (boundary == 3) {
						radius = 0.935;
						white = 1.0;
						a = 0.54;
					} else {
						radius = 0.96;
						white = 1.0;
						a = 0.35;
					}
#else
					if (boundary == 0) {
						white = 0.1;
						a = 0.24;
					} else if (boundary == 1) {
						radius = 0.5;
						white = 0.5 * flickerInner;
						a = 0.16 + 0.18 * flickerInner;
					} else if (boundary == 2) {
						radius = 0.88;
						white = 0.4 * flickerOuter;
						a = 0.13 + 0.1 * flickerOuter;
					} else if (boundary == 3) {
						radius = 0.935;
						white = 1.0;
						a = 0.54;
					} else {
						radius = 0.96;
						white = 1.0;
						a = 0.35;
					}
#endif
#else
#ifdef SHIELD_NOISE
					if (boundary == 0) {
						a = 0.14;
					} else if (boundary == 1) {
						radius = 0.5;
						a = 0.13;
					} else if (boundary == 2) {
						radius = 0.85;
						a = 0.11;
					} else {
						radius = 0.96;
						a = 0.09;
					}
#else
					if (boundary == 0) {
						white = 0.15;
						a = 0.38;
					} else if (boundary == 1) {
						radius = 0.5;
						white = 0.4 * flickerInner;
						a = 0.28 + 0.25 * flickerInner;
					} else if (boundary == 2) {
						radius = 0.85;
						white = 0.3 * flickerOuter;
						a = 0.2 + 0.16 * flickerOuter;
					} else {
						radius = 0.96;
						a = 0.17;
					}
#endif
#endif
#endif
					vec2 local = radius * dir;
					strip_position(2 * j + e, centre + local.x * halfX + local.y * halfY);
#ifdef SHIELD_NOISE
					if (layer == 0) {
						strip_color(2 * j + e, vec4(mix(baseColor, vec3(1.0, 1.0, 1.0), white) * darkness, a * alpha));
					} else {
						// q of the GLSL - (0.5 - 0.5 * atan(n.z, n.x) / PI, -acos(vPos.y) / PI) - on the unit sphere,
						// where atan(z, x) with z >= 0 is acos(x / sqrt(x^2 + z^2)) and x^2 + z^2 = 1 - y^2; acos as
						// the usual polynomial over sqrt(1 - |t|), mirrored for negative t (error below 1e-4)
						float ty = abs(local.y);
						float acosY = sqrt(1.0 - ty) * (1.5707288 + ty * (-0.2121144 + ty * (0.074261 - 0.0187293 * ty)));
						if (local.y < 0.0) {
							acosY = 3.14159265 - acosY;
						}
						float t = clamp(local.x / sqrt(max(1.0 - local.y * local.y, 0.0001)), -1.0, 1.0);
						float tx = abs(t);
						float acosX = sqrt(1.0 - tx) * (1.5707288 + tx * (-0.2121144 + tx * (0.074261 - 0.0187293 * tx)));
						if (t < 0.0) {
							acosX = 3.14159265 - acosX;
						}
						vec2 q = vec2(0.5 - acosX * 0.15915494, acosY * -0.31830989);
						vec2 lookupShift = vec2(noiseShift.x, noiseShift.y);
						if (layer == 2) {
							lookupShift = vec2(noiseShift.z, noiseShift.w);
						}
						strip_uv(2 * j + e, lookupShift + q * noiseScale);
					}
#else
					strip_color(2 * j + e, vec4(mix(baseColor, vec3(1.0, 1.0, 1.0), white) * darkness, a * alpha));
#endif
				}

				// The last vertex of a strip is the first of the next one
				if (j < SHIELD_STRIP_SEGMENTS) {
					dir = vec2(dir.x * stepCos - dir.y * stepSin, dir.x * stepSin + dir.y * stepCos);
				}
			}

#ifdef SHIELD_NOISE
			if (layer == 0) {
				submit_strip_shaded(p, 2 * SHIELD_STRIP_SEGMENTS + 2);
			} else {
				// The facing term b, one step per ring: about its average over the ring
				float fade = 1.0;
				if (ring == 1) {
					fade = 0.75;
				} else if (ring >= 2) {
					fade = 0.45;
				}
				pass glow;
				glow.color = vec4(0.0, 0.0, 0.0, fade * alpha);
				glow.offset_color = glowColor;
				submit_strip(glow, 2 * SHIELD_STRIP_SEGMENTS + 2);
				pass core;
				core.color = vec4(0.0, 0.0, 0.0, fade * coreAlpha * alpha);
				core.offset_color = vec3(1.0, 1.0, 0.9) * darkness;
				submit_strip(core, 2 * SHIELD_STRIP_SEGMENTS + 2);
			}
#else
			submit_strip_shaded(p, 2 * SHIELD_STRIP_SEGMENTS + 2);
#endif
		}
		stripDir = dir;
	}
}
