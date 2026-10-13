using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;

// Generates the "LayerFX" level and its tileset in the game's own formats (see README.md). Split into the core (tiles,
// image encoding and file writers, this file), the art (LevelGen.Art.cs) and the level itself (LevelGen.Level.cs).
public static partial class LevelGen
{
	public const int T = 32;

	public class Tile
	{
		public uint[] Px = new uint[T * T];
		public byte[] Mask = new byte[T * T / 8];

		public void Set(int x, int y, uint c) { Px[y * T + x] = c; }
		public uint Get(int x, int y) { return Px[y * T + x]; }
		public void SetMask(int x, int y) { Mask[y * 4 + (x >> 3)] |= (byte)(1 << (x & 7)); }
		public void MaskRow(int y) { for (int i = 0; i < 4; i++) Mask[y * 4 + i] = 0xFF; }
		public void MaskFrom(int y0) { for (int y = y0; y < T; y++) MaskRow(y); }
		public bool IsEmpty()
		{
			foreach (uint c in Px) if ((c >> 24) != 0) return false;
			foreach (byte m in Mask) if (m != 0) return false;
			return true;
		}
	}

	static List<Tile> _tiles = new List<Tile>();
	static Dictionary<ulong, List<int>> _dedupe = new Dictionary<ulong, List<int>>();

	public static void ResetTiles()
	{
		_tiles.Clear(); _dedupe.Clear();
		_tiles.Add(new Tile());	// Tile 0 is empty
	}

	static ulong HashTile(Tile t)
	{
		unchecked {
			ulong h = 14695981039346656037UL;
			foreach (uint c in t.Px) { h = (h ^ c) * 1099511628211UL; }
			foreach (byte m in t.Mask) { h = (h ^ m) * 1099511628211UL; }
			return h;
		}
	}

	static bool SameTile(Tile a, Tile b)
	{
		for (int i = 0; i < a.Px.Length; i++) if (a.Px[i] != b.Px[i]) return false;
		for (int i = 0; i < a.Mask.Length; i++) if (a.Mask[i] != b.Mask[i]) return false;
		return true;
	}

	// Adds a tile, or finds the same one added before; an empty tile is always tile 0. Fully transparent pixels are
	// normalized, so they never make two tiles differ
	public static int AddTile(Tile t)
	{
		for (int i = 0; i < t.Px.Length; i++) if ((t.Px[i] >> 24) == 0) t.Px[i] = 0;
		if (t.IsEmpty()) return 0;
		ulong h = HashTile(t);
		List<int> bucket;
		if (_dedupe.TryGetValue(h, out bucket)) {
			foreach (int id in bucket) if (SameTile(_tiles[id], t)) return id;
		} else {
			bucket = new List<int>();
			_dedupe[h] = bucket;
		}
		_tiles.Add(t);
		bucket.Add(_tiles.Count - 1);
		return _tiles.Count - 1;
	}

	// Cuts an image into tiles, returning the tile IDs of its grid
	public static int[,] Slice(uint[] img, int w, int h)
	{
		int cols = w / T, rows = h / T;
		int[,] ids = new int[cols, rows];
		for (int ty = 0; ty < rows; ty++) for (int tx = 0; tx < cols; tx++) {
			Tile t = new Tile();
			for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t.Set(x, y, img[(ty * T + y) * w + tx * T + x]);
			ids[tx, ty] = AddTile(t);
		}
		return ids;
	}

