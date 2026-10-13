using System;
using System.Collections.Generic;
using System.Drawing;

// The level itself: a cardboard countryside that turns into a film studio backlot and ends on a sound stage, with
// more than one way through most parts - over the clouds or through a burrow, across the roofs or through the sewer,
// along the stage floor, over the castle set or the catwalks - pits, collapsing planks, thumbtacks and wind in the
// way, and every special layer used where it belongs
public static partial class LevelGen
{
	public const int LW = 400, LH = 32;
	const int Ground = 22;					// The row the ground's surface is at, mostly
	const int G = Ground * T;
	const float Building = 0.5f;			// The facades are drawn at half their design size

	// Event types and their parameters, see README.md
	const int EvLevelStart = 1, EvCheckpoint = 3, EvSceneryDestruct = 4, EvSceneryCollapse = 7, EvOneWay = 9, EvHurt = 13;
	const int EvHForce = 23, EvText = 24, EvEndOfLevel = 25, EvSpring = 47, EvSignEol = 53;
	const int EvTurtle = 68, EvLizard = 69, EvLabRat = 72, EvBee = 82, EvDragonfly = 87, EvMadderHatter = 89;
	const int EvCoin = 107, EvGem = 108, EvGemGiant = 109, EvGemRing = 110, EvCarrot = 112, EvOneUp = 115, EvFastFire = 116, EvFood = 117;
	const int EvAmmo = 118, EvPowerUpWeapon = 119, EvCrate = 120, EvCrateAmmo = 122, EvCrateGem = 124, EvStopwatch = 61;

	// Where things are, in tiles
	const int CaveX0 = 75, FallX = 75, PoolX0 = 77, MarshX0 = 141, MarshX1 = 163, GateX = 172;
	const int TheaterX = 182, PlazaX0 = 195, PlazaX1 = 201, DinerX = 195, GlassX = 217, Shaft1X = 219, LensX = 222, DetectiveX = 226, PhotoX = 234, Shaft2X = 241, HangarX = 248;
	const int SewerX0 = 218, SewerX1 = 254, SewerFloor = G + 6 * T, SecretWallX = 249;
	const int StageX = 271, CastleX = 303, TankX0 = 332, TankX1 = 340, AquariumX = 320, WindX0 = 344, WindX1 = 366, FanX = 367, RiserX = 382, ExitX = 392;
	static readonly int[] Spots = { 288, 302, 316 };

	// A layer being built: static art drawn onto a canvas, plus animated props whose frames are cut into animated
	// tiles (the static art under them composited into every frame)
	class LayerBuilder
	{
		public Layer Layer;
		public LevelCanvas Canvas;
		public List<object[]> Anims = new List<object[]>();	// x, y (pixels, tile-aligned), Img[] frames, speed
		public LayerBuilder(Layer layer) { Layer = layer; Canvas = new LevelCanvas(layer.Width, layer.Height); }
		public void Anim(int x, int y, Img[] frames, double speed) { Anims.Add(new object[] { x, y, frames, speed }); }
	}

	static Tile CropTile(Img img, int ox, int oy)
	{
		Tile t = new Tile();
		for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t.Set(x, y, img.Get(ox + x, oy + y));
		return t;
	}

	static Tile OverTile(Tile top, Tile bottom)
	{
		Tile t = new Tile();
		for (int i = 0; i < T * T; i++) t.Px[i] = Over(top.Px[i], bottom.Px[i]);
		return t;
	}

	static Dictionary<string, int> _animIds = new Dictionary<string, int>();

	static int AnimFor(Level l, int[] frames, double speed)
	{
		string key = speed + ":" + string.Join(",", frames);
		int id;
		if (!_animIds.TryGetValue(key, out id)) { id = l.AddAnim(speed, frames); _animIds[key] = id; }
		return id;
	}

	// Cuts a layer into tiles. In an x-ray layer a prop's last frame is its mask frame and a single frame is shown only
	// in the mask, so those stay animated even when they don't change
	static void BuildLayer(Level l, LayerBuilder b)
	{
		bool xray = (b.Layer.Renderer == 3);
		foreach (var kv in b.Canvas.Chunks) {
			int tx = kv.Key % b.Layer.Width, ty = kv.Key / b.Layer.Width;
			b.Layer.Set(tx, ty, AddTile(b.Canvas.TileAt(tx, ty)));
		}
		foreach (object[] a in b.Anims) {
			int x0 = (int)a[0], y0 = (int)a[1];
			Img[] frames = (Img[])a[2];
			double speed = (double)a[3];
			for (int ty = 0; ty < frames[0].H / T; ty++) for (int tx = 0; tx < frames[0].W / T; tx++) {
				int cx = x0 / T + tx, cy = y0 / T + ty;
				Tile under = b.Canvas.TileAt(cx, cy);
				int[] ids = new int[frames.Length];
				bool same = true, empty = true;
				for (int f = 0; f < frames.Length; f++) {
					Tile frame = CropTile(frames[f], tx * T, ty * T);
					// The static art under a mask frame stays, so a prop doesn't cut it out
					ids[f] = AddTile(OverTile(frame, under));
					if (ids[f] != ids[0]) same = false;
					if (ids[f] != 0) empty = false;
				}
				if (empty) continue;
				if (same && !(xray && frames.Length == 1)) { b.Layer.Set(cx, cy, ids[0]); continue; }
				b.Layer.Set(cx, cy, AnimFor(l, ids, speed));
			}
		}
	}

	// Water from column x0 to x1 with its surface at `surface` and its deepest at `bottom` (pixels): its tint on the
	// overlay and its reflection on the blur layer, only where the ground (`groundAt`) is below the surface, and fading
	// out into the ground under it, so its edge is the shore wherever the bank is. Mist is still, water ripples
	const int WaterFrames = 24;

	static void Water(LayerBuilder overlay, LayerBuilder blur, Func<double, double> groundAt, int x0, int x1, double surface, double bottom, bool mist)
	{
		int top = (int)Math.Floor(surface / T), rows = (int)Math.Ceiling((bottom + 16) / T) - top;
		int frames = mist ? 1 : WaterFrames;
		double depth = bottom - surface;
		Img[] fx = new Img[frames];
		for (int f = 0; f < frames; f++) fx[f] = new Img((x1 - x0 + 1) * T, rows * T);
		for (int px = x0 * T; px < (x1 + 1) * T; px++) {
			double ground = groundAt(px + 0.5);
			if (ground <= surface + 1) continue;
			for (int y = top * T; y < (top + rows) * T; y++) {
				double inGround = y + 0.5 - ground;
				double k = inGround <= 0 ? 1 : 1 - Smooth(0, 14, inGround);
				if (k <= 0) break;
				double d = y + 0.5 - surface;
				uint tint;
				if (mist) {
					if (d < 0) continue;
					double f = Math.Min(1, d / depth);
					double wisp = 0.6 * Noise(px * 0.25, d * 0.5, 16, 64, 131) + 0.4 * Noise(px * 0.5, d, 16, 64, 133);
					tint = d < 2 ? Rgba(235, 240, 255, 200) : Rgba(205 + 20 * f, 210 + 20 * f, 232, 26 + 80 * f + 40 * Smooth(0.35, 0.8, wisp));
					fx[0].Set(px - x0 * T, y - top * T, Fade(Fx(255 * Math.Pow(f, 0.7) + Dither(px, y) * 2, 255 * Math.Pow(1 - f, 1.4) + Dither(px + 1, y) * 2), k, true));
				} else {
					tint = WaterTint(px, y, surface, depth);
					for (int f = 0; f < frames; f++) fx[f].Set(px - x0 * T, y - top * T, Fade(WaterFx(px, y, surface, depth, (double)f / frames), k, true));
				}
				overlay.Canvas.Blend(px, y, Fade(tint, k, false));
			}
		}
		if (mist) blur.Canvas.Draw(fx[0], x0 * T, top * T);
		else blur.Anim(x0 * T, top * T, fx, 1);
	}

