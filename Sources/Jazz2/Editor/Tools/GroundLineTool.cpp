#include "GroundLineTool.h"

#if defined(WITH_EDITOR)

#include "../Document/Commands.h"
#include "../Smart/SmartTileModel.h"
#include "../Smart/TileShapes.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#if !defined(N_)
#	define N_(text) text
#endif

namespace Jazz2::Editor
{
	namespace
	{
		std::uint64_t ToKey(Vector2i cell)
		{
			return (std::uint64_t(std::uint32_t(cell.Y)) << 32) | std::uint32_t(cell.X);
		}

		Vector2i FromKey(std::uint64_t key)
		{
			return Vector2i(std::int32_t(std::uint32_t(key)), std::int32_t(std::uint32_t(key >> 32)));
		}

		std::uint32_t HashCell(Vector2i cell)
		{
			std::uint32_t h = std::uint32_t(cell.X) * 0x8DA6B343u ^ std::uint32_t(cell.Y) * 0xD8163841u;
			h ^= h >> 15;
			return h * 0x2C1B3C6Du;
		}

		bool IsSolidTile(const TileRef& tile)
		{
			return (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
		}

		Vector2f CatmullRom(Vector2f p0, Vector2f p1, Vector2f p2, Vector2f p3, float t)
		{
			float t2 = t * t, t3 = t2 * t;
			return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
		}

		// Whether a point is inside of a closed line, which ends where it starts
		bool IsInside(ArrayView<const Vector2f> polygon, Vector2f point)
		{
			bool inside = false;
			for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
				const Vector2f& a = polygon[i];
				const Vector2f& b = polygon[j];
				if ((a.Y > point.Y) != (b.Y > point.Y) && point.X < (b.X - a.X) * (point.Y - a.Y) / (b.Y - a.Y) + a.X) {
					inside = !inside;
				}
			}
			return inside;
		}

		// Where a closed line crosses a vertical line, sorted from the top
		void GetCrossings(ArrayView<const Vector2f> polygon, float x, SmallVectorImpl<float>& crossings)
		{
			crossings.clear();
			for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
				const Vector2f& a = polygon[i];
				const Vector2f& b = polygon[j];
				if ((a.X > x) != (b.X > x)) {
					crossings.push_back(a.Y + (b.Y - a.Y) * (x - a.X) / (b.X - a.X));
				}
			}
			std::sort(crossings.begin(), crossings.end());
		}

		float DistanceToLine(ArrayView<const Vector2f> line, Vector2f point)
		{
			float best = 1e30f;
			for (std::size_t i = 0; i + 1 < line.size(); i++) {
				Vector2f ab = line[i + 1] - line[i];
				float length = ab.SqrLength();
				float t = (length > 0.0f ? std::clamp(Vector2f::Dot(point - line[i], ab) / length, 0.0f, 1.0f) : 0.0f);
				best = std::min(best, (line[i] + ab * t - point).Length());
			}
			return best;
		}

