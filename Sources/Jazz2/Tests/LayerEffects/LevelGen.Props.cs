using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;

// The props of the level. Every one is cut from painted corrugated card, most of them from several pieces glued on
// top of each other (see Layered() and Cardboard()), and stands on the ground: the pieces reach the bottom of their
// image, which is placed so that its bottom is the ground's surface, or below it for props behind the ground
public static partial class LevelGen
{
	// A bush in front of the meadow, cut out where an actor is behind it: three layers of leafy card
	static Img PaintBush(int w, int h, int seed)
	{
		Random rnd = new Random(seed);
		float[][] lobes = new float[7][];
		for (int i = 0; i < 7; i++) {
			float r = 22 + rnd.Next(0, 14);
			lobes[i] = new float[] { 18 + (w - 36) * i / 6f + rnd.Next(-4, 5), h - r - 6 - rnd.Next(0, 16), r };
		}
		return Layered(w, h, 3, seed,
			g => {
				foreach (float[] l in lobes) g.FillEllipse(Br(58, 116, 58), l[0] - l[2], l[1] - l[2] - 4, l[2] * 2, l[2] * 2);
				g.FillRectangle(Br(58, 116, 58), 12, h - 30, w - 24, 30);
			},
			g => { foreach (float[] l in lobes) g.FillEllipse(Br(86, 150, 70), l[0] - l[2] * 0.75f + 4, l[1] - l[2] * 0.6f + 6, l[2] * 1.4f, l[2] * 1.4f); },
			g => { foreach (float[] l in lobes) g.FillEllipse(Br(124, 180, 88), l[0] - l[2] * 0.35f, l[1] - l[2] * 0.45f + 4, l[2] * 0.6f, l[2] * 0.5f); });
	}

	static Img PaintPost(int h)
	{
		return Cardboard(Img.Gdi(40, h, g => g.FillRectangle(Br(140, 98, 58), 12, 0, 14, h)), 2, true, 5);
	}

	// Reeds: stalks, and the heads of a few of them. Their feet are behind the ground, so they stand on any bank
	static Img PaintReeds(int w, int h, int seed)
	{
		Random rnd = new Random(seed);
		List<float[]> stalks = new List<float[]>();
		for (int i = 0; i < w / 13; i++) stalks.Add(new float[] { 6 + i * 13 + rnd.Next(-2, 3), 12 + rnd.Next(0, h / 3), rnd.Next(0, 3) });
		return Layered(w, h, 2, seed,
			g => { foreach (float[] s in stalks) g.FillRectangle(Br(96, 146, 68), s[0], s[1], 7, h - s[1]); },
			g => { foreach (float[] s in stalks) if (s[2] == 0) g.FillEllipse(Br(134, 86, 48), s[0] - 3, s[1] - 6, 13, 26); });
	}