	// ---- The solid foreground ----

	static Terrain BuildTerrain()
	{
		Terrain t = new Terrain(LW, LH);
		// A cardboard box: its right and bottom edges a pixel in, so stacked boxes stay apart
		Action<int, int, int, int> crate = (x, y, w, h) => t.Box(x * T, y * T, (x + w) * T - 1, (y + h) * T - 1, 2, Mat.Crate);

		// The meadow: rolling hills, a burrow under them and clouds over them
		t.Flat(0, 4, G, Mat.Grass);
		t.Curve(5, 10, G, G - 48, Mat.Grass);
		t.Flat(11, 13, G - 48, Mat.Grass);
		t.Curve(14, 19, G - 48, G - 3 * T, Mat.Grass);
		t.Flat(20, 23, G - 3 * T, Mat.Grass);
		t.Curve(24, 28, G - 3 * T, G - 24, Mat.Grass);
		t.Curve(29, 32, G - 24, G, Mat.Grass);
		// The lake under the jetty, its banks curving into it
		t.Curve(33, 36, G, G + 3 * T, Mat.Grass);
		t.Flat(37, 50, G + 3 * T, Mat.Grass);
		t.Curve(51, 54, G + 3 * T, G - 16, Mat.Grass);
		t.Flat(55, 56, G - 16, Mat.Grass);
		// Up the hill to the waterfall, which falls into a pool at the foot of the cliff, a cave behind it
		t.Curve(57, 62, G - 16, G - 4 * T, Mat.Grass);
		t.Flat(63, 64, G - 4 * T, Mat.Grass);
		t.Hump(65, 72, G - 4 * T, 32, Mat.Grass);
		t.Flat(73, 76, G - 4 * T, Mat.Grass);
		t.Flat(77, 82, G + 4 * T, Mat.Grass);
		t.Curve(83, 86, G + 4 * T, G + 2 * T, Mat.Grass);
		t.Flat(87, 89, G + 2 * T, Mat.Grass);
		t.Curve(90, 95, G + 2 * T, G, Mat.Grass);
		// The box canyon: pits to the bottom of the level between islands of boxes, collapsing planks over the widest,
		// thumbtacks spilled where a careless jump lands, a hill after it
		t.Flat(96, 103, G, Mat.Grass);
		t.Gap(104, 107);
		t.Flat(108, 113, G, Mat.Grass);
		t.Gap(114, 119);
		t.Flat(120, 126, G, Mat.Grass);
		t.Gap(127, 131);
		t.Flat(132, 133, G, Mat.Grass);
		t.Hump(134, 140, G, 48, Mat.Grass);
		// The misty marsh, its banks curving into it, and a hill after it
		t.Curve(MarshX0, 145, G, G + 4 * T, Mat.Grass);
		t.Flat(146, 158, G + 4 * T, Mat.Grass);
		t.Curve(159, MarshX1, G + 4 * T, G, Mat.Grass);
		t.Hump(164, GateX - 1, G, 48, Mat.Grass);
		// The studio: the backlot street, a raised plaza on it, the sewer under it, then the stage
		t.Flat(GateX, 190, G, Mat.Street);
		t.Curve(191, PlazaX0 - 1, G, G - 2 * T, Mat.Street);
		t.Flat(PlazaX0, PlazaX1, G - 2 * T, Mat.Street);
		t.Curve(PlazaX1 + 1, 205, G - 2 * T, G, Mat.Street);
		t.Flat(206, 259, G, Mat.Street);
		t.Hump(260, StageX - 1, G, 32, Mat.Street);
		// The stage: a riser with ramps, the wave tank sunk into the floor, the riser the exit is on
		t.Flat(StageX, 279, G, Mat.Stage);
		t.Curve(280, 283, G, G - 2 * T, Mat.Stage);
		t.Flat(284, 292, G - 2 * T, Mat.Stage);
		t.Curve(293, 296, G - 2 * T, G, Mat.Stage);
		t.Flat(297, TankX0 - 1, G, Mat.Stage);
		t.Flat(TankX0, TankX1, G + 3 * T, Mat.Stage);
		t.Flat(TankX1 + 1, RiserX - 1, G, Mat.Stage);
		t.Flat(RiserX, LW - 1, G - 4 * T, Mat.Stage);

		// Holes: the burrow and its shafts, the cave, the sewer and its manholes (the shafts open wherever the surface is)
		Func<int, double> top = x => Math.Min(t.SurfaceAt(x * T + 0.5), t.SurfaceAt((x + 2) * T - 0.5)) - 16;
		t.Hole(8 * T, G + T + 8, 31 * T, G + 3 * T + 8, 14);
		t.Hole(8 * T, top(8), 10 * T, G + 2 * T, 6);
		t.Hole(28 * T, top(28), 30 * T, G + 2 * T, 6);
		t.Hole(CaveX0 * T + 4, G - 1.6 * T, 77.5 * T, G + 2 * T, 12);
		t.Hole(SewerX0 * T, G + 2 * T + 16, SewerX1 * T, SewerFloor, 10);
		t.Hole(Shaft1X * T, top(Shaft1X), (Shaft1X + 2) * T, G + 3 * T, 4);
		t.Hole(Shaft2X * T, top(Shaft2X), (Shaft2X + 2) * T, G + 3 * T, 4);

		// Platforms: clouds, a floating ledge over the plateau, the jetty, the planks, the boxes
		foreach (int[] c in Clouds) t.Box(c[0] * T, c[1] * T, (c[0] + c[2]) * T, c[1] * T + 14, 7, Mat.Cloud);
		t.Poly(Mat.Grass, 61 * T, 14 * T + 4, 68 * T, 12 * T, 68 * T, 12 * T + 26, 61 * T, 14 * T + 30);
		t.Box(69 * T, 10 * T, 73 * T, 10 * T + 12, 3, Mat.Plank);
		t.Box(33 * T, G, 42 * T, G + 12, 3, Mat.Plank);
		t.Box(45 * T, G, 52 * T, G + 12, 3, Mat.Plank);
		crate(100, 20, 2, 2);
		crate(109, 20, 2, 2); crate(110, 18, 2, 2);
		crate(121, 21, 1, 1); crate(122, 20, 2, 2); crate(124, 19, 2, 3);
		t.Box(114 * T, 20 * T, 116 * T, 20 * T + 12, 3, Mat.Plank);
		t.Box(117 * T, 19 * T, 119 * T, 19 * T + 12, 3, Mat.Plank);
		t.Box(128 * T, 17 * T, 131 * T, 17 * T + 14, 7, Mat.Cloud);
		t.Tacks.Add(new[] { 112, 113 });
		foreach (int[] s in MarshSteps) t.Box(s[0] * T, s[1] * T + 16, (s[0] + 3) * T, s[1] * T + 28, 3, Mat.Plank);
		// Road works on the street, the wall of boxes in the sewer, the roofs
		crate(206, 21, 1, 1); crate(207, 20, 2, 2); crate(209, 19, 2, 3);
		crate(214, 19, 2, 3);
		t.Tacks.Add(new[] { 211, 213 });
		t.Box(SecretWallX * T, 25 * T, (SecretWallX + 2) * T - 1, SewerFloor - 1, 2, Mat.Crate);
		foreach (int[] r in Roofs) t.Box(r[0] * T + 4, r[2], r[1] * T - 4, r[2] + 12, 3, Mat.Plank);
		// The stage: the catwalks and the stairs up to the first, the castle set with an archway through it, its
		// towers and battlements, clouds hanging over the wave tank, a raft in it, boxes and tacks in the wind, boxes up
		// to the exit's riser and a balcony over it
		t.Box(StageX * T, 3 * T + 8, 331 * T, 3 * T + 20, 2, Mat.Plank);
		t.Box(343 * T, 7 * T + 8, 381 * T, 7 * T + 20, 2, Mat.Plank);
		foreach (int[] s in StageStairs) t.Box(s[0] * T, s[1] * T, (s[0] + 3) * T, s[1] * T + 12, 3, Mat.Plank);
		t.Box(CastleX * T + T, 16 * T, (CastleX + 5) * T, G, 0, Mat.Castle);
		t.Box((CastleX + 9) * T, 16 * T, (CastleX + 13) * T, G, 0, Mat.Castle);
		t.Box((CastleX + 5) * T, 16 * T, (CastleX + 9) * T, 18 * T, 0, Mat.Castle);
		t.Box(CastleX * T, 13 * T, (CastleX + 3) * T, 16 * T, 0, Mat.Castle);
		t.Box((CastleX + 11) * T, 13 * T, (CastleX + 14) * T, 16 * T, 0, Mat.Castle);
		foreach (int[] c in StageClouds) t.Box(c[0] * T, c[1] * T, (c[0] + c[2]) * T, c[1] * T + 14, 7, Mat.Cloud);
		t.Box(334 * T, 20 * T, 338 * T, 20 * T + 12, 3, Mat.Plank);
		crate(356, 20, 2, 2);
		foreach (int px in WindPlanks) t.Box(px * T, 12 * T, (px + 3) * T, 12 * T + 12, 3, Mat.Plank);
		t.Tacks.Add(new[] { 352, 354 }); t.Tacks.Add(new[] { 360, 362 });
		crate(374, 21, 1, 1); crate(375, 20, 2, 2); crate(377, 19, 2, 3); crate(379, 18, 3, 4);
		t.Box(384 * T, 13 * T, 391 * T, 13 * T + 12, 3, Mat.Plank);
		t.Build();
		return t;
	}

