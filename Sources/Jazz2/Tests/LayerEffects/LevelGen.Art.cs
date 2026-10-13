using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;

// The art of everything but the solid foreground: cardboard props, the facades and their halftone x-ray frames, water,
// glass, lights and the background
public static partial class LevelGen
{
	// ---- Canvases ----

	// An image in the level's coordinates, stored by tile so only the tiles drawn on take memory
	public class LevelCanvas
	{
		public readonly Dictionary<int, uint[]> Chunks = new Dictionary<int, uint[]>();
		public readonly int W, H;		// In tiles
		public LevelCanvas(int w, int h) { W = w; H = h; }

		public uint Get(int x, int y)
		{
			if (x < 0 || y < 0 || x >= W * T || y >= H * T) return 0;
			uint[] c;
			return Chunks.TryGetValue((y / T) * W + x / T, out c) ? c[(y % T) * T + x % T] : 0;
		}
		public void Blend(int x, int y, uint px)
		{
			if ((px >> 24) == 0 || x < 0 || y < 0 || x >= W * T || y >= H * T) return;
			int k = (y / T) * W + x / T;
			uint[] c;
			if (!Chunks.TryGetValue(k, out c)) { c = new uint[T * T]; Chunks[k] = c; }
			int i = (y % T) * T + x % T;
			c[i] = Over(px, c[i]);
		}
		public void Draw(Img img, int x, int y) { for (int py = 0; py < img.H; py++) for (int px = 0; px < img.W; px++) Blend(x + px, y + py, img.Px[py * img.W + px]); }
		public Tile TileAt(int tx, int ty)
		{
			Tile t = new Tile();
			uint[] c;
			if (Chunks.TryGetValue(ty * W + tx, out c)) Array.Copy(c, t.Px, T * T);
			return t;
		}
	}

	// A small image, of a prop or a frame of one
	public class Img
	{
		public readonly int W, H;
		public readonly uint[] Px;
		public Img(int w, int h) { W = w; H = h; Px = new uint[w * h]; }
		public uint Get(int x, int y) { return (x < 0 || y < 0 || x >= W || y >= H) ? 0 : Px[y * W + x]; }
		public void Set(int x, int y, uint c) { if (x >= 0 && y >= 0 && x < W && y < H) Px[y * W + x] = c; }
		public void Blend(int x, int y, uint c) { if (x >= 0 && y >= 0 && x < W && y < H) Px[y * W + x] = Over(c, Px[y * W + x]); }
		// Draws another image over this one; opaque pixels replace what is under them as they are, so the alpha that
		// marks a cut edge (see EdgeAlpha) survives
		public void Draw(Img o, int x, int y)
		{
			for (int py = 0; py < o.H; py++) for (int px = 0; px < o.W; px++) {
				uint c = o.Px[py * o.W + px];
				if ((c >> 24) >= 250) Set(x + px, y + py, c);
				else Blend(x + px, y + py, c);
			}
		}
		public Img Clone() { Img c = new Img(W, H); Array.Copy(Px, c.Px, Px.Length); return c; }
		public Img Map(Func<int, int, uint, uint> f) { Img c = new Img(W, H); for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) c.Px[y * W + x] = f(x, y, Px[y * W + x]); return c; }