	// ---- Colors ----
	public static int Clamp(double v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)Math.Round(v)); }
	public static uint Rgba(double r, double g, double b, double a) { return (uint)(Clamp(r) | (Clamp(g) << 8) | (Clamp(b) << 16) | (Clamp(a) << 24)); }
	public static uint Rgb(double r, double g, double b) { return Rgba(r, g, b, 255); }
	public static uint Rgb(double[] c) { return Rgba(c[0], c[1], c[2], 255); }
	public static uint Rgb(double[] c, double k) { return Rgba(c[0] * k, c[1] * k, c[2] * k, 255); }
	public static double[] C(double r, double g, double b) { return new double[] { r, g, b }; }
	public static double Mix(double a, double b, double t) { return a + (b - a) * t; }
	public static double[] MixC(double[] a, double[] b, double t) { return C(Mix(a[0], b[0], t), Mix(a[1], b[1], t), Mix(a[2], b[2], t)); }
	public static uint MixC(double[] a, double[] b, double t, double alpha) { return Rgba(Mix(a[0], b[0], t), Mix(a[1], b[1], t), Mix(a[2], b[2], t), alpha); }
	public static double[] Scale(double[] c, double k) { return C(c[0] * k, c[1] * k, c[2] * k); }
	public static double Smooth(double e0, double e1, double x) { double t = Math.Max(0, Math.Min(1, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); }
	public static double Frac(double v) { return v - Math.Floor(v); }
	public static int Mod(int a, int m) { int r = a % m; return r < 0 ? r + m : r; }

	static readonly int[] Bayer = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	public static double Dither(int x, int y) { return (Bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5; }

	// Alpha blending of a color over another, both straight (not premultiplied)
	public static uint Over(uint top, uint bottom)
	{
		double ta = (top >> 24) / 255.0, ba = (bottom >> 24) / 255.0;
		double a = ta + ba * (1 - ta);
		if (a <= 0) return 0;
		Func<int, double> ch = s => (((top >> s) & 0xFF) * ta + ((bottom >> s) & 0xFF) * ba * (1 - ta)) / a;
		return Rgba(ch(0), ch(8), ch(16), a * 255);
	}

	// ---- Noise (wrapping value noise) ----
	public static double Hash(int x, int y, int seed)
	{
		unchecked {
			uint h = (uint)(x * 374761393 + y * 668265263 + seed * 2147483647);
			h = (h ^ (h >> 13)) * 1274126177;
			h ^= h >> 16;
			return (h & 0xFFFFFF) / (double)0x1000000;
		}
	}

	// Value noise of the given cell size, repeating every `period` pixels in both directions
	public static double Noise(double x, double y, int cell, int period, int seed)
	{
		double fx = x / cell, fy = y / cell;
		int ix = (int)Math.Floor(fx), iy = (int)Math.Floor(fy);
		double tx = fx - ix, ty = fy - iy;
		tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
		int p = Math.Max(1, period / cell);
		Func<int, int, double> h = (a, b) => Hash(((a % p) + p) % p, ((b % p) + p) % p, seed);
		double v00 = h(ix, iy), v10 = h(ix + 1, iy), v01 = h(ix, iy + 1), v11 = h(ix + 1, iy + 1);
		return Mix(Mix(v00, v10, tx), Mix(v01, v11, tx), ty);
	}

	// A texel of a blur-behind layer: red/green = displacement (128 = none, a step is a quarter of a pixel), blue =
	// reflection, alpha = blur (0 = the texel does nothing, 1-255 = no blur to full blur). `blur` and `reflection` are
	// 0-255, the displacement is in pixels
	public static uint Fx(double blur, double reflection, double dx = 0, double dy = 0)
	{
		return Rgba(128 + dx * 4, 128 + dy * 4, reflection, 1 + Math.Max(0, Math.Min(255, blur)) * 254 / 255);
	}

	// ---- Output ----
	static void W8(BinaryWriter w, int v) { w.Write((byte)v); }
	static void W16(BinaryWriter w, int v) { w.Write((ushort)v); }
	static void WStr(BinaryWriter w, string s) { byte[] b = System.Text.Encoding.UTF8.GetBytes(s); W8(w, b.Length); w.Write(b); }
	static void WVar(BinaryWriter w, uint v) { while (v >= 0x80) { W8(w, (int)((v & 0x7F) | 0x80)); v >>= 7; } W8(w, (int)v); }

	static byte[] Deflate(byte[] data)
	{
		using (MemoryStream ms = new MemoryStream()) {
			using (DeflateStream ds = new DeflateStream(ms, CompressionLevel.Optimal, true)) { ds.Write(data, 0, data.Length); }
			return ms.ToArray();
		}
	}

	public static uint[] ReadPaletteFrom(string tilesetPath)
	{
		using (FileStream fs = File.OpenRead(tilesetPath)) {
			fs.Seek(23 + 4, SeekOrigin.Begin);
			using (DeflateStream ds = new DeflateStream(fs, CompressionMode.Decompress)) {
				byte[] buf = new byte[1024];
				int read = 0;
				while (read < buf.Length) { int n = ds.Read(buf, read, buf.Length - read); if (n <= 0) break; read += n; }
				uint[] pal = new uint[256];
				Buffer.BlockCopy(buf, 0, pal, 0, 1024);
				return pal;
			}
		}
	}

	// The image encoding of the game's content files, which is QOI's: runs, a 64-entry index of recent colors, small
	// differences and literals
	static byte[] EncodeQoi(Func<int, uint> pixelAt, int count)
	{
		MemoryStream o = new MemoryStream();
		uint[] index = new uint[64];
		uint prev = 0xFF000000u;
		int run = 0;
		Func<uint, int> hash = c => (int)(((c & 0xFF) * 3 + ((c >> 8) & 0xFF) * 5 + ((c >> 16) & 0xFF) * 7 + (c >> 24) * 11) & 63);
		for (int i = 0; i < count; i++) {
			uint px = pixelAt(i);
			if (px == prev) {
				run++;
				if (run == 62 || i == count - 1) { o.WriteByte((byte)(0xC0 | (run - 1))); run = 0; }
				continue;
			}
			if (run > 0) { o.WriteByte((byte)(0xC0 | (run - 1))); run = 0; }
			int ip = hash(px);
			if (index[ip] == px) {
				o.WriteByte((byte)ip);
			} else {
				index[ip] = px;
				if ((px >> 24) == (prev >> 24)) {
					int vr = (sbyte)(byte)((px & 0xFF) - (prev & 0xFF));
					int vg = (sbyte)(byte)(((px >> 8) & 0xFF) - ((prev >> 8) & 0xFF));
					int vb = (sbyte)(byte)(((px >> 16) & 0xFF) - ((prev >> 16) & 0xFF));
					int vgr = vr - vg, vgb = vb - vg;
					if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2) {
						o.WriteByte((byte)(0x40 | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2)));
					} else if (vgr > -9 && vgr < 8 && vg > -33 && vg < 32 && vgb > -9 && vgb < 8) {
						o.WriteByte((byte)(0x80 | (vg + 32)));
						o.WriteByte((byte)((vgr + 8) << 4 | (vgb + 8)));
					} else {
						o.WriteByte(0xFE);
						o.WriteByte((byte)(px & 0xFF)); o.WriteByte((byte)((px >> 8) & 0xFF)); o.WriteByte((byte)((px >> 16) & 0xFF));
					}
				} else {
					o.WriteByte(0xFF);
					o.WriteByte((byte)(px & 0xFF)); o.WriteByte((byte)((px >> 8) & 0xFF));
					o.WriteByte((byte)((px >> 16) & 0xFF)); o.WriteByte((byte)(px >> 24));
				}
			}
			prev = px;
		}
		return o.ToArray();
	}

	static void WriteTileset(string path, uint[] palette)
	{
		int count = _tiles.Count;
		const int perRow = 32;
		int width = perRow * T, height = ((count + perRow - 1) / perRow) * T;

		using (BinaryWriter w = new BinaryWriter(File.Create(path))) {
			w.Write(0xB8EF8498E2BFBBEFUL);
			W16(w, 0x208F);
			W8(w, 2);			// Version
			W8(w, 0x20 | 0x40);	// Mask and palette included
			W8(w, 4);			// RGBA
			w.Write((uint)width);
			w.Write((uint)height);
			W16(w, count);

			MemoryStream block = new MemoryStream();
			BinaryWriter bw = new BinaryWriter(block);
			for (int i = 0; i < 256; i++) bw.Write(i == 0 ? 0u : palette[i]);
			for (int i = 0; i < (count + 7) / 8; i++) W8(bw, 0xFF);	// Every tile is 32-bit
			bw.Write((uint)(count * T * T / 8));
			foreach (Tile t in _tiles) bw.Write(t.Mask);
			bw.Flush();
			byte[] compressed = Deflate(block.ToArray());
			w.Write(compressed.Length);
			w.Write(compressed);

			w.Write(EncodeQoi(i => {
				int x = i % width, y = i / width;
				int ti = (y / T) * perRow + (x / T);
				return (ti < count ? _tiles[ti].Get(x % T, y % T) : 0u);
			}, width * height));
		}
	}

	// A preview of the tileset sheet, for checking the art
	public static void SavePreview(string path)
	{
		int perRow = 32, rows = (_tiles.Count + perRow - 1) / perRow;
		using (System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(perRow * T, rows * T)) {
			for (int i = 0; i < _tiles.Count; i++) for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
				uint c = _tiles[i].Get(x, y);
				int a = (int)(c >> 24);
				int bg = (((x / 4) + (y / 4)) % 2 == 0) ? 200 : 160;
				int r = (int)(c & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)((c >> 16) & 0xFF);
				r = (r * a + bg * (255 - a)) / 255; g = (g * a + bg * (255 - a)) / 255; b = (b * a + bg * (255 - a)) / 255;
				bmp.SetPixel((i % perRow) * T + x, (i / perRow) * T + y, System.Drawing.Color.FromArgb(r, g, b));
			}
			bmp.Save(path);
		}
	}

	// Draws a grid of tiles (animated ones at their first frame, -1 - index as in a layer) over a background color, for
	// checking the art without the game
	public static void PreviewGrid(List<int[,]> grids, List<Anim> anims, int x0, int y0, int w, int h, uint background, string path)
	{
		using (System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(w * T, h * T)) {
			for (int py = 0; py < h * T; py++) for (int px = 0; px < w * T; px++) {
				uint c = background;
				foreach (int[,] g in grids) {
					int tx = x0 + px / T, ty = y0 + py / T;
					if (tx >= g.GetLength(0) || ty >= g.GetLength(1)) continue;
					int id = g[tx, ty];
					if (id < 0) id = anims[-1 - id].Frames[0];
					if (id == 0) continue;
					c = Over(_tiles[id].Get(px % T, py % T), c);
				}
				bmp.SetPixel(px, py, System.Drawing.Color.FromArgb((int)(c & 0xFF), (int)((c >> 8) & 0xFF), (int)((c >> 16) & 0xFF)));
			}
			bmp.Save(path);
		}
	}

	// ---- Level ----
	public class Layer
	{
		public int Type, Width, Height, Depth, Renderer;
		public bool RepeatX, RepeatY;
		public float SpeedX = 1, SpeedY = 1, OffsetX, OffsetY;
		public byte[] Color = { 255, 255, 255, 255 };
		public int[] Tiles; public int[] Flags;
		public Layer(int type, int w, int h) { Type = type; Width = w; Height = h; Tiles = new int[w * h]; Flags = new int[w * h]; }
		public void Set(int x, int y, int id) { Set(x, y, id, 0); }
		public void Set(int x, int y, int id, int flags) { if (x < 0 || y < 0 || x >= Width || y >= Height) return; Tiles[y * Width + x] = id; Flags[y * Width + x] = flags; }
		public int Get(int x, int y) { return (x < 0 || y < 0 || x >= Width || y >= Height) ? 0 : Tiles[y * Width + x]; }
	}

	public class Anim
	{
		public int[] Frames;
		public double Speed;		// Game frames per animation frame, 0 = never advances
		public bool PingPong;
	}

	public class Event
	{
		public int Type;
		public byte[] Params;
	}

	public class Level
	{
		public string Name, Tileset, Music = "", Next = "", Secret = "", Bonus = "";
		public int Width, Height;
		public uint AmbientLight = 0xFF000000u;		// Fully lit
		public int Flags;							// LevelFlags, e.g. 0x03 = falling out of the bottom kills
		public int CaptionTile;
		public List<Anim> Anims = new List<Anim>();
		public List<Layer> Layers = new List<Layer>();
		public List<string> Texts = new List<string>();
		public Event[] Events;

		public Level(int w, int h) { Width = w; Height = h; Events = new Event[w * h]; }
		// Adds an animated tile, returning what a layer refers to it by (resolved when the level is written)
		public int AddAnim(double speed, params int[] frames)
		{
			Anims.Add(new Anim { Frames = frames, Speed = speed });
			return -Anims.Count;
		}
		public void Ev(int x, int y, int type, params int[] p)
		{
			if (x < 0 || y < 0 || x >= Width || y >= Height) throw new ArgumentException("Event out of the level at " + x + "," + y);
			byte[] b = new byte[16];
			for (int i = 0; i < p.Length; i++) b[i] = (byte)p[i];
			Events[y * Width + x] = new Event { Type = type, Params = b };
		}
	}

	static void WriteLevel(string path, Level l, int firstAnimatedTile)
	{
		MemoryStream ls = new MemoryStream();
		BinaryWriter w = new BinaryWriter(ls);
		WStr(w, l.Name);
		WStr(w, l.Next); WStr(w, l.Secret); WStr(w, l.Bonus);
		WStr(w, l.Tileset);
		WStr(w, l.Music);
		w.Write(l.AmbientLight);
		W8(w, 0); W8(w, 0);						// Weather
		W16(w, 32767);							// No water
		W16(w, l.CaptionTile);
		W8(w, 0);								// Additional palettes
		W8(w, 0);								// Extra tilesets
		WVar(w, 0); WVar(w, 0);					// Overridden tiles
		W8(w, l.Texts.Count);
		foreach (string s in l.Texts) { byte[] b = System.Text.Encoding.UTF8.GetBytes(s); W16(w, b.Length); w.Write(b); }

		W16(w, firstAnimatedTile);
		W16(w, l.Anims.Count);
		foreach (Anim a in l.Anims) {
			W8(w, a.Frames.Length);
			W16(w, (int)Math.Round(a.Speed * 16));	// Frame duration, stored times 16
			W16(w, 0); W16(w, 0);				// Delay, delay jitter
			W8(w, a.PingPong ? 1 : 0); W16(w, 0);
			foreach (int f in a.Frames) { W8(w, 0); W16(w, f); }
		}

		W8(w, l.Layers.Count);
		foreach (Layer layer in l.Layers) {
			W8(w, layer.Type);
			W16(w, (layer.RepeatX ? 0x01 : 0) | (layer.RepeatY ? 0x02 : 0) | 0x08);
			w.Write(layer.Width); w.Write(layer.Height);
			if (layer.Type != 2) {
				W8(w, 0);							// Speed models
				w.Write(layer.OffsetX); w.Write(layer.OffsetY);
				w.Write(layer.SpeedX); w.Write(layer.SpeedY);
				w.Write(0f); w.Write(0f);			// Auto speed
				w.Write((short)layer.Depth);
				W8(w, layer.Renderer);
				foreach (byte c in layer.Color) W8(w, c);
			}
			for (int i = 0; i < layer.Tiles.Length; i++) {
				// Animated tiles are referenced as -1 - their index until the number of static tiles is known
				int id = layer.Tiles[i];
				W8(w, layer.Flags[i]);
				W16(w, id < 0 ? firstAnimatedTile - 1 - id : id);
			}
		}

		for (int i = 0; i < l.Width * l.Height; i++) {
			Event e = l.Events[i];
			W16(w, e == null ? 0 : e.Type);
			if (e == null) {
				W8(w, 0x71);						// All difficulties, no parameters
			} else {
				W8(w, 0x70);						// All difficulties
				w.Write(e.Params);
			}
		}
		WVar(w, 0);								// Off-grid events
		w.Flush();

		byte[] compressedLevel = Deflate(ls.ToArray());
		using (BinaryWriter lw = new BinaryWriter(File.Create(path))) {
			lw.Write(0x2095A59FF0BFBBEFUL);
			W8(lw, 1);								// ContentFileType::Level
			W16(lw, 1);								// LevelFormat::CurrentVersion
			W16(lw, l.Flags);						// LevelFlags
			lw.Write(compressedLevel.Length);
			lw.Write(compressedLevel);
		}
	}
}
