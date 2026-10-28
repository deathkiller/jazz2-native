program TexturedBackground;
variant DITHER;
variant KEYED;
// The perspective of JJ2+ (SURFACE::FULLSCREEN and the inner surfaces), where the shift is the pixel of the layer at the
// top left of the view, as an ordinary layer would be placed - without it, the sky layers are drawn as they always were
variant PLUS;
variant PLUS + DITHER;
variant PLUS + KEYED;

shader_type canvas_item;
precision highp;

uniform vec2 uViewSize;
uniform vec2 uCameraPos;

uniform vec4 uHorizonColor;
uniform vec2 uShift;
// xy: how far the vanishing point and the horizon are from the middle of the quad, as a fraction of it, z: 1 if it doesn't
// fade into the horizon color there, as `warpHorizon` of JJ2+ - all 0 draws it as it always was; w: unused (a vec4, so the
// fixed-function rebuild can read it)
uniform vec4 uHorizonOffset;

#include "Include/TexturedBackgroundKey.inc"
#include "Include/TexturedBackgroundDither.inc"

// The perspective of JJ2+, measured in it: two planes, the one below the horizon seen from 60 pixels above it with a focal
// length of 256 pixels, so a row d pixels away is 15360 / d pixels of the texture deep and shows 60 / d of them per pixel.
// They would meet 8 rows past the horizon, so the texture never quite reaches the distance, and they fade into the horizon
// color from 141 rows away from it, faster the closer they are.
#define PLUS_DEPTH 15360.0
#define PLUS_SCALE 60.0
#define PLUS_GAP 8.0
#define PLUS_FADE_ROWS 141.0
#if LOW_POWER_GPU

// Low-power-GPU variant (the PS Vita's sceGxm and the OpenGL|ES 2.0 profile): the horizon that may be anywhere cost the
// old warp two divisions and a comparison more per pixel. All of that is linear across the quad, so the vertex stage works
// it out, and the fragment only takes the curves of the distance (see fragment()).
// The old warp: how far a row is from the horizon towards the edge of the half above it and of the half below it (only
// the one of its own half isn't negative) and the horizontal term; JJ2+: the row from the horizon, the same over the rows
// of the fade and the horizontal term, the pixels from the vanishing point. And how opaque the layer is, the alpha of the
// instance color.
varying vec4 vWarpTerms;

void vertex() {
#if PLUS
	vec2 pixel = UV * uViewSize - vec2(0.5);
	float row = pixel.y - floor((0.5 + uHorizonOffset.y) * uViewSize.y);
	vWarpTerms = vec4(row, row / PLUS_FADE_ROWS, (pixel.x - (0.5 + uHorizonOffset.x) * uViewSize.x) * (PLUS_SCALE / 256.0), COLOR.a);
#else
	float horizonY = clamp(0.5 + uHorizonOffset.y, 0.0, 1.0);
	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));
	vWarpTerms = vec4((horizonY - UV.y) / max(horizonY, 0.0001), (UV.y - horizonY) / max(1.0 - horizonY, 0.0001),
		(UV.x - 0.5 - uHorizonOffset.x) * correction, COLOR.a);
#endif
}
#endif

vec2 hash2D(in vec2 p) {
	float h = dot(p, vec2(12.9898, 78.233));
	float h2 = dot(p, vec2(37.271, 377.632));
	return -1.0 + 2.0 * vec2(fract(sin(h) * 43758.5453), fract(sin(h2) * 43758.5453));
}

vec3 voronoi(in vec2 p) {
	vec2 n = floor(p);
	vec2 f = fract(p);

	vec2 mg, mr;

	float md = 8.0;
	for (int j = -1; j <= 1; ++j) {
		for (int i = -1; i <= 1; ++i) {
			vec2 g = vec2(float(i), float(j));
			vec2 o = hash2D(n + g);

			vec2 r = g + o - f;
			float d = dot(r, r);

			if (d < md) {
				md = d;
				mr = r;
				mg = g;
			}
		}
	}
	return vec3(md, mr);
}

float addStarField(vec2 samplePosition, float threshold) {
	vec3 starValue = voronoi(samplePosition);
	if (starValue.x < threshold) {
		float power = 1.0 - (starValue.x / threshold);
		return min(power * power * power, 0.5);
	}
	return 0.0;
}