	// Puffs of white card on top of a cloud, `w` wide
	static Img PaintCloudPuffs(int w, int seed)
	{
		Random rnd = new Random(seed);
		List<float[]> puffs = new List<float[]>();
		for (float x = 18; x < w - 14; x += 14 + rnd.Next(0, 8)) puffs.Add(new float[] { x, 11 + rnd.Next(0, 8) });
		return Layered(w, 52, 2, seed,
			g => { foreach (float[] p in puffs) g.FillEllipse(Br(214, 224, 242), p[0] - p[1], 40 - p[1] * 1.3f, p[1] * 2, p[1] * 2); g.FillRectangle(Br(214, 224, 242), 12, 30, w - 24, 14); },
			g => { foreach (float[] p in puffs) g.FillEllipse(Br(244, 247, 252), p[0] - p[1] * 0.7f - 2, 38 - p[1] * 1.3f, p[1] * 1.3f, p[1] * 1.2f); });
	}
	// A waterfall falling over a cliff edge into a pool, `frame` of `frames` of its loop: blue card with lighter
	// stripes painted along it, the stripes travelling 64 px a loop, and puffs of white card where it lands. The mask
	// frame keeps only the cut edges and a faint wash, so whatever is behind it shows through
	static Img PaintWaterfall(int w, int h, int frame, int frames, bool mask)
	{
		Img img = new Img(w, h);
		double s = 64.0 * frame / frames;
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			double left = 2, right = w - 6 - 2 * Smooth(0, h, y);
			double top = 4 + 10 * Math.Pow((x - left) / (right - left), 2);
			if (x < left || x >= right || y < top || y > h - 18) continue;
			int edge = (int)Math.Ceiling(Math.Min(Math.Min(x - left + 0.5, right - x), y - top + 0.5));
			if (edge <= 3) { img.Set(x, y, CutEdge(x, y, edge)); continue; }
			int col = (int)((x - left) / 12);
			double v = Frac((y - s + Hash(col, 0, 3) * 64) / 64.0) * 64;
			double u = Mod((int)(x - left), 12);
			bool stripe = v < 22 && Math.Abs(u - 6) < 2.6;
			if (mask) {
				img.Set(x, y, stripe ? Rgba(200, 236, 255, 110) : Rgba(120, 190, 240, 36));
				continue;
			}
			double[] c = PaintedCard(stripe ? C(200, 232, 248) : C(92, 166, 222), x, y, edge - 3, 41);
			if (!stripe && u < 1.5) c = Scale(c, 0.86);
			img.Set(x, y, Rgb(c));
		}
		if (!mask) {
			// Puffs of foam where it lands, bobbing with the loop, each casting a little shadow on the water
			Img foam = new Img(w, h);
			for (int i = 0; i < 7; i++) {
				double cx = 10 + i * (w - 20) / 6.0, cy = h - 22 + 3 * Math.Sin(2 * Math.PI * (frame / (double)frames * 2 + i * 0.37));
				double r = 11 + 3 * Math.Sin(i * 1.7);
				for (int y = (int)(cy - r - 1); y <= cy + r + 1; y++) for (int x = (int)(cx - r - 1); x <= cx + r + 1; x++) {
					double d = Math.Sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
					if (d < r) foam.Set(x, y, Rgb(238, 244, 248));
				}
			}
			img.Draw(Cardboard(foam, 2, true, 43), 0, 0);
		}
		return img;
	}

	// The gate of the studio: two striped posts and the sign over them, its letters cut out of yellow card. The posts
	// are 20 px wide at both edges of the image, so the green screen between them never shares a tile with them
	static Img PaintGate(int w, int h)
	{
		Img posts = Layered(w, h, 2, 6, g => {
			g.FillRectangle(Br(226, 212, 176), 6, 50, 20, h - 50);
			g.FillRectangle(Br(226, 212, 176), w - 26, 50, 20, h - 50);
		});
		// Red bands painted around the posts
		posts = posts.Map((x, y, c) => {
			if ((c >> 24) != 255) return c;
			return Mod(x + y, 14) < 7 ? Rgb(PaintedCard(C(196, 64, 54), x, y, 9, 6)) : c;
		});
		Img sign = Layered(w, h, 2, 7,
			g => { using (GraphicsPath p = RoundRect(2, 4, w - 4, 46, 10)) g.FillPath(Br(54, 60, 96), p); },
			g => Text(g, "CARDBOARD STUDIOS", "Arial Black", 15, C(246, 206, 92), w / 2f, 27));
		posts.Draw(sign, 0, 0);
		return posts;
	}

	// The green screen in the gate: a panel of green card in a grey frame, with the crosses of the tracking markers on
	// it. In its mask frame the green is keyed out to the backdrop it stands for - outer space - so whoever walks
	// behind it is a cut-out of space
	static Img PaintGreenScreen(int w, int h, bool mask)
	{
		Img img = Layered(w, h, 3, 64,
			g => { g.FillRectangle(Br(96, 100, 112), 2, 4, w - 4, h - 16); g.FillRectangle(Br(80, 84, 96), 14, h - 14, 14, 14); g.FillRectangle(Br(80, 84, 96), w - 28, h - 14, 14, 14); },
			g => g.FillRectangle(Br(KeyGlass), 10, 12, w - 20, h - 32));
		return img.Map((x, y, c) => {
			if (!IsKey(c, KeyGlass)) return c;
			if (!mask) {
				// The green, with a tracking marker every 40 pixels
				int u = Mod(x - 30, 40), v = Mod(y - 30, 40);
				bool marker = (u < 9 && v == 4) || (v < 9 && u == 4);
				return marker ? Rgb(40, 130, 60) : Rgb(PaintedCard(C(72, 190, 92), x, y, 9, 65));
			}
			// Space: a dark gradient, stars, and a ringed planet
			double f = (double)y / h;
			uint px = Rgb(Mix(10, 34, f), Mix(8, 18, f), Mix(36, 70, f));
			double s = Hash(x, y, 66);
			if (s > 0.992) px = Rgb(255, 255, 240);
			else if (s > 0.982) px = Rgb(150, 160, 210);
			double dx = x - w * 0.68, dy = y - h * 0.3, d = Math.Sqrt(dx * dx + dy * dy);
			if (d < 18) px = Rgb(Mix(230, 160, Smooth(-18, 18, dx + dy)), Mix(140, 80, Smooth(-18, 18, dx + dy)), Mix(90, 60, Smooth(-18, 18, dx + dy)));
			double rx = dx * Math.Cos(-0.35) - dy * Math.Sin(-0.35), ry = dx * Math.Sin(-0.35) + dy * Math.Cos(-0.35);
			double ring = Math.Sqrt(rx * rx / (34.0 * 34.0) + ry * ry / (9.0 * 9.0));
			if (ring > 0.82 && ring < 1.0 && (d >= 18 || ry > 0)) px = Rgb(220, 200, 160);
			return px;
		});
	}
	// ---- Facades of the backlot ----
	// Drawn in design coordinates (`dw` x `dh`) `s` times larger than the image, so they can be any size

	// The theater: a marquee of chasing bulbs and lettering, arched windows, a canopy over the doors
	static Img PaintTheater(int w, int h, float s, out List<PointF> bulbs)
	{
		float dw = w / s, dh = h / s;
		List<PointF> pts = null;
		Img img = Layered(w, h, 2, 11, s,
			g => g.FillRectangle(Br(210, 112, 98), 10, 22, dw - 20, dh - 22),
			g => {
				g.FillRectangle(Br(172, 74, 70), 4, 8, dw - 8, 24);
				for (float x = 24; x < dw - 30; x += (dw - 64) / 3f) g.FillRectangle(Br(238, 196, 168), x, 40, 16, dh - 40);
			},
			g => {
				g.FillRectangle(Br(240, 206, 100), 40, 52, dw - 80, 76);
				pts = BulbBorder(g, 48, 60, dw - 96, 60, 16, 5.5f);
			},
			g => Text(g, "THEATER", "Arial Black", 36, C(186, 46, 58), dw / 2f, 92),
			g => {
				for (int i = 0; i < 3; i++) {
					float x = 60 + i * (dw - 120 - 44) / 2f;
					using (GraphicsPath p = new GraphicsPath()) {
						p.AddArc(x - 6, 144, 56, 56, 180, 180); p.AddLine(x + 50, 172, x + 50, 232); p.AddLine(x + 50, 232, x - 6, 232); p.CloseFigure();
						g.FillPath(Br(238, 222, 196), p);
					}
					using (GraphicsPath p = new GraphicsPath()) {
						p.AddArc(x, 150, 44, 44, 180, 180); p.AddLine(x + 44, 172, x + 44, 226); p.AddLine(x + 44, 226, x, 226); p.CloseFigure();
						g.FillPath(Br(KeyGlass), p);
					}
					g.FillRectangle(Br(KeyLine), x + 20, 160, 5, 66);
				}
			},
			g => g.FillRectangle(Br(66, 56, 104), 70, 252, dw - 140, 44),
			g => Text(g, "LAYER FX", "Arial Black", 28, C(248, 228, 140), dw / 2f, 275),
			g => {
				g.FillRectangle(Br(122, 46, 56), dw / 2f - 70, 312, 140, dh - 312);
				g.FillRectangle(Br(KeyGlass), dw / 2f - 60, 324, 54, 44);
				g.FillRectangle(Br(KeyGlass), dw / 2f + 6, 324, 54, 44);
			});
		bulbs = pts.ConvertAll(p => new PointF(p.X * s, p.Y * s));
		return img;
	}

	// The diner, which the street passes in front of: big windows, a door and a neon sign on its roof (its awning is
	// in front of the street, see PaintAwning())
	static Img PaintDiner(int w, int h, float s)
	{
		float dw = w / s, dh = h / s;
		return Layered(w, h, 2, 12, s,
			g => g.FillRectangle(Br(96, 176, 176), 8, 56, dw - 16, dh - 56),
			g => { g.FillRectangle(Br(64, 132, 142), 4, 50, dw - 8, 16); g.FillRectangle(Br(232, 228, 214), 8, 214, dw - 16, 12); },
			g => {
				for (int i = 0; i < 3; i++) {
					float x = 24 + i * (dw - 48) / 3f;
					g.FillRectangle(Br(226, 234, 232), x - 4, 92, (dw - 48) / 3f - 16, 84);
					g.FillRectangle(Br(KeyGlass), x + 2, 98, (dw - 48) / 3f - 28, 72);
				}
				g.FillRectangle(Br(196, 124, 66), dw / 2f - 30, 234, 60, dh - 234);
				g.FillRectangle(Br(KeyGlass), dw / 2f - 18, 246, 36, 32);
			},
			g => {
				using (GraphicsPath p = RoundRect(dw / 2f - 84, 4, 168, 46, 10)) g.FillPath(Br(44, 34, 62), p);
				Text(g, "DINER", "Arial Black", 34, KeyNeon, dw / 2f, 27);
			});
	}

	// The diner's awning, red and white stripes with a scalloped edge, hanging over the street in front of it
	static Img PaintAwning(int w, int h, float s)
	{
		float dw = w / s;
		return Layered(w, h, 2, 19, s,
			g => g.FillRectangle(Br(124, 122, 128), 6, 4, dw - 12, 8),
			g => {
				for (int x = 6; x < dw - 6; x += 24) {
					g.FillRectangle(Br(198, 60, 62), x, 10, 12, 24);
					g.FillRectangle(Br(240, 232, 218), x + 12, 10, 12, 24);
					g.FillEllipse(Br(198, 60, 62), x, 26, 12, 14);
					g.FillEllipse(Br(240, 232, 218), x + 12, 26, 12, 14);
				}
			});
	}

	// The diner's chimney, standing on its roof
	static Img PaintChimney(int w, int h)
	{
		return Layered(w, h, 2, 20,
			g => g.FillRectangle(Br(150, 84, 70), 6, 6, w - 12, h - 6),
			g => g.FillRectangle(Br(120, 66, 56), 3, 2, w - 6, 8));
	}

	// The detective agency
	static Img PaintDetective(int w, int h, float s)
	{
		float dw = w / s, dh = h / s;
		return Layered(w, h, 2, 13, s,
			g => g.FillRectangle(Br(220, 178, 82), 8, 14, dw - 16, dh - 14),
			g => g.FillRectangle(Br(164, 118, 46), 4, 4, dw - 8, 22),
			g => { using (GraphicsPath p = RoundRect(26, 36, dw - 52, 54, 8)) g.FillPath(Br(58, 46, 40), p); },
			g => Text(g, "DETECTIVE", "Arial Black", 30, C(242, 228, 196), dw / 2f, 63),
			g => {
				// Clear windows upstairs, frosted ones (the frost is the blur layer's) on the ground floor
				for (int r = 0; r < 2; r++) for (int i = 0; i < 2; i++) {
					float x = 34 + i * (dw - 68) / 2f + (r == 1 && i == 1 ? 40 : 0), y = 106 + r * 96, ww = (dw - 68) / 2f - 26 - (r == 1 ? 40 : 0);
					double[] pane = r == 0 ? KeyGlass : KeyFrost;
					g.FillRectangle(Br(116, 78, 38), x, y, ww, 72);
					g.FillRectangle(Br(pane), x + 7, y + 7, ww - 14, 58);
					if (r == 0) g.FillRectangle(Br(KeyLine), x + 7, y + 34, ww - 14, 5);
				}
				g.FillRectangle(Br(96, 64, 44), dw / 2f - 30, dh - 104, 60, 104);
			},
			g => g.FillEllipse(Br(236, 206, 112), dw / 2f + 14, dh - 60, 12, 12));
	}

	// The photo studio: a big camera on its sign, a shop window and a door
	static Img PaintPhoto(int w, int h, float s)
	{
		float dw = w / s, dh = h / s;
		return Layered(w, h, 2, 15, s,
			g => g.FillRectangle(Br(176, 156, 206), 8, 30, dw - 16, dh - 30),
			g => { g.FillRectangle(Br(120, 100, 160), 4, 20, dw - 8, 22); g.FillRectangle(Br(236, 230, 240), 8, dh - 150, dw - 16, 12); },
			g => {
				// The camera: its body, its lens and its flash
				using (GraphicsPath p = RoundRect(dw / 2f - 90, 60, 180, 110, 16)) g.FillPath(Br(60, 58, 70), p);
				g.FillRectangle(Br(60, 58, 70), dw / 2f + 30, 46, 40, 20);
			},
			g => { g.FillEllipse(Br(206, 210, 222), dw / 2f - 46, 72, 92, 92); g.FillRectangle(Br(250, 244, 200), dw / 2f + 38, 50, 24, 12); },
			g => { g.FillEllipse(Br(KeyGlass), dw / 2f - 32, 86, 64, 64); },
			g => Text(g, "PHOTO", "Arial Black", 30, C(250, 236, 120), dw / 2f, 206),
			g => {
				g.FillRectangle(Br(236, 230, 240), 30, dh - 128, dw / 2f - 50, 92);
				g.FillRectangle(Br(KeyGlass), 38, dh - 120, dw / 2f - 66, 76);
				g.FillRectangle(Br(80, 66, 110), dw - 120, dh - 120, 76, 120);
				g.FillRectangle(Br(KeyGlass), dw - 108, dh - 108, 52, 40);
			});
	}

	// The front of the sound stage: a big hangar with its sliding door
	static Img PaintHangar(int w, int h, float s)
	{
		float dw = w / s, dh = h / s;
		return Layered(w, h, 2, 14, s,
			g => {
				using (GraphicsPath p = new GraphicsPath()) {
					p.AddArc(6, 6, dw - 12, 120, 180, 180); p.AddLine(dw - 6, 66, dw - 6, dh); p.AddLine(dw - 6, dh, 6, dh); p.CloseFigure();
					g.FillPath(Br(124, 142, 172), p);
				}
			},
			g => { for (float x = 30; x < dw - 20; x += 36) g.FillRectangle(Br(108, 126, 156), x, 64, 10, dh - 64); },
			g => { using (GraphicsPath p = RoundRect(dw / 2f - 120, 74, 240, 66, 10)) g.FillPath(Br(238, 230, 212), p); },
			g => Text(g, "STAGE 1", "Arial Black", 40, C(64, 54, 110), dw / 2f, 107),
			g => {
				g.FillRectangle(Br(84, 96, 122), dw / 2f - 140, 172, 280, dh - 172);
				for (float x = dw / 2f - 110; x < dw / 2f + 130; x += 56) g.FillRectangle(Br(KeyLine), x, 180, 4, dh - 188);
				g.FillEllipse(Br(KeyBulb), dw / 2f - 10, 148, 20, 20);
			});
	}

	// A street lamp; its foot is behind the sidewalk
	static Img PaintLamp(int h)
	{
		return Layered(64, h, 2, 15,
			g => { g.FillRectangle(Br(64, 70, 92), 28, 24, 8, h - 24); g.FillRectangle(Br(64, 70, 92), 20, h - 20, 24, 20); },
			g => { using (GraphicsPath p = RoundRect(14, 4, 36, 24, 8)) g.FillPath(Br(74, 80, 102), p); },
			g => g.FillEllipse(Br(250, 232, 164), 22, 12, 20, 16));
	}

	// The magnifying glass on its stand, a ring of card around the lens (which is on the blur layer)
	static Img PaintMagnifierFrame(int w, int h, double cx, double cy, double radius)
	{
		float r = (float)radius;
		Img img = Layered(w, h, 2, 61,
			g => { g.FillRectangle(Br(140, 96, 56), (float)cx - 5, (float)(cy + radius), 10, h - (float)(cy + radius)); g.FillRectangle(Br(120, 82, 48), (float)cx - 24, h - 8, 48, 8); },
			g => { using (Pen pen = new Pen(Gc(176, 136, 76), 9)) g.DrawEllipse(pen, (float)cx - r - 1, (float)cy - r - 1, r * 2 + 2, r * 2 + 2); });
		// A glint on the glass
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			double px = x + 0.5 - cx, py = y + 0.5 - cy, d = Math.Sqrt(px * px + py * py);
			if (d <= radius - 5 && d > radius - 12 && px < 0 && py < 0 && Math.Abs(px - py) < d * 0.5) img.Blend(x, y, Rgba(255, 255, 255, 80));
		}
		return img;
	}

	static Img PaintMagnifierLens(int w, int h, double cx, double cy, double radius) { return PaintLens(w, h, cx, cy, radius, -0.42); }

	// A lens: whatever is behind it seen k times nearer its center (k < 0 magnifies, k > 0 shrinks)
	static Img PaintLens(int w, int h, double cx, double cy, double radius, double k)
	{
		Img img = new Img(w, h);
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			double px = x + 0.5 - cx, py = y + 0.5 - cy;
			if (px * px + py * py > (radius - 4) * (radius - 4)) continue;
			img.Set(x, y, Fx(0, 0, k * px, k * py));
		}
		return img;
	}

	// A pane of frosted glass of any shape (`inside` tells which pixels of the w x h box it covers) in a frame of card:
	// the frame and a tint on the overlay, the frost on the blur layer, going from `frostTop` at its top to
	// `frostBottom` at its bottom (0-255)
	static void FrostedPane(LevelCanvas overlay, LevelCanvas blur, int x0, int y0, int w, int h, Func<int, int, bool> inside,
		double[] tint, double tintAlpha, double frostTop, double frostBottom)
	{
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			if (!inside(x, y)) continue;
			int dist = 7;
			for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++) {
				int d = (int)Math.Ceiling(Math.Sqrt(dx * dx + dy * dy) - 0.3);
				if (d < dist && (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h || !inside(x + dx, y + dy))) dist = d;
			}
			int gx = x0 + x, gy = y0 + y;
			if (dist <= 2) { overlay.Blend(gx, gy, CutEdge(gx, gy, dist) | 0xFF000000u); continue; }
			if (dist <= 6) { overlay.Blend(gx, gy, Rgb(Scale(PaintedCard(C(168, 164, 186), gx, gy, dist - 3, 67), dist == 3 ? 1.12 : 1))); continue; }
			int s = Mod(x + y, 40);
			uint glass = Rgba(tint[0], tint[1], tint[2], tintAlpha);
			if (s < 3) glass = Over(Rgba(255, 255, 255, 80), glass);
			else if (s >= 7 && s < 9) glass = Over(Rgba(255, 255, 255, 50), glass);
			overlay.Blend(gx, gy, glass);
			blur.Blend(gx, gy, Fx(Mix(frostTop, frostBottom, (double)y / h), 0));
		}
	}
	// A wall of glass blocks: the frame and the mortar between the blocks are card, every block is a little lens that
	// bends and softens what is behind it (on the blur layer, see GlassBlockFx())
	const int GlassBlock = 16;

	static Img PaintGlassBlockFrame(int w, int h)
	{
		Img img = Cardboard(Img.Gdi(w, h, g => {
			g.FillRectangle(Br(150, 156, 170), 0, 0, w, h);
			g.FillRectangle(Br(KeyGlass), 8, 8, w - 16, h - 16);
		}), 3, true, 62);
		return img.Map((x, y, c) => {
			if (!IsKey(c, KeyGlass)) return c;
			int u = Mod(x - 8, GlassBlock), v = Mod(y - 8, GlassBlock);
			if (u < 2 || v < 2) return Rgb(PaintedCard(C(196, 200, 206), x, y, 9, 63));	// Mortar
			// A faint tint and a glint on each block
			if (u >= 4 && u <= 6 && v >= 4 && v <= 7) return Rgba(255, 255, 255, 90);
			return Rgba(200, 228, 240, 40);
		});
	}

	static uint GlassBlockFx(int x, int y, int w, int h)
	{
		if (x < 8 || y < 8 || x >= w - 8 || y >= h - 8) return 0;
		int u = Mod(x - 8, GlassBlock), v = Mod(y - 8, GlassBlock);
		if (u < 2 || v < 2) return 0;
		double cu = u - (GlassBlock + 1) / 2.0, cv = v - (GlassBlock + 1) / 2.0;
		return Fx(70, 0, -0.8 * cu, -0.8 * cv);
	}

	// A puddle on the sidewalk: it reflects, a little rippled, and fades out at its edges
	static uint PuddleFx(int x, int y, int w, double wave)
	{
		double u = (x + 0.5) / w * 2 - 1;
		double edge = 1 - u * u;
		if (edge <= 0) return 0;
		double depth = 7 * Math.Sqrt(edge);
		if (y + 0.5 > depth) return 0;
		double strength = Smooth(0, 0.35, edge) * (1 - Smooth(depth * 0.5, depth, y + 0.5) * 0.6);
		double phase = x * 0.4 - wave * 2 * Math.PI;
		return Fx(30, 220 * strength, 0, 1.2 * Math.Sin(phase) * strength);
	}

	static uint PuddleTint(int x, int y, int w)
	{
		double u = (x + 0.5) / w * 2 - 1;
		double edge = 1 - u * u;
		if (edge <= 0) return 0;
		double depth = 7 * Math.Sqrt(edge);
		if (y + 0.5 > depth) return 0;
		return y == 0 ? Rgba(230, 245, 255, 120) : Rgba(90, 120, 170, 50);
	}

	// Rising heat or steam: it shimmers what is behind it sideways with a little blur, rising `rise` px (it repeats
	// every 64) and thinning out at its sides and towards its top
	static uint HazeFx(int gx, int gy, int width, int rows, double rise)
	{
		double n = Noise(gx, gy + rise, 8, 64, 171);
		double wobble = Noise(gx, gy + rise, 16, 64, 181) - 0.5;
		double amount = Smooth(0, 14, gx + 0.5) * Smooth(0, 14, width - gx - 0.5) * Smooth(0, rows * T * 0.8, gy);
		if (amount < 0.01) return 0;
		double blur = 120 * amount * (0.3 + 0.7 * Smooth(0.3, 0.7, n));
		return Fx(blur + Dither(gx, gy) * 2, 0, 14 * amount * wobble, 4 * amount * (n - 0.5));
	}

	static Img[] Haze(int width, int rows, int frames)
	{
		Img[] haze = new Img[frames];
		for (int f = 0; f < frames; f++) {
			haze[f] = new Img(width, rows * T);
			for (int y = 0; y < haze[f].H; y++) for (int x = 0; x < width; x++) haze[f].Set(x, y, HazeFx(x, y, width, rows, f * 64.0 / frames));
		}
		return haze;
	}

	// ---- Under the ground ----

	// The back of a burrow: dark earth card with roots
	static void PaintBurrowBack(LevelCanvas c, int x0, int y0, int x1, int y1)
	{
		for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
			int mx = Mod(x, 128), my = Mod(y, 128);
			double root = Math.Abs(Mod(my + (int)(6 * Math.Sin(2 * Math.PI * mx / 64.0)), 40) - 20);
			double[] col = PaintedCard(C(104, 74, 48), mx, my, 9, 71);
			if (root < 1.5 && Mod(mx / 32, 2) == 0) col = C(150, 108, 66);
			c.Blend(x, y, Rgb(col));
		}
	}

	// The back of the sewer: bricks of grey card
	static void PaintSewerBack(LevelCanvas c, int x0, int y0, int x1, int y1)
	{
		for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
			int mx = Mod(x, 128), my = Mod(y, 128);
			int row = my / 16, bx = Mod(mx + (row % 2) * 16, 32);
			double[] col = PaintedCard(Mod(row * 7 + mx / 32, 3) == 0 ? C(86, 92, 104) : C(78, 84, 96), mx, my, Math.Min(bx, Mod(my, 16)) - 2, 73);
			if (bx < 2 || Mod(my, 16) < 2) col = C(52, 54, 62);
			c.Blend(x, y, Rgb(col));
		}
	}

	// ---- The sound stage ----

	// The back of the stage: panels of dark painted card, and curtains of red card at its ends and along its top,
	// their folds cut as separate strips
	static void PaintStageBack(LevelCanvas c, int x0, int x1, int y0, int y1)
	{
		for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
			int u = x - x0, v = y - y0;
			int fromEnd = Math.Min(u, x1 - 1 - x);
			// The card's texture repeats every 128 pixels, so the wall's tiles do too
			int mx = Mod(x, 128), my = Mod(y, 128);
			uint px;
			if (fromEnd < 150 || v < 44) {
				// Folds every 32 pixels, each a strip of card lit on its left
				int fu = Mod(u, 32);
				double fold = 0.5 + 0.5 * Math.Sin(2 * Math.PI * (fu + 5) / 32.0);
				double[] col = PaintedCard(C(150 + 50 * fold, 34 + 12 * fold, 46 + 12 * fold), mx, my, 9, 51);
				px = fu < 2 ? CutEdge(mx, my, fu + 1) | 0xFF000000u : Rgb(col);
				if (v >= 40 && v < 44 && fromEnd >= 150) px = CutEdge(mx, my, v - 39) | 0xFF000000u;
				if (fromEnd >= 147 && fromEnd < 150 && v >= 44) px = CutEdge(mx, my, 150 - fromEnd) | 0xFF000000u;
			} else {
				int pu = Mod(u, 128), pv = Mod(v, 128);
				if (pu < 2 || pv < 2) px = CutEdge(mx, my, Math.Min(pu, pv) + 1) | 0xFF000000u;
				else px = Rgb(PaintedCard(C(60, 48, 78), mx, my, Math.Min(pu, pv) - 2, 53));
			}
			c.Blend(x, y, px);
		}
	}

	// A truss of card strips, with a lamp hanging from it at `lamps`
	static Img PaintTruss(int w, int[] lamps)
	{
		return Layered(w, 80, 2, 16,
			g => {
				using (Pen pen = new Pen(Gc(144, 150, 164), 5)) for (int x = 0; x < w; x += 24) { g.DrawLine(pen, x, 12, x + 12, 38); g.DrawLine(pen, x + 12, 38, x + 24, 12); }
			},
			g => { g.FillRectangle(Br(156, 162, 176), 0, 8, w, 9); g.FillRectangle(Br(156, 162, 176), 0, 34, w, 9); },
			g => {
				foreach (int lx in lamps) {
					g.FillRectangle(Br(64, 64, 74), lx - 4, 40, 8, 12);
					using (GraphicsPath p = new GraphicsPath()) {
						p.AddPolygon(new PointF[] { new PointF(lx - 14, 50), new PointF(lx + 14, 50), new PointF(lx + 19, 70), new PointF(lx - 19, 70) });
						g.FillPath(Br(56, 56, 70), p);
					}
				}
			},
			g => { foreach (int lx in lamps) g.FillRectangle(Br(250, 240, 200), lx - 14, 66, 28, 6); });
	}

	// A light cone from a stage lamp: the light seen in the air (on the overlay) and, in the same shape, the color it
	// throws on the actors in it (the mask-only x-ray tile)
	static uint ConeLight(double x, double y, double h, double topW, double bottomW, double[] color, double alpha)
	{
		double f = Math.Max(0, Math.Min(1, y / h));
		double half = Mix(topW, bottomW, f) / 2;
		double d = Math.Abs(x) / half;
		if (d >= 1) return 0;
		double a = alpha * Smooth(1, 0.7, d) * (0.6 + 0.4 * f);
		return Rgba(color[0], color[1], color[2], a);
	}

	// A smoke machine on the stage floor
	static Img PaintSmokeMachine()
	{
		return Layered(88, 52, 2, 17,
			g => { using (GraphicsPath p = RoundRect(6, 12, 66, 40, 6)) g.FillPath(Br(76, 80, 98), p); },
			g => g.FillRectangle(Br(58, 60, 74), 24, 4, 22, 12),
			g => { g.FillEllipse(Br(236, 120, 84), 14, 22, 9, 9); g.FillEllipse(Br(126, 226, 140), 28, 22, 9, 9); });
	}

	// The waves in front of the wave tank, two rows of them cut from blue card
	static Img PaintStageWaves(int w)
	{
		Func<int, Action<Graphics>> row = r => g => {
			using (GraphicsPath p = new GraphicsPath()) {
				List<PointF> pts = new List<PointF>();
				float baseY = 26 + r * 18;
				for (int x = 0; x <= w; x += 4) pts.Add(new PointF(x, baseY - 10 * (float)Math.Abs(Math.Sin((x + r * 20) * Math.PI / 48))));
				pts.Add(new PointF(w, 72)); pts.Add(new PointF(0, 72));
				p.AddPolygon(pts.ToArray());
				g.FillPath(r == 0 ? Br(84, 150, 212) : Br(56, 114, 184), p);
			}
		};
		return Layered(w, 72, 2, 18, row(0), row(1));
	}

	// The aquarium on the stage: a big glass tank standing on the floor. Its frame, the gravel and a few fish of card
	// are in front; the water bends and softens whatever is behind it (on the blur layer, see AquariumFx())
	const int TankTop = 12, TankBottom = 7 * 32 - 6;

	static Img PaintAquariumFront(int w, int h)
	{
		Img img = Layered(w, h, 3, 81,
			g => {
				g.FillRectangle(Br(58, 96, 104), 0, TankTop - 8, w, 12);					// The lid
				g.FillRectangle(Br(58, 96, 104), 0, TankTop - 8, 10, TankBottom - TankTop + 16);
				g.FillRectangle(Br(58, 96, 104), w - 10, TankTop - 8, 10, TankBottom - TankTop + 16);
				g.FillRectangle(Br(58, 96, 104), 0, TankBottom, w, h - TankBottom);
			},
			g => {
				// Gravel along the bottom of the tank
				for (int x = 14; x < w - 14; x += 9) g.FillEllipse(Br(196 - (x % 3) * 20, 170 - (x % 3) * 18, 120), x, TankBottom - 5, 10, 8);
			},
			g => {
				// Fish
				foreach (float[] f in new[] { new float[] { 50, 70, 1 }, new float[] { 120, 120, -1 }, new float[] { 150, 46, 1 } }) {
					using (GraphicsPath p = new GraphicsPath()) {
						p.AddEllipse(f[0] - 14, f[1] - 8, 28, 16);
						float tx = f[0] - f[2] * 14;
						p.AddPolygon(new PointF[] { new PointF(tx, f[1]), new PointF(tx - f[2] * 12, f[1] - 9), new PointF(tx - f[2] * 12, f[1] + 9) });
						g.FillPath(f[2] > 0 ? Br(244, 150, 60) : Br(250, 210, 80), p);
					}
				}
			});
		// The water itself: a light blue wash between the walls
		for (int y = TankTop + 4; y < TankBottom; y++) for (int x = 10; x < w - 10; x++) {
			uint c = img.Get(x, y);
			if ((c >> 24) == 0 || (c >> 24) < 250) img.Set(x, y, Over(c, Rgba(110, 190, 220, 54)));
		}
		return img;
	}

	// The bubbles rising in the aquarium, `frame` of `frames` of their loop
	static Img PaintAquariumBubbles(int w, int h, int frame, int frames)
	{
		Img img = new Img(w, h);
		double[][] columns = { new double[] { 34, 0.0 }, new double[] { 96, 0.4 }, new double[] { 160, 0.7 } };
		foreach (double[] col in columns) for (int i = 0; i < 4; i++) {
			double t = Frac(frame / (double)frames + i / 4.0 + col[1]);
			double cy = TankBottom - 12 - t * (TankBottom - TankTop - 24), cx = col[0] + 3 * Math.Sin(t * 12);
			double r = 2 + 2 * t;
			for (int y = (int)(cy - r - 1); y <= cy + r + 1; y++) for (int x = (int)(cx - r - 1); x <= cx + r + 1; x++) {
				double d = Math.Sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
				if (d < r) img.Blend(x, y, d > r - 1.2 ? Rgba(240, 250, 255, 220) : Rgba(200, 236, 250, 70));
			}
		}
		return img;
	}

	static uint AquariumFx(int x, int y, int w, double phase)
	{
		if (x < 10 || x >= w - 10 || y < TankTop + 4 || y >= TankBottom) return 0;
		double dx = 2.6 * Math.Sin(y * 0.16 + phase * 2 * Math.PI), dy = 1.6 * Math.Sin(x * 0.11 - phase * 2 * Math.PI);
		return Fx(55 + Dither(x, y) * 2, 0, dx, dy);
	}

	// A star cut from yellow card, hanging on a wire over the stage (`size` across; `moon` makes it a crescent moon)
	static Img PaintHangingStar(int size, int wireLength, bool moon, int seed)
	{
		int h = wireLength + size;
		Img img = Layered(size + 12, h + 8, 2, seed,
			g => {
				float cx = (size + 12) / 2f, cy = wireLength + size / 2f, r = size / 2f - 4;
				if (moon) {
					using (System.Drawing.Drawing2D.GraphicsPath p = new System.Drawing.Drawing2D.GraphicsPath()) {
						p.AddEllipse(cx - r, cy - r, r * 2, r * 2);
						using (Region reg = new Region(p)) {
							reg.Exclude(new RectangleF(cx - r * 0.35f, cy - r * 1.1f, r * 1.6f, r * 2.2f));
							g.FillRegion(Br(236, 220, 150), reg);
						}
					}
				} else {
					PointF[] pts = new PointF[10];
					for (int i = 0; i < 10; i++) {
						double a = -Math.PI / 2 + i * Math.PI / 5, rr = (i % 2 == 0) ? r : r * 0.45;
						pts[i] = new PointF(cx + (float)(Math.Cos(a) * rr), cy + (float)(Math.Sin(a) * rr));
					}
					g.FillPolygon(Br(242, 204, 84), pts);
				}
			});
		// The wire, drawn after so the card's shadow doesn't fall on it
		int wx = (size + 12) / 2;
		for (int y = 0; y < wireLength + 6; y++) { img.Set(wx, y, Rgb(40, 34, 48)); img.Set(wx + 1, y, Rgb(70, 62, 80)); }
		return img;
	}
	// A pennant on a pole, for the towers of the castle set
	static Img PaintPennant()
	{
		return Layered(48, 64, 2, 95,
			g => g.FillRectangle(Br(110, 78, 50), 6, 4, 5, 60),
			g => g.FillPolygon(Br(200, 64, 70), new[] { new PointF(11, 6), new PointF(44, 16), new PointF(11, 28) }));
	}
	// The wind machine at the end of the stage: a big fan on a stand, its blades turning (`frame` of `frames`; four
	// blades, so a loop is a quarter turn)
	static Img PaintFan(int w, int h, int frame, int frames)
	{
		float cx = w / 2f, cy = w / 2f, r = w / 2f - 8;
		float angle = 90f * frame / frames;
		return Layered(w, h, 2, 91,
			g => { g.FillRectangle(Br(70, 74, 90), cx - 6, cy, 12, h - cy); g.FillRectangle(Br(70, 74, 90), cx - 30, h - 12, 60, 12); },
			g => g.FillEllipse(Br(96, 102, 122), cx - r, cy - r, r * 2, r * 2),
			g => {
				g.TranslateTransform(cx, cy); g.RotateTransform(angle);
				for (int i = 0; i < 4; i++) { g.FillEllipse(Br(206, 210, 222), -9, -r + 8, 18, r - 10); g.RotateTransform(90); }
			},
			g => g.FillEllipse(Br(60, 64, 78), cx - 9, cy - 9, 18, 18));
	}

	// The air the wind machine blows: bands of shimmer travelling with the wind, to the left (`phase` 0-1 over the
	// loop), fading out at the edges of the area
	static uint WindFx(int x, int y, int w, int h, double phase)
	{
		double fade = Smooth(0, 24, x) * Smooth(0, 48, w - x) * Smooth(0, 16, y) * Smooth(0, 16, h - y);
		if (fade < 0.02) return 0;
		double band = Math.Sin((x + phase * 64) * 2 * Math.PI / 64 + Math.Sin(y * 0.2) * 1.2);
		return Fx(25 * fade, 0, 3.5 * band * fade, 1.2 * Math.Cos(y * 0.3 + phase * 2 * Math.PI) * fade);
	}
	// Box print stamped on the bare card of the ground
	static void PaintStamps(LevelCanvas c, int[][] places)
	{
		string[] texts = { "FRAGILE", "THIS SIDE UP", "HANDLE WITH CARE", "Nº 7" };
		double[][] inks = { C(168, 44, 40), C(56, 44, 40), C(56, 44, 40), C(56, 44, 40) };
		foreach (int[] p in places) {
			string text = texts[p[2]];
			double[] ink = inks[p[2]];
			float size = (p[2] == 0 ? 22 : (p[2] == 3 ? 26 : 15));
			Img img = Img.Gdi(260, 90, g => {
				g.TranslateTransform(130, 45); g.RotateTransform(p[3]);
				using (Font f = new Font("Arial Black", size, FontStyle.Bold, GraphicsUnit.Pixel)) {
					SizeF sz = g.MeasureString(text, f);
					using (Pen pen = new Pen(Gc(ink), 3)) g.DrawRectangle(pen, -sz.Width / 2 - 6, -sz.Height / 2 - 2, sz.Width + 12, sz.Height + 4);
					using (StringFormat sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center }) g.DrawString(text, f, Br(ink), 0, 0, sf);
				}
			});
			// Ink doesn't take everywhere
			for (int py = 0; py < img.H; py++) for (int px = 0; px < img.W; px++) {
				uint q = img.Get(px, py);
				if ((q >> 24) == 0 || Noise(px, py, 3, 96, 91) < 0.3) continue;
				c.Blend(p[0] * T + px - 130, p[1] * T + py - 45, q);
			}
		}
	}
}
