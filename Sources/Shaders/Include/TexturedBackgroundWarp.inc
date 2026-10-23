// Console rebuild of the TexturedBackground warp out of geometry, shared by every
// void fixed_function block of TexturedBackground.shader, TexturedBackgroundTunnel.shader and TexturedBackgroundCylinder.shader
// (the tunnel keeps the same planar approximation on this tier, exactly as the handwritten backend code did, unless it's drawn
// in the perspective of JJ2+ - see below; the cylinder is always drawn in it).
//
// Everything below is portable extended vocabulary (the strip builder, the pre-clip quad axes and
// resolved uniforms), so the band geometry is literally the same code on every console. The one
// place they differ is how the horizon tint is delivered, and the difference is a capability, not a
// policy: a block that defines WARP_TINT_IN_VERTEX_COLOR before including this file asks for the
// tint to ride in the band pieces' own per-vertex colours, folded into their draw by the TINT_MIX
// combiner stage. The gx, rdp and pica blocks do - neither the PVR's texture environment nor the PSP's
// GE can lerp a texel toward an interpolated colour, so they leave the macro undefined and get the
// tint as a second, alpha-blended gradient pass over the bands instead. The two produce the same
// pixels (`mix(texel, tint, a)` either way); the folded form just draws half the primitives and
// never leaves the textured vertex format.
//
// The warp's mapping is affine along every screen row - texturePos.x is linear in UV.x for a fixed
// row and texturePos.y depends on UV.y alone - so a stack of horizontal bands carrying
// interpolated texture coordinates reproduces it. The horizon tint is likewise a per-row value, so
// it goes on as a second pass whose vertex alpha the rasterizer interpolates down the band.
//
// The shader relies on the texture repeating: the warp spans roughly two periods across the
// screen. The source here is a render target, which the PowerVR's tile accelerator writes as a linear
// surface that the hardware cannot tile, so unless a block defines WARP_TEXTURE_REPEAT (a GPU that
// wraps the render target itself) each band is cut at every whole-texture boundary and the pieces are emitted with coordinates inside a single period. A cut follows the
// line where the coordinate crosses the boundary, which is slanted (the row's span widens toward
// the horizon) - so the pieces are trapezoids rather than columns, and their corners land exactly
// on the boundary. That is exact, not an approximation: the shader's mapping is affine across each
// row and so is the interpolation across each trapezoid.
//
// The two curve approximations are the SOFTWARE_RENDERER branch above (distance for
// pow(distance, 1.4), distance squared for pow(distance, 1.5)), which is what every tier without
// real shaders already draws; it drops the per-pixel star field along with them.
//
// In the perspective of JJ2+ (PLUS, SURFACE::FULLSCREEN and the inner surfaces) the warp horizon and the cylinder are
// affine along every row as well, so their bands carry the exact coordinates of the rows at their edges - only the curves
// down a band (the depth of the warp horizon, thinner bands towards its horizon; the curve of the cylinder) are straight
// within it. The tunnel of JJ2+ is radial instead: rings a quarter of a texture deep cut into 32 sectors, each corner with
// its exact coordinates, only the logarithm of the distance and the spiral are straight across a piece. The vertical
// coordinate of either is cut at whole textures where it's crossed (found exactly, the curves are known), so the same
// pieces serve the targets that can't repeat the texture.
//
// A block that defines WARP_PERSPECTIVE (every target but the PSP, whose GE draws the screen-space vertices of this tier
// without any perspective) draws the warp horizon of JJ2+ in perspective instead: the depth is the q of the vertices
// (see FixedFunctionPass.h), which makes it exact at any size, so the bands are only as many as the tint and the whole
// textures need - see below.
//
// Only what a program draws is rebuilt by its effect: the variant says which perspective (PLUS defined for JJ2+), and the
// blocks of the tunnel and the cylinder define WARP_TUNNEL or WARP_CYLINDER before including this file.
//
// An inner surface of JJ2+ (and the part of any surface that is in the view) is a quad that shows only a part of the whole,
// which its texRect says. The geometry is laid out over the whole, only what is seen of it is made, and the strips are cut to
// the quad (strip_clip) where they reach past it.