#if PLUS
// The perspective of JJ2+, by the pixels of the view as it counts them - the row of the horizon is the first one of the plane
// below it. The texture is half of it further down than the shift, V runs up the layer (see TileMap::TexturedBackgroundPass).
// xy the position in the texture, z how far it fades into the color of the horizon
vec3 plusWarp(vec2 uv) {
	vec2 pixel = uv * uViewSize - vec2(0.5);
	float row = pixel.y - floor((0.5 + uHorizonOffset.y) * uViewSize.y);
	float below = step(0.0, row);
	float distance = abs(row) + PLUS_GAP;
	vec2 texel = vec2(
		uShift.x + (pixel.x - (0.5 + uHorizonOffset.x) * uViewSize.x) * PLUS_SCALE / distance,
		uShift.y + 128.0 + (1.0 - 2.0 * below) * PLUS_DEPTH / distance
	);
	float rows = row / PLUS_FADE_ROWS;
	return vec3(texel.x / 256.0, 1.0 - texel.y / 256.0, clamp(1.0 - rows * rows, 0.0, 1.0));
}
#else
// The sky layers as this game always drew them, its own approximation of the original game: xy the position in the texture,
// z how far it fades into the color of the horizon
vec3 legacyWarp(vec2 uv) {
	// How far the row is from the horizon towards the edge of its half (0: the horizon, 1: the top or the bottom edge),
	// which side of the horizon it is on, and the distance from the edge (1.3: the horizon, 0.3: the edge)
	float horizonY = clamp(0.5 + uHorizonOffset.y, 0.0, 1.0);
	float aboveHorizon = (uv.y <= horizonY ? 1.0 : 0.0);
	float fromHorizon = (aboveHorizon > 0.5 ? (horizonY - uv.y) / max(horizonY, 0.0001) : (uv.y - horizonY) / max(1.0 - horizonY, 0.0001));
	float distance = 1.3 - fromHorizon;
#if SOFTWARE_RENDERER
	// Software-renderer variant: this full-screen per-pixel fragment is dominated by transcendentals
	// on the CPU - two pow() curves, an optional dithering second texture sample and a per-pixel
	// voronoi "star field" (dozens of sin() per pixel when uHorizonColor.w > 0). One such draw costs
	// roughly half a frame there, so this branch keeps the warp geometry and the horizon tint but
	// approximates the pow() curves with plain polynomials and drops the dither sample and the star
	// field entirely; the visible loss is the faint stars and a slightly different horizon falloff.
	float horizonDepth = distance;	// Approximates pow(distance, 1.4)
#else
	float horizonDepth = pow(distance, 1.4);
#endif

	float correction = ((uViewSize.x * 9.0) / (uViewSize.y * 16.0));

	// Half of the way from the edge to the horizon, the plane above the horizon runs up, the one below it down
	vec2 texturePos = vec2(
		(uShift.x / 256.0) + (uv.x - 0.5 - uHorizonOffset.x) * (0.5 + (1.5 * horizonDepth)) * correction,
		(uShift.y / 256.0) + (2.0 * aboveHorizon - 1.0) * 0.5 * (1.0 - fromHorizon) * 1.4 * distance
	);

#if SOFTWARE_RENDERER
	float horizonOpacity = clamp(distance * distance - 0.3, 0.0, 1.0);	// Approximates pow(distance, 1.5)
#else
	float horizonOpacity = clamp(pow(distance, 1.5) - 0.3, 0.0, 1.0);
#endif
	return vec3(texturePos, horizonOpacity);
}
#endif

void fragment() {
	// How opaque a layer drawn screened or multiplied is, the alpha of the instance color (see TileMap::RenderTexturedBackground())
#if LOW_POWER_GPU
	float opacity = vWarpTerms.w;
#else
	float opacity = COLOR.a;
#endif
#if LOW_POWER_GPU
#if PLUS
	// See vertex(): the distance, |row| + 8 rows, and its reciprocal; below the horizon (its row too) the texture runs down
	float qInv = 1.0 / (abs(vWarpTerms.x) + PLUS_GAP);
	float side = (vWarpTerms.x >= 0.0 ? 1.0 : -1.0);
	vec3 warp = vec3(
		(uShift.x / 256.0) + vWarpTerms.z * qInv,
		1.0 - (uShift.y + 128.0) / 256.0 + side * PLUS_SCALE * qInv,
		clamp(1.0 - vWarpTerms.y * vWarpTerms.y, 0.0, 1.0)
	);
#else
	// See vertex(): m how far the row is from the horizon and the distance 1.3 - m. pow() is two transcendentals on parts
	// like the SGX543's USSE, the square root gives pow(distance, 1.5) exactly, and blending that against the linear term
	// lands on pow(distance, 1.4) to within 0.007 over the range it takes ([0.3, 1.3]) - far closer than the plain distance
	// the software branch settles for, which is a different shape and made the horizon read as too sharp. Above the horizon
	// the texture runs up.
	float m = max(vWarpTerms.x, vWarpTerms.y);
	float distance = 1.3 - m;
	float distancePow15 = distance * sqrt(distance);
	float side = (vWarpTerms.x >= 0.0 ? 1.0 : -1.0);
	vec3 warp = vec3(
		(uShift.x / 256.0) + vWarpTerms.z * (0.5 + 1.2 * distancePow15 + 0.3 * distance),
		(uShift.y / 256.0) + side * 0.7 * (1.0 - m) * distance,
		clamp(distancePow15 - 0.3, 0.0, 1.0)
	);
#endif
#else
#if PLUS
	vec3 warp = plusWarp(UV);
#else
	vec3 warp = legacyWarp(UV);
#endif
#endif
	vec2 texturePos = warp.xy;
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	vec4 texColor = textureTexel(texturePos);
#else
	vec4 texColor = texture(TEXTURE, texturePos);
#endif
	float horizonOpacity = warp.z;

	vec4 horizonColorWithStars = vec4(uHorizonColor.xyz, 1.0);
#if !SOFTWARE_RENDERER && !NO_DYNAMIC_BRANCHING && !LOW_POWER_GPU
	if (uHorizonColor.w > 0.0) {
		vec2 samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00012;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));

		samplePosition = (UV * uViewSize / uViewSize.xx) + uCameraPos.xy * 0.00018 + 0.5;
		horizonColorWithStars += vec4(addStarField(samplePosition * 7.0, 0.00008));
	}
