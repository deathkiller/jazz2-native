// Console rebuild of the TexturedBackground warp out of geometry, shared by the
// void fixed_function(pvr) AND void fixed_function(gx) blocks of TexturedBackground.shader and
// TexturedBackgroundCircle.shader (the tunnel keeps the same planar approximation on this tier,
// exactly as the handwritten backend code did).
//
// Everything below is portable extended vocabulary (the strip builder, the pre-clip quad axes and
// resolved uniforms), so the band geometry is literally the same code on both consoles. The one
// place they differ is how the horizon tint is delivered, and the difference is a capability, not a
// policy: a block that defines WARP_TINT_IN_VERTEX_COLOR before including this file asks for the
// tint to ride in the band pieces' own per-vertex colours, folded into their draw by the TINT_MIX
// combiner stage. The PVR has no texture environment that can lerp a texel toward a colour, so it
// leaves the macro undefined and gets the tint as a second, alpha-blended gradient pass over each
// band instead - the two produce the same pixels (`mix(texel, tint, a)` either way), the folded
// form just costs one draw per band instead of two and never leaves the textured vertex format.
//
// The warp's mapping is affine along every screen row - texturePos.x is linear in UV.x for a fixed
// row and texturePos.y depends on UV.y alone - so a stack of horizontal bands carrying
// interpolated texture coordinates reproduces it. The horizon tint is likewise a per-row value, so
// it goes on as a second pass whose vertex alpha the rasterizer interpolates down the band.
//
// The shader relies on the texture repeating: the warp spans roughly two periods across the
// screen. The source here is a render target, which the tile accelerator writes as a linear
// surface that the hardware cannot tile, so instead each band is cut at every whole-texture
// boundary and the pieces are emitted with coordinates inside a single period. A cut follows the
// line where the coordinate crosses the boundary, which is slanted (the row's span widens toward
// the horizon) - so the pieces are trapezoids rather than columns, and their corners land exactly
// on the boundary. That is exact, not an approximation: the shader's mapping is affine across each
// row and so is the interpolation across each trapezoid.
//
// The two curve approximations are the SOFTWARE_RENDERER branch above (distance for
// pow(distance, 1.4), distance squared for pow(distance, 1.5)), which is what every tier without
// real shaders already draws; it drops the per-pixel star field along with them.

if (!has_uniform(uViewSize) || !has_uniform(uShift) || !has_uniform(uHorizonColor)) {
	// Without the resolved uniforms there is no warp to rebuild - a plain textured quad keeps the
	// background visible
	pass fallback;
	submit_quad(fallback);
} else {
	vec2 viewSize = uniform_vec2(uViewSize);
	vec2 shift = uniform_vec2(uShift);
	vec4 horizon = uniform_vec4(uHorizonColor);

	float correction = (viewSize.y > 0.0 ? (viewSize.x * 9.0) / (viewSize.y * 16.0) : 1.0);
	float shiftU = shift.x / 256.0;
	float shiftV = shift.y / 256.0;
	// The band geometry comes from the transformed sprite axes rather than the corner array, so
	// scissor clipping of the quad cannot distort the warp
	float leftX = quad_origin().x;
	float spanX = quad_axis_x().x;
	float topY = quad_origin().y;
	float spanY = quad_axis_y().y;

	pass warp;				// Textured band pieces under the material state
#ifdef WARP_TINT_IN_VERTEX_COLOR
	// One combiner stage lerps each texel toward the per-vertex tint by the per-vertex alpha, so the
	// band piece IS the tinted band; nothing is drawn over it
	warp.tev = TINT_MIX;
#else
	pass horizonPass;		// Flat horizon tint, interpolated down each band
	horizonPass.blend = ALPHA;
#endif

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

#ifndef WARP_TINT_IN_VERTEX_COLOR
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
#endif
				}
			}
		}
	}
}