		// Draws with GDI+, without antialiasing, so every color stays exact (the key colors below rely on it)
		public static Img Gdi(int w, int h, Action<Graphics> draw)
		{
			using (Bitmap bmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb)) {
				using (Graphics g = Graphics.FromImage(bmp)) {
					g.SmoothingMode = SmoothingMode.None;
					g.PixelOffsetMode = PixelOffsetMode.Half;
					g.TextRenderingHint = TextRenderingHint.SingleBitPerPixelGridFit;
					g.Clear(System.Drawing.Color.Transparent);
					draw(g);
				}
				Img img = new Img(w, h);
				var data = bmp.LockBits(new Rectangle(0, 0, w, h), System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
				int[] raw = new int[w * h];
				System.Runtime.InteropServices.Marshal.Copy(data.Scan0, raw, 0, raw.Length);
				bmp.UnlockBits(data);
				for (int i = 0; i < raw.Length; i++) {
					uint v = (uint)raw[i];	// BGRA in memory, so ARGB as an integer
					uint a = v >> 24, r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
					img.Px[i] = (a < 128 ? 0 : (r | (g << 8) | (b << 16) | 0xFF000000u));
				}
				return img;
			}
		}
	}

	public static System.Drawing.Color Gc(double[] c) { return System.Drawing.Color.FromArgb(Clamp(c[0]), Clamp(c[1]), Clamp(c[2])); }
	public static System.Drawing.Color Gc(int r, int g, int b) { return System.Drawing.Color.FromArgb(r, g, b); }
	public static SolidBrush Br(double[] c) { return new SolidBrush(Gc(c)); }
	public static SolidBrush Br(int r, int g, int b) { return new SolidBrush(Gc(r, g, b)); }

	// Key colors: drawn as exact colors and turned into what they stand for afterwards
	// Glass = a reflective pane; bulb = a marquee bulb that chases; neon = a lit tube; line = printed dark line; frost =
	// a hole in the facade where a frosted pane is (the frost itself is the blur layer's)
	public static readonly double[] KeyGlass = C(1, 2, 3), KeyBulb = C(2, 3, 4), KeyNeon = C(3, 4, 5), KeyLine = C(4, 5, 6), KeyFrost = C(5, 6, 7);
	public static bool IsKey(uint c, double[] k) { return (c >> 24) != 0 && (c & 0xFF) == (uint)k[0] && ((c >> 8) & 0xFF) == (uint)k[1] && ((c >> 16) & 0xFF) == (uint)k[2]; }
	static bool IsAnyKey(uint c) { return IsKey(c, KeyGlass) || IsKey(c, KeyBulb) || IsKey(c, KeyNeon) || IsKey(c, KeyLine) || IsKey(c, KeyFrost); }

	public static GraphicsPath RoundRect(float x, float y, float w, float h, float r)
	{
		GraphicsPath p = new GraphicsPath();
		float d = r * 2;
		p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90);
		p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90);
		p.CloseFigure();
		return p;
	}

	public static void Text(Graphics g, string s, string font, float size, double[] color, float cx, float cy, FontStyle style = FontStyle.Bold)
	{
		using (Font f = new Font(font, size, style, GraphicsUnit.Pixel))
		using (StringFormat sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center }) {
			g.DrawString(s, f, Br(color), cx, cy, sf);
		}
	}

	// ---- The cardboard look ----

	// The raw card a cut edge shows, and the color of the shadow a piece casts
	public static readonly double[] Kraft = C(212, 168, 114), KraftDark = C(132, 92, 56);
	// Alpha of the pixels of a cut edge: opaque for all purposes, but recognizable (see Halftone())
	public const uint EdgeAlpha = 254;

	// The cut edge of a piece of corrugated card seen at the very rim of its face: raw kraft, the flutes inside it
	// showing as short dashes along it, a darker line at the outermost pixel
	public static uint CutEdge(int x, int y, int dist)
	{
		double[] c = Kraft;
		if (dist <= 1) c = Scale(Kraft, 0.6);
		else if (Mod(x + y, 4) < 2) c = Scale(Kraft, 1.05);
		else c = Scale(Kraft, 0.84);
		double fiber = (Hash(x, y, 77) - 0.5) * 8;
		return Rgba(c[0] + fiber, c[1] + fiber, c[2] + fiber, EdgeAlpha);
	}

	// Paint on card: a little uneven, with the faint ribs of the flutes under the liner and the fibers of the paper,
	// worn through to the kraft here and there near the edges
	// (`fromEdge` is how far the pixel is from the cut edge)
	public static double[] PaintedCard(double[] paint, int x, int y, int fromEdge, int seed)
	{
		double k = 1 + (Noise(x, y, 16, 128, seed) - 0.5) * 0.1 + 0.035 * Math.Sin(2 * Math.PI * x / 7.0) + (Hash(x, y, seed + 1) - 0.5) * 0.05;
		double[] c = Scale(paint, k);
		if (fromEdge <= 3 && Noise(x, y, 3, 64, seed + 2) > 0.6 + fromEdge * 0.08) c = MixC(c, Kraft, 0.5);
		return c;
	}

	// Turns flat shapes into a piece of painted corrugated card: its paint gets the texture of card, its rim the cut
	// edge, its face a slight bend lit from the top left, and it casts a soft shadow down and to the right on whatever
	// is under it. Key colors are kept as they are. The shapes need a margin of about 8 pixels for the shadow
	public static Img Cardboard(Img src, int edge = 3, bool shadow = true, int seed = 1)
	{
		int w = src.W, h = src.H;
		Img o = new Img(w, h);
		int reach = edge + 3;
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			uint c = src.Px[y * w + x];
			if ((c >> 24) == 0) {
				if (shadow) {
					int cover = 0;
					for (int k = 2; k <= 7; k++) if ((src.Get(x - k + 1, y - k - 1) >> 24) != 0) cover++;
					if (cover > 0) o.Px[y * w + x] = Rgba(36, 22, 14, 14 * cover);
				}
				continue;
			}
			if (IsAnyKey(c)) { o.Px[y * w + x] = c; continue; }
			// The distance to the outside, and on which side the outside is
			int dist = reach + 1, sx = 0, sy = 0;
			for (int dy = -reach; dy <= reach; dy++) for (int dx = -reach; dx <= reach; dx++) {
				if ((src.Get(x + dx, y + dy) >> 24) != 0) continue;
				int d = (int)Math.Ceiling(Math.Sqrt(dx * dx + dy * dy) - 0.3);
				if (d < dist) { dist = d; sx = dx; sy = dy; }
			}
			if (dist <= edge) { o.Px[y * w + x] = CutEdge(x, y, dist); continue; }
			double[] col = PaintedCard(C(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF), x, y, dist - edge, seed);
			if (dist <= edge + 3) col = Scale(col, (sy > 0 || sx > 0) ? 0.9 : 1.07);
			o.Px[y * w + x] = Rgb(col);
		}
		return o;
	}

	// A prop made of several pieces of card glued on top of each other, each drawn by one of `pieces` in order and
	// casting its shadow on the ones under it
	public static Img Layered(int w, int h, int edge, int seed, params Action<Graphics>[] pieces)
	{
		return Layered(w, h, edge, seed, 1, pieces);
	}

	// The same, the pieces drawn in coordinates `scale` times larger than the image (so a prop can be drawn at any
	// size, its cut edges as crisp as ever)
	public static Img Layered(int w, int h, int edge, int seed, float scale, params Action<Graphics>[] pieces)
	{
		Img result = new Img(w, h);
		for (int i = 0; i < pieces.Length; i++) {
			Action<Graphics> piece = pieces[i];
			Img flat = Img.Gdi(w, h, g => { g.ScaleTransform(scale, scale); piece(g); });
			result.Draw(Cardboard(flat, i == 0 ? edge : Math.Max(1, edge - 1), true, seed + i * 7), 0, 0);
		}
		return result;
	}

	// A line drawn on a prop after the cardboard pass: a crease or a printed outline
	public static void Line(Img img, double x0, double y0, double x1, double y1, double width, uint c)
	{
		int minX = (int)Math.Floor(Math.Min(x0, x1) - width), maxX = (int)Math.Ceiling(Math.Max(x0, x1) + width);
		int minY = (int)Math.Floor(Math.Min(y0, y1) - width), maxY = (int)Math.Ceiling(Math.Max(y0, y1) + width);
		double vx = x1 - x0, vy = y1 - y0, len2 = Math.Max(1e-9, vx * vx + vy * vy);
		for (int y = minY; y <= maxY; y++) for (int x = minX; x <= maxX; x++) {
			double t = Math.Max(0, Math.Min(1, ((x + 0.5 - x0) * vx + (y + 0.5 - y0) * vy) / len2));
			double dx = x + 0.5 - (x0 + vx * t), dy = y + 0.5 - (y0 + vy * t);
			if (dx * dx + dy * dy <= width * width / 4) img.Set(x, y, c);
		}
	}

	// ---- Facades and their x-ray frames ----

	// The frames a facade shows over the actors behind it, each building its own way of seeing through it. They are
	// made from the facade before its key colors are resolved, so they know its cut edges, glass and bulbs

	// Shadow play: the facade is a paper screen lit from behind, and whoever is behind it a shadow on it
	public static Img ShadowPlay(Img facade)
	{
		return facade.Map((x, y, c) => {
			if ((c >> 24) == 0 || ((c >> 24) < 250 && !IsAnyKey(c))) return 0;
			if (IsKey(c, KeyBulb)) return Rgb(255, 250, 200);
			if (IsKey(c, KeyNeon)) return Rgb(255, 120, 200);
			double r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			if (IsKey(c, KeyGlass) || IsKey(c, KeyFrost) || IsKey(c, KeyLine)) { r = 60; g = 40; b = 50; }
			// Warm light through the paper around the shadow, the shadow itself soft at the edges of the card
			return Rgba(r * 0.2 + 14, g * 0.16 + 8, b * 0.18 + 16, (c >> 24) == EdgeAlpha ? 255 : 228);
		});
	}

	// Blacklight: under a UV lamp the facade almost disappears; its cut edges glow, its print fluoresces, and
	// whoever is behind it shows through in violet
	public static Img Blacklight(Img facade)
	{
		return facade.Map((x, y, c) => {
			if ((c >> 24) == 0 || ((c >> 24) < 250 && !IsAnyKey(c))) return 0;
			if ((c >> 24) == EdgeAlpha) return Rgb(170, 255, 250);
			if (IsKey(c, KeyLine)) return Rgb(170, 255, 250);
			if (IsKey(c, KeyGlass) || IsKey(c, KeyFrost)) return Rgba(120, 50, 255, 40);
			double r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			double luma = 0.3 * r + 0.59 * g + 0.11 * b;
			if (luma < 70) return Rgba(255, 110, 250, 230);		// Print
			// Fibers of the paper glowing faintly
			return Hash(x, y, 5) > 0.93 ? Rgba(200, 160, 255, 120) : Rgba(110, 40, 230, 78);
		});
	}

	// Photo negative: the facade turns into the negative of itself, thin enough to see whoever is behind it, their
	// colors inverted with it
	public static Img PhotoNegative(Img facade)
	{
		return facade.Map((x, y, c) => {
			if ((c >> 24) == 0 || ((c >> 24) < 250 && !IsAnyKey(c))) return 0;
			if (IsKey(c, KeyGlass) || IsKey(c, KeyFrost)) return Rgba(250, 230, 190, 50);
			double r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			if (IsKey(c, KeyLine)) { r = 40; g = 22; b = 40; }
			if ((c >> 24) == EdgeAlpha) return Rgb(255 - r, 255 - g, 255 - b);
			return Rgba(255 - r, 255 - g, 255 - b, 150);
		});
	}

	// Blueprint: the facade is drawn as its own plan, white lines on blue paper with a grid, and whoever is behind it
	// shows through the paper
	public static Img Blueprint(Img facade)
	{
		return facade.Map((x, y, c) => {
			if ((c >> 24) == 0 || ((c >> 24) < 250 && !IsAnyKey(c))) return 0;
			if ((c >> 24) == EdgeAlpha || IsKey(c, KeyLine)) return Rgb(232, 242, 255);
			if (IsKey(c, KeyBulb)) return Rgb(232, 242, 255);
			if (Mod(x, 16) == 0 || Mod(y, 16) == 0) return Rgba(150, 190, 240, 160);
			if (IsKey(c, KeyGlass) || IsKey(c, KeyFrost)) return Rgba(40, 90, 170, 70);
			return Rgba(36, 86, 168, 150);
		});
	}
	// Turns the key colors of a facade into what they show in a given frame: glass, bulbs lit or not (the bulbs chase
	// along `bulbOrder`, every fourth one lit in a frame), neon
	public static Img ResolveKeys(Img facade, int frame, Func<int, int, int> bulbIndex)
	{
		return facade.Map((x, y, c) => {
			if (IsKey(c, KeyGlass)) {
				// Glass reflecting a light sky: a gradient and a streak
				double f = (double)y / facade.H;
				bool streak = Mod(x - y, 23) < 3;
				return streak ? Rgb(210, 236, 248) : Rgb(Mix(120, 70, f), Mix(176, 120, f), Mix(214, 170, f));
			}
			if (IsKey(c, KeyBulb)) {
				bool lit = Mod(bulbIndex(x, y) - frame, 4) == 0;
				return lit ? Rgb(255, 250, 200) : Rgb(196, 140, 60);
			}
			if (IsKey(c, KeyNeon)) return Rgb(255, 120, 200);
			if (IsKey(c, KeyLine)) return Rgb(40, 22, 40);
			if (IsKey(c, KeyFrost)) return 0;
			if ((c >> 24) == EdgeAlpha) return c | 0xFF000000u;
			return c;
		});
	}

	// Bulbs: small circles of the bulb key around a rectangle, every `step` pixels; returns the centers in order
	public static List<PointF> BulbBorder(Graphics g, float x, float y, float w, float h, float step, float r)
	{
		List<PointF> pts = new List<PointF>();
		float per = 2 * (w + h);
		for (float s = 0; s < per - step / 2; s += step) {
			float px, py;
			if (s < w) { px = x + s; py = y; }
			else if (s < w + h) { px = x + w; py = y + (s - w); }
			else if (s < 2 * w + h) { px = x + w - (s - w - h); py = y + h; }
			else { px = x; py = y + h - (s - 2 * w - h); }
			pts.Add(new PointF(px, py));
			g.FillEllipse(Br(Gc(KeyBulb).R, Gc(KeyBulb).G, Gc(KeyBulb).B), px - r, py - r, r * 2, r * 2);
		}
		return pts;
	}

	// ---- Water, glass and light ----

	// The water seen in front of a lake or pool at (x, y), its surface at `surfaceY` and `depthPx` deep: a bright, wavy
	// surface line and a translucent body with darker bands
	public static uint WaterTint(int x, int y, double surfaceY, double depthPx)
	{
		double wave = 1.6 * Math.Sin(2 * Math.PI * x / 32.0);
		double d = y + 0.5 - (surfaceY + wave);
		if (d < 0) return 0;
		if (d < 2.2) return Rgba(236, 250, 255, 235);
		if (d < 4) return Rgba(120, 200, 236, 170);
		double f = Math.Min(1, d / depthPx);
		bool band = Mod((int)(d + 2 * Math.Sin(2 * Math.PI * x / 64.0)), 14) < 2;
		return Rgba(Mix(70, 34, f), Mix(160, 96, f), Mix(214, 170, f), Mix(70, 150, f) + (band ? 30 : 0));
	}

	// The reflection and blur under a water surface at (x, y), moving with `wave` (0-1 over the loop): bands of
	// weaker, bent reflection travel down, and deeper the reflection fades into blur
	public static uint WaterFx(int x, int y, double surfaceY, double depthPx, double wave)
	{
		double d = y + 0.5 - surfaceY;
		if (d < 0) return 0;
		double f = Math.Min(1, d / depthPx);
		double phase = d * 0.45 - wave * 2 * Math.PI + Math.Sin(x * 2 * Math.PI / T) * 0.9;
		double band = 0.5 + 0.5 * Math.Sin(phase);
		double reflection = 235 * (1 - 0.7 * f) * (0.72 + 0.28 * band);
		double blur = 20 + 170 * f + 40 * (1 - band) * (1 - f);
		double strength = Smooth(0, 10, d);
		return Fx(blur + Dither(x, y) * 2, reflection + Dither(x + 1, y) * 2, 1.6 * strength * Math.Cos(phase), 2.4 * strength * Math.Sin(phase));
	}

	// Scales the alpha of a color (a blur-behind texel keeps at least 1, which is "does nothing more")
	public static uint Fade(uint c, double k, bool fx)
	{
		if ((c >> 24) == 0 || k >= 1) return c;
		if (k <= 0) return 0;
		if (fx) {
			// Fade the amounts, not the switch: blur and reflection down, displacement towards none
			double a = (c >> 24), r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			return Rgba(128 + (r - 128) * k, 128 + (g - 128) * k, b * k, 1 + (a - 1) * k);
		}
		return (c & 0xFFFFFFu) | ((uint)Clamp((c >> 24) * k) << 24);
	}
	// ---- Background ----

	public static uint SkyColor(double y, int x, int iy)
	{
		double[][] stops = { C(14, 22, 58), C(40, 74, 150), C(96, 140, 210), C(196, 186, 210), C(255, 190, 140) };
		double[] pos = { 0.0, 0.3, 0.6, 0.85, 1.0 };
		double t = y / 575.0;
		int i = 0;
		while (i < pos.Length - 2 && t > pos[i + 1]) i++;
		double f = (t - pos[i]) / (pos[i + 1] - pos[i]);
		double d = Dither(x, iy) * 2.0;
		return Rgb(Mix(stops[i][0], stops[i + 1][0], f) + d, Mix(stops[i][1], stops[i + 1][1], f) + d, Mix(stops[i][2], stops[i + 1][2], f) + d);
	}

	public static uint[] PaintMountains(int w, int h)
	{
		uint[] img = new uint[w * h];
		double tau = Math.PI * 2;
		Func<double, double> ridgeAt = x => 215 - (55 * Math.Sin(tau * x / w + 0.3) + 32 * Math.Sin(tau * x / (w / 2.0) + 1.7) + 16 * Math.Sin(tau * x / (w / 4.0) + 0.9)
			+ 7 * Math.Sin(tau * x / (w / 8.0) + 2.1) + 10 * (Noise(x, 0, 16, w, 11) - 0.5));
		for (int x = 0; x < w; x++) {
			double ridge = ridgeAt(x), ridgeNext = ridgeAt(x + 1);
			bool litSide = (ridgeNext > ridge);
			for (int y = 0; y < h; y++) {
				double d = y + 0.5 - ridge;
				if (d < 0) continue;
				double haze = Smooth(0, 260, d);
				double[] top = litSide ? C(176, 172, 214) : C(140, 140, 196);
				double[] deep = C(64, 70, 118);
				double n = (Noise(x, y, 8, w, 5) - 0.5) * 10;
				double[] c = MixC(top, deep, haze);
				double r = c[0] + n, g = c[1] + n, b = c[2] + n;
				double snowDepth = (200 - ridge) * 0.45 + (Noise(x, y, 4, w, 9) - 0.5) * 8;
				if (ridge < 200 && d < snowDepth) {
					r = litSide ? 240 : 205; g = litSide ? 242 : 210; b = litSide ? 250 : 236;
				}
				img[y * w + x] = Rgba(r, g, b, d < 1 ? 255 * d : 255);
			}
		}
		return img;
	}

	public static uint[] PaintHills(int w, int h)
	{
		uint[] img = new uint[w * h];
		double tau = Math.PI * 2;
		Func<double, double> ridgeAt = x => 380 - (34 * Math.Sin(tau * x / w + 1.2) + 20 * Math.Sin(tau * x / (w / 2.0) + 0.2) + 9 * Math.Sin(tau * x / (w / 4.0) + 2.5));
		Random rnd = new Random(7);
		List<double[]> trees = new List<double[]>();
		for (double tx = 10; tx < w - 10; tx += 26 + rnd.Next(0, 40)) {
			trees.Add(new double[] { tx, 34 + rnd.Next(0, 18), 60 + rnd.Next(0, 55), rnd.NextDouble() });
		}
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				double ridge = ridgeAt(x);
				double d = y + 0.5 - ridge;
				uint c = 0;
				foreach (double[] tr in trees) {
					for (int k = -1; k <= 1; k++) {
						double dx = x - (tr[0] + k * w);
						double baseY = ridgeAt(tr[0]) + 12;
						double topY = baseY - tr[2];
						if (y < topY || y > baseY) continue;
						double f = (y - topY) / tr[2];
						double tier = (f * 3) % 1.0;
						double halfWidth = tr[1] * 0.5 * (0.35 + 0.65 * f) * (0.55 + 0.45 * tier);
						if (Math.Abs(dx) <= halfWidth) {
							double shade = dx < 0 ? 1.12 : 0.88;
							c = Rgb((26 + 10 * tr[3]) * shade, (62 + 12 * tr[3]) * shade, (48 + 6 * tr[3]) * shade);
						}
					}
				}
				if (d >= 0) {
					double depth = Smooth(0, 300, d);
					double n = (Noise(x, y, 6, w, 21) - 0.5) * 14;
					c = MixC(C(96 + n, 156 + n, 78 + n), C(38, 82, 52), depth, 255);
					if (d < 2.5) c = Rgb(150, 205, 110);
					if (d < 1) c = Rgba(150, 205, 110, 255 * d);
				}
				img[y * w + x] = c;
			}
		}
		return img;
	}
}