if (!has_uniform(uViewSize) || !has_uniform(uShift) || !has_uniform(uHorizonColor)) {
	// Without the resolved uniforms there is no warp to rebuild - a plain textured quad keeps the
	// background visible
	pass fallback;
	submit_quad(fallback);
} else {
	vec2 viewSize = uniform_vec2(uViewSize);
	vec2 shift = uniform_vec2(uShift);
	vec4 horizon = uniform_vec4(uHorizonColor);

	// The band geometry comes from the transformed sprite axes rather than the corner array, so
	// scissor clipping of the quad cannot distort the warp
	float leftX = quad_origin().x;
	float spanX = quad_axis_x().x;
	float topY = quad_origin().y;
	float spanY = quad_axis_y().y;
	// The quad is only a part of what the shader draws where its texRect says so - an inner surface, or a surface only partly
	// in the view (see TileMap::RenderTexturedBackground()). The geometry is then laid out over the whole and cut to the quad
	vec4 texRect = quad_tex_rect();
	bool partial = (texRect.x > 0.0 && texRect.z > 0.0 && (texRect.x < 0.9999 || texRect.z < 0.9999 || abs(texRect.y) > 0.0001 || abs(texRect.w) > 0.0001));
	if (partial) {
		strip_clip(vec4(min(leftX, leftX + spanX), min(topY, topY + spanY), max(leftX, leftX + spanX), max(topY, topY + spanY)));
		spanX = spanX / texRect.x;
		spanY = spanY / texRect.z;
		leftX = leftX - texRect.y * spanX;
		topY = topY - texRect.w * spanY;
	}
#if defined(PLUS) || defined(WARP_CYLINDER)
	// In the perspective of JJ2+, what lies wholly outside isn't made at all: the part that is seen, in pixels of the whole
	float viewW = max(viewSize.x, 1.0);
	float viewH = max(viewSize.y, 1.0);
	float visLeft = (partial ? texRect.y * viewW : 0.0);
	float visRight = (partial ? (texRect.y + texRect.x) * viewW : viewW);
	float visTop = (partial ? texRect.w * viewH : 0.0);
	float visBottom = (partial ? (texRect.w + texRect.z) * viewH : viewH);
	float scaleX = spanX / viewW;
	float scaleY = spanY / viewH;
#endif

	pass warp;				// Textured band pieces under the material state
#ifdef WARP_TEXTURE_REPEAT_IN_PASS
	// The RDP is told to wrap by the pass (a mask on its tile descriptor); the other GPUs that define
	// WARP_TEXTURE_REPEAT wrap through the render target's own sampler state
	warp.texture_repeat = true;
#endif
#ifdef WARP_TINT_IN_VERTEX_COLOR
	// One combiner stage lerps each texel toward the per-vertex tint by the per-vertex alpha, so the
	// band piece IS the tinted band; nothing is drawn over it
	warp.tev = TINT_MIX;
#else
	pass horizonPass;		// Flat horizon tint, interpolated down each band
	horizonPass.blend = ALPHA;
#endif

#ifdef WARP_TINT_IN_VERTEX_COLOR
	int stageCount = 1;
#else
	// The textured pieces of every band go out first and the horizon tint over all of them after, in
	// a second walk of the same bands, rather than each band's tint right after its own pieces: every
	// draw stays under one state for the whole walk, so the backends batch the warp into two draws
	// where alternating between the two states made two per band. It changes no pixel - the bands
	// share only their edge rows, which the rasterizer gives to one band or the other, and a band's
	// tint covers exactly the rows of its own pieces whichever band went before.
	int stageCount = 2;
#endif

	// How finely the perspective of JJ2+ is cut: a block that defines WARP_LOW_DETAIL (the N64, the Dreamcast and the PS2)
	// draws it with about as many vertices as the old warp, a few texels off its curves, the others within a texel. Macros,
	// as each is taken by one part of the rebuild only.
#ifdef WARP_LOW_DETAIL
#define WARP_BAND_COUNT 16			// Bands each side of the horizon
#define WARP_PIECE_COLUMNS 1		// Columns a piece between two whole textures is drawn in
#define WARP_REPEAT_COLUMNS 2		// Columns a band is drawn in where the texture repeats
#define WARP_RING_STEP 0.5			// How deep a ring of the tunnel is, in textures
#define WARP_SECTOR_COUNT 16		// Sectors a ring of the tunnel is cut into
#define WARP_TINY_RINGS false		// Whether the rings start under a pixel from the center, or at 2 pixels
#define WARP_MAX_PIECES 8			// Pieces a band in perspective is cut into at most, more is squeezed into one
#else
#define WARP_BAND_COUNT 32
#define WARP_PIECE_COLUMNS 3
#define WARP_REPEAT_COLUMNS 7
#define WARP_RING_STEP 0.25
#define WARP_SECTOR_COUNT 32
#define WARP_TINY_RINGS true
#define WARP_MAX_PIECES 24
#endif
#ifdef WARP_ROW_BANDS
	// A block that defines WARP_ROW_BANDS (the RDP) draws a band of 4 vertices row by row, each row exactly as it maps,
	// so a band is one column there - more would make it triangles
#undef WARP_REPEAT_COLUMNS
#define WARP_REPEAT_COLUMNS 1
#endif

#if defined(WARP_TUNNEL) && defined(PLUS)
	{
		// The tunnel of JJ2+ (see TexturedBackgroundTunnel.shader): u = X / 4 + 64 + 256 * angle / 2pi (+ the spiral),
		// v = Y - 256 * log2(r), the angle clockwise from the right around the center
		vec4 tunnel = uniform_vec4(uTunnel);
		float centerX = tunnel.x * viewW;
		float centerY = floor(tunnel.y * viewH);
		float fadeOn = (tunnel.z > 0.5 ? 1.0 : 0.0);
		float spiral = (tunnel.w > 0.5 ? 1.0 : 0.0);
		// The center on the screen, in the middle of its pixel, and how far from it what is seen begins and ends
		float screenX = leftX + (centerX + 0.5) * scaleX;
		float screenY = topY + (centerY + 0.5) * scaleY;
		float farX = max(abs(visLeft - centerX - 0.5), abs(visRight - centerX - 0.5));
		float farY = max(abs(visTop - centerY - 0.5), abs(visBottom - centerY - 0.5));
		float farthest = sqrt(farX * farX + farY * farY) + 1.0;
		float nearX = max(max(visLeft - centerX - 0.5, centerX + 0.5 - visRight), 0.0);
		float nearY = max(max(visTop - centerY - 0.5, centerY + 0.5 - visBottom), 0.0);
		float nearest = sqrt(nearX * nearX + nearY * nearY) - 1.0;
		// The texture coordinate 1 - v / 256 = c + log2(r) is a multiple of a ring's depth s (a quarter or a half) at
		// r = 2^(-f) * 2^(j s), f being what c has over whole rings - 2^(-f) of f < 0.5 as a polynomial; r^-0.15 of the
		// spiral there likewise, 2^(0.15 f) at j = 0 and then 2^(-0.15 s) each ring
		bool quarterRings = (WARP_RING_STEP < 0.3);
		float vC = 1.0 - shift.y / 256.0;
		float vSteps = floor(vC / WARP_RING_STEP);
		float f = vC - vSteps * WARP_RING_STEP;
		float firstRadius = 1.0 - f * (0.693147 - f * (0.240227 - f * (0.0555041 - f * 0.0096181)));
		float g = 0.15 * f;
		float firstSpiral = 1.0 + g * (0.693147 + g * (0.240227 + g * 0.0555041));
		float ringFactor = (quarterRings ? 1.18920712 : 1.41421356);
		float spiralFactor = (quarterRings ? 0.974342 : 0.949342);
		// The first ring reaches from the center to about 0.7 pixels (j s = -0.5), or to about 2.8 (j s = 1, the rings in
		// the middle are too small to see the tunnel in anyway); the radius and r^-0.15 there, 2^(j s) and 2^(-0.15 j s)
		int ringCount = (quarterRings ? 52 : (WARP_TINY_RINGS ? 26 : 23));
		int ringStart = (quarterRings ? (WARP_TINY_RINGS ? -2 : 4) : (WARP_TINY_RINGS ? -1 : 2));
		float startRadius = (WARP_TINY_RINGS ? 0.707107 : 2.0);
		float startSpiral = (WARP_TINY_RINGS ? 1.05330 : 0.901250);
		// The outer corners 1 / cos(pi / sectors) further out, and log2 of that
		bool fineSectors = (WARP_SECTOR_COUNT > 16);
		float cornerFactor = (fineSectors ? 1.0048386 : 1.0195911);
		float cornerLog = (fineSectors ? 0.0069638 : 0.0279914);
		int groupCount = (WARP_SECTOR_COUNT + 2) / 3;
		float uOrigin = (shift.x * 0.25 + 64.0) / 256.0;
		float sectorStep = 6.28318531 / float(WARP_SECTOR_COUNT);

		// The sectors turn by the same angle each, the direction of each edge is the one before turned by it
		float stepCos = cos(sectorStep);
		float stepSin = sin(sectorStep);
		// Nearer to the center than this, the tunnel fades at least 98 % into its color, (87 / r)^2 - 0.0414, so the rings there
		// aren't drawn - the many smallest ones - but the color alone is, inside the first ring that is
		float flatRadius = (fadeOn > 0.5 ? 87.0 / sqrt(0.98 + 0.0414) : 0.0);

		for (int stage = 0; stage < stageCount; stage++) {
			// From the center out past the farthest corner, 2^12.5 times further at most
			float ringRadius = firstRadius * startRadius;
			float ringSpiral = firstSpiral * startSpiral;
			bool flatPending = (flatRadius > 0.0);
			for (int ring = 0; ring < ringCount; ring++) {
				float innerR = ringRadius;
				float outerR = ringRadius * ringFactor;
				float innerS = ringSpiral;
				float outerS = ringSpiral * spiralFactor;
				ringRadius = outerR;
				ringSpiral = outerS;
				bool isFlat = (outerR <= flatRadius);
				// The color ends at the inner edge of the first ring out of it, which is drawn with it
				bool drawsFlat = (flatPending && !isFlat);
				if (drawsFlat) {
					flatPending = false;
				}
				// The outer edge goes round the circle (its corners further out, with the coordinate of there), so the next
				// ring, turned against this one by the spiral, leaves no gap along its straight inner edge
				float outerCorner = outerR * cornerFactor;
				float innerCorner = (ring == 0 ? 0.0 : innerR);
				// What is wholly nearer than what is seen or farther isn't drawn, the color inside the ring may be seen alone
				bool ringSeen = (innerR < farthest && outerCorner > nearest);
				bool flatSeen = (drawsFlat && innerCorner > nearest && stage == stageCount - 1);
				if (!isFlat && (ringSeen || flatSeen)) {
					// The ring is a quarter or a half of a texture deep, inside one period
					float tvInner = WARP_RING_STEP * (vSteps + float(ring + ringStart));
					float tvBase = floor(tvInner + 0.5 * WARP_RING_STEP);
					float v0 = tvInner - tvBase;
					float v1 = tvInner + WARP_RING_STEP + cornerLog - tvBase;
					// The sectors start where the texture repeats around the inner edge, so the seam falls between two of
					// them; the spiral turns the outer edge against it
					float twistInner = spiral * (356.2 * innerS - 91.0) / 256.0;
					float twistOuter = spiral * (356.2 * outerS - 91.0) / 256.0;
					float seam = uOrigin + twistInner;
					seam = seam - floor(seam);
					float startAngle = -seam * 6.28318531;
					float twist = twistOuter - twistInner;
					float tintInner = fadeOn * clamp(7569.0 / (innerR * innerR) - 0.0414, 0.0, 1.0);
					float tintOuter = fadeOn * clamp(7569.0 / (outerR * outerR) - 0.0414, 0.0, 1.0);
					if (flatSeen && innerCorner > 0.0) {
						// The color inside the inner edge of the ring, its corners exactly, across the polygon from both ends
						// towards the middle, eight vertices a strip that starts with the last two of the one before (what the PVR
						// holds) - under the ring where the tint is folded into the pieces, with the tint of the rings otherwise
						int flatStrips = (WARP_SECTOR_COUNT + 3) / 6;
						for (int flatStrip = 0; flatStrip < 6; flatStrip++) {
							if (flatStrip < flatStrips) {
								int first = flatStrip * 6;
								int count = min(8, WARP_SECTOR_COUNT - first);
								for (int j = 0; j < 8; j++) {
									if (j < count) {
										int z = first + j;
										int corner = (z - 2 * (z / 2) == 0 ? z / 2 : WARP_SECTOR_COUNT - 1 - (z - 1) / 2);
										float angle = startAngle + float(corner) * sectorStep;
										strip_position(j, vec2(screenX + innerCorner * cos(angle) * scaleX, screenY + innerCorner * sin(angle) * scaleY));
										strip_uv(j, vec2(0.0, 0.0));
										strip_color(j, vec4(horizon.xyz, 1.0));
									}
								}
#ifdef WARP_TINT_IN_VERTEX_COLOR
								submit_strip_shaded(warp, count);
#else
								submit_strip_shaded(horizonPass, count);
#endif
							}
						}
					}
					if (ringSeen) {
						// The direction of the first edge, the others turned from it - a strip ends on the edge the next one
						// starts with, so it's only turned within a strip
						float ca = cos(startAngle);
						float sa = sin(startAngle);
						// Three sectors a strip (8 vertices, what the PVR holds)
						for (int group = 0; group < groupCount; group++) {
							int firstSector = group * 3;
							int sectors = min(3, WARP_SECTOR_COUNT - firstSector);
							if (stage == 0 || tintInner > 0.0 || tintOuter > 0.0) {
								for (int k = 0; k < 4; k++) {
									if (k <= sectors) {
										if (k > 0) {
											float turnedCos = ca * stepCos - sa * stepSin;
											sa = sa * stepCos + ca * stepSin;
											ca = turnedCos;
										}
										float u = float(firstSector + k) / float(WARP_SECTOR_COUNT);
										strip_position(2 * k, vec2(screenX + innerCorner * ca * scaleX, screenY + innerCorner * sa * scaleY));
										strip_position(2 * k + 1, vec2(screenX + outerCorner * ca * scaleX, screenY + outerCorner * sa * scaleY));
										strip_uv(2 * k, vec2(u, v0));
										strip_uv(2 * k + 1, vec2(u + twist, v1));
										strip_color(2 * k, vec4(horizon.xyz, tintInner));
										strip_color(2 * k + 1, vec4(horizon.xyz, tintOuter));
									}
								}
#ifdef WARP_TINT_IN_VERTEX_COLOR
								submit_strip_shaded(warp, 2 * (sectors + 1));
#else
								if (stage == 0) {
									submit_strip(warp, 2 * (sectors + 1));
								} else {
									submit_strip_shaded(horizonPass, 2 * (sectors + 1));
								}
#endif
							}
						}
					}
				}
			}
		}
	}
#elif defined(PLUS) && defined(WARP_PERSPECTIVE)
	{
		// The warp horizon of JJ2+ in perspective (see TexturedBackground.shader): d = |row - horizon| + 8 rows away, the row
		// maps u to X + (x - center) * 60 / d and v to Y + 128 -/+ 15360 / d below/above the horizon. So u * d, v * d and d are
		// all linear across the view and down it, and with d as the q of the vertices a piece of any size has the texture
		// exactly where it is. The bands are cut only where the vertical coordinate crosses a whole texture, and where the
		// tint is (it curves down the view, the colors are interpolated linearly) every 30 rows. The targets that can't repeat
		// the texture cut a band into pieces along the lines the horizontal coordinate crosses a whole texture on - straight
		// lines too, from the vanishing point.
		float screenLeft = leftX + visLeft * scaleX;
		float screenRight = leftX + visRight * scaleX;
		vec4 offsets = vec4(0.0, 0.0, 0.0, 0.0);
		if (has_uniform(uHorizonOffset)) {
			offsets = uniform_vec4(uHorizonOffset);
		}
		float centerX = (0.5 + offsets.x) * viewW;
		float horizonRow = floor((0.5 + offsets.y) * viewH);
		float fadeOn = 1.0 - offsets.z;
		float warpC = 1.0 - (shift.y + 128.0) / 256.0;
		// Nearer to the horizon than this, it fades at least 98 % into its color, 1 - ((d - 8) / 141)^2, so it's drawn in the
		// color alone, one piece instead of the thinnest bands cut most often
		float flatDistance = (fadeOn > 0.5 ? 8.0 + 141.0 * 0.141421 : 8.0);
		// The pieces fan out from the vanishing point and reach far past the view away from the horizon, so they're cut to it
		// (and never drawn past it where the view isn't the whole screen)
		strip_clip(vec4(min(screenLeft, screenRight), min(topY + visTop * scaleY, topY + visBottom * scaleY),
			max(screenLeft, screenRight), max(topY + visTop * scaleY, topY + visBottom * scaleY)));
#ifdef WARP_COLOR_IN_PERSPECTIVE
		// A block that defines WARP_COLOR_IN_PERSPECTIVE (the PICA200) has the colors interpolated in perspective too, which
		// bends the tint where the distance changes much across a band, so a band of the tint reaches 15 % further at most
		float fadeRatio = 1.15;
#else
		float fadeRatio = 100.0;
#endif

		for (int stage = 0; stage < stageCount; stage++) {
			// The color goes first, or over the textured walk with the tint
			float flatTop = max(horizonRow + 8.5 - flatDistance, visTop);
			float flatBottom = min(horizonRow + flatDistance - 7.5, visBottom);
			if (flatBottom > flatTop && stage == stageCount - 1) {
				strip_position(0, vec2(screenRight, topY + flatTop * scaleY));
				strip_position(1, vec2(screenRight, topY + flatBottom * scaleY));
				strip_position(2, vec2(screenLeft, topY + flatTop * scaleY));
				strip_position(3, vec2(screenLeft, topY + flatBottom * scaleY));
				strip_uv(0, vec2(0.0, 0.0));
				strip_uv(1, vec2(0.0, 0.0));
				strip_uv(2, vec2(0.0, 0.0));
				strip_uv(3, vec2(0.0, 0.0));
				strip_color(0, vec4(horizon.xyz, 1.0));
				strip_color(1, vec4(horizon.xyz, 1.0));
				strip_color(2, vec4(horizon.xyz, 1.0));
				strip_color(3, vec4(horizon.xyz, 1.0));
#ifdef WARP_TINT_IN_VERTEX_COLOR
				submit_strip_shaded(warp, 4);
#else
				submit_strip_shaded(horizonPass, 4);
#endif
			}
			for (int halfIndex = 0; halfIndex < 2; halfIndex++) {
				// Below the horizon, then above it, each from the horizon out over what is seen of it
				float sigma = (halfIndex == 0 ? 1.0 : -1.0);
				float dFrom = max(halfIndex == 0 ? visTop - horizonRow + 7.5 : horizonRow + 8.5 - visBottom, flatDistance);
				float dTo = (halfIndex == 0 ? visBottom - horizonRow + 7.5 : horizonRow + 8.5 - visTop);
				// The tint ends 149 rows away, the walk of the tint over the pieces goes only that far
				float fadeEnd = (fadeOn > 0.5 ? min(149.0, dTo) : dFrom);
				float walkEnd = (stage == 1 ? fadeEnd : dTo);
				// The texture the first band is in - the coordinate 1 - v / 256 falls with the distance below the horizon and
				// rises above it
				float tvFrom = warpC + sigma * 60.0 / dFrom;
				float vIndex = (halfIndex == 0 ? ceil(tvFrom) - 1.0 : floor(tvFrom));
				float dA = dFrom;
				for (int band = 0; band < 48 && dA < walkEnd; band++) {
					// Where the next whole texture is crossed - above the horizon, the coordinate may only near it
					float vCross = (halfIndex == 0 ? vIndex : vIndex + 1.0);
					float crossAt = walkEnd;
					if (stage == 0 && sigma * (vCross - warpC) > 0.0) {
						crossAt = 60.0 / (sigma * (vCross - warpC));
					}
					// The tint in even steps to where it ends, 30 rows apart at most
					float fadeAt = walkEnd;
					if (dA < fadeEnd && (stageCount == 1 || stage == 1)) {
						float fadeStep = min(30.0, dA * (fadeRatio - 1.0));
						fadeAt = dA + (fadeEnd - dA) / max(ceil((fadeEnd - dA) / fadeStep - 0.01), 1.0);
					}
					float dB = max(min(min(crossAt, fadeAt), walkEnd), dA);

					if (dB > dA) {
						// The upper edge first: its distance and row, the vertical coordinate in the texture of the band, the tint
						float d0 = (halfIndex == 0 ? dA : dB);
						float d1 = (halfIndex == 0 ? dB : dA);
						float y0 = topY + (halfIndex == 0 ? d0 + horizonRow - 7.5 : horizonRow + 8.5 - d0) * scaleY;
						float y1 = topY + (halfIndex == 0 ? d1 + horizonRow - 7.5 : horizonRow + 8.5 - d1) * scaleY;
						float v0 = warpC + sigma * 60.0 / d0 - vIndex;
						float v1 = warpC + sigma * 60.0 / d1 - vIndex;
						float from0 = (d0 - 8.0) / 141.0;
						float from1 = (d1 - 8.0) / 141.0;
						float tint0 = fadeOn * clamp(1.0 - from0 * from0, 0.0, 1.0);
						float tint1 = fadeOn * clamp(1.0 - from1 * from1, 0.0, 1.0);

						if (stage == 0) {
							// The horizontal coordinate at the left and the right edge of what is seen
							float uLeft0 = (shift.x + (visLeft - 0.5 - centerX) * 60.0 / d0) / 256.0;
							float uRight0 = (shift.x + (visRight - 0.5 - centerX) * 60.0 / d0) / 256.0;
							float uLeft1 = (shift.x + (visLeft - 0.5 - centerX) * 60.0 / d1) / 256.0;
							float uRight1 = (shift.x + (visRight - 0.5 - centerX) * 60.0 / d1) / 256.0;
#ifdef WARP_TEXTURE_REPEAT
							// One strip across, reduced by whole texture widths (see the old warp below) - a rectangle, which the
							// RDP draws row by row
							float uBase = floor(min(uLeft0, uLeft1));
							strip_position(0, vec2(screenRight, y0));
							strip_position(1, vec2(screenRight, y1));
							strip_position(2, vec2(screenLeft, y0));
							strip_position(3, vec2(screenLeft, y1));
							strip_uv(0, vec2(uRight0 - uBase, v0));
							strip_uv(1, vec2(uRight1 - uBase, v1));
							strip_uv(2, vec2(uLeft0 - uBase, v0));
							strip_uv(3, vec2(uLeft1 - uBase, v1));
							strip_q(0, d0);
							strip_q(1, d1);
							strip_q(2, d0);
							strip_q(3, d1);
#ifdef WARP_TINT_IN_VERTEX_COLOR
							strip_color(0, vec4(horizon.xyz, tint0));
							strip_color(1, vec4(horizon.xyz, tint1));
							strip_color(2, vec4(horizon.xyz, tint0));
							strip_color(3, vec4(horizon.xyz, tint1));
							submit_strip_shaded(warp, 4);
#else
							submit_strip(warp, 4);
#endif
#else
							int firstPiece = int(floor(min(uLeft0, uLeft1)));
							int lastPiece = int(ceil(max(uRight0, uRight1)));
							if (lastPiece - firstPiece > WARP_MAX_PIECES) {
								// More periods across than pieces, only right at the horizon without the fade: one strip with the
								// row squeezed into a single period, as much noise as the texture would be there
								strip_position(0, vec2(screenRight, y0));
								strip_position(1, vec2(screenRight, y1));
								strip_position(2, vec2(screenLeft, y0));
								strip_position(3, vec2(screenLeft, y1));
								strip_uv(0, vec2(1.0, v0));
								strip_uv(1, vec2(1.0, v1));
								strip_uv(2, vec2(0.0, v0));
								strip_uv(3, vec2(0.0, v1));
								strip_q(0, d0);
								strip_q(1, d1);
								strip_q(2, d0);
								strip_q(3, d1);
#ifdef WARP_TINT_IN_VERTEX_COLOR
								strip_color(0, vec4(horizon.xyz, tint0));
								strip_color(1, vec4(horizon.xyz, tint1));
								strip_color(2, vec4(horizon.xyz, tint0));
								strip_color(3, vec4(horizon.xyz, tint1));
								submit_strip_shaded(warp, 4);
#else
								submit_strip(warp, 4);
#endif
							} else {
								for (int piece = firstPiece; piece < lastPiece; piece++) {
									// Between the lines the horizontal coordinate is a whole texture on, x = center + (256 k - X) * d / 60,
									// the far one first as the sprite corners go
									float edgeA = 256.0 * float(piece) - shift.x;
									float edgeB = edgeA + 256.0;
									strip_position(0, vec2(leftX + (0.5 + centerX + edgeB * d0 / 60.0) * scaleX, y0));
									strip_position(1, vec2(leftX + (0.5 + centerX + edgeB * d1 / 60.0) * scaleX, y1));
									strip_position(2, vec2(leftX + (0.5 + centerX + edgeA * d0 / 60.0) * scaleX, y0));
									strip_position(3, vec2(leftX + (0.5 + centerX + edgeA * d1 / 60.0) * scaleX, y1));
									strip_uv(0, vec2(1.0, v0));
									strip_uv(1, vec2(1.0, v1));
									strip_uv(2, vec2(0.0, v0));
									strip_uv(3, vec2(0.0, v1));
									strip_q(0, d0);
									strip_q(1, d1);
									strip_q(2, d0);
									strip_q(3, d1);
#ifdef WARP_TINT_IN_VERTEX_COLOR
									strip_color(0, vec4(horizon.xyz, tint0));
									strip_color(1, vec4(horizon.xyz, tint1));
									strip_color(2, vec4(horizon.xyz, tint0));
									strip_color(3, vec4(horizon.xyz, tint1));
									submit_strip_shaded(warp, 4);
#else
									submit_strip(warp, 4);
#endif
								}
							}
#endif
						}

#ifndef WARP_TINT_IN_VERTEX_COLOR
						if (stage == 1 && (tint0 > 0.0 || tint1 > 0.0)) {
							strip_position(0, vec2(screenRight, y0));
							strip_position(1, vec2(screenRight, y1));
							strip_position(2, vec2(screenLeft, y0));
							strip_position(3, vec2(screenLeft, y1));
							strip_color(0, vec4(horizon.xyz, tint0));
							strip_color(1, vec4(horizon.xyz, tint1));
							strip_color(2, vec4(horizon.xyz, tint0));
							strip_color(3, vec4(horizon.xyz, tint1));
							submit_strip_shaded(horizonPass, 4);
						}
#endif
					}

					if (dB >= crossAt) {
						vIndex = vIndex - sigma;
					}
					dA = dB;
				}
			}
		}
	}
#elif defined(PLUS) || defined(WARP_CYLINDER)
	{
		// The warp horizon of JJ2+ (see TexturedBackground.shader): d = |row - horizon| + 8 rows away, the row maps u to
		// X + (x - center) * 60 / d and v to Y + 128 -/+ 15360 / d below/above the horizon; or the cylinder of JJ2+ (see
		// TexturedBackgroundCylinder.shader): u = X / 4 + (x - center) * the slope of the row, v running down the view
		float screenLeft = leftX + visLeft * scaleX;
		float screenRight = leftX + visRight * scaleX;
#ifdef WARP_CYLINDER
		bool isCylinder = true;
#else
		bool isCylinder = false;
#endif
		vec4 cylinder = vec4(0.5, 0.5, 1.0, 0.0);
		if (isCylinder) {
			cylinder = uniform_vec4(uCylinder);
		}
		vec4 offsets = vec4(0.0, 0.0, 0.0, 0.0);
		if (has_uniform(uHorizonOffset)) {
			offsets = uniform_vec4(uHorizonOffset);
		}
		float centerX = (isCylinder ? cylinder.x : 0.5 + offsets.x) * viewW;
		float horizonRow = floor((isCylinder ? cylinder.y : 0.5 + offsets.y) * viewH);
		// How far it fades into the horizon color, the cylinder only to 65 %
		float fadeOn = (isCylinder ? (cylinder.z > 0.5 ? 0.65 : 0.0) : 1.0 - offsets.z);
		float halfSize = (isCylinder && cylinder.w > 0.5 ? 1.0 : 0.0);
		// The texture coordinate 1 - v / 256: c + 60 / d below the horizon of the warp and c - 60 / d above it, falling
		// down the view at a rate from the middle of the view of the cylinder
		float warpC = 1.0 - (shift.y + 128.0) / 256.0;
		float cylinderC = 1.0 - (shift.y + 128.0 + 128.0 * halfSize) / 256.0;
		float cylinderRate = (1.0 + halfSize) / viewH;
		// Nearer to the horizon than this, the warp horizon fades at least 98 % into its color, 1 - ((d - 8) / 141)^2, so it's
		// drawn in the color alone, one piece instead of the thinnest bands cut most often (below 8 for none, without the fade)
		float flatDistance = (!isCylinder && fadeOn > 0.5 ? 8.0 + 141.0 * 0.141421 : 0.0);
		float screenSpan = screenRight - screenLeft;

		for (int stage = 0; stage < stageCount; stage++) {
			// The color goes first, under the textured bands that reach into it, or over the textured walk with the tint
			float flatTop = max(horizonRow + 8.5 - flatDistance, visTop);
			float flatBottom = min(horizonRow + flatDistance - 7.5, visBottom);
			if (flatBottom > flatTop && stage == stageCount - 1) {
				strip_position(0, vec2(screenRight, topY + flatTop * scaleY));
				strip_position(1, vec2(screenRight, topY + flatBottom * scaleY));
				strip_position(2, vec2(screenLeft, topY + flatTop * scaleY));
				strip_position(3, vec2(screenLeft, topY + flatBottom * scaleY));
				strip_uv(0, vec2(0.0, 0.0));
				strip_uv(1, vec2(0.0, 0.0));
				strip_uv(2, vec2(0.0, 0.0));
				strip_uv(3, vec2(0.0, 0.0));
				strip_color(0, vec4(horizon.xyz, 1.0));
				strip_color(1, vec4(horizon.xyz, 1.0));
				strip_color(2, vec4(horizon.xyz, 1.0));
				strip_color(3, vec4(horizon.xyz, 1.0));
#ifdef WARP_TINT_IN_VERTEX_COLOR
				submit_strip_shaded(warp, 4);
#else
				submit_strip_shaded(horizonPass, 4);
#endif
			}
			for (int halfIndex = 0; halfIndex < 2; halfIndex++) {
				// The warp: below the horizon, then above it, each from the horizon out, the bands thinner towards the horizon
				// where the depth changes fastest; the cylinder: the upper and the lower half of the view. Either over what is
				// seen of it only.
				float sigma = (halfIndex == 0 ? 1.0 : -1.0);
				float dNear = (halfIndex == 0 ? max(horizonRow + 0.5, visTop) - horizonRow + 7.5 : horizonRow + 8.5 - min(horizonRow + 0.5, visBottom));
				float dFar = (halfIndex == 0 ? visBottom - horizonRow + 7.5 : horizonRow + 8.5 - visTop);
				float rowFrom = max(0.5 * viewH * float(halfIndex), visTop);
				float rowTo = min(0.5 * viewH * float(halfIndex + 1), visBottom);
				bool rising = (!isCylinder && halfIndex == 1);
				// The distances of the warp grow by the same ratio from band to band, so a band is as wide on the screen as it
				// is bent by the depth straightened within it - the 16th or the 32nd root of the whole range, square roots
				float bandRatio = sqrt(sqrt(sqrt(sqrt(max(dFar, 8.0) / max(dNear, 8.0)))));
				if (WARP_BAND_COUNT > 16) {
					bandRatio = sqrt(bandRatio);
				}
				float bandDistance = dNear;
				if (isCylinder ? rowTo > rowFrom : dFar > dNear) {
					for (int band = 0; band < WARP_BAND_COUNT; band++) {
						float fa = float(band) / float(WARP_BAND_COUNT);
						float fb = float(band + 1) / float(WARP_BAND_COUNT);
						// Along the band: the distance of the warp, the row (continuous, 0 at the top of the view) of the cylinder
						float pBandA = (isCylinder ? rowFrom + (rowTo - rowFrom) * fa : bandDistance);
						float pBandB = (isCylinder ? rowFrom + (rowTo - rowFrom) * fb : (band == WARP_BAND_COUNT - 1 ? dFar : bandDistance * bandRatio));
						bandDistance = pBandB;
						// What's in the color alone isn't drawn textured, the bands are spaced as without it
						pBandA = max(pBandA, flatDistance);
						float tvBandA = (isCylinder ? cylinderC - (pBandA - 0.5 - 0.5 * viewH) * cylinderRate : warpC + sigma * 60.0 / pBandA);
						float tvBandB = (isCylinder ? cylinderC - (pBandB - 0.5 - 0.5 * viewH) * cylinderRate : warpC + sigma * 60.0 / pBandB);
						int crossings = int(abs(floor(tvBandB) - floor(tvBandA)));

						for (int part = 0; part < 4; part++) {
							// Cut where the coordinate crosses a whole texture, up to three times - what is left of a band at the
							// horizon, too deep to show anything but noise, stays one piece
							float boundaryStart = (rising ? floor(tvBandA) + float(part) : floor(tvBandA) - float(part - 1));
							float boundaryEnd = (rising ? floor(tvBandA) + 1.0 + float(part) : floor(tvBandA) - float(part));
							bool lastPart = (part >= crossings || part == 3);
							float pA = pBandA;
							float pB = pBandB;
							if (isCylinder) {
								if (part > 0) {
									pA = 0.5 + 0.5 * viewH + (cylinderC - boundaryStart) / cylinderRate;
								}
								if (!lastPart) {
									pB = 0.5 + 0.5 * viewH + (cylinderC - boundaryEnd) / cylinderRate;
								}
							} else {
								if (part > 0) {
									pA = 60.0 / (sigma * (boundaryStart - warpC));
								}
								if (!lastPart) {
									pB = 60.0 / (sigma * (boundaryEnd - warpC));
								}
							}

							if (part <= crossings && pB > pA && pBandB > pBandA) {
								// Both edges of the part: the row on the screen, the coordinates at the left and the right edge
								// of what is seen, and the tint
								float sA = pA;
								float sB = pB;
								float tvA = 0.0;
								float tvB = 0.0;
								float slopeA = 0.0;
								float slopeB = 0.0;
								float uCenter = 0.0;
								float fromA = 0.0;
								float fromB = 0.0;
								if (isCylinder) {
									float cosA = cos(3.14159265 * (pA - 0.5 - horizonRow) / viewH);
									float cosB = cos(3.14159265 * (pB - 0.5 - horizonRow) / viewH);
									slopeA = (halfSize > 0.5 ? 256.0 * (1.0 + 2.0 * cosA) : 128.0 * (2.0 + cosA)) / viewW;
									slopeB = (halfSize > 0.5 ? 256.0 * (1.0 + 2.0 * cosB) : 128.0 * (2.0 + cosB)) / viewW;
									tvA = cylinderC - (pA - 0.5 - 0.5 * viewH) * cylinderRate;
									tvB = cylinderC - (pB - 0.5 - 0.5 * viewH) * cylinderRate;
									uCenter = shift.x * 0.25;
									fromA = (pA - 0.5 - horizonRow) / 141.0;
									fromB = (pB - 0.5 - horizonRow) / 141.0;
								} else {
									sA = (halfIndex == 0 ? pA + horizonRow - 7.5 : horizonRow + 8.5 - pA);
									sB = (halfIndex == 0 ? pB + horizonRow - 7.5 : horizonRow + 8.5 - pB);
									slopeA = 60.0 / pA;
									slopeB = 60.0 / pB;
									tvA = warpC + sigma * slopeA;
									tvB = warpC + sigma * slopeB;
									uCenter = shift.x;
									fromA = (pA - 8.0) / 141.0;
									fromB = (pB - 8.0) / 141.0;
								}
								float tintA = fadeOn * clamp(1.0 - fromA * fromA, 0.0, 1.0);
								float tintB = fadeOn * clamp(1.0 - fromB * fromB, 0.0, 1.0);
								// The upper edge first
								bool swap = (sA > sB);
								float y0 = topY + spanY * (swap ? sB : sA) / viewH;
								float y1 = topY + spanY * (swap ? sA : sB) / viewH;
								float slope0 = (swap ? slopeB : slopeA);
								float slope1 = (swap ? slopeA : slopeB);
								float tv0 = (swap ? tvB : tvA);
								float tv1 = (swap ? tvA : tvB);
								float tint0 = (swap ? tintB : tintA);
								float tint1 = (swap ? tintA : tintB);
								float uLeft0 = (uCenter + (visLeft - 0.5 - centerX) * slope0) / 256.0;
								float uRight0 = (uCenter + (visRight - 0.5 - centerX) * slope0) / 256.0;
								float uLeft1 = (uCenter + (visLeft - 0.5 - centerX) * slope1) / 256.0;
								float uRight1 = (uCenter + (visRight - 0.5 - centerX) * slope1) / 256.0;

								if (stage == 0) {
									// One period of the vertical coordinate for the part, from its middle (a part starts or ends
									// exactly on a boundary)
									float vBase = floor(0.5 * (tv0 + tv1));
									float v0 = tv0 - vBase;
									float v1 = tv1 - vBase;
#ifdef WARP_TEXTURE_REPEAT
									// One strip across the whole part, reduced by whole texture widths (see the old warp below), in
									// columns: the slope of a row changes down the part, which two triangles across the whole row
									// would bend (up to 16 vertices, these targets hold them)
									float uBase = floor(min(uLeft0, uLeft1));
									for (int column = 0; column <= WARP_REPEAT_COLUMNS; column++) {
										float across = float(column) / float(WARP_REPEAT_COLUMNS);
										strip_position(2 * column, vec2(screenLeft + across * screenSpan, y0));
										strip_position(2 * column + 1, vec2(screenLeft + across * screenSpan, y1));
										strip_uv(2 * column, vec2(uLeft0 + across * (uRight0 - uLeft0) - uBase, v0));
										strip_uv(2 * column + 1, vec2(uLeft1 + across * (uRight1 - uLeft1) - uBase, v1));
										strip_color(2 * column, vec4(horizon.xyz, tint0));
										strip_color(2 * column + 1, vec4(horizon.xyz, tint1));
									}
#ifdef WARP_TINT_IN_VERTEX_COLOR
									submit_strip_shaded(warp, 2 * (WARP_REPEAT_COLUMNS + 1));
#else
									submit_strip(warp, 2 * (WARP_REPEAT_COLUMNS + 1));
#endif
#else
									int firstPiece = int(floor(min(uLeft0, uLeft1)));
									int lastPiece = int(ceil(max(uRight0, uRight1)));
									if (lastPiece - firstPiece > 8) {
										// More periods across a row than pieces, only right at the horizon: one strip with the row
										// squeezed into a single period, as much noise as the texture would be there
										strip_position(0, vec2(screenRight, y0));
										strip_position(1, vec2(screenRight, y1));
										strip_position(2, vec2(screenLeft, y0));
										strip_position(3, vec2(screenLeft, y1));
										strip_uv(0, vec2(1.0, v0));
										strip_uv(1, vec2(1.0, v1));
										strip_uv(2, vec2(0.0, v0));
										strip_uv(3, vec2(0.0, v1));
#ifdef WARP_TINT_IN_VERTEX_COLOR
										strip_color(0, vec4(horizon.xyz, tint0));
										strip_color(1, vec4(horizon.xyz, tint1));
										strip_color(2, vec4(horizon.xyz, tint0));
										strip_color(3, vec4(horizon.xyz, tint1));
										submit_strip_shaded(warp, 4);
#else
										submit_strip(warp, 4);
#endif
									} else {
										for (int piece = firstPiece; piece < lastPiece; piece++) {
											// Trapezoids between the whole-texture boundaries, as in the old warp below
											float span0 = uRight0 - uLeft0;
											float span1 = uRight1 - uLeft1;
											float topA = (span0 > 0.0 ? (float(piece) - uLeft0) / span0 : 0.0);
											float topB = (span0 > 0.0 ? (float(piece + 1) - uLeft0) / span0 : 0.0);
											float botA = (span1 > 0.0 ? (float(piece) - uLeft1) / span1 : 0.0);
											float botB = (span1 > 0.0 ? (float(piece + 1) - uLeft1) / span1 : 0.0);
											if (!((topB <= 0.0 && botB <= 0.0) || (topA >= 1.0 && botA >= 1.0))) {
												// In columns, the slope of a row changes down the part, which two triangles across the
												// whole piece would bend (up to 8 vertices, what the PVR holds)
												for (int column = 0; column <= WARP_PIECE_COLUMNS; column++) {
													float across = float(column) / float(WARP_PIECE_COLUMNS);
													float topF = topA + across * (topB - topA);
													float botF = botA + across * (botB - botA);
													strip_position(2 * column, vec2(screenLeft + topF * screenSpan, y0));
													strip_position(2 * column + 1, vec2(screenLeft + botF * screenSpan, y1));
													strip_uv(2 * column, vec2((uLeft0 + topF * span0) - float(piece), v0));
													strip_uv(2 * column + 1, vec2((uLeft1 + botF * span1) - float(piece), v1));
													strip_color(2 * column, vec4(horizon.xyz, tint0));
													strip_color(2 * column + 1, vec4(horizon.xyz, tint1));
												}
#ifdef WARP_TINT_IN_VERTEX_COLOR
												submit_strip_shaded(warp, 2 * (WARP_PIECE_COLUMNS + 1));
#else
												submit_strip(warp, 2 * (WARP_PIECE_COLUMNS + 1));
#endif
											}
										}
									}
#endif
								}

#ifndef WARP_TINT_IN_VERTEX_COLOR
								if (stage == 1 && (tint0 > 0.0 || tint1 > 0.0)) {
									strip_position(0, vec2(screenRight, y0));
									strip_position(1, vec2(screenRight, y1));
									strip_position(2, vec2(screenLeft, y0));
									strip_position(3, vec2(screenLeft, y1));
									strip_color(0, vec4(horizon.xyz, tint0));
									strip_color(1, vec4(horizon.xyz, tint1));
									strip_color(2, vec4(horizon.xyz, tint0));
									strip_color(3, vec4(horizon.xyz, tint1));
									submit_strip_shaded(horizonPass, 4);
								}
#endif
							}
						}
					}
				}
			}
		}
	}
#else
	{
		float correction = (viewSize.y > 0.0 ? (viewSize.x * 9.0) / (viewSize.y * 16.0) : 1.0);
		float shiftU = shift.x / 256.0;
		float shiftV = shift.y / 256.0;

		for (int stage = 0; stage < stageCount; stage++) {
			for (int topHalf = 0; topHalf < 2; topHalf++) {
				float yShift = float(topHalf);
				for (int band = 0; band < 16; band++) {
					float t0 = (float(topHalf) + float(band) / 16.0) * 0.5;
					float t1 = (float(topHalf) + float(band + 1) / 16.0) * 0.5;

					// The vertical coordinate rises monotonically across each half, so a band crosses at
					// most one whole-texture boundary; splitting there keeps every piece inside a period
					// (the boundary curve is found by bisection - 12 halvings pin it well below a texel)
					float split = -1.0;
					float vStart = shiftV + (t0 - yShift) * 1.4 * (1.3 - abs(2.0 * t0 - 1.0));
					float vEnd = shiftV + (t1 - yShift) * 1.4 * (1.3 - abs(2.0 * t1 - 1.0));
					if (floor(vStart) != floor(vEnd)) {
						float boundary = floor(vEnd);
						float lo = t0;
						float hi = t1;
						for (int i = 0; i < 12; i++) {
							float mid = 0.5 * (lo + hi);
							if (shiftV + (mid - yShift) * 1.4 * (1.3 - abs(2.0 * mid - 1.0)) < boundary) {
								lo = mid;
							} else {
								hi = mid;
							}
						}
						split = 0.5 * (lo + hi);
					}

					for (int part = 0; part < 2; part++) {
						float bandT0 = (part == 0 ? t0 : split);
						float bandT1 = (part == 0 ? (split > 0.0 ? split : t1) : t1);
						// A second part only exists when the band was split; degenerate spans are skipped
						if (!((part == 1 && split <= 0.0) || (bandT1 <= bandT0))) {
							float y0 = topY + spanY * bandT0;
							float y1 = topY + spanY * bandT1;

							// Distance-to-horizon term of the shader ("1.3 - abs(2.0 * UV.y - 1.0)") at
							// both band edges; every per-row curve below derives from it
							float d0 = 1.3 - abs(2.0 * bandT0 - 1.0);
							float d1 = 1.3 - abs(2.0 * bandT1 - 1.0);

							if (stage == 0) {
								// One period of the vertical coordinate for the whole band. The period is taken
								// from the band's middle rather than an end, because a band produced by the
								// split above starts exactly on a boundary and an endpoint would pick the
								// neighbouring period.
								float v0 = shiftV + (bandT0 - yShift) * 1.4 * d0;
								float v1 = shiftV + (bandT1 - yShift) * 1.4 * d1;
								float vBase = floor(0.5 * (v0 + v1));
								v0 -= vBase;
								v1 -= vBase;

								float halfSpan0 = 0.5 * (0.5 + 1.5 * d0) * correction;
								float halfSpan1 = 0.5 * (0.5 + 1.5 * d1) * correction;
								float uLeft0 = shiftU - halfSpan0;
								float uRight0 = shiftU + halfSpan0;
								float uLeft1 = shiftU - halfSpan1;
								float uRight1 = shiftU + halfSpan1;

#ifdef WARP_TINT_IN_VERTEX_COLOR
								// The folded tint needs the band's two edge factors before the pieces go out (the
								// separate pass below computes the identical pair after them)
								float tint0 = clamp(d0 * d0 - 0.3, 0.0, 1.0);
								float tint1 = clamp(d1 * d1 - 0.3, 0.0, 1.0);
#endif
#ifdef WARP_TEXTURE_REPEAT
								// One strip across the whole band part; U runs unwrapped from the left edge to the right
								// and the hardware wraps it (see the rdp block of the shader). Reduced by a whole number
								// of texture widths first: the scroll offset grows without bound, and the RDP's texture
								// coordinate is a 10.5 fixed-point number - past 1024 texels (four widths) it wrapped
								// into garbage for the few seconds until the offset passed the next power of two.
								float uBase = floor(shiftU);
								strip_position(0, vec2(leftX + spanX, y0));
								strip_position(1, vec2(leftX + spanX, y1));
								strip_position(2, vec2(leftX, y0));
								strip_position(3, vec2(leftX, y1));
								strip_uv(0, vec2(uRight0 - uBase, v0));
								strip_uv(1, vec2(uRight1 - uBase, v1));
								strip_uv(2, vec2(uLeft0 - uBase, v0));
								strip_uv(3, vec2(uLeft1 - uBase, v1));
#ifdef WARP_TINT_IN_VERTEX_COLOR
								strip_color(0, vec4(horizon.xyz, tint0));
								strip_color(1, vec4(horizon.xyz, tint1));
								strip_color(2, vec4(horizon.xyz, tint0));
								strip_color(3, vec4(horizon.xyz, tint1));
								submit_strip_shaded(warp, 4);
#else
								submit_strip(warp, 4);
#endif
#else
								int firstPiece = int(floor(min(uLeft0, uLeft1)));
								int lastPiece = int(ceil(max(uRight0, uRight1)));
								int emitted = 0;
								for (int piece = firstPiece; piece < lastPiece && emitted < 8; piece++) {
									// Where this period begins and ends along each edge of the band.
									// Deliberately not clamped to the band: a piece at either end reaches past
									// the screen because the row's span differs between the band's edges, and
									// letting it do so keeps the corner exactly on the boundary - so its
									// coordinate is exactly 0 or 1 - while the rasterizer discards whatever
									// falls outside. Clamping instead would pull the coordinate off the
									// boundary and tear the seam open.
									float span0 = uRight0 - uLeft0;
									float span1 = uRight1 - uLeft1;
									float topA = (span0 > 0.0 ? (float(piece) - uLeft0) / span0 : 0.0);
									float topB = (span0 > 0.0 ? (float(piece + 1) - uLeft0) / span0 : 0.0);
									float botA = (span1 > 0.0 ? (float(piece) - uLeft1) / span1 : 0.0);
									float botB = (span1 > 0.0 ? (float(piece + 1) - uLeft1) / span1 : 0.0);
									// Skip pieces wholly off one side of the screen along both edges
									if (!((topB <= 0.0 && botB <= 0.0) || (topA >= 1.0 && botA >= 1.0))) {
										emitted++;
										// Strip order matches the sprite corners: the far edge first, then the
										// near one
										strip_position(0, vec2(leftX + topB * spanX, y0));
										strip_position(1, vec2(leftX + botB * spanX, y1));
										strip_position(2, vec2(leftX + topA * spanX, y0));
										strip_position(3, vec2(leftX + botA * spanX, y1));
										strip_uv(0, vec2((uLeft0 + topB * (uRight0 - uLeft0)) - float(piece), v0));
										strip_uv(1, vec2((uLeft1 + botB * (uRight1 - uLeft1)) - float(piece), v1));
										strip_uv(2, vec2((uLeft0 + topA * (uRight0 - uLeft0)) - float(piece), v0));
										strip_uv(3, vec2((uLeft1 + botA * (uRight1 - uLeft1)) - float(piece), v1));
#ifdef WARP_TINT_IN_VERTEX_COLOR
										// The tint colour and its per-row weight, interpolated down the piece by
										// the rasterizer exactly as the separate gradient pass would have been
										strip_color(0, vec4(horizon.xyz, tint0));
										strip_color(1, vec4(horizon.xyz, tint1));
										strip_color(2, vec4(horizon.xyz, tint0));
										strip_color(3, vec4(horizon.xyz, tint1));
										submit_strip_shaded(warp, 4);
#else
										submit_strip(warp, 4);
#endif
									}
								}

#endif
							}

#ifndef WARP_TINT_IN_VERTEX_COLOR
							if (stage == 1) {
								// Horizon tint of the band: the shader's clamp(distance^1.5 - 0.3, ...) at each
								// edge (squared here - the SOFTWARE_RENDERER approximation), interpolated by
								// the rasterizer down the band
								float alpha0 = clamp(d0 * d0 - 0.3, 0.0, 1.0);
								float alpha1 = clamp(d1 * d1 - 0.3, 0.0, 1.0);
								if (alpha0 > 0.0 || alpha1 > 0.0) {
									strip_position(0, vec2(leftX + spanX, y0));
									strip_position(1, vec2(leftX + spanX, y1));
									strip_position(2, vec2(leftX, y0));
									strip_position(3, vec2(leftX, y1));
									strip_color(0, vec4(horizon.xyz, alpha0));
									strip_color(1, vec4(horizon.xyz, alpha1));
									strip_color(2, vec4(horizon.xyz, alpha0));
									strip_color(3, vec4(horizon.xyz, alpha1));
									submit_strip_shaded(horizonPass, 4);
								}
							}
#endif
						}
					}
				}
			}
		}
	}
#endif
}