	static readonly int[][] Clouds = { new[] { 11, 15, 4 }, new[] { 17, 12, 4 }, new[] { 23, 9, 4 }, new[] { 29, 8, 4 }, new[] { 35, 9, 4 }, new[] { 41, 10, 4 },
		new[] { 47, 9, 4 }, new[] { 53, 11, 4 }, new[] { 58, 12, 4 }, new[] { 75, 9, 3 }, new[] { 79, 11, 4 }, new[] { 85, 12, 4 } };
	static readonly int[][] StageClouds = { new[] { 333, 17, 3 }, new[] { 337, 14, 3 }, new[] { 339, 11, 3 }, new[] { 342, 9, 3 } };
	static readonly int[] WindPlanks = { 348, 358 };
	static readonly int[][] MarshSteps = { new[] { 145, 20 }, new[] { 149, 19 }, new[] { 153, 20 }, new[] { 157, 19 } };
	// Roofs: from column, to column, top in pixels
	static readonly int[][] Roofs = { new[] { TheaterX, TheaterX + 7, 15 * T + 4 }, new[] { DinerX, DinerX + 6, 15 * T + 25 }, new[] { DetectiveX, DetectiveX + 6, 16 * T + 2 },
		new[] { PhotoX, PhotoX + 6, 16 * T + 10 }, new[] { HangarX, HangarX + 9, 14 * T + 4 } };
	static readonly int[][] StageStairs = { new[] { 272, 19 }, new[] { 276, 16 }, new[] { 272, 13 }, new[] { 276, 10 }, new[] { 272, 7 }, new[] { 276, 5 },
		new[] { 299, 19 }, new[] { 300, 16 } };

	// ---- The level ----