#endif

	COLOR = mix(texColor, horizonColorWithStars, horizonOpacity * (1.0 - uHorizonOffset.z));
#if DITHER && !SOFTWARE_RENDERER && !LOW_POWER_GPU
	COLOR.rgb = reduceColors(COLOR.rgb, gl_FragCoord.xy);
#endif
#if KEYED
	COLOR = keyedColor(COLOR.rgb);
#else
	// Premultiplied, which the blending of the sprite modes takes as it is
	COLOR = vec4(COLOR.rgb * opacity, opacity);
#endif
}

void fixed_function(pvr, gs) {
	// Banded trapezoid rebuild of the warp (shared with the tunnel and the cylinder). These consoles
	// take the include exactly as it is, with both macros left UNDEFINED. The PVR and the GS cannot lerp
	// a texel toward a colour at all (the PVR only modulates and adds), so the horizon tint stays a second
	// gradient pass over the bands, which is the include's default delivery. And the textured bands are
	// cut into pieces at the texture's edges, because neither samples this render target repeating: the
	// PVR's is a stride surface, which its texture unit cannot wrap. The perspective of JJ2+ is drawn in perspective
	// (the PVR takes the depth as 1/w of the vertices, the GS as their Q), which needs no more pieces than the whole
	// textures and the tint cut it into, at most 8 a band - every vertex counts on the Dreamcast and the PS2.
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gu) {
	// The GE cannot fold the tint into the textured draw either (its GU_TFX_BLEND weighs by the TEXEL
	// rather than by an interpolated alpha), so the tint stays the include's second gradient pass, but it
	// does wrap the 256x256 render target in hardware like the RDP: each band goes out as ONE textured
	// strip instead of being cut into up to eight pieces at the texture's edges. The perspective of JJ2+ is
	// cut coarsely, every vertex counts on the PSP - and it's drawn in bands, the GE takes the vertices of this
	// tier in screen space, without any perspective.
#define WARP_TEXTURE_REPEAT
#define WARP_LOW_DETAIL
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(rdp) {
	// The same rebuild, but each band goes out as ONE strip: the RDP wraps texture coordinates in
	// hardware (a mask on the tile descriptor), and so does the PICA200 (the render target is a power of
	// two, which is all its repeat mode needs), so the band's unwrapped U span is sampled straight from the
	// 256-texel row window instead of being cut into up to eight pieces at the texture's edges. That is
	// ~130 strips a frame down to ~64, on CPUs where every strip costs more than the GPU's time to draw it.
	// The horizon tint rides the band's own strip as a per-vertex colour through the TINT_MIX combiner,
	// exactly like the GX block: one strip per band instead of a textured one and a gradient one, and no
	// combiner change between them. The perspective of JJ2+ is drawn in perspective: a band is a rectangle
	// of 4 vertices, which the RDP draws row by row, each row exactly as it maps, so it's cut only where the
	// whole textures and the tint need it - about a dozen bands a frame.
#define WARP_TEXTURE_REPEAT
#define WARP_TEXTURE_REPEAT_IN_PASS
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(pica) {
	// The rdp block's rebuild exactly, with the wrap taken from the render target's sampler state. The
	// PICA200 interpolates the colors in perspective too (the depth is the w of the vertices), so the
	// bands of the tint are kept short where the depth changes much across them.
#define WARP_TEXTURE_REPEAT
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_LOW_DETAIL
#define WARP_PERSPECTIVE
#define WARP_COLOR_IN_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}

void fixed_function(gx, legacygl) {
	// The same banded trapezoid rebuild as above, from the same include - the geometry is portable
	// vocabulary. The GX additionally folds the horizon tint into each band piece's own draw with a
	// TINT_MIX combiner stage instead of laying a second gradient pass over it (same pixels, half
	// the primitives, and the textured vertex format is never left). The perspective of JJ2+ is drawn
	// in perspective (the depth as the q of a projective texture coordinate).
#define WARP_TINT_IN_VERTEX_COLOR
#define WARP_PERSPECTIVE
#include "Include/TexturedBackgroundWarp.inc"
}
