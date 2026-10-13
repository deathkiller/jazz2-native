using System;
using System.Collections.Generic;

// The solid foreground of the level, painted as one cardboard cut-out and then cut into tiles: every tile is a crop of
// the same picture, so neighbours always connect, slopes included, and the collision mask is the shape itself
public static partial class LevelGen
{
	public enum Mat : byte { None = 0, Grass, Street, Stage, Plank, Wood, Cloud, Crate, Castle }

	public class Terrain
	{
		public readonly int W, H, PW, PH;		// In tiles and in pixels
		public readonly double[] Height;		// Surface height of the ground at every pixel column, NaN = none
		public readonly Mat[] Ground;			// Material of the ground in every tile column
		public byte[] Grid;						// Material of every pixel
		public byte[] Dist, Up, Down, Side;		// Distances to the empty space: any direction, up, down, sideways
		public readonly List<double[]> Boxes = new List<double[]>();	// x0, y0, x1, y1, radius, material (pixels)
		public readonly List<double[]> Holes = new List<double[]>();	// x0, y0, x1, y1, radius
		public readonly List<object[]> Polys = new List<object[]>();	// double[] x, y pairs (pixels), material
		public LevelCanvas Stamps;				// Box print on the bare card of the ground
		public readonly List<int[]> Tacks = new List<int[]>();	// Columns x0-x1 with thumbtacks spilled on the ground

		public Terrain(int w, int h)
		{
			W = w; H = h; PW = w * T; PH = h * T;
			Height = new double[PW]; Ground = new Mat[w];
			for (int i = 0; i < PW; i++) Height[i] = double.NaN;
			for (int i = 0; i < w; i++) Ground[i] = Mat.Grass;
		}

		// Ground from column x0 to x1 (inclusive), its surface going from h0 to h1 pixels from the top of the level
		// between the left edge of x0 and the right edge of x1, along `shape` (0-1 to 0-1)
		void Shape(int x0, int x1, double h0, double h1, Mat m, Func<double, double> shape)
		{
			int px0 = x0 * T, px1 = (x1 + 1) * T;
			for (int x = px0; x < px1; x++) Height[x] = Mix(h0, h1, shape((x + 0.5 - px0) / (px1 - px0)));
			for (int x = x0; x <= x1; x++) Ground[x] = m;
		}
		// A straight slope
		public void Span(int x0, int x1, double h0, double h1, Mat m) { Shape(x0, x1, h0, h1, m, f => f); }
		// An S-shaped slope: it eases in and out, steepest in its middle (1.5 times the average slope)
		public void Curve(int x0, int x1, double h0, double h1, Mat m) { Shape(x0, x1, h0, h1, m, f => f * f * (3 - 2 * f)); }
		// A rounded hump of height `h` over a base at `h0`
		public void Hump(int x0, int x1, double h0, double h, Mat m) { Shape(x0, x1, h0, h0 - h, m, f => 0.5 - 0.5 * Math.Cos(f * 2 * Math.PI)); }
		public void Flat(int x0, int x1, double h, Mat m) { Shape(x0, x1, h, h, m, f => 0); }
		public void Gap(int x0, int x1) { for (int x = x0 * T; x < (x1 + 1) * T; x++) Height[x] = double.NaN; }
		public void Box(double x0, double y0, double x1, double y1, double r, Mat m) { Boxes.Add(new double[] { x0, y0, x1, y1, r, (double)m }); }
		public void Hole(double x0, double y0, double x1, double y1, double r) { Holes.Add(new double[] { x0, y0, x1, y1, r }); }
		// A solid polygon, its corners as x, y pairs in pixels, clockwise or not
		public void Poly(Mat m, params double[] xy) { Polys.Add(new object[] { xy, m }); }

		public double SurfaceAt(double gx)
		{
			return Height[Math.Max(0, Math.Min(PW - 1, (int)Math.Floor(gx)))];
		}

		static bool InRoundBox(double px, double py, double[] b)
		{
			double r = b[4];
			double cx = Math.Max(b[0] + r, Math.Min(b[2] - r, px)), cy = Math.Max(b[1] + r, Math.Min(b[3] - r, py));
			double dx = px - cx, dy = py - cy;
			return px >= b[0] && px < b[2] && py >= b[1] && py < b[3] && dx * dx + dy * dy <= r * r;
		}