	public static string Generate(uint[] palette, string tilesetPath, string levelPath, string tilesetName, int spawnX)
	{
		ResetTiles(); _animIds.Clear();
		Level l = new Level(LW, LH) { Name = "Jazz² Resurrection - LayerFX", Tileset = tilesetName, Music = "carrotus.j2b", Flags = 0x03 };
		l.Texts.Add("\f[c:#FFE080]Cardboard Studios\f[/c]\nVisitors welcome - mind the scenery!");
		l.Texts.Add("\f[c:#FFE080]Stage 1\f[/c]\nQuiet please, we're rolling!");

		Terrain t = BuildTerrain();
		t.Stamps = new LevelCanvas(LW, LH);
		PaintStamps(t.Stamps, new[] { new[] { 14, 28, 0, -6 }, new[] { 45, 28, 3, -8 }, new[] { 67, 23, 1, 4 }, new[] { 89, 28, 2, -3 },
			new[] { 137, 27, 1, 5 }, new[] { 152, 29, 0, -4 } });
		int[,] terrain = SliceTerrain(t);
		Layer spr = new Layer(2, LW, LH);
		for (int y = 0; y < LH; y++) for (int x = 0; x < LW; x++) spr.Set(x, y, terrain[x, y]);
		// The row the ground's surface is in at a column
		Func<int, int> groundRow = x => (int)Math.Floor(t.SurfaceAt(x * T + 16) / T);

		// Layers in front of the sprite layer: x-ray (frontmost), the overlay, the blur; and the decoration behind it
		// (the sprite layer is at depth 50, so the decoration is > 50) in front of the background
		LayerBuilder xray = new LayerBuilder(new Layer(0, LW, LH) { Depth = -100, Renderer = 3 });
		LayerBuilder overlay = new LayerBuilder(new Layer(0, LW, LH) { Depth = -70 });
		LayerBuilder blur = new LayerBuilder(new Layer(0, LW, LH) { Depth = -60, Renderer = 4 });
		LayerBuilder deco = new LayerBuilder(new Layer(0, LW, LH) { Depth = 100 });
		Func<double, double> ground = x => t.SurfaceAt(x);

		// ---- The meadow ----
		// Bushes cut out where something is behind them; the burrow under the meadow; puffs of card on the clouds
		foreach (int[] bush in new[] { new[] { 0, 1 }, new[] { 19, 2 } }) {
			xray.Anim(bush[0] * T, (groundRow(bush[0] + 2) - 3) * T, new[] { PaintBush(160, 96, bush[1]), new Img(160, 96) }, 0);
		}
		PaintBurrowBack(deco.Canvas, 8 * T, G + 2, 31 * T, G + 4 * T);
		foreach (int[] c in Clouds) deco.Canvas.Draw(PaintCloudPuffs(c[2] * T + 24, c[0] * 7 + c[1]), c[0] * T - 12, c[1] * T - 34);
		deco.Canvas.Draw(PaintCloudPuffs(3 * T + 24, 129), 128 * T - 12, 17 * T - 34);

		// ---- The lake: jetty posts behind it, the water in front, its reflection rippling ----
		foreach (int px in new[] { 37, 41, 46, 50 }) deco.Canvas.Draw(PaintPost(3 * T + 8), px * T, G + 8);
		Water(overlay, blur, ground, 33, 54, G + 14, G + 3 * T, false);

		// ---- The waterfall, the cave behind it, the pool it falls into ----
		const int FallFrames = 16;
		Img[] fall = new Img[FallFrames + 1];
		for (int f = 0; f < FallFrames; f++) fall[f] = PaintWaterfall(4 * T, 7 * T, f, FallFrames, false);
		fall[FallFrames] = PaintWaterfall(4 * T, 7 * T, 0, FallFrames, true);
		xray.Anim(FallX * T, (Ground - 4) * T, fall, 1.0);
		PaintBurrowBack(deco.Canvas, CaveX0 * T, G - 2 * T, 78 * T, G + 2 * T);
		Water(overlay, blur, ground, PoolX0, 86, G + 2 * T + 12, G + 4 * T, false);

		// ---- The misty marsh: a reflection that fades into blur with depth, under a mist ----
		Water(overlay, blur, ground, MarshX0, MarshX1, G + 8, G + 4 * T, true);
		deco.Canvas.Draw(PaintReeds(96, 5 * T, 21), (MarshX0 - 2) * T, G - 3 * T + 8);
		deco.Canvas.Draw(PaintReeds(96, 5 * T, 22), (MarshX1 - 1) * T, G - 3 * T + 8);

		// ---- The studio gate, and the green screen in it: whoever walks behind it is keyed out to outer space ----
		xray.Canvas.Draw(PaintGate(7 * T, 7 * T), GateX * T, (Ground - 7) * T);
		xray.Anim((GateX + 1) * T, (Ground - 5) * T, new[] { PaintGreenScreen(5 * T, 5 * T, false), PaintGreenScreen(5 * T, 5 * T, true) }, 0);

		// ---- The backlot street ----
		// The theater: its marquee bulbs chase, and over whatever is behind it, it is a paper screen lit from behind
		{
			List<PointF> bulbs;
			Img theater = PaintTheater(7 * T, 7 * T, Building, out bulbs);
			Func<int, int, int> bulbAt = (x, y) => {
				int best = 0; double bd = 1e9;
				for (int i = 0; i < bulbs.Count; i++) { double d = (bulbs[i].X - x) * (bulbs[i].X - x) + (bulbs[i].Y - y) * (bulbs[i].Y - y); if (d < bd) { bd = d; best = i; } }
				return best;
			};
			Img[] frames = new Img[5];
			for (int f = 0; f < 4; f++) frames[f] = ResolveKeys(theater, f, bulbAt);
			frames[4] = ShadowPlay(theater);
			xray.Anim(TheaterX * T, (Ground - 7) * T, frames, 6);
		}
		// The other facades, each seen through its own way, their frosted panes blurring what is behind them
		Action<Img, int, int, Func<Img, Img>> facade = (img, x, y, mask) => {
			xray.Anim(x * T, y * T, new[] { ResolveKeys(img, 0, (a, b) => 0), mask(img) }, 0);
			for (int py = 0; py < img.H; py++) for (int px = 0; px < img.W; px++)
				if (IsKey(img.Get(px, py), KeyFrost)) blur.Canvas.Blend(x * T + px, y * T + py, Fx(235, 0));
		};
		facade(PaintDetective(6 * T, 6 * T, Building), DetectiveX, Ground - 6, Blacklight);
		facade(PaintPhoto(6 * T, 6 * T, Building), PhotoX, Ground - 6, PhotoNegative);
		facade(PaintHangar(9 * T, 8 * T, Building), HangarX, Ground - 8, Blueprint);
		// A wall of glass blocks bending what is behind it
		{
			int w = 2 * T, h = 5 * T;
			overlay.Canvas.Draw(PaintGlassBlockFrame(w, h), GlassX * T, (Ground - 5) * T);
			for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) blur.Canvas.Blend(GlassX * T + x, (Ground - 5) * T + y, GlassBlockFx(x, y, w, h));
		}
		// The diner on the plaza, behind the street, its awning in front of it, the awning's shade only on what is under
		// it, and heat shimmering over its chimney
		{
			int baseRow = Ground - 2;
			deco.Canvas.Draw(ResolveKeys(PaintDiner(6 * T, 5 * T, Building), 0, (a, b) => 0), DinerX * T, (baseRow - 5) * T);
			deco.Canvas.Draw(PaintChimney(24, 40), (DinerX + 5) * T, (baseRow - 5) * T - 11);
			blur.Anim((DinerX + 5) * T, (baseRow - 9) * T, Haze(T, 4, 16), 3.75);
			overlay.Canvas.Draw(PaintAwning(6 * T, 28, Building), DinerX * T, (baseRow - 5) * T + 86);
			Img shade = new Img(6 * T, 2 * T);
			for (int y = 0; y < shade.H; y++) for (int x = 4; x < shade.W - 4; x++) shade.Set(x, y, Rgba(16, 8, 40, 110 - y * 0.4));
			xray.Anim(DinerX * T, (baseRow - 2) * T, new[] { shade }, 0);
		}
		// Lamps, the magnifying glass with gems behind its lens (another one on the meadow, and a reducing glass on the
		// stage), puddles
		foreach (int lx in new[] { PlazaX1, LensX + 3, PhotoX - 1, 246, 258 }) deco.Canvas.Draw(PaintLamp(4 * T + 16), lx * T - 16, (groundRow(lx) - 4) * T);
		{
			double cx = 1.5 * T, cy = 1.5 * T, radius = 1.5 * T - 6;
			// Column, top in pixels (so the stand reaches the ground), magnification
			foreach (int[] m in new[] { new[] { LensX, G - 4 * T, -42 }, new[] { 11, G - 48 - 4 * T, -42 }, new[] { 327, G - 4 * T, 50 } }) {
				blur.Canvas.Draw(PaintLens(3 * T, 3 * T, cx, cy, radius, m[2] / 100.0), m[0] * T, m[1]);
				overlay.Canvas.Draw(PaintMagnifierFrame(3 * T, 4 * T, cx, cy, radius), m[0] * T, m[1]);
			}
		}
		foreach (int[] p in new[] { new[] { 189, 2 }, new[] { 244, 2 } }) {
			int w = p[1] * T;
			Img[] frames = new Img[WaterFrames];
			for (int f = 0; f < WaterFrames; f++) {
				frames[f] = new Img(w, T);
				for (int y = 0; y < T; y++) for (int x = 0; x < w; x++) frames[f].Set(x, y, PuddleFx(x, y, w, (double)f / WaterFrames));
			}
			blur.Anim(p[0] * T, G, frames, 2);
			for (int y = 0; y < T; y++) for (int x = 0; x < w; x++) overlay.Canvas.Blend(p[0] * T + x, G + y, PuddleTint(x, y, w));
		}

