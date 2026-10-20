#include "MaskOverlay.h"

#if defined(WITH_EDITOR)

#include "RuntimeSync.h"
#include "../Common/ToolMath.h"
#include "../UI/UiPainter.h"
#include "../../Rendering/PolylineRenderer.h"
#include "../../Tiles/TileMap.h"
#include "../../Tiles/TileSet.h"
#include "../../../nCine/Base/TimeStamp.h"

#include <algorithm>
#include <cstring>

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// Most memory the chunks take, those drawn the longest time ago go first, less where memory is short; a chunk
		// is 512 KB close up and 8 KB from far away. Chunks are made for some time of a frame, at least one, the others
		// follow in the next frames, so moving the view doesn't stall; the chunks are small from far away, a few hundred
		// of them are in view, so many are made in that time then.
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t MaxBytes = 4 * 1024 * 1024;
		constexpr std::int32_t MaxBuildsPerFrame = 2;
		constexpr float MaxBuildMilliseconds = 2.0f;
#else
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_WINDOWS_RT)
		constexpr std::size_t MaxBytes = 8 * 1024 * 1024;
#	else
		constexpr std::size_t MaxBytes = 32 * 1024 * 1024;
#	endif
		constexpr std::int32_t MaxBuildsPerFrame = 128;
		constexpr float MaxBuildMilliseconds = 4.0f;