		static bool InPoly(double px, double py, double[] xy)
		{
			bool inside = false;
			int n = xy.Length / 2;
			for (int i = 0, j = n - 1; i < n; j = i++) {
				double xi = xy[i * 2], yi = xy[i * 2 + 1], xj = xy[j * 2], yj = xy[j * 2 + 1];
				if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
			}
			return inside;
		}
		public Mat At(int x, int y)
		{
			if (y < 0) return Mat.None;
			x = Math.Max(0, Math.Min(PW - 1, x));
			y = Math.Min(PH - 1, y);
			return (Mat)Grid[y * PW + x];
		}

		public void Build()
		{
			Grid = new byte[PW * PH];
			for (int x = 0; x < PW; x++) {
				double h = Height[x];
				if (double.IsNaN(h)) continue;
				for (int y = Math.Max(0, (int)Math.Ceiling(h - 0.5)); y < PH; y++) Grid[y * PW + x] = (byte)Ground[x / T];
			}
			foreach (double[] b in Holes) {
				for (int y = Math.Max(0, (int)b[1]); y < Math.Min(PH, (int)Math.Ceiling(b[3])); y++)
					for (int x = Math.Max(0, (int)b[0]); x < Math.Min(PW, (int)Math.Ceiling(b[2])); x++)
						if (InRoundBox(x + 0.5, y + 0.5, b)) Grid[y * PW + x] = 0;
			}
			foreach (object[] p in Polys) {
				double[] xy = (double[])p[0];
				double minX = double.MaxValue, minY = double.MaxValue, maxX = double.MinValue, maxY = double.MinValue;
				for (int i = 0; i < xy.Length; i += 2) { minX = Math.Min(minX, xy[i]); maxX = Math.Max(maxX, xy[i]); minY = Math.Min(minY, xy[i + 1]); maxY = Math.Max(maxY, xy[i + 1]); }
				for (int y = Math.Max(0, (int)minY); y < Math.Min(PH, (int)Math.Ceiling(maxY)); y++)
					for (int x = Math.Max(0, (int)minX); x < Math.Min(PW, (int)Math.Ceiling(maxX)); x++)
						if (InPoly(x + 0.5, y + 0.5, xy)) Grid[y * PW + x] = (byte)(Mat)p[1];
			}
			foreach (double[] b in Boxes) {
				for (int y = Math.Max(0, (int)b[1]); y < Math.Min(PH, (int)Math.Ceiling(b[3])); y++)
					for (int x = Math.Max(0, (int)b[0]); x < Math.Min(PW, (int)Math.Ceiling(b[2])); x++)
						if (InRoundBox(x + 0.5, y + 0.5, b)) Grid[y * PW + x] = (byte)b[5];
			}

			const int Cap = 24;
			Dist = new byte[PW * PH]; Up = new byte[PW * PH]; Down = new byte[PW * PH]; Side = new byte[PW * PH];
			// Runs of solid pixels up, down and sideways to the nearest empty one
			for (int x = 0; x < PW; x++) {
				int run = 0;
				for (int y = 0; y < PH; y++) { run = (Grid[y * PW + x] == 0 ? 0 : Math.Min(Cap, run + 1)); Up[y * PW + x] = (byte)run; }
				run = Cap;	// The bottom of the level is solid ground going on
				for (int y = PH - 1; y >= 0; y--) { run = (Grid[y * PW + x] == 0 ? 0 : Math.Min(Cap, run + 1)); Down[y * PW + x] = (byte)run; }
			}
			for (int y = 0; y < PH; y++) {
				byte[] left = new byte[PW];
				int run = Cap;
				for (int x = 0; x < PW; x++) { run = (Grid[y * PW + x] == 0 ? 0 : Math.Min(Cap, run + 1)); left[x] = (byte)run; }
				run = Cap;
				for (int x = PW - 1; x >= 0; x--) { run = (Grid[y * PW + x] == 0 ? 0 : Math.Min(Cap, run + 1)); Side[y * PW + x] = (byte)Math.Min(run, left[x]); }
			}
			// Distance in any direction, a chamfer approximation of the Euclidean one, in tenths of a pixel
			int[] d = new int[PW * PH];
			const int Inf = Cap * 10;
			for (int i = 0; i < d.Length; i++) d[i] = (Grid[i] == 0 ? 0 : Inf);
			for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++) {
				int i = y * PW + x; if (d[i] == 0) continue;
				int v = d[i];
				if (x > 0) v = Math.Min(v, d[i - 1] + 10);
				if (y > 0) { v = Math.Min(v, d[i - PW] + 10); if (x > 0) v = Math.Min(v, d[i - PW - 1] + 14); if (x < PW - 1) v = Math.Min(v, d[i - PW + 1] + 14); }
				d[i] = v;
			}
			for (int y = PH - 1; y >= 0; y--) for (int x = PW - 1; x >= 0; x--) {
				int i = y * PW + x; if (d[i] == 0) continue;
				int v = d[i];
				if (x < PW - 1) v = Math.Min(v, d[i + 1] + 10);
				if (y < PH - 1) { v = Math.Min(v, d[i + PW] + 10); if (x < PW - 1) v = Math.Min(v, d[i + PW + 1] + 14); if (x > 0) v = Math.Min(v, d[i + PW - 1] + 14); }
				d[i] = v;
			}
			// Stored in pixels, rounded up, so 1 is a pixel on the very edge
			for (int i = 0; i < d.Length; i++) Dist[i] = (byte)Math.Min(Cap, (d[i] + 9) / 10);
		}
	}

	// ---- Cardboard materials ----
	// The foreground is made of corrugated card: raw kraft for the ground, painted card for the grass, the sidewalk and
	// the stage, and strips of board for the planks. Every texture repeats every 64 pixels, so the tiles repeat too
	static readonly double[] GrassPaint = C(108, 168, 74), GrassPaintLight = C(146, 196, 98);
	static readonly double[] KraftA = C(196, 150, 98), KraftB = C(186, 140, 90);
	static readonly double[] SidewalkPaint = C(190, 188, 182), StreetPaintA = C(108, 110, 120), StreetPaintB = C(100, 102, 112);
	static readonly double[] BoardPaintA = C(178, 112, 66), BoardPaintB = C(164, 102, 60), StagePaintA = C(74, 58, 82), StagePaintB = C(66, 52, 74);

	// Card in the foreground, see PaintedCard(): the same, but repeating every 64 pixels
	static double[] TerrainCard(double[] paint, int gx, int gy, int dist, int seed, bool worn)
	{
		int x = Mod(gx, 64), y = Mod(gy, 64);
		double k = 1 + (Noise(x, y, 16, 64, seed) - 0.5) * 0.1 + 0.03 * Math.Sin(2 * Math.PI * x / 8.0) + (Hash(x, y, seed + 1) - 0.5) * 0.05;
		double[] c = Scale(paint, k);
		if (worn && dist - 3 <= 3 && Noise(x, y, 3, 64, seed + 2) > 0.6 + (dist - 3) * 0.08) c = MixC(c, Kraft, 0.5);
		return c;
	}

	// How deep the top band of a material is (grass, sidewalk, boards) at a given column, scalloped for the grass
	static double BandDepth(Mat m, int gx)
	{
		switch (m) {
			case Mat.Grass: return 15 + 3 * Math.Sin(2 * Math.PI * gx / 32.0) + 1.5 * Math.Sin(2 * Math.PI * gx / 16.0 + 1);
			case Mat.Street: return 14;
			case Mat.Stage: return 16;
			default: return 0;
		}
	}

	// The body of a material, below its top band: big sheets of board glued on top of each other, each sheet's lower
	// edge a cut edge with its shadow on the sheet under it
	static double[] BodyColor(Mat m, int gx, int gy, int dist)
	{
		// The ground is torn into wavy sheets, the street and the stage are cut straight
		double wave = (m == Mat.Grass ? 3 * Math.Sin(2 * Math.PI * gx / 64.0) : 0);
		double layer = (gy + wave) / 32.0;
		bool odd = (((int)Math.Floor(layer)) & 1) != 0;
		double f = Frac(layer) * 32;
		double[] c;
		switch (m) {
			case Mat.Street: c = TerrainCard(odd ? StreetPaintB : StreetPaintA, gx, gy, dist, 31, true); break;
			case Mat.Stage: c = TerrainCard(odd ? StagePaintB : StagePaintA, gx, gy, dist, 33, true); break;
			default: c = TerrainCard(odd ? KraftB : KraftA, gx, gy, dist, 35, false); break;
		}
		bool painted = (m != Mat.Grass);
		if (f < 2) return painted ? Scale(c, 1.12) : Scale(Kraft, Mod(gx, 4) < 2 ? 0.98 : 0.84);	// The cut edge of the sheet above
		if (f < 2.8) return painted ? Scale(c, 0.7) : Scale(Kraft, 0.62);
		if (f < 7) c = Scale(c, 0.72 + 0.28 * Smooth(2.8, 7, f));			// Its shadow
		return c;
	}

	// The color of one solid pixel of the foreground
	static uint TerrainPixel(Terrain t, int gx, int gy)
	{
		int i = gy * t.PW + gx;
		Mat m = (Mat)t.Grid[i];
		int dist = t.Dist[i], up = t.Up[i], down = t.Down[i], side = t.Side[i];
		double band = BandDepth(m, gx);
		bool inBand = (band > 0 && up <= band && up < 24);
		int mx = Mod(gx, 64), my = Mod(gy, 64);

		// Where two different pieces meet, and the rim of every piece: a cut edge
		Mat r = t.At(gx + 1, gy), b = t.At(gx, gy + 1);
		if ((r != Mat.None && r != m) || (b != Mat.None && b != m)) return CutEdge(mx, my, 1) | 0xFF000000u;
		if (dist <= 3) return CutEdge(mx, my, dist) | 0xFF000000u;

		double[] c;
		if (m == Mat.Castle) {
			// Painted stone: grey card with the joints of its blocks drawn on
			int row = Mod(gy, 32) / 16, bx = Mod(gx + row * 16, 32), by = Mod(gy, 16);
			c = TerrainCard(row == 0 ? C(152, 150, 166) : C(144, 142, 158), gx, gy, dist, 39, true);
			if (bx < 2 || by < 2) c = Scale(c, 0.68);
			else if (by < 4) c = Scale(c, 1.08);
		} else if (m == Mat.Crate) {
			// A cardboard box: plain kraft, a strip of packing tape along the top of its side, the flaps' seam above it
			c = TerrainCard(C(204, 160, 104), gx, gy, dist, 29, false);
			if (up >= 8 && up <= 15) c = MixC(c, C(178, 128, 72), 0.7);
			if (up == 9) c = Scale(c, 1.15);
			if (down <= 6) c = Scale(c, 0.9);
		} else if (m == Mat.Cloud) {
			// White painted card, its underside in a blue shade
			c = TerrainCard(C(240, 244, 250), gx, gy, dist, 27, false);
			if (down <= 7) c = MixC(c, C(196, 210, 236), 0.6);
			else if (up <= 4) c = Scale(c, 1.02);
		} else if (m == Mat.Plank) {
			// A strip of corrugated board seen from its cut side: the two liners and the fluted sheet between them
			double flute = 6 + 2.2 * Math.Sin(2 * Math.PI * gx / 8.0);
			if (up <= 5) c = Scale(Kraft, 1.04);
			else if (down <= 5) c = Scale(Kraft, 0.92);
			else if (Math.Abs(up - flute) < 1.3) c = Scale(Kraft, 0.98);
			else c = Scale(KraftDark, 0.82);
			// A strip of packing tape across it every so often
			int u = Mod(gx, 64);
			if (u >= 24 && u < 38) c = MixC(c, C(196, 150, 92), 0.55);
		} else if (inBand) {
			if (up > band - 2.5) return CutEdge(mx, my, 2) | 0xFF000000u;	// The rim of the band's piece
			switch (m) {
				case Mat.Grass:
					c = TerrainCard(up <= 7 ? GrassPaintLight : GrassPaint, gx, gy, dist, 21, true);
					// Brush strokes of lighter paint
					if (up > 7 && Mod(mx * 3 + up * 2, 29) < 2) c = MixC(c, GrassPaintLight, 0.5);
					break;
				case Mat.Street:
					c = TerrainCard(SidewalkPaint, gx, gy, dist, 23, true);
					if (Mod(gx, 32) <= 1) return CutEdge(mx, my, 2) | 0xFF000000u;
					if (up <= 5) c = Scale(c, 1.06);
					break;
				default: {
					// Boards in two rows, each row's joints half a board from the other's
					int row = up <= 8 ? 0 : 1;
					int u = Mod(gx + row * 16, 32);
					c = TerrainCard((Mod(gx + row * 16, 64) < 32) ? BoardPaintA : BoardPaintB, gx, gy, dist, 25, true);
					if (u <= 1 || up == 8 || up == 9) c = Scale(c, 0.55);
					else if (up <= 5) c = Scale(c, 1.08);
					break;
				}
			}
		} else {
			c = BodyColor(m, gx, gy, dist);
			// The shadow the band's piece casts on the body under it
			if (band > 0 && up < band + 6) c = Scale(c, 0.7 + 0.3 * Smooth(band, band + 6, up));
		}
		// The face bends a little towards its rim: lit from the top left
		if (dist <= 6) {
			bool faceDown = down < up, faceRight = side < 8 && t.At(gx + side, gy) == Mat.None;
			c = Scale(c, (faceDown || faceRight) ? 0.9 : 1.06);
		}
		// Box print stamped on the bare card
		if (!inBand && m == Mat.Grass && t.Stamps != null) {
			uint s = t.Stamps.Get(gx, gy);
			if ((s >> 24) != 0 && dist > 5) {
				double a = (s >> 24) / 255.0 * 0.62;
				c = MixC(c, C(s & 0xFF, (s >> 8) & 0xFF, (s >> 16) & 0xFF), a);
			}
		}
		return Rgb(c);
	}

	// The paper grass tips of a grass surface: (center, half width, height) of the tips in every 32 pixels
	static readonly double[,] GrassTips = { { 3, 4, 7 }, { 10.5, 4.5, 9 }, { 18, 3.5, 6 }, { 25.5, 4.5, 8 } };

	// What is drawn in the empty space next to the foreground: tips of grass cut out of the same painted card above a
	// grass surface (outside the collision mask), and the soft shadow the foreground casts on what is behind it
	static uint FringePixel(Terrain t, int gx, int gy)
	{
		int below = 0;
		while (below < 10 && t.At(gx, gy + below + 1) == Mat.None) below++;
		if (below < 10 && t.At(gx, gy + below + 1) == Mat.Grass) {
			double u = Mod(gx, 32) + 0.5, h = below + 0.5;
			for (int i = 0; i < 4; i++) {
				double dx = Math.Abs(u - GrassTips[i, 0]) / GrassTips[i, 1];
				if (dx >= 1) continue;
				double tip = GrassTips[i, 2] * (1 - dx);
				if (h > tip) continue;
				bool edge = h > tip - 1.6 || (1 - dx) * GrassTips[i, 1] < 1.6;
				return edge ? CutEdge(Mod(gx, 64), Mod(gy, 64), 2) | 0xFF000000u : Rgb(TerrainCard(GrassPaintLight, gx, gy, 9, 21, false));
			}
		}
		// Thumbtacks spilled on the ground, their pins up
		foreach (int[] s in t.Tacks) {
			if (gx < s[0] * T || gx >= (s[1] + 1) * T) continue;
			double h = t.SurfaceAt(gx + 0.5) - gy - 0.5;
			if (h < 0 || h > 16) break;
			double u = Mod(gx, 16) + 0.5 - 8;
			if (h < 4.5 && Math.Abs(u) < 7 - h * 0.4) {
				double[][] heads = { C(206, 62, 58), C(70, 112, 200), C(228, 188, 64) };
				double[] head = heads[Mod(gx / 16, 3)];
				return Math.Abs(u) > 5.6 - h * 0.4 || h > 3.4 ? CutEdge(Mod(gx, 64), Mod(gy, 64), 1) | 0xFF000000u : Rgb(TerrainCard(head, gx, gy, 9, 37, false));
			}
			double pin = 1.8 * (1 - h / 16);
			if (h >= 4 && Math.Abs(u) < pin) return Math.Abs(u) < pin - 0.9 ? Rgb(214, 220, 230) : Rgb(120, 126, 140);
			break;
		}
		int cover = 0;
		for (int k = 2; k <= 7; k++) if (t.At(gx - k + 1, gy - k - 1) != Mat.None) cover++;
		return cover > 0 ? Rgba(36, 22, 14, 14 * cover) : 0;
	}
	// One tile of the foreground, with its collision mask or without it (for a copy of it on another layer)
	public static Tile TerrainTile(Terrain t, int tx, int ty, bool withMask)
	{
		Tile tile = new Tile();
		for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
			int gx = tx * T + x, gy = ty * T + y;
			if (t.Grid[gy * t.PW + gx] != 0) {
				tile.Set(x, y, TerrainPixel(t, gx, gy));
				if (withMask) tile.SetMask(x, y);
			} else {
				tile.Set(x, y, FringePixel(t, gx, gy));
			}
		}
		return tile;
	}

	// Cuts the foreground into tiles: the sprite layer's tile IDs, and the collision masks with them
	public static int[,] SliceTerrain(Terrain t)
	{
		int[,] ids = new int[t.W, t.H];
		for (int ty = 0; ty < t.H; ty++) for (int tx = 0; tx < t.W; tx++) ids[tx, ty] = AddTile(TerrainTile(t, tx, ty, true));
		return ids;
	}
}
