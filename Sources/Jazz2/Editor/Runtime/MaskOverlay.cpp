#include "MaskOverlay.h"

#if defined(WITH_EDITOR)

#include "../../Rendering/PolylineRenderer.h"
#include "../../Tiles/TileMap.h"
#include "../../Tiles/TileSet.h"

#include <algorithm>
#include <cstring>

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// Most chunks kept, those drawn the longest time ago go first, fewer where memory is short. Most chunks made in
		// one frame, the others follow in the next frames, so moving the view doesn't stall.
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t MaxChunks = 32;
		constexpr std::int32_t MaxBuildsPerFrame = 2;
#else
		constexpr std::size_t MaxChunks = 192;
		constexpr std::int32_t MaxBuildsPerFrame = 6;
#endif
		// How solid parts look, as luminance and opacity, and their outline
		constexpr std::uint8_t InsideLuminance = 0;
		constexpr std::uint8_t InsideOpacity = 120;
		constexpr Colorf OutlineColor = Colorf(1.0f, 1.0f, 1.0f, 0.66f);
		// Steps of the outline closer than this to a straight line are straightened, in pixels of the chunk image
		constexpr float OutlineTolerance = 0.55f;
		// Floats of vertices of the outlines drawn in a frame
		constexpr std::int32_t MaxOutlineFloats = 1 << 20;

		std::int32_t FloorDiv(std::int32_t value, std::int32_t divisor)
		{
			return (value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor));
		}

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
		: _layer(-1), _layerRepeats(false), _frame(0)
	{
	}

	MaskOverlay::~MaskOverlay()
	{
	}

	void MaskOverlay::Invalidate()
	{
		_chunks.clear();
		_layer = -1;
	}

	void MaskOverlay::InvalidateTiles(std::int32_t layer, const Recti& area)
	{
		if (layer != _layer) {
			return;
		}
		if (_layerRepeats) {
			// A tile of a repeated layer is in many chunks
			_chunks.clear();
			return;
		}
		// The outline of a chunk depends on the tiles next to it too
		std::int32_t x1 = FloorDiv(area.X - 1, ChunkTiles), y1 = FloorDiv(area.Y - 1, ChunkTiles);
		std::int32_t x2 = FloorDiv(area.X + area.W, ChunkTiles), y2 = FloorDiv(area.Y + area.H, ChunkTiles);
		std::size_t target = 0;
		for (std::size_t i = 0; i < _chunks.size(); i++) {
			const Vector2i& pos = _chunks[i].Pos;
			if (pos.X >= x1 && pos.X <= x2 && pos.Y >= y1 && pos.Y <= y2) {
				continue;
			}
			if (target != i) {
				_chunks[target] = std::move(_chunks[i]);
			}
			target++;
		}
		_chunks.erase(_chunks.begin() + target, _chunks.end());
	}

	void MaskOverlay::Draw(UiPainter& painter, TileMap& tileMap, const LevelDocument& document, std::int32_t layer,
		const LayerView& view, const Rectf& viewRect, bool alone)
	{
		_frame++;
		if (layer != _layer) {
			_chunks.clear();
			_layer = layer;
		}
		_layerRepeats = (view.RepeatX || view.RepeatY);

		if (alone) {
			// The masks only, the level is only hinted behind them
			painter.FillRect(viewRect, Colorf(0.42f, 0.42f, 0.48f, 0.94f), UiLayer::Back);
		}
		float cellSize = TileSet::DefaultTileSize * view.Scale;
		if (cellSize < 4.0f || layer < 0 || layer >= std::int32_t(document.Layers.size())) {
			return;
		}

		// Fewer pixels of a chunk when the view is far
		std::int32_t step = (view.Scale >= 0.75f ? 1 : (view.Scale >= 0.375f ? 2 : 4));
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X, viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X + viewRect.W, viewRect.Y + viewRect.H)));
		if (!view.RepeatX) {
			from.X = std::max(from.X, 0);
			to.X = std::min(to.X, view.Size.X - 1);
		}
		if (!view.RepeatY) {
			from.Y = std::max(from.Y, 0);
			to.Y = std::min(to.Y, view.Size.Y - 1);
		}

		_screenOutline.clear();
		_screenOutlineEnds.clear();
		std::int32_t builds = 0;
		for (std::int32_t cy = FloorDiv(from.Y, ChunkTiles); cy <= FloorDiv(to.Y, ChunkTiles); cy++) {
			for (std::int32_t cx = FloorDiv(from.X, ChunkTiles); cx <= FloorDiv(to.X, ChunkTiles); cx++) {
				Vector2i pos(cx, cy);
				Chunk* chunk = FindChunk(pos, step);
				if (chunk == nullptr) {
					if (builds >= MaxBuildsPerFrame) {
						continue;
					}
					builds++;
					chunk = &_chunks.emplace_back();
					chunk->Pos = pos;
					chunk->Step = step;
					BuildChunk(*chunk, tileMap, document, view);
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

		if (!_screenOutlineEnds.empty()) {
			if (_outlineRenderer == nullptr) {
				_outlineRenderer = std::make_unique<Rendering::PolylineRenderer>(MaxOutlineFloats);
			}
			_outlineRenderer->BeginFrame();
			// Thin when the view is far, a little thicker when it's close, but never as thick as the pixels then are
			float halfThickness = std::max(1.0f, painter.GetScale()) * std::clamp(0.5f + 0.35f * view.Scale, 0.75f, 1.5f);
			// Far away, all the outlines are more than one command can draw
			SmallVector<RenderCommand*, 8> commands;
			_outlineRenderer->DrawMany(_screenOutline, _screenOutlineEnds, halfThickness, painter.GetDepth(UiLayer::Content), OutlineColor, commands);			for (RenderCommand* command : commands) {
				painter.Command(command);
			}
		}

		DropOldChunks();
	}

	MaskOverlay::Chunk* MaskOverlay::FindChunk(Vector2i pos, std::int32_t step)
	{
		for (Chunk& chunk : _chunks) {
			if (chunk.Pos == pos && chunk.Step == step) {
				return &chunk;
			}
		}
		return nullptr;
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
				if (!view.Wrap(Vector2i(chunk.Pos.X * ChunkTiles + tx, chunk.Pos.Y * ChunkTiles + ty), tile)) {
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
		if (!any) {
			chunk.Image = nullptr;
			chunk.Empty = chunk.OutlineEnds.empty();
			return;
		}
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
		if (_chunks.size() <= MaxChunks) {
			return;
		}
		std::sort(_chunks.begin(), _chunks.end(), [](const Chunk& a, const Chunk& b) {
			return (a.LastUsed > b.LastUsed);
		});
		_chunks.erase(_chunks.begin() + MaxChunks, _chunks.end());
	}
}

#endif