#endif
		// Pixels a tile takes on the screen at least for the outline to be drawn, it'd only hide the fill below that
		constexpr float MinOutlineCellPixels = 8.0f;
		// How solid parts look, as luminance and opacity, and their outline
		constexpr std::uint8_t InsideLuminance = 0;
		constexpr std::uint8_t InsideOpacity = 120;
		constexpr Colorf OutlineColor = Colorf(1.0f, 1.0f, 1.0f, 0.66f);
		// Steps of the outline closer than this to a straight line are straightened, in pixels of the chunk image
		constexpr float OutlineTolerance = 0.55f;
		// Floats of vertices of the outlines drawn in a frame
		constexpr std::int32_t MaxOutlineFloats = 1 << 20;

		using ToolMath::FloorDivide;
		using ToolMath::PositiveModulo;

		// Edges of a cell between the middles of four pixels, which the outline crosses
		enum CellEdge : std::uint8_t { EdgeTop, EdgeRight, EdgeBottom, EdgeLeft, EdgeNone };

		// Lines across a cell by which of its corners are solid (top left 8, top right 4, bottom right 2, bottom left 1),
		// diagonal solid corners are connected
		constexpr CellEdge CellLines[16][4] = {
			{ EdgeNone, EdgeNone, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeBottom, EdgeNone, EdgeNone },
			{ EdgeBottom, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeTop, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeTop, EdgeBottom, EdgeRight },
			{ EdgeTop, EdgeBottom, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeTop, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeTop, EdgeNone, EdgeNone },
			{ EdgeTop, EdgeBottom, EdgeNone, EdgeNone },
			{ EdgeTop, EdgeRight, EdgeLeft, EdgeBottom },
			{ EdgeTop, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeBottom, EdgeRight, EdgeNone, EdgeNone },
			{ EdgeLeft, EdgeBottom, EdgeNone, EdgeNone },
			{ EdgeNone, EdgeNone, EdgeNone, EdgeNone },
		};
	}

	MaskOverlay::MaskOverlay()
		: _bytes(0), _outlineValid(false), _outlineStep(0), _outlineScale(0.0f), _layer(-1), _repeatX(false), _repeatY(false), _frame(0)
	{
	}

	MaskOverlay::~MaskOverlay()
	{
	}

	void MaskOverlay::Invalidate()
	{
		_chunks.clear();
		_bytes = 0;
		_layer = -1;
		_outlineValid = false;
	}

	void MaskOverlay::Release()
	{
		Invalidate();
		_outlineRenderer = nullptr;
		_solid = {};
		_pixels = {};
		_edgeLinks = {};
		_segments = {};
		_traced = {};
		_screenOutline = {};
		_screenOutlineEnds = {};
	}

	void MaskOverlay::InvalidateTiles(std::int32_t layer, const Recti& area)
	{
		if (layer != _layer) {
			return;
		}
		if (_repeatX || _repeatY) {
			// A tile of a repeated layer is in many chunks
			Invalidate();
			return;
		}
		// The outline of a chunk depends on the tiles next to it too
		for (auto it = _chunks.begin(); it != _chunks.end(); ) {
			const Vector2i& start = it->second.Start;
			if (start.X - 1 < area.X + area.W && start.X + ChunkTiles >= area.X && start.Y - 1 < area.Y + area.H && start.Y + ChunkTiles >= area.Y) {
				_bytes -= it->second.Bytes;
				it = _chunks.erase(it);
				_outlineValid = false;
			} else {
				++it;
			}
		}
	}

	void MaskOverlay::InvalidateTileIds(const LevelDocument& document, ArrayView<const std::uint16_t> sortedTileIds)
	{
		if (_chunks.empty() || sortedTileIds.empty()) {
			return;
		}
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			Invalidate();
			return;
		}
		const DocumentLayer& layer = document.Layers[_layer];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		auto showsAny = [&](const Chunk& chunk) {
			// With the tiles around, which the outline depends on
			for (std::int32_t ty = -1; ty <= ChunkTiles; ty++) {
				std::int32_t y = chunk.Start.Y + ty;
				if (_repeatY) {
					y = PositiveModulo(y, height);
				} else if (y < 0 || y >= height) {
					continue;
				}
				for (std::int32_t tx = -1; tx <= ChunkTiles; tx++) {
					std::int32_t x = chunk.Start.X + tx;
					if (_repeatX) {
						x = PositiveModulo(x, width);
					} else if (x < 0 || x >= width) {
						continue;
					}
					if (RuntimeSync::ShowsAnyTile(document, layer.GetTile(x, y), sortedTileIds)) {
						return true;
					}
				}
			}
			return false;
		};
		if (width <= 0 || height <= 0) {
			return;
		}
		for (auto it = _chunks.begin(); it != _chunks.end(); ) {
			if (showsAny(it->second)) {
				_bytes -= it->second.Bytes;
				it = _chunks.erase(it);
				_outlineValid = false;
			} else {
				++it;
			}
		}
	}

	std::uint64_t MaskOverlay::MakeKey(Vector2i start, std::int32_t step)
	{
		// The start is never negative, see Draw()
		return (std::uint64_t(std::uint32_t(start.X)) << 32) | (std::uint64_t(std::uint32_t(start.Y) & 0xFFFFFFu) << 8) | std::uint64_t(step & 0xFF);
	}

	void MaskOverlay::Draw(UiPainter& painter, TileMap& tileMap, const LevelDocument& document, std::int32_t layer,
		const LayerView& view, const Rectf& viewRect, bool alone)
	{
		_frame++;
		if (layer != _layer || view.RepeatX != _repeatX || view.RepeatY != _repeatY) {
			// The chunks of a repeated layer are known by where in its pattern they start
			Invalidate();
			_layer = layer;
			_repeatX = view.RepeatX;
			_repeatY = view.RepeatY;
		}

		if (alone) {
			// The masks only, the level is only hinted behind them
			painter.FillRect(viewRect, Colorf(0.42f, 0.42f, 0.48f, 0.94f), UiLayer::Back);
		}
		float cellSize = TileSet::DefaultTileSize * view.Scale;
		if (cellSize < 4.0f || layer < 0 || layer >= std::int32_t(document.Layers.size()) || view.Size.X <= 0 || view.Size.Y <= 0) {
			return;
		}

		// Fewer pixels of a chunk when the view is far, about as many as the screen shows of it
		std::int32_t step = (view.Scale >= 0.75f ? 1 : (view.Scale >= 0.375f ? 2 : (view.Scale >= 0.1875f ? 4 : 8)));
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X, viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X + viewRect.W, viewRect.Y + viewRect.H)));
		if (!view.RepeatX) {
			from.X = std::max<std::int32_t>(from.X, 0);
			to.X = std::min<std::int32_t>(to.X, view.Size.X - 1);
		}
		if (!view.RepeatY) {
			from.Y = std::max<std::int32_t>(from.Y, 0);
			to.Y = std::min<std::int32_t>(to.Y, view.Size.Y - 1);
		}

		// The outlines on the screen are the same as in the last frame while the view and the chunks in it stay the same,
		// they're far more points than chunks from far away; none when a tile is small on the screen
		const bool drawsOutline = (cellSize >= MinOutlineCellPixels);
		const Rectf origin = view.CellToScreen(Vector2i(0, 0));
		const bool reuseOutline = (_outlineValid && _outlineStep == step && _outlineScale == view.Scale && _outlineOrigin == Vector2f(origin.X, origin.Y) &&
			_outlineViewRect.X == viewRect.X && _outlineViewRect.Y == viewRect.Y && _outlineViewRect.W == viewRect.W && _outlineViewRect.H == viewRect.H);
		if (!reuseOutline) {
			_screenOutline.clear();
			_screenOutlineEnds.clear();
		}
		std::int32_t builds = 0;
		bool skipped = false;
		TimeStamp buildStart = TimeStamp::now();
		for (std::int32_t cy = FloorDivide(from.Y, ChunkTiles); cy <= FloorDivide(to.Y, ChunkTiles); cy++) {
			for (std::int32_t cx = FloorDivide(from.X, ChunkTiles); cx <= FloorDivide(to.X, ChunkTiles); cx++) {
				// Where the pattern of a layer that repeats is at the start of the chunk, the chunk is the same there
				Vector2i chunkStart(_repeatX ? PositiveModulo(cx * ChunkTiles, view.Size.X) : cx * ChunkTiles, _repeatY ? PositiveModulo(cy * ChunkTiles, view.Size.Y) : cy * ChunkTiles);
				std::uint64_t key = MakeKey(chunkStart, step);
				auto it = _chunks.find(key);
				Chunk* chunk;
				if (it != _chunks.end()) {
					chunk = &it->second;
				} else {
					if (builds >= MaxBuildsPerFrame || (builds > 0 && buildStart.millisecondsSince() >= MaxBuildMilliseconds)) {
						skipped = true;
						continue;
					}
					builds++;
					chunk = &_chunks[key];
					chunk->Start = chunkStart;
					chunk->Step = step;
					BuildChunk(*chunk, tileMap, document, view);
					_bytes += chunk->Bytes;
				}
				chunk->LastUsed = _frame;
				if (chunk->Empty) {
					continue;
				}
				Rectf first = view.CellToScreen(Vector2i(cx * ChunkTiles, cy * ChunkTiles));
				Rectf last = view.CellToScreen(Vector2i(cx * ChunkTiles + ChunkTiles - 1, cy * ChunkTiles + ChunkTiles - 1));
				Rectf rect(first.X, first.Y, last.X + last.W - first.X, last.Y + last.H - first.Y);
				if (chunk->Image != nullptr) {
					painter.Image(*chunk->Image, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Fill);
				}
				if (reuseOutline || !drawsOutline) {
					continue;
				}

				// The outline of all chunks is drawn at once, over the fill
				float texelSize = rect.W / float(ChunkTiles * (TileSet::DefaultTileSize / chunk->Step));
				std::int32_t start = 0;
				for (std::int32_t end : chunk->OutlineEnds) {
					for (std::int32_t i = start; i < end; i++) {
						const Vector2f& point = chunk->Outline[i];
						_screenOutline.emplace_back(rect.X + point.X * texelSize, rect.Y + point.Y * texelSize);
					}
					_screenOutlineEnds.push_back(std::int32_t(_screenOutline.size()));
					start = end;
				}
			}
		}
		// A chunk made while the outlines were kept isn't in them yet, nor is one left for the next frames
		_outlineValid = (reuseOutline ? builds == 0 : !skipped);
		_outlineStep = step;
		_outlineScale = view.Scale;
		_outlineOrigin = Vector2f(origin.X, origin.Y);
		_outlineViewRect = viewRect;

		if (!_screenOutlineEnds.empty()) {
			if (_outlineRenderer == nullptr) {
				_outlineRenderer = std::make_unique<Rendering::PolylineRenderer>(MaxOutlineFloats);
			}
			_outlineRenderer->BeginFrame();
			// Thin when the view is far, a little thicker when it's close, but never as thick as the pixels then are
			float halfThickness = std::max(1.0f, painter.GetScale()) * std::clamp(0.5f + 0.35f * view.Scale, 0.75f, 1.5f);
			// Far away, all the outlines are more than one command can draw
			SmallVector<RenderCommand*, 8> commands;
			_outlineRenderer->DrawMany(_screenOutline, _screenOutlineEnds, halfThickness, painter.GetDepth(UiLayer::Content), OutlineColor, commands);
			for (RenderCommand* command : commands) {
				painter.Command(command);
			}
		}

		DropOldChunks();
	}

	void MaskOverlay::BuildChunk(Chunk& chunk, TileMap& tileMap, const LevelDocument& document, const LayerView& view)
	{
		constexpr std::int32_t TileSize = TileSet::DefaultTileSize;
		const DocumentLayer& layer = document.Layers[_layer];
		std::int32_t step = chunk.Step;
		std::int32_t tileTexels = TileSize / step;
		// A whole tile around the chunk, so the outline knows what is next to its edges
		std::int32_t apron = tileTexels;
		std::int32_t size = ChunkTiles * tileTexels;
		std::int32_t stride = size + apron * 2;

		_solid.resize_for_overwrite(std::size_t(stride) * std::size_t(stride));
		std::memset(_solid.data(), 0, _solid.size());

		for (std::int32_t ty = -1; ty <= ChunkTiles; ty++) {
			for (std::int32_t tx = -1; tx <= ChunkTiles; tx++) {
				Vector2i tile;
				if (!view.Wrap(Vector2i(chunk.Start.X + tx, chunk.Start.Y + ty), tile)) {
					continue;
				}
				const TileRef& ref = layer.GetTile(tile.X, tile.Y);
				if (ref.IsEmpty()) {
					continue;
				}
				std::int32_t tileId = tileMap.ResolveStaticTileId(document.ToTileId(ref));
				const std::uint8_t* mask = (tileId >= 0 ? tileMap.GetStaticTileMask(tileId) : nullptr);
				if (mask == nullptr) {
					continue;
				}
				bool flipX = (ref.Flags & 0x01) != 0;
				bool flipY = (ref.Flags & 0x02) != 0;
				std::uint8_t* target = &_solid[std::size_t((ty + 1) * tileTexels) * stride + std::size_t((tx + 1) * tileTexels)];
				for (std::int32_t y = 0; y < tileTexels; y++) {
					// The middle of the pixels a texel stands for
					std::int32_t sy = y * step + step / 2;
					std::uint32_t row = TileSet::GetTileMaskRow(mask, flipY ? TileSize - 1 - sy : sy);
					if (row == 0) {
						continue;
					}
					for (std::int32_t x = 0; x < tileTexels; x++) {
						std::int32_t sx = x * step + step / 2;
						if ((row >> (flipX ? TileSize - 1 - sx : sx)) & 1) {
							target[std::size_t(y) * stride + x] = 1;
						}
					}
				}
			}
		}

		// Solid texels are darkened, the outline is drawn over them as lines
		_pixels.resize_for_overwrite(std::size_t(size) * std::size_t(size) * 2);
		bool any = false;
		for (std::int32_t y = 0; y < size; y++) {
			const std::uint8_t* solidRow = &_solid[std::size_t(y + apron) * stride + apron];
			std::uint8_t* out = &_pixels[std::size_t(y) * size * 2];
			for (std::int32_t x = 0; x < size; x++) {
				bool solid = (solidRow[x] != 0);
				any |= solid;
				out[x * 2] = InsideLuminance;
				out[x * 2 + 1] = (solid ? InsideOpacity : 0);
			}
		}

		chunk.Empty = !any;
		chunk.Outline.clear();
		chunk.OutlineEnds.clear();
		// The outline of the edge of a chunk can come from the solid pixels of the chunk next to it
		TraceOutline(chunk, size, stride, apron);
		chunk.Bytes = sizeof(Chunk) + chunk.Outline.size() * sizeof(Vector2f) + chunk.OutlineEnds.size() * sizeof(std::int32_t);
		if (!any) {
			chunk.Image = nullptr;
			chunk.Empty = chunk.OutlineEnds.empty();
			return;
		}
		chunk.Bytes += std::size_t(size) * std::size_t(size) * 2;
		// The luminance is the same everywhere, but RG8 is the only format sampled as gray with an opacity everywhere,
		// OpenGL|ES 2.0 and WebGL can't swizzle another one, see Texture::SetSwizzle()
		chunk.Image = std::make_unique<Texture>("EditorMaskChunk", Texture::Format::RG8, size, size);
		chunk.Image->LoadFromTexels(_pixels.data(), 0, 0, size, size);
		// Gray of the luminance, see-through by the opacity, smooth edges that the outline goes along
		chunk.Image->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Red, SwizzleChannel::Red, SwizzleChannel::Green);
		chunk.Image->SetMinFiltering(SamplerFilter::Linear);
		chunk.Image->SetMagFiltering(SamplerFilter::Linear);
		chunk.Image->SetWrap(SamplerWrapping::ClampToEdge);
	}

	void MaskOverlay::TraceOutline(Chunk& chunk, std::int32_t size, std::int32_t stride, std::int32_t apron)
	{
		// The chunk has the cells between the middles of its pixels and those of the next column and row, so each
		// cell belongs to a single chunk. An edge of a cell is known by the two pixels it lies between, the edge
		// between a pixel and the one to the right of it has an even number, that to the one below it an odd one.
		const std::int32_t columns = size + 1;
		const std::int32_t edgeCount = columns * columns * 2;
		if (std::int32_t(_edgeLinks.size()) < edgeCount * 2) {
			_edgeLinks.resize(std::size_t(edgeCount) * 2, -1);
		}
		auto edgeId = [columns](std::int32_t x, std::int32_t y, CellEdge edge) {
			switch (edge) {
				case EdgeTop: return (y * columns + x) * 2;
				case EdgeBottom: return ((y + 1) * columns + x) * 2;
				case EdgeLeft: return (y * columns + x) * 2 + 1;
				default: return (y * columns + x + 1) * 2 + 1;
			}
		};
		auto edgePosition = [columns](std::int32_t id) {
			std::int32_t cell = id / 2;
			float x = float(cell % columns), y = float(cell / columns);
			return ((id & 1) == 0 ? Vector2f(x + 1.0f, y + 0.5f) : Vector2f(x + 0.5f, y + 1.0f));
		};

		_segments.clear();
		for (std::int32_t y = 0; y < size; y++) {
			const std::uint8_t* row = &_solid[std::size_t(y + apron) * stride + apron];
			const std::uint8_t* below = row + stride;
			for (std::int32_t x = 0; x < size; x++) {
				std::int32_t index = (row[x] ? 8 : 0) | (row[x + 1] ? 4 : 0) | (below[x + 1] ? 2 : 0) | (below[x] ? 1 : 0);
				const CellEdge* lines = CellLines[index];
				for (std::int32_t i = 0; i < 4 && lines[i] != EdgeNone; i += 2) {
					std::int32_t a = edgeId(x, y, lines[i]);
					std::int32_t b = edgeId(x, y, lines[i + 1]);
					std::int32_t segment = std::int32_t(_segments.size()) / 2;
					_segments.push_back(a);
					_segments.push_back(b);
					// An edge is shared by the two cells next to it, so it's an end of two lines at most
					for (std::int32_t id : { a, b }) {
						_edgeLinks[id * 2 + (_edgeLinks[id * 2] < 0 ? 0 : 1)] = segment;
					}
				}
			}
		}

		std::int32_t segmentCount = std::int32_t(_segments.size()) / 2;
		SmallVector<bool, 0> visited;
		visited.resize(std::size_t(segmentCount), false);
		auto walk = [&](std::int32_t id, std::int32_t segment) {
			_traced.clear();
			_traced.push_back(edgePosition(id));
			while (segment >= 0 && !visited[segment]) {
				visited[segment] = true;
				std::int32_t next = (_segments[segment * 2] == id ? _segments[segment * 2 + 1] : _segments[segment * 2]);
				_traced.push_back(edgePosition(next));
				id = next;
				std::int32_t first = _edgeLinks[id * 2], second = _edgeLinks[id * 2 + 1];
				segment = (first == segment ? second : first);
			}
			// Steps of a slope become a straight line
			Rendering::PolylineRenderer::SimplifyRdp(_traced, OutlineTolerance, chunk.Outline);
			chunk.OutlineEnds.push_back(std::int32_t(chunk.Outline.size()));
		};
		// Lines cut off by the edge of the chunk first, from their open ends, then the closed ones
		for (std::int32_t segment = 0; segment < segmentCount; segment++) {
			for (std::int32_t end = 0; end < 2; end++) {
				std::int32_t id = _segments[segment * 2 + end];
				if (!visited[segment] && _edgeLinks[id * 2 + 1] < 0) {
					walk(id, segment);
				}
			}
		}
		for (std::int32_t segment = 0; segment < segmentCount; segment++) {
			if (!visited[segment]) {
				walk(_segments[segment * 2], segment);
			}
		}

		// Only the edges that were used are cleared, for the next chunk
		for (std::int32_t id : _segments) {
			_edgeLinks[id * 2] = -1;
			_edgeLinks[id * 2 + 1] = -1;
		}
	}

	void MaskOverlay::DropOldChunks()
	{
		if (_bytes <= MaxBytes) {
			return;
		}
		// Those drawn in this frame stay even if there are more of them, their images are still to be drawn
		SmallVector<std::pair<std::uint32_t, std::uint64_t>, 0> candidates;
		for (const auto& entry : _chunks) {
			if (entry.second.LastUsed != _frame) {
				candidates.emplace_back(entry.second.LastUsed, entry.first);
			}
		}
		std::sort(candidates.begin(), candidates.end());
		for (const auto& candidate : candidates) {
			if (_bytes <= MaxBytes) {
				break;
			}
			auto it = _chunks.find(candidate.second);
			_bytes -= it->second.Bytes;
			_chunks.erase(it);
		}
	}
}

#endif