		constexpr std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };

		// The tiles of a layer as they are with some of them changed, by their positions in the layer
		struct LayerState
		{
			const DocumentLayer& Layer;
			std::unordered_map<std::uint64_t, TileRef> Changed;

			explicit LayerState(const DocumentLayer& layer)
				: Layer(layer)
			{
			}

			// The tile a cell shows, wrapped around a layer that repeats, `false` outside of the layer
			bool Wrap(Vector2i cell, Vector2i& tile) const
			{
				std::int32_t width = Layer.GetWidth(), height = Layer.GetHeight();
				bool repeatX = !Layer.IsSpriteLayer() && (Layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
				bool repeatY = !Layer.IsSpriteLayer() && (Layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;
				if (width <= 0 || height <= 0) {
					return false;
				}
				tile = cell;
				if (repeatX) {
					tile.X = ((tile.X % width) + width) % width;
				} else if (tile.X < 0 || tile.X >= width) {
					return false;
				}
				if (repeatY) {
					tile.Y = ((tile.Y % height) + height) % height;
				} else if (tile.Y < 0 || tile.Y >= height) {
					return false;
				}
				return true;
			}

			TileRef Get(Vector2i tile) const
			{
				auto it = Changed.find(ToKey(tile));
				return (it != Changed.end() ? it->second : Layer.GetTile(tile.X, tile.Y));
			}
		};

		// Lays the ground of a line onto the tiles of its layer as they're without it, the tiles in pinned aren't written
		void Generate(const TerrainSpline& spline, LayerState& state, const std::unordered_set<std::uint64_t>& pinned,
			SmartTileModel& model, const TileShapes& shapes, std::unordered_map<std::uint64_t, TileRef>& decided)
		{
			SmallVector<Vector2f, 0> line;
			GroundLineTool::SampleLine(spline, line);
			if (line.size() < 2) {
				return;
			}

			bool spriteLayer = state.Layer.IsSpriteLayer();
			// Cells are positions in the layer, unwrapped; the ground made solid there, those of the surface keep the
			// pieces of ground chosen for them
			std::unordered_map<std::uint64_t, bool> solid;
			std::unordered_set<std::uint64_t> surface;
			auto cellTile = [&](Vector2i cell, TileRef& tile) {
				Vector2i wrapped;
				if (!state.Wrap(cell, wrapped)) {
					return false;
				}
				tile = state.Get(wrapped);
				return true;
			};
			auto isPinned = [&](Vector2i cell) {
				Vector2i wrapped;
				return (state.Wrap(cell, wrapped) && pinned.find(ToKey(wrapped)) != pinned.end());
			};
			auto isSolid = [&](Vector2i cell) {
				auto it = solid.find(ToKey(cell));
				if (it != solid.end()) {
					return it->second;
				}
				TileRef tile;
				// Outside of the layer counts as solid, as the levels were learned
				return (!cellTile(cell, tile) || IsSolidTile(tile));
			};
			auto currentTile = [&](Vector2i cell, TileRef& tile) {
				auto it = decided.find(ToKey(cell));
				if (it != decided.end()) {
					tile = it->second;
					return true;
				}
				return cellTile(cell, tile);
			};
			auto getNeighbors = [&](Vector2i cell) {
				std::uint8_t neighbors = 0;
				for (std::int32_t i = 0; i < 8; i++) {
					if (isSolid(Vector2i(cell.X + Offsets[i][0], cell.Y + Offsets[i][1]))) {
						neighbors |= std::uint8_t(1 << i);
					}
				}
				return neighbors;
			};
			auto markSolid = [&](Vector2i cell) {
				if (!isPinned(cell)) {
					solid[ToKey(cell)] = true;
				}
			};

			// A piece of solid ground for anything the learned tiles can't fill, the flat one the levels use the most
			TileRef fullTile = shapes.GetSurfaces().front().Tile;
			{
				float best = -1e30f;
				for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
					if (s.Left < 1.0f && s.Right < 1.0f && s.Error < 0.5f && s.Tile.Flags == 0) {
						float score = std::log(1.0f + float(model.GetSurfaceUsage(s.Tile)));
						if (score > best) {
							best = score;
							fullTile = s.Tile;
						}
					}
				}
			}

			// First where the ground is, the cells its surface goes through get pieces that follow the line
			struct SurfaceCell
			{
				Vector2i Cell;
				float Left;
				float Right;
				bool Ceiling;
			};
			SmallVector<SurfaceCell, 0> surfaceCells;
			auto addSurface = [&](Vector2i cell, float left, float right, bool ceiling) {
				surfaceCells.push_back(SurfaceCell{ cell, std::clamp(left, 0.0f, 32.0f), std::clamp(right, 0.0f, 32.0f), ceiling });
				surface.insert(ToKey(cell));
			};

			if (spline.Kind == SplineKind::Ground || spline.Kind == SplineKind::Band) {
				std::int32_t cx1 = std::int32_t(std::floor(line.front().X / 32.0f));
				std::int32_t cx2 = std::int32_t(std::floor((line.back().X - 0.5f) / 32.0f));
				for (std::int32_t cx = cx1; cx <= cx2; cx++) {
					float yl = GroundLineTool::GetLineY(line, cx * 32.0f), yr = GroundLineTool::GetLineY(line, cx * 32.0f + 32.0f);
					std::int32_t cyTop = std::int32_t(std::floor(std::min(yl, yr) / 32.0f));
					std::int32_t cyBottom = std::int32_t(std::floor(std::max(yl, yr) / 32.0f));
					std::int32_t lowest = cyTop;
					for (std::int32_t cy = cyTop; cy <= cyBottom; cy++) {
						Vector2i cell(cx, cy);
						TileRef existing;
						if (!cellTile(cell, existing)) {
							continue;
						}
						float dl = yl - cy * 32.0f, dr = yr - cy * 32.0f;
						if (dl >= 31.0f && dr >= 31.0f) {
							// The line is below the cell
							continue;
						}
						lowest = cy;
						if (isPinned(cell)) {
							continue;
						}
						solid[ToKey(cell)] = true;
						if (dl >= 0.0f || dr >= 0.0f) {
							addSurface(cell, dl, dr, false);
						}
					}

					// Ground below the surface goes down to the ground that's there, a band is as thick as its depth
					for (std::int32_t depth = 1; depth <= spline.Depth - (spline.Kind == SplineKind::Band ? 1 : 0); depth++) {
						Vector2i cell(cx, lowest + depth);
						TileRef existing;
						if (!cellTile(cell, existing) || (spline.Kind == SplineKind::Ground && IsSolidTile(existing))) {
							break;
						}
						markSolid(cell);
					}
				}
			} else if (spline.Kind == SplineKind::Ceiling) {
				std::int32_t cx1 = std::int32_t(std::floor(line.front().X / 32.0f));
				std::int32_t cx2 = std::int32_t(std::floor((line.back().X - 0.5f) / 32.0f));
				for (std::int32_t cx = cx1; cx <= cx2; cx++) {
					float yl = GroundLineTool::GetLineY(line, cx * 32.0f), yr = GroundLineTool::GetLineY(line, cx * 32.0f + 32.0f);
					std::int32_t cyTop = std::int32_t(std::floor(std::min(yl, yr) / 32.0f));
					std::int32_t cyBottom = std::int32_t(std::floor(std::max(yl, yr) / 32.0f));
					std::int32_t highest = cyBottom;
					for (std::int32_t cy = cyBottom; cy >= cyTop; cy--) {
						Vector2i cell(cx, cy);
						TileRef existing;
						if (!cellTile(cell, existing)) {
							continue;
						}
						// Depth of the bottom of the solid part from the top of the cell
						float dl = yl - cy * 32.0f, dr = yr - cy * 32.0f;
						if (dl <= 1.0f && dr <= 1.0f) {
							// The line is above the cell
							continue;
						}
						highest = cy;
						if (isPinned(cell)) {
							continue;
						}
						solid[ToKey(cell)] = true;
						if (dl < 32.0f || dr < 32.0f) {
							addSurface(cell, dl, dr, true);
						}
					}
					for (std::int32_t depth = 1; depth <= spline.Depth; depth++) {
						Vector2i cell(cx, highest - depth);
						TileRef existing;
						if (!cellTile(cell, existing) || IsSolidTile(existing)) {
							break;
						}
						markSolid(cell);
					}
				}
			} else {
				// A closed shape, the cells mostly inside of it are solid
				float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
				for (const Vector2f& p : line) {
					minX = std::min(minX, p.X);
					minY = std::min(minY, p.Y);
					maxX = std::max(maxX, p.X);
					maxY = std::max(maxY, p.Y);
				}
				std::int32_t cx1 = std::int32_t(std::floor(minX / 32.0f)), cx2 = std::int32_t(std::floor(maxX / 32.0f));
				std::int32_t cy1 = std::int32_t(std::floor(minY / 32.0f)), cy2 = std::int32_t(std::floor(maxY / 32.0f));
				std::unordered_set<std::uint64_t> inside;
				for (std::int32_t cy = cy1; cy <= cy2; cy++) {
					for (std::int32_t cx = cx1; cx <= cx2; cx++) {
						std::int32_t count = 0;
						for (std::int32_t sy = 0; sy < 4; sy++) {
							for (std::int32_t sx = 0; sx < 4; sx++) {
								count += (IsInside(line, Vector2f(cx * 32.0f + sx * 8.0f + 4.0f, cy * 32.0f + sy * 8.0f + 4.0f)) ? 1 : 0);
							}
						}
						TileRef existing;
						if (count >= 8 && cellTile(Vector2i(cx, cy), existing)) {
							inside.insert(ToKey(Vector2i(cx, cy)));
						}
					}
				}
				SmallVector<float, 8> crossings;
				// The edge of the shape near a cell, at the left and the right edge of the cell, the topmost or the lowest
				auto edgeDepth = [&](Vector2i cell, float x, bool top) {
					GetCrossings(line, x, crossings);
					float best = (top ? 0.0f : 32.0f);
					bool found = false;
					for (float y : crossings) {
						float depth = y - cell.Y * 32.0f;
						if (depth >= -16.0f && depth <= 48.0f) {
							if (!found || (top ? depth < best : depth > best)) {
								best = depth;
								found = true;
							}
						}
					}
					return best;
				};
				for (std::uint64_t key : inside) {
					Vector2i cell = FromKey(key);
					if (isPinned(cell)) {
						continue;
					}
					solid[key] = true;
					bool openAbove = (inside.find(ToKey(Vector2i(cell.X, cell.Y - 1))) == inside.end());
					bool openBelow = (inside.find(ToKey(Vector2i(cell.X, cell.Y + 1))) == inside.end());
					if (openAbove) {
						addSurface(cell, edgeDepth(cell, cell.X * 32.0f + 0.5f, true), edgeDepth(cell, cell.X * 32.0f + 31.5f, true), false);
					} else if (openBelow) {
						addSurface(cell, edgeDepth(cell, cell.X * 32.0f + 0.5f, false), edgeDepth(cell, cell.X * 32.0f + 31.5f, false), true);
					}
				}
			}

			// Then the pieces of ground closest to the line, of those the levels use with the same neighbors, going well with
			// the one on the left, from the left
			std::sort(surfaceCells.begin(), surfaceCells.end(), [](const SurfaceCell& a, const SurfaceCell& b) {
				return (a.Cell.X != b.Cell.X ? a.Cell.X < b.Cell.X : a.Cell.Y < b.Cell.Y);
			});
			for (const SurfaceCell& target : surfaceCells) {
				std::uint8_t neighbors = getNeighbors(target.Cell);
				TileRef left;
				bool hasLeft = currentTile(Vector2i(target.Cell.X - 1, target.Cell.Y), left);
				const TileShapes::Surface* best = nullptr;
				float bestScore = -1e30f;
				for (const TileShapes::Surface& s : (target.Ceiling ? shapes.GetCeilings() : shapes.GetSurfaces())) {
					float cost = std::abs(s.Left - target.Left) + std::abs(s.Right - target.Right) + 2.0f * s.Error;
					float score = -0.15f * cost + 1.0f * std::log(1.0f + float(model.GetCount(neighbors, spriteLayer, s.Tile)));
					if (!target.Ceiling) {
						score += 0.3f * std::log(1.0f + float(model.GetSurfaceUsage(s.Tile)));
					}
					if (hasLeft) {
						score += 0.8f * std::log(1.0f + float(model.GetRightPair(left, s.Tile)));
					}
					std::uint32_t h = (HashCell(target.Cell) ^ (std::uint32_t(s.Tile.Index) * 0x9E3779B1u)) * 0x85EBCA6Bu;
					score += 0.15f * float((h >> 8) & 0xFFFF) / 65535.0f;
					if (score > bestScore) {
						bestScore = score;
						best = &s;
					}
				}
				if (best != nullptr) {
					decided[ToKey(target.Cell)] = best->Tile;
				}
			}

			// The tiles under the surface and around the ground by their neighbors, from the left and from the top
			SmallVector<std::uint64_t, 0> toChoose;
			for (const auto& [key, isCellSolid] : solid) {
				if (isCellSolid && surface.find(key) == surface.end()) {
					toChoose.push_back(key);
				}
			}
			for (const auto& [key, isCellSolid] : solid) {
				Vector2i cell = FromKey(key);
				for (std::int32_t i = 0; i < 8; i++) {
					Vector2i neighbor(cell.X + Offsets[i][0], cell.Y + Offsets[i][1]);
					TileRef tile;
					if (solid.find(ToKey(neighbor)) == solid.end() && !isPinned(neighbor) && cellTile(neighbor, tile) && SmartTileModel::IsModelTile(tile)) {
						toChoose.push_back(ToKey(neighbor));
					}
				}
			}
			std::sort(toChoose.begin(), toChoose.end(), [](std::uint64_t a, std::uint64_t b) {
				Vector2i ca = FromKey(a), cb = FromKey(b);
				return (ca.Y != cb.Y ? ca.Y < cb.Y : ca.X < cb.X);
			});
			toChoose.erase(std::unique(toChoose.begin(), toChoose.end()), toChoose.end());
			for (std::uint64_t key : toChoose) {
				Vector2i cell = FromKey(key);
				std::uint8_t neighbors = getNeighbors(cell);
				TileRef existing;
				// Tiles around the new ground stay as they are if the levels use them so too
				bool isNeighbor = (solid.find(key) == solid.end() && cellTile(cell, existing));
				if (isNeighbor && model.Fits(neighbors, spriteLayer, existing)) {
					continue;
				}
				auto neighborTile = [&](std::int32_t ox, std::int32_t oy, TileRef& tile) {
					return (currentTile(Vector2i(cell.X + ox, cell.Y + oy), tile) && SmartTileModel::IsModelTile(tile));
				};
				TileRef left, up, right, down, picked;
				bool hasLeft = neighborTile(-1, 0, left), hasUp = neighborTile(0, -1, up);
				bool hasRight = neighborTile(1, 0, right), hasDown = neighborTile(0, 1, down);
				if (model.Pick(neighbors, spriteLayer, hasLeft ? &left : nullptr, hasUp ? &up : nullptr,
					hasRight ? &right : nullptr, hasDown ? &down : nullptr, HashCell(cell), picked, isNeighbor ? &existing : nullptr)) {
					decided[key] = picked;
				} else if (solid.find(key) != solid.end()) {
					decided[key] = fullTile;
				}
			}
		}

		// What laying a line changes: the tiles of its layer, and the line with the tiles it owns then
		bool ComputeSpline(IToolHost& host, const SmallVectorImpl<TerrainSpline>& splines, std::int32_t index, const TerrainSpline* spline,
			bool quiet, std::int32_t& layerIndex, std::unordered_map<std::uint64_t, TileRef>& changed, TerrainSpline& result)
		{
			LevelDocument& document = host.GetDocument();
			layerIndex = (spline != nullptr ? spline->Layer : (index >= 0 && index < std::int32_t(splines.size()) ? splines[index].Layer : -1));
			if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || !host.IsLayerEditable(layerIndex)) {
				if (!quiet) {
					host.ShowMessage(_("The layer is locked or hidden"));
				}
				return false;
			}
			const TileShapes& shapes = host.GetTileShapes();
			if (spline != nullptr && shapes.GetSurfaces().empty()) {
				if (!quiet) {
					host.ShowMessage(_("The tile set has no pieces of ground whose masks follow a line"));
				}
				return false;
			}
			SmartTileModel& model = host.GetSmartTiles();
			const DocumentLayer& layer = document.Layers[layerIndex];
			LayerState state(layer);

			// The ground as it was goes first: its tiles still as it wrote them get back what was there, those changed by hand
			// since stay and aren't written again
			std::unordered_set<std::uint64_t> pinned;
			if (index >= 0 && index < std::int32_t(splines.size())) {
				for (const TerrainSpline::OwnedTile& owned : splines[index].Owned) {
					if (owned.X < 0 || owned.Y < 0 || owned.X >= layer.GetWidth() || owned.Y >= layer.GetHeight()) {
						continue;
					}
					Vector2i tile(owned.X, owned.Y);
					if (layer.GetTile(owned.X, owned.Y) == owned.Written) {
						state.Changed[ToKey(tile)] = owned.Previous;
					} else {
						pinned.insert(ToKey(tile));
					}
				}
			}

			// Then the ground as it should be
			if (spline != nullptr) {
				result = *spline;
				result.Owned.clear();
				std::unordered_map<std::uint64_t, TileRef> decided;
				Generate(result, state, pinned, model, shapes, decided);
				for (const auto& [key, tile] : decided) {
					Vector2i wrapped;
					if (!state.Wrap(FromKey(key), wrapped) || pinned.find(ToKey(wrapped)) != pinned.end()) {
						continue;
					}
					TileRef previous = state.Get(wrapped);
					state.Changed[ToKey(wrapped)] = tile;
					result.Owned.push_back(TerrainSpline::OwnedTile{ wrapped.X, wrapped.Y, tile, previous });
				}
			}
			changed = std::move(state.Changed);
			return true;
		}
	}

	GroundLineTool::GroundLineTool(IToolHost& host)
		: EditorTool(host), _dragSpline(-1), _dragPoint(-1), _dragInsert(false), _selectedSpline(-1), _options(std::make_shared<Options>()),
			_window(std::make_shared<UiElement*>(nullptr)), _previewLayer(-1)
	{
	}

	GroundLineTool::~GroundLineTool()
	{
		// The window goes with the interface, it only shares the pointer to itself
	}

	EditorToolType GroundLineTool::GetType() const
	{
		return EditorToolType::Ground;
	}

	void GroundLineTool::OnActivate()
	{
		// Learning the tile set takes a while, it starts right away
		_host.GetSmartTiles();
		ShowWindow();
	}

	void GroundLineTool::OnDeactivate()
	{
		ClearPreview();
		CloseWindow();
	}

	void GroundLineTool::SampleLine(const TerrainSpline& spline, SmallVectorImpl<Vector2f>& line)
	{
		line.clear();
		SmallVector<Vector2f, 0> points;
		for (const Vector2f& p : spline.Points) {
			points.push_back(p);
		}
		bool closed = (spline.Kind == SplineKind::Closed);
		if (!closed) {
			std::sort(points.begin(), points.end(), [](const Vector2f& a, const Vector2f& b) { return a.X < b.X; });
		}
		std::size_t count = points.size();
		if (count < 2) {
			return;
		}
		if (!spline.Smooth || count < 3) {
			for (const Vector2f& p : points) {
				line.push_back(p);
			}
			if (closed) {
				line.push_back(points[0]);
			}
			return;
		}

		// Through the points in a curve, around for a closed shape, at most 4 pixels apart
		std::size_t segments = (closed ? count : count - 1);
		auto at = [&](std::ptrdiff_t i) {
			if (closed) {
				return points[std::size_t((i % std::ptrdiff_t(count) + std::ptrdiff_t(count)) % std::ptrdiff_t(count))];
			}
			return points[std::size_t(std::clamp<std::ptrdiff_t>(i, 0, std::ptrdiff_t(count) - 1))];
		};
		line.push_back(points[0]);
		for (std::size_t i = 0; i < segments; i++) {
			Vector2f p0 = at(std::ptrdiff_t(i) - 1), p1 = at(std::ptrdiff_t(i)), p2 = at(std::ptrdiff_t(i) + 1), p3 = at(std::ptrdiff_t(i) + 2);
			std::int32_t steps = std::clamp(std::int32_t((p2 - p1).Length() / 4.0f), 2, 64);
			for (std::int32_t s = 1; s <= steps; s++) {
				Vector2f p = CatmullRom(p0, p1, p2, p3, float(s) / float(steps));
				if (!closed) {
					// A line from left to right stays so, the curve doesn't swing back
					p.X = std::clamp(p.X, line.back().X, p2.X);
				}
				line.push_back(p);
			}
		}
	}

	float GroundLineTool::GetLineY(ArrayView<const Vector2f> line, float x)
	{
		if (line.empty()) {
			return 0.0f;
		}
		x = std::clamp(x, line.front().X, line.back().X);
		for (std::size_t i = 0; i + 1 < line.size(); i++) {
			if (x <= line[i + 1].X) {
				float span = line[i + 1].X - line[i].X;
				float t = (span > 0.0f ? (x - line[i].X) / span : 0.0f);
				return line[i].Y + (line[i + 1].Y - line[i].Y) * t;
			}
		}
		return line.back().Y;
	}

	Vector2f GroundLineTool::GetPoint(const ToolPointer& pointer) const
	{
		// In pixels of the layer, so the line stays with it
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		Vector2f point = pointer.World - view.Origin;
		if ((pointer.Modifiers & KeyMod::Ctrl) != 0) {
			point = Vector2f(std::round(point.X / 32.0f) * 32.0f, std::round(point.Y / 32.0f) * 32.0f);
		}
		return Vector2f(std::round(point.X), std::round(point.Y));
	}

	bool GroundLineTool::HitTest(const SmallVectorImpl<TerrainSpline>& splines, const LayerView& view, Vector2f screen,
		std::int32_t& spline, std::int32_t& point, std::int32_t& segment) const
	{
		spline = -1;
		point = -1;
		segment = -1;
		std::int32_t layer = _host.GetCurrentLayer();
		float pointReach = std::max(8.0f, 6.0f * view.Scale);
		float lineReach = std::max(5.0f, 3.0f * view.Scale);
		float best = pointReach;
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (splines[i].Layer != layer) {
				continue;
			}
			const auto& points = splines[i].Points;
			for (std::int32_t j = 0; j < std::int32_t(points.size()); j++) {
				float distance = (view.WorldToScreen(points[j] + view.Origin) - screen).Length();
				if (distance <= best) {
					best = distance;
					spline = i;
					point = j;
				}
			}
		}
		if (point >= 0) {
			return true;
		}
		best = lineReach;
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (splines[i].Layer != layer) {
				continue;
			}
			// Along the line as it's laid, a segment is between the points it's laid between
			const auto& points = splines[i].Points;
			bool closed = (splines[i].Kind == SplineKind::Closed);
			std::int32_t segments = std::int32_t(points.size()) - (closed ? 0 : 1);
			for (std::int32_t j = 0; j < segments; j++) {
				Vector2f a = view.WorldToScreen(points[j] + view.Origin), b = view.WorldToScreen(points[(j + 1) % points.size()] + view.Origin);
				Vector2f ab = b - a;
				float length = ab.SqrLength();
				float t = (length > 0.0f ? std::clamp(Vector2f::Dot(screen - a, ab) / length, 0.0f, 1.0f) : 0.0f);
				float distance = (a + ab * t - screen).Length();
				if (distance <= best) {
					best = distance;
					spline = i;
					segment = j;
				}
			}
		}
		return (spline >= 0);
	}

	void GroundLineTool::OnPointerDown(const ToolPointer& pointer)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_host.GetDocument(), splines);
		std::int32_t hitSpline = -1, hitPoint = -1, hitSegment = -1;
		bool hit = (_points.empty() && HitTest(splines, view, pointer.Screen, hitSpline, hitPoint, hitSegment));
		if (hit) {
			// Its options are shown, and changed by the window
			_selectedSpline = hitSpline;
			_options->Kind = splines[hitSpline].Kind;
			_options->Smooth = splines[hitSpline].Smooth;
			_options->Depth = splines[hitSpline].Depth;
		}

		if (pointer.Button == MouseButton::Right) {
			if (!_points.empty()) {
				Lay();
			} else if (hit) {
				// The ground of the line goes away, what was there before comes back
				ApplySpline(_host, splines, hitSpline, nullptr, _("Remove ground"));
				_selectedSpline = -1;
			} else {
				_host.ShowMessage(_("Click points of the surface first, the right button then lays the ground"));
			}
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}

		if (hit && hitPoint >= 0 && (pointer.Modifiers & KeyMod::Shift) != 0) {
			// A point less, a line keeps at least two, a shape three
			TerrainSpline changed = splines[hitSpline];
			std::size_t minPoints = (changed.Kind == SplineKind::Closed ? 3 : 2);
			if (changed.Points.size() <= minPoints) {
				_host.ShowMessage(_("A line has two points at least, a shape three - the right button removes the whole ground"));
				return;
			}
			changed.Points.erase(changed.Points.begin() + hitPoint);
			ApplySpline(_host, splines, hitSpline, &changed, _("Change ground"));
			return;
		}
		if (hit && hitPoint >= 0) {
			_dragSpline = hitSpline;
			_dragPoint = hitPoint;
			_dragInsert = false;
			_dragPos = splines[hitSpline].Points[hitPoint];
			_previewPos = _dragPos;
			return;
		}
		if (hit && hitSegment >= 0) {
			// A new point on the line, dragged right away, added when it's let go
			_dragSpline = hitSpline;
			_dragPoint = hitSegment + 1;
			_dragInsert = true;
			_dragPos = GetPoint(pointer);
			_previewPos = Vector2f(-1e30f, -1e30f);
			return;
		}
		_selectedSpline = -1;
		_points.push_back(GetPoint(pointer));
	}

	void GroundLineTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragSpline >= 0) {
			_dragPos = GetPoint(pointer);
			if (_dragPos != _previewPos) {
				_previewPos = _dragPos;
				UpdatePreview();
			}
		}
	}

	bool GroundLineTool::GetDraggedSpline(const SmallVectorImpl<TerrainSpline>& splines, TerrainSpline& spline) const
	{
		std::int32_t index = _dragSpline, point = _dragPoint;
		if (index < 0 || index >= std::int32_t(splines.size()) || point > std::int32_t(splines[index].Points.size()) ||
			(!_dragInsert && (point == std::int32_t(splines[index].Points.size()) || splines[index].Points[point] == _dragPos))) {
			return false;
		}
		spline = splines[index];
		if (_dragInsert) {
			spline.Points.insert(spline.Points.begin() + point, _dragPos);
		} else {
			spline.Points[point] = _dragPos;
		}
		if (spline.Kind != SplineKind::Closed) {
			std::sort(spline.Points.begin(), spline.Points.end(), [](const Vector2f& a, const Vector2f& b) { return a.X < b.X; });
		}
		return true;
	}

	void GroundLineTool::UpdatePreview()
	{
		// The ground as it would be laid now, shown in the view only
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_host.GetDocument(), splines);
		TerrainSpline dragged, result;
		std::int32_t layerIndex;
		std::unordered_map<std::uint64_t, TileRef> changed;
		if (!GetDraggedSpline(splines, dragged) || !ComputeSpline(_host, splines, _dragSpline, &dragged, true, layerIndex, changed, result)) {
			return;
		}
		ClearPreview();
		_previewLayer = layerIndex;
		bool first = true;
		for (const auto& [key, tile] : changed) {
			Vector2i position = FromKey(key);
			_host.PreviewTile(layerIndex, position.X, position.Y, tile);
			if (first) {
				_previewArea = Recti(position.X, position.Y, 1, 1);
				first = false;
			} else {
				std::int32_t x2 = std::max(_previewArea.X + _previewArea.W, position.X + 1);
				std::int32_t y2 = std::max(_previewArea.Y + _previewArea.H, position.Y + 1);
				_previewArea.X = std::min(_previewArea.X, position.X);
				_previewArea.Y = std::min(_previewArea.Y, position.Y);
				_previewArea.W = x2 - _previewArea.X;
				_previewArea.H = y2 - _previewArea.Y;
			}
		}
		if (first) {
			_previewLayer = -1;
		}
	}

	void GroundLineTool::ClearPreview()
	{
		if (_previewLayer >= 0) {
			_host.RevertPreview(_previewLayer, _previewArea);
			_previewLayer = -1;
		}
	}

	void GroundLineTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (_dragSpline < 0 || pointer.Button != MouseButton::Left) {
			return;
		}
		ClearPreview();
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_host.GetDocument(), splines);
		TerrainSpline changed;
		std::int32_t index = _dragSpline;
		bool valid = GetDraggedSpline(splines, changed);
		_dragSpline = -1;
		_dragPoint = -1;
		_dragInsert = false;
		if (valid) {
			ApplySpline(_host, splines, index, &changed, _("Change ground"));
		}
	}

	void GroundLineTool::Cancel()
	{
		ClearPreview();
		_points.clear();
		_dragSpline = -1;
		_dragPoint = -1;
		_dragInsert = false;
	}

	bool GroundLineTool::IsBusy() const
	{
		return (!_points.empty() || _dragSpline >= 0);
	}

	void GroundLineTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		std::int32_t layerIndex = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layerIndex);
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 1.5f));
		float handle = std::max(2.0f, painter.Px(2.5f));
		LevelDocument& document = _host.GetDocument();
		const TileShapes& shapes = _host.GetTileShapes();

		// The pieces of ground and of ceiling by their tiles, to draw where the collision of the ground laid is
		std::unordered_map<std::uint32_t, const TileShapes::Surface*> grounds, ceilings;
		for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
			grounds[std::uint32_t(s.Tile.Index) | (std::uint32_t(s.Tile.Flags & 0x03) << 16)] = &s;
		}
		for (const TileShapes::Surface& s : shapes.GetCeilings()) {
			ceilings[std::uint32_t(s.Tile.Index) | (std::uint32_t(s.Tile.Flags & 0x03) << 16)] = &s;
		}

		// The lines kept, of the layer being edited
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(document, splines);
		std::int32_t hoverSpline = -1, hoverPoint = -1, hoverSegment = -1;
		if (hoverValid && _points.empty() && _dragSpline < 0) {
			HitTest(splines, view, hover.Screen, hoverSpline, hoverPoint, hoverSegment);
		}
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (splines[i].Layer != layerIndex) {
				continue;
			}
			TerrainSpline shown = splines[i];
			if (i == _dragSpline) {
				GetDraggedSpline(splines, shown);
			}
			SmallVector<Vector2f, 0> fine;
			SampleLine(shown, fine);

			// Where the collision of the ground laid is, red where it misses the line
			if (i != _dragSpline && layerIndex >= 0 && layerIndex < std::int32_t(document.Layers.size())) {
				const DocumentLayer& layer = document.Layers[layerIndex];
				for (const TerrainSpline::OwnedTile& owned : splines[i].Owned) {
					if (!layer.Contains(owned.X, owned.Y) || layer.GetTile(owned.X, owned.Y) != owned.Written) {
						continue;
					}
					std::uint32_t key = std::uint32_t(owned.Written.Index) | (std::uint32_t(owned.Written.Flags & 0x03) << 16);
					bool openAbove = !layer.Contains(owned.X, owned.Y - 1) || !IsSolidTile(layer.GetTile(owned.X, owned.Y - 1));
					bool openBelow = !layer.Contains(owned.X, owned.Y + 1) || !IsSolidTile(layer.GetTile(owned.X, owned.Y + 1));
					const TileShapes::Surface* s = nullptr;
					if (openAbove && owned.Written.Source == TileRef::DefaultTileSet) {
						auto it = grounds.find(key);
						s = (it != grounds.end() ? it->second : nullptr);
					}
					if (s == nullptr && openBelow && owned.Written.Source == TileRef::DefaultTileSet) {
						auto it = ceilings.find(key);
						s = (it != ceilings.end() ? it->second : nullptr);
					}
					if (s == nullptr) {
						continue;
					}
					Vector2f a(owned.X * 32.0f, owned.Y * 32.0f + s->Left), b(owned.X * 32.0f + 32.0f, owned.Y * 32.0f + s->Right);
					float miss = std::max(DistanceToLine(fine, a), DistanceToLine(fine, b));
					Colorf color = (miss <= 4.0f ? Colorf(0.4f, 1.0f, 0.5f, 0.45f) : Colorf(1.0f, 0.3f, 0.25f, std::clamp(0.4f + miss / 32.0f, 0.4f, 0.95f)));
					painter.Line(view.WorldToScreen(a + view.Origin), view.WorldToScreen(b + view.Origin), std::max(1.0f, thickness * 0.75f), color, UiLayer::Overlay);
				}
			}

			SmallVector<Vector2f, 0> screenLine;
			for (const Vector2f& p : fine) {
				screenLine.push_back(view.WorldToScreen(p + view.Origin));
			}
			bool active = (i == hoverSpline || i == _dragSpline || i == _selectedSpline);
			Colorf color = (active ? Colorf(1.0f, 0.95f, 0.5f, 0.95f) : Colorf(0.6f, 1.0f, 0.4f, 0.7f));
			painter.Polyline(screenLine, thickness, color, UiLayer::Overlay);
			for (std::int32_t j = 0; j < std::int32_t(shown.Points.size()); j++) {
				Vector2f p = view.WorldToScreen(shown.Points[j] + view.Origin);
				bool hovered = (i == hoverSpline && j == hoverPoint);
				painter.Circle(p, hovered ? handle * 1.6f : handle, hovered ? UiTheme::Cursor : UiTheme::Accent, UiLayer::OverlayText);
			}
		}

		// The new line, with where the pointer is
		TerrainSpline drawing;
		drawing.Kind = _options->Kind;
		drawing.Smooth = _options->Smooth;
		for (const Vector2f& p : _points) {
			drawing.Points.push_back(p);
		}
		if (hoverValid && !_points.empty()) {
			drawing.Points.push_back(GetPoint(hover));
		}
		if (drawing.Points.empty()) {
			return;
		}
		SmallVector<Vector2f, 0> fine;
		SampleLine(drawing, fine);
		if (fine.empty()) {
			fine.push_back(drawing.Points[0]);
		}
		SmallVector<Vector2f, 0> screenLine;
		for (const Vector2f& p : fine) {
			screenLine.push_back(view.WorldToScreen(p + view.Origin));
		}
		// Where the ground goes, on the side of the line it's laid on
		if (drawing.Kind != SplineKind::Closed) {
			float depth = 32.0f * view.Scale * (drawing.Kind == SplineKind::Band ? float(_options->Depth) : 1.0f);
			for (std::size_t i = 0; i + 1 < screenLine.size(); i++) {
				Vector2f a = screenLine[i], b = screenLine[i + 1];
				float top = std::min(a.Y, b.Y), bottom = std::max(a.Y, b.Y);
				Rectf rect = (drawing.Kind == SplineKind::Ceiling
					? Rectf(a.X, top - depth, b.X - a.X, bottom - top + depth)
					: Rectf(a.X, top, b.X - a.X, bottom - top + depth));
				painter.FillRect(rect, Colorf(0.55f, 0.4f, 0.2f, 0.12f), UiLayer::Fill);
			}
		}
		painter.Polyline(screenLine, thickness, Colorf(0.6f, 1.0f, 0.4f, 0.85f), UiLayer::Overlay);
		for (const Vector2f& p : _points) {
			painter.Circle(view.WorldToScreen(p + view.Origin), handle, UiTheme::Accent, UiLayer::OverlayText);
		}
	}

	StringView GroundLineTool::GetHint() const
	{
		if (!_points.empty()) {
			return _("Click: add a point of the surface - Ctrl: corner of a tile - Right: lay the ground - Esc: start again");
		}
		return _("Click: start a line, or drag a point of one - click on a line: add a point - Shift+click: remove one - Right on a line: remove it");
	}

	void GroundLineTool::Lay()
	{
		std::size_t minPoints = (_options->Kind == SplineKind::Closed ? 3 : 2);
		if (_points.size() < minPoints) {
			_host.ShowMessage(_options->Kind == SplineKind::Closed ? _("Click at least three points around the shape first") : _("Click at least two points of the surface first"));
			return;
		}
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_host.GetDocument(), splines);
		TerrainSpline spline;
		spline.Layer = _host.GetCurrentLayer();
		spline.Depth = _options->Depth;
		spline.Kind = _options->Kind;
		spline.Smooth = _options->Smooth;
		for (const Vector2f& p : _points) {
			spline.Points.push_back(p);
		}
		if (spline.Kind != SplineKind::Closed) {
			std::sort(spline.Points.begin(), spline.Points.end(), [](const Vector2f& a, const Vector2f& b) { return a.X < b.X; });
		}
		if (ApplySpline(_host, splines, -1, &spline, _("Lay ground"))) {
			_points.clear();
			_selectedSpline = std::int32_t(splines.size()) - 1;
		}
	}

	void GroundLineTool::ApplyOptionsToSelected()
	{
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_host.GetDocument(), splines);
		if (_selectedSpline < 0 || _selectedSpline >= std::int32_t(splines.size())) {
			return;
		}
		TerrainSpline changed = splines[_selectedSpline];
		if (changed.Kind == _options->Kind && changed.Smooth == _options->Smooth && changed.Depth == _options->Depth) {
			return;
		}
		// A shape and a line go through their points in another order
		if ((changed.Kind == SplineKind::Closed) != (_options->Kind == SplineKind::Closed) && _options->Kind != SplineKind::Closed) {
			std::sort(changed.Points.begin(), changed.Points.end(), [](const Vector2f& a, const Vector2f& b) { return a.X < b.X; });
		}
		if (_options->Kind == SplineKind::Closed && changed.Points.size() < 3) {
			_host.ShowMessage(_("A shape has three points at least"));
			_options->Kind = changed.Kind;
			return;
		}
		changed.Kind = _options->Kind;
		changed.Smooth = _options->Smooth;
		changed.Depth = _options->Depth;
		ApplySpline(_host, splines, _selectedSpline, &changed, _("Change ground"));
	}

	void GroundLineTool::ShowWindow()
	{
		UiSurface* surface = _host.GetSurface();
		if (surface == nullptr || *_window != nullptr) {
			return;
		}

		std::shared_ptr<Options> options = _options;
		auto dialog = std::make_unique<UiDialog>(_("Ground Line"));
		auto* content = dialog->GetContent();
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			struct KindOption { const char* Label; SplineKind Kind; const char* Tooltip; };
			// TRANSLATORS: What a ground line lays, ground below it, a ceiling above it, a floating band, a closed shape
			static const KindOption Kinds[] = {
				{ N_("Ground"), SplineKind::Ground, N_("Ground below the line, down to the ground that's there") },
				{ N_("Ceiling"), SplineKind::Ceiling, N_("Ground above the line, its surface faces down") },
				{ N_("Band"), SplineKind::Band, N_("A floating strip of ground below the line, as thick as the depth") },
				{ N_("Shape"), SplineKind::Closed, N_("Ground inside of the line, its points go around the shape") },
			};
			for (const KindOption& option : Kinds) {
				SplineKind kind = option.Kind;
				auto* button = row->Add<UiButton>(_(option.Label), EditorIcon::None, [this, options, kind]() {
					options->Kind = kind;
					ApplyOptionsToSelected();
				});
				button->Style = UiButtonStyle::Flat;
				button->Tooltip = _(option.Tooltip);
				button->IsCheckedFunc = [options, kind]() { return (options->Kind == kind); };
			}
		}
		content->Add<UiCheckBox>(_("Smooth curve"), [options]() { return options->Smooth; }, [this, options](bool value) {
			options->Smooth = value;
			ApplyOptionsToSelected();
		})->Tooltip = _("Through the points in a curve, otherwise straight from one to the next");
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(_("Depth (tiles)"))->Flex = 1.0f;
			auto* depth = row->Add<UiNumberBox>([options]() { return float(options->Depth); }, [this, options](float value, bool final) {
				options->Depth = std::clamp(std::int32_t(value), 1, 32);
				if (final) {
					ApplyOptionsToSelected();
				}
			}, 1.0f, 32.0f, 1.0f, 0);
			depth->Width = 50.0f;
			depth->Tooltip = _("How deep the ground goes at most, how thick a band is");
		}
		content->Add<UiLabel>(_("The options change the line clicked last too"), UiTheme::TextDim);

		std::shared_ptr<UiElement*> window = _window;
		UiOverlayOptions overlayOptions;
		overlayOptions.Position = Vector2f(1.0e6f, 1.0e6f);
		overlayOptions.Modal = false;
		overlayOptions.CloseOnOutsideClick = false;
		overlayOptions.OnClosed = [window]() { *window = nullptr; };
		*_window = surface->PushOverlay(std::move(dialog), std::move(overlayOptions));
	}

	void GroundLineTool::CloseWindow()
	{
		if (*_window != nullptr) {
			if (UiSurface* surface = _host.GetSurface()) {
				surface->CloseOverlay(*_window);
			}
			*_window = nullptr;
		}
	}

	bool GroundLineTool::TraceSelection(IToolHost& host)
	{
		const TileSelection& selection = host.GetSelection();
		LevelDocument& document = host.GetDocument();
		std::int32_t layerIndex = selection.GetLayer();
		if (selection.IsEmpty() || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			host.ShowMessage(_("Select the ground to make a line of first"));
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		const TileShapes& shapes = host.GetTileShapes();
		auto surfaceOf = [&shapes](const TileRef& tile, float& left, float& right) {
			for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
				if (s.Tile == tile) {
					left = s.Left;
					right = s.Right;
					return;
				}
			}
			left = right = 0.0f;
		};

		// The top of the ground in each column, from the masks of its tiles
		TerrainSpline spline;
		spline.Layer = layerIndex;
		std::int32_t deepest = 1;
		const Recti& bounds = selection.GetBounds();
		for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
			std::int32_t top = -1, count = 0;
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
				const TileRef& tile = layer.GetTile(x, y);
				if (!selection.Contains(x, y) || !IsSolidTile(tile)) {
					continue;
				}
				if (top < 0) {
					top = y;
					float left, right;
					surfaceOf(tile, left, right);
					Vector2f a(x * 32.0f, y * 32.0f + left), b(x * 32.0f + 32.0f, y * 32.0f + right);
					if (spline.Points.empty() || (spline.Points.back() - a).Length() > 0.5f) {
						spline.Points.push_back(a);
					}
					spline.Points.push_back(b);
				}
				// Its own tiles, with nothing before them
				spline.Owned.push_back(TerrainSpline::OwnedTile{ x, y, tile, TileRef{} });
				count = y - top + 1;
			}
			deepest = std::max(deepest, count);
		}
		if (spline.Points.size() < 2) {
			host.ShowMessage(_("Select the ground to make a line of first"));
			return false;
		}
		// The ground laid again reaches as deep as it's selected
		spline.Depth = std::min(deepest, 255);

		// Points on a straight line between their neighbors aren't needed
		for (std::size_t i = 1; i + 1 < spline.Points.size(); ) {
			Vector2f a = spline.Points[i - 1], b = spline.Points[i], c = spline.Points[i + 1];
			float t = (c.X > a.X ? (b.X - a.X) / (c.X - a.X) : 0.0f);
			if (std::abs(a.Y + (c.Y - a.Y) * t - b.Y) < 1.0f) {
				spline.Points.erase(spline.Points.begin() + i);
			} else {
				i++;
			}
		}

		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(document, splines);
		splines.push_back(std::move(spline));
		host.Execute(std::make_unique<SetTerrainSplinesCommand>(document, splines, nullptr, _("Make ground line")));
		host.ShowMessage(_("The ground can be changed by its line now, with the Ground Line tool"));
		return true;
	}

	bool GroundLineTool::ApplySpline(IToolHost& host, SmallVectorImpl<TerrainSpline>& splines, std::int32_t index, const TerrainSpline* spline, StringView name)
	{
		LevelDocument& document = host.GetDocument();
		std::int32_t layerIndex;
		std::unordered_map<std::uint64_t, TileRef> changed;
		TerrainSpline result;
		if (!ComputeSpline(host, splines, index, spline, false, layerIndex, changed, result)) {
			return false;
		}

		auto tiles = std::make_unique<SetTilesCommand>(document, layerIndex, String(name));
		for (const auto& [key, tile] : changed) {
			Vector2i position = FromKey(key);
			tiles->Set(position.X, position.Y, tile);
		}
		tiles->Seal();

		SmallVector<TerrainSpline, 0> newSplines;
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (i != index) {
				newSplines.push_back(splines[i]);
			} else if (spline != nullptr) {
				newSplines.push_back(result);
			}
		}
		if (index < 0 && spline != nullptr) {
			newSplines.push_back(result);
		}
		host.Execute(std::make_unique<SetTerrainSplinesCommand>(document, newSplines, tiles->IsEmpty() ? nullptr : std::move(tiles), String(name)));
		splines.clear();
		for (TerrainSpline& s : newSplines) {
			splines.push_back(std::move(s));
		}
		return true;
	}
}

#endif