		// ---- The sewer under the street: brick walls, a film of water on its floor, steam rising out of a manhole, a
		// wall of boxes with a secret behind it ----
		PaintSewerBack(deco.Canvas, SewerX0 * T, G + 2, SewerX1 * T, SewerFloor + 8);
		Water(overlay, blur, x => SewerFloor, SewerX0, SecretWallX - 1, SewerFloor - 10, SewerFloor, false);
		blur.Anim(Shaft2X * T, 18 * T, Haze(2 * T, 9, 16), 3.75);

		// ---- The sound stage ----
		PaintStageBack(deco.Canvas, StageX * T, LW * T, 0, G + 3 * T + 8);
		{
			int[] lampX = new int[Spots.Length];
			for (int i = 0; i < Spots.Length; i++) lampX[i] = (Spots[i] - StageX) * T + 16;
			xray.Canvas.Draw(PaintTruss(60 * T, lampX), StageX * T, 3 * T);
		}
		// Wires the hanging clouds and the second catwalk hang from, behind them
		{
			Img wires = new Img((381 - 333) * T, 13 * T);
			Action<int, int> wire = (x, y) => { for (int py = 0; py < y; py++) { wires.Set(x - 333 * T, py, Rgb(40, 34, 48)); wires.Set(x - 333 * T + 1, py, Rgb(70, 62, 80)); } };
			foreach (int[] c in StageClouds) { wire(c[0] * T + 12, c[1] * T + 4); wire((c[0] + c[2]) * T - 12, c[1] * T + 4); }
			for (int x = 344; x <= 380; x += 6) wire(x * T, 7 * T + 10);
			foreach (int px in WindPlanks) { for (int py = 7 * T + 20; py < 12 * T + 4; py++) { wires.Set(px * T + 10 - 333 * T, py, Rgb(40, 34, 48)); wires.Set((px + 3) * T - 10 - 333 * T, py, Rgb(40, 34, 48)); } }
			deco.Canvas.Draw(wires, 333 * T, 0);
			foreach (int[] c in StageClouds) deco.Canvas.Draw(PaintCloudPuffs(c[2] * T + 24, c[0] * 5 + c[1]), c[0] * T - 12, c[1] * T - 34);
		}
		// Stars and a moon of card hanging over the stage
		foreach (int[] s in new[] { new[] { 283, 6, 0 }, new[] { 296, 8, 0 }, new[] { 309, 7, 1 }, new[] { 323, 9, 0 }, new[] { 351, 3, 0 }, new[] { 363, 4, 0 } })
			deco.Canvas.Draw(PaintHangingStar(s[2] == 1 ? 72 : 48, s[1] * T, s[2] == 1, 100 + s[0]), s[0] * T, 0);
		// Pennants on the castle's towers
		foreach (int tx in new[] { CastleX + 1, CastleX + 12 }) deco.Canvas.Draw(PaintPennant(), tx * T - 4, 13 * T - 56);
		// Spotlights: the light in the air, the pool of it on the floor under it, its color on whoever stands in it
		double[][] spotColors = { C(255, 120, 200), C(90, 220, 255), C(255, 230, 90) };
		for (int i = 0; i < Spots.Length; i++) {
			int cx = Spots[i] * T + 16, top = 5 * T + 4, floor = (int)t.SurfaceAt(cx), h = floor - top;
			int rows = (floor - 5 * T + T - 1) / T;
			Img tint = new Img(8 * T, rows * T);
			for (int y = top; y < floor; y++) for (int x = cx - 4 * T; x < cx + 4 * T; x++) {
				overlay.Canvas.Blend(x, y, ConeLight(x + 0.5 - cx, y - top, h, 28, 200, C(255, 248, 220), 46));
				tint.Set(x - (cx - 4 * T), y - 5 * T, ConeLight(x + 0.5 - cx, y - top, h, 28, 200, spotColors[i], 120));
			}
			for (int y = floor - 10; y < floor + 6; y++) for (int x = cx - 110; x < cx + 110; x++) {
				double u = (x + 0.5 - cx) / 100.0, v = (y + 0.5 - floor + 2) / 8.0;
				double d = u * u + v * v;
				if (d < 1) overlay.Canvas.Blend(x, y, Rgba(255, 246, 210, 60 * (1 - d)));
			}
			xray.Anim(cx - 4 * T - 16, 5 * T, new[] { tint }, 0);
		}
		// Smoke machines, the air shimmering over them
		foreach (int mx in new[] { 278, 297 }) {
			deco.Canvas.Draw(PaintSmokeMachine(), mx * T - 8, G - 44);
			blur.Anim(mx * T, (Ground - 9) * T, Haze(2 * T, 8, 16), 3.75);
		}
		// The glossy floor: a shallow reflection that fades out downwards and at both ends of each stretch of floor
		foreach (int[] floor in new[] { new[] { StageX + 1, 279 }, new[] { 284, 292 }, new[] { 297, CastleX - 1 }, new[] { CastleX + 14, TankX0 - 1 }, new[] { TankX1 + 1, WindX0 - 1 }, new[] { WindX1 + 6, RiserX - 9 } }) {
			int x0 = floor[0] * T, x1 = (floor[1] + 1) * T, y0 = (int)t.SurfaceAt(x0 + 1);
			for (int y = 0; y < 14; y++) for (int x = x0; x < x1; x++) {
				double h = Smooth(0, 64, x - x0 + 0.5) * Smooth(0, 64, x1 - x - 0.5);
				double v = y / 13.0;
				blur.Canvas.Blend(x, y0 + y, Fx(60 * h * v * (1 - v) * 4 + Dither(x, y) * 2, 120 * h * Math.Pow(1 - v, 1.4) + Dither(x + 1, y) * 2));
			}
		}
		// The aquarium: whoever walks behind it is seen through the water, bent and softened
		{
			int w = 6 * T, h = 7 * T, x0 = AquariumX * T, y0 = (Ground - 7) * T;
			overlay.Canvas.Draw(PaintAquariumFront(w, h), x0, y0);
			const int BubbleFrames = 12;
			Img[] bubbles = new Img[BubbleFrames], fx = new Img[BubbleFrames];
			for (int f = 0; f < BubbleFrames; f++) {
				bubbles[f] = PaintAquariumBubbles(w, h, f, BubbleFrames);
				fx[f] = new Img(w, h);
				for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) fx[f].Set(x, y, AquariumFx(x, y, w, (double)f / BubbleFrames));
			}
			overlay.Anim(x0, y0, bubbles, 5);
			blur.Anim(x0, y0, fx, 5);
		}
		// The wave tank: its water rising and falling, its reflection with it, cardboard waves in front
		{
			const int TideFrames = 48;
			Img[] fx = new Img[TideFrames], tint = new Img[TideFrames];
			for (int f = 0; f < TideFrames; f++) {
				double s = Math.Round(18 + 14 * Math.Cos(f * 2 * Math.PI / TideFrames));
				fx[f] = new Img((TankX1 - TankX0 + 1) * T, 3 * T); tint[f] = new Img((TankX1 - TankX0 + 1) * T, 3 * T);
				for (int y = 0; y < 3 * T; y++) for (int x = 0; x < fx[f].W; x++) {
					fx[f].Set(x, y, y + 0.5 < s ? 0 : Fx(20 + 120 * Math.Min(1, (y - s) / 70.0), 225 * (1 - 0.5 * Math.Min(1, (y - s) / 70.0))));
					tint[f].Set(x, y, WaterTint(TankX0 * T + x, G + y, s + G, 3 * T - s));
				}
			}
			blur.Anim(TankX0 * T, G, fx, 2.5);
			overlay.Anim(TankX0 * T, G, tint, 2.5);
			overlay.Canvas.Draw(PaintStageWaves((TankX1 - TankX0 + 1) * T), TankX0 * T, G + T - 8);
		}
		// The wind machine: its blades turning, the air it blows rippling to the left
		{
			const int FanFrames = 8;
			Img[] fan = new Img[FanFrames];
			for (int f = 0; f < FanFrames; f++) fan[f] = PaintFan(4 * T, 6 * T, f, FanFrames);
			deco.Anim(FanX * T, (Ground - 6) * T, fan, 1);
			const int WindFrames = 16;
			int w = (WindX1 - WindX0 + 1) * T, h = 7 * T;
			Img[] wind = new Img[WindFrames];
			for (int f = 0; f < WindFrames; f++) {
				wind[f] = new Img(w, h);
				for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) wind[f].Set(x, y, WindFx(x, y, w, h, (double)f / WindFrames));
			}
			blur.Anim(WindX0 * T, (Ground - 7) * T, wind, 1.5);
		}
		// Frosted glass of all shapes before and on the riser of the exit: a tall screen, a pane on the boxes that is
		// clear at its top and frosted at its bottom, an arched pane, a porthole and a diamond, each tinted its own way
		{
			Func<int, int, bool> rect = (x, y) => true;
			FrostedPane(overlay.Canvas, blur.Canvas, 372 * T, 15 * T, 2 * T, 7 * T, rect, C(226, 238, 255), 30, 255, 255);
			FrostedPane(overlay.Canvas, blur.Canvas, 377 * T, 15 * T, 2 * T, 4 * T, rect, C(226, 238, 255), 24, 0, 255);
			FrostedPane(overlay.Canvas, blur.Canvas, 385 * T, 14 * T, 2 * T, 4 * T,
				(x, y) => y >= T || (x + 0.5 - T) * (x + 0.5 - T) + (y + 0.5 - T) * (y + 0.5 - T) <= T * T, C(255, 150, 200), 60, 220, 220);
			FrostedPane(overlay.Canvas, blur.Canvas, 388 * T, 15 * T, 3 * T, 3 * T,
				(x, y) => (x + 0.5 - 48) * (x + 0.5 - 48) + (y + 0.5 - 48) * (y + 0.5 - 48) <= 46 * 46, C(120, 200, 255), 50, 255, 160);
			FrostedPane(overlay.Canvas, blur.Canvas, 396 * T, 15 * T, 3 * T, 3 * T,
				(x, y) => Math.Abs(x + 0.5 - 48) + Math.Abs(y + 0.5 - 48) <= 47, C(255, 220, 120), 50, 200, 255);
		}

		// ---- The background ----
		const int MountW = 20, MountH = 16, HillW = 24, HillH = 26;
		int[,] mountains = Slice(PaintMountains(MountW * T, MountH * T), MountW * T, MountH * T);
		int[,] hills = Slice(PaintHills(HillW * T, HillH * T), HillW * T, HillH * T);
		Layer hillsLayer = new Layer(0, HillW, HillH) { Depth = 200, RepeatX = true, SpeedX = 0.55f, SpeedY = 0.55f, OffsetY = -20 };
		for (int y = 0; y < HillH; y++) for (int x = 0; x < HillW; x++) hillsLayer.Set(x, y, hills[x, y]);
		Layer mountLayer = new Layer(0, MountW, MountH) { Depth = 300, RepeatX = true, SpeedX = 0.25f, SpeedY = 0.25f, OffsetY = -70 };
		for (int y = 0; y < MountH; y++) for (int x = 0; x < MountW; x++) mountLayer.Set(x, y, mountains[x, y]);
		Layer skyLayer = new Layer(0, 1, 18) { Depth = 400, RepeatX = true, SpeedX = 0, SpeedY = 0, OffsetY = 197 };
		for (int y = 0; y < 18; y++) {
			Tile s = new Tile();
			for (int py = 0; py < T; py++) for (int px = 0; px < T; px++) s.Set(px, py, SkyColor(Math.Max(0, (y - 4) * T + py), px, py));
			skyLayer.Set(0, y, AddTile(s));
		}

		BuildLayer(l, xray); BuildLayer(l, overlay); BuildLayer(l, blur); BuildLayer(l, deco);
		l.Layers.AddRange(new[] { overlay.Layer, blur.Layer, xray.Layer, spr, deco.Layer, hillsLayer, mountLayer, skyLayer });
		l.CaptionTile = terrain[2, Ground];

		PlaceEvents(l, t, spawnX);

		int firstAnimated = _tiles.Count;
		_lastLevel = l;
		if (string.IsNullOrEmpty(tilesetPath)) return "preview only";
		WriteTileset(tilesetPath, palette);
		WriteLevel(levelPath, l, firstAnimated);
		return string.Format("{0} tiles, {1} animated tiles, level {2}x{3}", _tiles.Count, l.Anims.Count, LW, LH);
	}

	static Level _lastLevel;

	// Draws a part of the level as the layers stack up, for checking the art without the game: the parallax background
	// is a flat sky, the blur layer (which only says what to do with what is behind it) is left out, animated tiles
	// show their first frame and events are marked with dots
	public static void PreviewLevel(int x0, int y0, int w, int h, int scale, string path)
	{
		Level l = _lastLevel;
		List<Layer> layers = new List<Layer>(l.Layers);
		layers.RemoveAll(layer => layer.Width != l.Width || layer.Renderer == 4);
		layers.Sort((a, b) => (b.Type == 2 ? 50 : b.Depth).CompareTo(a.Type == 2 ? 50 : a.Depth));
		using (Bitmap bmp = new Bitmap(w * T * scale, h * T * scale)) {
			for (int py = 0; py < h * T; py++) for (int px = 0; px < w * T; px++) {
				int gx = x0 * T + px, gy = y0 * T + py;
				uint c = SkyColor(Math.Max(0, gy * 0.45 - 60), gx, gy);
				foreach (Layer layer in layers) {
					int id = layer.Get(gx / T, gy / T);
					if (id < 0) id = l.Anims[-1 - id].Frames[0];
					if (id != 0) c = Over(_tiles[id].Get(gx % T, gy % T), c);
				}
				Event e = l.Events[(gy / T) * l.Width + gx / T];
				if (e != null && e.Type != EvOneWay && Math.Abs(gx % T - 16) < 3 && Math.Abs(gy % T - 16) < 3) c = Rgb(255, 0, 255);
				var col = System.Drawing.Color.FromArgb((int)(c & 0xFF), (int)((c >> 8) & 0xFF), (int)((c >> 16) & 0xFF));
				for (int sy = 0; sy < scale; sy++) for (int sx = 0; sx < scale; sx++) bmp.SetPixel(px * scale + sx, py * scale + sy, col);
			}
			bmp.Save(path);
		}
	}

	// ---- Events ----

	static void OneWay(Level l, int x0, int x1, int row) { for (int x = x0; x <= x1; x++) l.Ev(x, row, EvOneWay); }

	static void PlaceEvents(Level l, Terrain t, int spawnX)
	{
		// The row an actor standing on the ground at a column is placed in
		Func<int, int> on = x => (int)Math.Floor((t.SurfaceAt(x * T + 16) - 1) / T);
		const int Burrow = Ground + 2, Sewer = SewerFloor / T - 1;

		// The meadow: a spring up to the clouds, the burrow under it with a spring back out
		l.Ev(13, on(13), EvSpring, 0, 0, 1);
		foreach (int[] c in Clouds) {
			OneWay(l, c[0], c[0] + c[2] - 1, c[1]);
			for (int x = c[0]; x < c[0] + c[2]; x++) l.Ev(x, c[1] - 1, x % 2 == 0 ? EvGem : EvCoin, x % 2 == 0 ? 1 : 0);
		}
		l.Ev(30, 7, EvFood, 7); l.Ev(48, 8, EvFastFire);
		for (int x = 5; x <= 10; x += 2) l.Ev(x, on(x) - 1, EvCoin, 0);
		for (int x = 14; x <= 19; x += 2) l.Ev(x, on(x) - 1, EvCoin, 0);
		l.Ev(11, on(11), EvTurtle, 0);
		l.Ev(22, on(22), EvLizard, 0);
		for (int x = 11; x <= 26; x += 3) l.Ev(x, Burrow, EvGem, 0);
		l.Ev(19, Burrow, EvCarrot, 1);
		l.Ev(29, Burrow, EvSpring, 0, 0, 1);

		// The lake: gems along the jetty, gold coins over the gap in it, a crate on the bottom
		for (int x = 34; x <= 41; x++) l.Ev(x, Ground - 2, EvGem, 0);
		for (int x = 42; x <= 44; x++) l.Ev(x, Ground - 4, EvCoin, 1);
		for (int x = 46; x <= 51; x += 2) l.Ev(x, Ground - 2, EvGem, 2);
		l.Ev(44, Ground + 2, EvCrateGem, 0, 2, 2, 0);
		l.Ev(44, Ground - 6, EvDragonfly);

		// The hill and the plateau, the ledge and the plank above it, the cave behind the waterfall
		for (int x = 57; x <= 62; x++) l.Ev(x, on(x) - 1, EvCoin, 0);
		l.Ev(63, on(63), EvCrateAmmo, 0);
		l.Ev(67, on(67), EvLizard, 0);
		l.Ev(73, on(73), EvTurtle, 0);
		for (int x = 62; x <= 67; x++) l.Ev(x, (int)Math.Floor((14 * T + 4 - (x - 61 + 0.5) * 2 * T / 7.0) / T) - 1, EvGem, 2);
		OneWay(l, 69, 72, 10);
		l.Ev(71, 9, EvPowerUpWeapon, 1);
		l.Ev(74, 14, EvBee);
		l.Ev(76, Ground + 1, EvOneUp);
		l.Ev(76, Ground, EvGem, 3);
		l.Ev(88, on(88), EvCheckpoint, 0);
		l.Ev(90, on(90) - 3, EvDragonfly);
		for (int x = 90; x <= 95; x++) l.Ev(x, on(x) - 1, EvCoin, 0);

		// The box canyon: coins in arcs over the pits, collapsing planks, gems on the boxes, bees over the gaps
		for (int x = 104; x <= 107; x++) l.Ev(x, x == 104 || x == 107 ? 18 : 17, EvCoin, 0);
		l.Ev(100, 19, EvGem, 1); l.Ev(101, 19, EvGem, 1);
		l.Ev(110, 17, EvGem, 2); l.Ev(111, 17, EvGem, 2);
		for (int x = 112; x <= 113; x++) l.Ev(x, Ground, EvHurt, 1);
		for (int x = 114; x <= 115; x++) l.Ev(x, 20, EvSceneryCollapse, 30);
		for (int x = 117; x <= 118; x++) l.Ev(x, 19, EvSceneryCollapse, 30);
		l.Ev(116, 17, EvCoin, 1); l.Ev(119, 16, EvCoin, 1);
		l.Ev(106, 15, EvBee); l.Ev(129, 13, EvBee);
		l.Ev(124, 18, EvGem, 3); l.Ev(125, 18, EvGem, 3);
		OneWay(l, 128, 130, 17);
		l.Ev(129, 16, EvCarrot, 0);
		l.Ev(126, Ground - 1, EvTurtle, 0); l.Ev(138, on(138), EvLizard, 0);
		for (int x = 134; x <= 140; x += 2) l.Ev(x, on(x) - 1, EvCoin, 0);

		// The marsh: gems on the stepping planks, things flying over it, a full carrot on its bottom
		foreach (int[] s in MarshSteps) {
			OneWay(l, s[0], s[0] + 2, s[1]);
			for (int x = s[0]; x <= s[0] + 2; x++) l.Ev(x, s[1] - 1, EvGem, 1);
		}
		l.Ev(148, 15, EvDragonfly); l.Ev(155, 14, EvDragonfly); l.Ev(152, 16, EvBee);
		l.Ev(152, Ground + 3, EvCarrot, 1);
		l.Ev(167, on(167), EvFood, 3);

		// The gate of the studio
		l.Ev(GateX + 2, Ground - 1, EvText, 0, 0, 0);
		l.Ev(179, Ground - 1, EvCheckpoint, 0);

		// The backlot street: behind the facades, on their roofs and the plaza, over the road works, and in the sewer
		l.Ev(181, Ground - 1, EvSpring, 0, 0, 1);
		l.Ev(186, Ground - 1, EvTurtle, 0);
		for (int x = TheaterX + 1; x <= TheaterX + 5; x += 2) l.Ev(x, Ground - 2, EvGem, 2);
		foreach (int[] r in Roofs) for (int x = r[0] + 1; x < r[1] - 1; x++) l.Ev(x, r[2] / T - 1, x % 2 == 0 ? EvCoin : EvGem, x % 2 == 0 ? 1 : 0);
		foreach (int[] r in Roofs) OneWay(l, r[0], r[1] - 1, r[2] / T);
		for (int x = 191; x <= 205; x += 2) if (x < PlazaX0 || x > PlazaX1) l.Ev(x, on(x) - 1, EvCoin, 0);
		l.Ev(198, on(198), EvMadderHatter);
		l.Ev(196, on(196) - 1, EvFood, 34); l.Ev(200, on(200) - 1, EvFood, 6);
		l.Ev(207, 19, EvGem, 0); l.Ev(209, 18, EvGem, 2);
		for (int x = 211; x <= 213; x++) { l.Ev(x, Ground, EvHurt, 1); l.Ev(x, 16, EvCoin, 1); }
		l.Ev(214, 18, EvGem, 2); l.Ev(215, 18, EvGem, 2);
		l.Ev(GlassX, Ground - 2, EvGem, 1); l.Ev(GlassX + 1, Ground - 2, EvGem, 1);
		l.Ev(LensX + 1, Ground - 3, EvGem, 2); l.Ev(LensX + 1, Ground - 2, EvGem, 3);
		l.Ev(12, 17, EvGem, 1); l.Ev(12, 18, EvGem, 2);
		l.Ev(328, Ground - 3, EvGem, 2); l.Ev(328, Ground - 2, EvGem, 3);
		l.Ev(373, 18, EvGem, 1); l.Ev(373, 20, EvGem, 1); l.Ev(378, 16, EvGem, 2); l.Ev(389, 16, EvGem, 3); l.Ev(397, 16, EvGem, 3);
		l.Ev(229, Ground - 1, EvLabRat);
		l.Ev(231, Ground - 1, EvCrate, EvOneUp, 0, 1);
		l.Ev(PhotoX + 1, Ground - 2, EvGem, 3); l.Ev(PhotoX + 4, Ground - 2, EvGem, 3);
		l.Ev(237, Ground - 1, EvTurtle, 0);
		l.Ev(253, Ground - 1, EvTurtle, 0);
		for (int x = 261; x <= 269; x += 2) l.Ev(x, on(x) - 1, EvCoin, 1);
		l.Ev(264, on(264) - 1, EvAmmo, 3); l.Ev(266, on(266) - 1, EvAmmo, 3);
		for (int x = SewerX0 + 3; x <= SecretWallX - 3; x += 2) l.Ev(x, Sewer, x % 4 == 0 ? EvGem : EvCoin, x % 4 == 0 ? 2 : 1);
		l.Ev(227, Sewer, EvLabRat); l.Ev(236, Sewer, EvLabRat);
		l.Ev(232, Sewer - 1, EvCrateAmmo, 0);
		l.Ev(Shaft2X, Sewer, EvSpring, 0, 0, 1);
		for (int y = 25; y <= 27; y++) for (int x = SecretWallX; x <= SecretWallX + 1; x++) l.Ev(x, y, EvSceneryDestruct, 0xFB, 0xFF);
		l.Ev(252, Sewer, EvOneUp); l.Ev(251, Sewer, EvGem, 3); l.Ev(253, Sewer, EvGem, 3);
		l.Ev(StageX + 1, Ground - 1, EvCheckpoint, 0);

		// The sound stage: the stairs and the catwalk, the riser, the castle set, the hanging clouds and the second
		// catwalk, the wind machine and the boxes up to the exit, a balcony over them
		l.Ev(StageX + 5, Ground - 1, EvText, 1, 0, 0);
		foreach (int[] s in StageStairs) OneWay(l, s[0], s[0] + 2, s[1]);
		OneWay(l, StageX, 330, 3);
		for (int x = 281; x <= 329; x += 3) l.Ev(x, 2, EvCoin, 1);
		l.Ev(295, 2, EvStopwatch); l.Ev(311, 2, EvCarrot, 0);
		for (int i = 0; i < Spots.Length; i++) for (int x = Spots[i] - 2; x <= Spots[i] + 2; x++) l.Ev(x, on(x) - 1, EvGem, i + 1);
		l.Ev(287, on(287), EvMadderHatter);
		l.Ev(CastleX + 7, Ground - 1, EvTurtle, 0);
		l.Ev(CastleX + 7, 15, EvMadderHatter);
		for (int x = CastleX; x <= CastleX + 2; x++) l.Ev(x, 12, EvGem, 3);
		for (int x = CastleX + 11; x <= CastleX + 13; x++) l.Ev(x, 12, EvGem, 3);
		l.Ev(CastleX + 6, 14, EvPowerUpWeapon, 4);
		l.Ev(312, 11, EvBee);
		l.Ev(AquariumX + 2, Ground - 1, EvGem, 3); l.Ev(AquariumX + 3, Ground - 1, EvGem, 3);
		l.Ev(326, Ground - 1, EvLabRat);
		for (int x = TankX0 + 1; x <= TankX1 - 1; x += 2) l.Ev(x, Ground + 2, EvGem, 2);
		OneWay(l, 334, 337, 20);
		l.Ev(336, 19, EvFood, 21);
		foreach (int[] c in StageClouds) { OneWay(l, c[0], c[0] + c[2] - 1, c[1]); l.Ev(c[0] + 1, c[1] - 1, EvGem, 2); }
		OneWay(l, 343, 380, 7);
		for (int x = 345; x <= 379; x += 2) l.Ev(x, 6, x % 6 == 1 ? EvGem : EvCoin, x % 6 == 1 ? 3 : 1);
		l.Ev(362, 6, EvBee);
		foreach (int px in WindPlanks) { OneWay(l, px, px + 2, 12); l.Ev(px + 1, 11, EvGem, 2); }
		for (int y = Ground - 7; y < Ground; y++) for (int x = WindX0; x <= WindX1; x++) l.Ev(x, y, EvHForce, 0, 0, 0, 0, 3, 0);
		foreach (int[] s in new[] { new[] { 352, 354 }, new[] { 360, 362 } }) for (int x = s[0]; x <= s[1]; x++) l.Ev(x, Ground, EvHurt, 1);
		l.Ev(356, 19, EvGem, 2); l.Ev(357, 19, EvGem, 2);
		l.Ev(342, Ground - 1, EvCarrot, 0);
		l.Ev(374, 20, EvGem, 1); l.Ev(376, 19, EvGem, 1); l.Ev(378, 18, EvGem, 1);
		OneWay(l, 384, 390, 13);
		for (int x = 385; x <= 389; x++) l.Ev(x, 12, EvGem, 3);
		l.Ev(384, Ground - 5, EvMadderHatter);
		l.Ev(388, Ground - 5, EvGemGiant);

		// The end of the level: the sign, and the area that ends it beside it
		l.Ev(ExitX, Ground - 5, EvSignEol, 1);
		for (int y = Ground - 7; y < Ground - 4; y++) for (int x = ExitX + 1; x <= ExitX + 3; x++) l.Ev(x, y, EvEndOfLevel, 1);

		// Coins are silver, but a few gold ones in the harder places
		foreach (Event e in l.Events) if (e != null && e.Type == EvCoin) e.Params[0] = 0;
		foreach (int[] g in new[] { new[] { 43, Ground - 4 }, new[] { 116, 17 }, new[] { 119, 16 }, new[] { 212, 16 }, new[] { 305, 2 }, new[] { 365, 6 } }) {
			Event e = l.Events[g[1] * l.Width + g[0]];
			if (e != null && e.Type == EvCoin) e.Params[0] = 1;
		}

		// Whatever was placed inside something solid (a box, a wall of the castle) is lifted out of it
		int[] lifted = { EvCoin, EvGem, EvGemGiant, EvGemRing, EvCarrot, EvOneUp, EvFastFire, EvFood, EvAmmo, EvPowerUpWeapon, EvCrate, EvCrateAmmo,
			EvCrateGem, EvStopwatch, EvTurtle, EvLizard, EvLabRat, EvBee, EvDragonfly, EvMadderHatter };
		Func<int, int, bool> solid = (x, y) => t.Grid[(y * T + 16) * t.PW + x * T + 16] != 0;
		for (int y = 0; y < l.Height; y++) for (int x = 0; x < l.Width; x++) {
			Event e = l.Events[y * l.Width + x];
			if (e == null || Array.IndexOf(lifted, e.Type) < 0 || !solid(x, y)) continue;
			int ny = y;
			while (ny > 0 && (solid(x, ny) || l.Events[ny * l.Width + x] != null)) ny--;
			l.Events[ny * l.Width + x] = e;
			l.Events[y * l.Width + x] = null;
		}

		// Last, so nothing placed on the same tile replaces it
		l.Ev(spawnX, on(spawnX), EvLevelStart, 7);
	}
}
