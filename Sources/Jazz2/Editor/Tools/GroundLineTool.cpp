#include "GroundLineTool.h"

#if defined(WITH_EDITOR)

#include "../Common/ToolMath.h"
#include "../Document/Commands.h"
#include "../Smart/SmartTileModel.h"
#include "../Smart/TileShapes.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace Jazz2::Editor
{
	namespace
	{
		using ToolMath::CatmullRom;
		using ToolMath::HashCell;
		using ToolMath::IsSolidTile;
		using ToolMath::PositiveModulo;
		inline std::uint64_t ToKey(Vector2i cell) { return ToolMath::ToCellKey(cell); }
		inline Vector2i FromKey(std::uint64_t key) { return ToolMath::FromCellKey(key); }

		// Points far beyond any level, a damaged chunk can have anything there
		constexpr float MaxCoordinate = 65536.0f * 32.0f;

		// Reads the lines of a level, with points that can't be laid made harmless
		void ReadSplines(const LevelDocument& document, SmallVectorImpl<TerrainSpline>& splines)
		{
			TerrainSpline::Read(document, splines);
			for (TerrainSpline& spline : splines) {
				for (Vector2f& p : spline.Points) {
					p.X = (std::isfinite(p.X) ? std::clamp(p.X, -MaxCoordinate, MaxCoordinate) : 0.0f);
					p.Y = (std::isfinite(p.Y) ? std::clamp(p.Y, -MaxCoordinate, MaxCoordinate) : 0.0f);
				}
			}
		}

		// A tile another line laid over a line taken away, what's under it is what was there before the line taken away
		struct Handover
		{
			std::int32_t Spline;
			std::int32_t Owned;
			TileRef Previous;
		};

		// Where a closed line crosses a horizontal line, sorted from the left
		void GetRowCrossings(ArrayView<const Vector2f> polygon, float y, SmallVectorImpl<float>& crossings)
		{
			crossings.clear();
			for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
				const Vector2f& a = polygon[i];
				const Vector2f& b = polygon[j];
				if ((a.Y > y) != (b.Y > y)) {
					crossings.push_back(a.X + (b.X - a.X) * (y - a.Y) / (b.Y - a.Y));
				}
			}
			std::sort(crossings.begin(), crossings.end());
		}

		std::int32_t FloorDiv(std::int32_t value, std::int32_t divisor)
		{
			return (value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor));
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

		using ToolMath::NeighborOffsets;

		// The tiles of a layer as they are with some of them changed, by their positions in the layer
		struct LayerState
		{
			const DocumentLayer& Layer;
			std::unordered_map<std::uint64_t, TileRef> Changed;
			bool RepeatX;
			bool RepeatY;

			explicit LayerState(const DocumentLayer& layer)
				: Layer(layer),
					RepeatX(!layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0),
					RepeatY(!layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0)
			{
			}

			// The tile a cell shows, wrapped around a layer that repeats, `false` outside of the layer
			bool Wrap(Vector2i cell, Vector2i& tile) const
			{
				std::int32_t width = Layer.GetWidth(), height = Layer.GetHeight();
				if (width <= 0 || height <= 0) {
					return false;
				}
				tile = cell;
				if (RepeatX) {
					tile.X = PositiveModulo(tile.X, width);
				} else if (tile.X < 0 || tile.X >= width) {
					return false;
				}
				if (RepeatY) {
					tile.Y = PositiveModulo(tile.Y, height);
				} else if (tile.Y < 0 || tile.Y >= height) {
					return false;
				}
				return true;
			}

			// Limits cells along an axis to those that can be laid, a line can't loop forever on points far away: the
			// layer and a cell around it, or a single round of a layer that repeats
			void ClampRange(std::int32_t& from, std::int32_t& to, bool horizontal) const
			{
				std::int32_t size = (horizontal ? Layer.GetWidth() : Layer.GetHeight());
				if (horizontal ? RepeatX : RepeatY) {
					to = std::min<std::int32_t>(to, from + size - 1);
				} else {
					from = std::max<std::int32_t>(from, -1);
					to = std::min<std::int32_t>(to, size);
				}
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
					if (isSolid(Vector2i(cell.X + NeighborOffsets[i][0], cell.Y + NeighborOffsets[i][1]))) {
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
				state.ClampRange(cx1, cx2, true);
				for (std::int32_t cx = cx1; cx <= cx2; cx++) {
					float yl = GroundLineTool::GetLineY(line, cx * 32.0f), yr = GroundLineTool::GetLineY(line, cx * 32.0f + 32.0f);
					std::int32_t cyTop = std::int32_t(std::floor(std::min(yl, yr) / 32.0f));
					std::int32_t cyBottom = std::int32_t(std::floor(std::max(yl, yr) / 32.0f));
					state.ClampRange(cyTop, cyBottom, false);
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
				state.ClampRange(cx1, cx2, true);
				for (std::int32_t cx = cx1; cx <= cx2; cx++) {
					float yl = GroundLineTool::GetLineY(line, cx * 32.0f), yr = GroundLineTool::GetLineY(line, cx * 32.0f + 32.0f);
					std::int32_t cyTop = std::int32_t(std::floor(std::min(yl, yr) / 32.0f));
					std::int32_t cyBottom = std::int32_t(std::floor(std::max(yl, yr) / 32.0f));
					state.ClampRange(cyTop, cyBottom, false);
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
				state.ClampRange(cx1, cx2, true);
				state.ClampRange(cy1, cy2, false);
				std::unordered_set<std::uint64_t> inside;
				SmallVector<float, 8> crossings;
				if (cx1 <= cx2) {
					// 4 by 4 samples in a cell, the samples of a row are inside between the crossings of the line, so each row
					// of them is found at once
					std::int32_t columns = cx2 - cx1 + 1;
					SmallVector<std::uint8_t, 0> counts;
					counts.resize(std::size_t(columns));
					for (std::int32_t cy = cy1; cy <= cy2; cy++) {
						std::fill(counts.begin(), counts.end(), std::uint8_t(0));
						for (std::int32_t sy = 0; sy < 4; sy++) {
							GetRowCrossings(line, cy * 32.0f + sy * 8.0f + 4.0f, crossings);
							for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
								// Samples at x = 8 * s + 4 with crossings[k] <= x < crossings[k + 1]
								std::int32_t first = std::int32_t(std::ceil((crossings[k] - 4.0f) / 8.0f));
								std::int32_t last = std::int32_t(std::ceil((crossings[k + 1] - 4.0f) / 8.0f)) - 1;
								first = std::max<std::int32_t>(first, cx1 * 4);
								last = std::min<std::int32_t>(last, cx2 * 4 + 3);
								for (std::int32_t s = first; s <= last; s++) {
									counts[std::size_t(FloorDiv(s, 4) - cx1)]++;
								}
							}
						}
						for (std::int32_t cx = cx1; cx <= cx2; cx++) {
							TileRef existing;
							if (counts[std::size_t(cx - cx1)] >= 8 && cellTile(Vector2i(cx, cy), existing)) {
								inside.insert(ToKey(Vector2i(cx, cy)));
							}
						}
					}
				}
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
					Vector2i neighbor(cell.X + NeighborOffsets[i][0], cell.Y + NeighborOffsets[i][1]);
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

		// What laying a line changes: the tiles of its layer, and the line with the tiles it owns then; a line taken away hands
		// over the tiles other lines laid over it, see `handovers`
		bool ComputeSpline(IToolHost& host, const SmallVectorImpl<TerrainSpline>& splines, std::int32_t index, const TerrainSpline* spline,
			bool quiet, std::int32_t& layerIndex, std::unordered_map<std::uint64_t, TileRef>& changed, TerrainSpline& result,
			SmallVectorImpl<Handover>* handovers = nullptr)
		{
			LevelDocument& document = host.GetDocument();
			layerIndex = (spline != nullptr ? spline->Layer : (index >= 0 && index < std::int32_t(splines.size()) ? splines[index].Layer : -1));
			if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || !host.IsLayerEditable(layerIndex)) {
				if (!quiet) {
					host.ShowMessage(_x("editor", "The layer is locked or hidden"));
				}
				return false;
			}
			const TileShapes& shapes = host.GetTileShapes();
			if (spline != nullptr && shapes.GetSurfaces().empty()) {
				if (!quiet) {
					host.ShowMessage(_x("editor", "The tile set has no pieces of ground whose masks follow a line"));
				}
				return false;
			}
			SmartTileModel& model = host.GetSmartTiles();
			const DocumentLayer& layer = document.Layers[layerIndex];
			LayerState state(layer);

			// The tiles the other lines of the layer wrote, a line laid over this one may have written the same tile again
			struct OtherOwner
			{
				std::int32_t Spline;
				std::int32_t Owned;
			};
			std::unordered_map<std::uint64_t, SmallVector<OtherOwner, 1>> others;
			for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
				if (i == index || splines[i].Layer != layerIndex) {
					continue;
				}
				for (std::int32_t j = 0; j < std::int32_t(splines[i].Owned.size()); j++) {
					const TerrainSpline::OwnedTile& owned = splines[i].Owned[j];
					if (layer.Contains(owned.X, owned.Y)) {
						others[ToKey(Vector2i(owned.X, owned.Y))].push_back(OtherOwner{ i, j });
					}
				}
			}

			// The ground as it was goes first: its tiles still as it wrote them get back what was there, those changed by hand
			// since stay and aren't written again. A tile another line wrote over it stays too, owned as it was, so it comes
			// back once that line goes; a line taken away hands what was under it over to that line instead.
			std::unordered_set<std::uint64_t> pinned;
			SmallVector<TerrainSpline::OwnedTile, 0> covered;
			if (index >= 0 && index < std::int32_t(splines.size())) {
				for (const TerrainSpline::OwnedTile& owned : splines[index].Owned) {
					if (!layer.Contains(owned.X, owned.Y)) {
						continue;
					}
					std::uint64_t key = ToKey(Vector2i(owned.X, owned.Y));
					const TileRef& current = layer.GetTile(owned.X, owned.Y);
					const OtherOwner* above = nullptr;
					auto it = others.find(key);
					if (it != others.end()) {
						for (const OtherOwner& other : it->second) {
							const TerrainSpline::OwnedTile& otherTile = splines[other.Spline].Owned[other.Owned];
							if (otherTile.Previous == owned.Written && otherTile.Written == current) {
								above = &other;
								break;
							}
						}
					}
					if (above != nullptr) {
						pinned.insert(key);
						if (spline != nullptr) {
							covered.push_back(owned);
						} else if (handovers != nullptr) {
							handovers->push_back(Handover{ above->Spline, above->Owned, owned.Previous });
						}
						continue;
					}
					if (current == owned.Written) {
						state.Changed[key] = owned.Previous;
					} else {
						pinned.insert(key);
					}
				}
			}

			// Then the ground as it should be
			if (spline != nullptr) {
				result = *spline;
				result.Owned.clear();
				for (const TerrainSpline::OwnedTile& owned : covered) {
					result.Owned.push_back(owned);
				}
				std::unordered_map<std::uint64_t, TileRef> decided;
				Generate(result, state, pinned, model, shapes, decided);
				// A line longer than a layer that repeats goes over some of its tiles twice, the first one is kept
				std::unordered_set<std::uint64_t> written;
				for (const auto& [key, tile] : decided) {
					Vector2i wrapped;
					if (!state.Wrap(FromKey(key), wrapped)) {
						continue;
					}
					std::uint64_t wrappedKey = ToKey(wrapped);
					if (pinned.find(wrappedKey) != pinned.end() || !written.insert(wrappedKey).second) {
						continue;
					}
					TileRef previous = state.Get(wrapped);
					state.Changed[wrappedKey] = tile;
					result.Owned.push_back(TerrainSpline::OwnedTile{ wrapped.X, wrapped.Y, tile, previous });
				}
			}
			changed = Death::move(state.Changed);
			return true;
		}
	}

	GroundLineTool::GroundLineTool(IToolHost& host)
		: EditorTool(host), _dragSpline(-1), _dragPoint(-1), _dragInsert(false), _selectedSpline(-1), _selectedLayer(-1),
			_options(std::make_shared<Options>()), _previewLayer(-1), _previewPending(false), _cacheValid(false), _overlayShapesRevision(0)
	{
	}

	EditorToolType GroundLineTool::GetType() const
	{
		return EditorToolType::Ground;
	}

	void GroundLineTool::OnActivate()
	{
		// Learning the tile set takes a while, it starts right away
		_host.GetSmartTiles();
	}

	void GroundLineTool::OnDeactivate()
	{
		// The points of a new line stay, the view can be moved by another tool for a while between them, but a point
		// being dragged isn't, the release goes to the other tool
		ClearPreview();
		_previewPending = false;
		_dragSpline = -1;
		_dragPoint = -1;
		_dragInsert = false;
	}

	bool GroundLineTool::HasPointerCapture() const
	{
		return (_dragSpline >= 0);
	}

	std::int32_t GroundLineTool::GetSelectedSpline() const
	{
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(_host.GetDocument(), splines);
		return FindSelected(splines);
	}

	void GroundLineTool::Select(const SmallVectorImpl<TerrainSpline>& splines, std::int32_t index)
	{
		_selectedPoints.clear();
		if (index < 0 || index >= std::int32_t(splines.size())) {
			_selectedSpline = -1;
			_selectedLayer = -1;
			return;
		}
		_selectedSpline = index;
		_selectedLayer = splines[index].Layer;
		for (const Vector2f& p : splines[index].Points) {
			_selectedPoints.push_back(p);
		}
	}

	std::int32_t GroundLineTool::FindSelected(const SmallVectorImpl<TerrainSpline>& splines) const
	{
		if (_selectedSpline < 0) {
			return -1;
		}
		auto isSelected = [this](const TerrainSpline& spline) {
			if (spline.Layer != _selectedLayer || spline.Points.size() != _selectedPoints.size()) {
				return false;
			}
			for (std::size_t i = 0; i < spline.Points.size(); i++) {
				if (spline.Points[i] != _selectedPoints[i]) {
					return false;
				}
			}
			return true;
		};
		// Where it was, or where an undo moved it
		if (_selectedSpline < std::int32_t(splines.size()) && isSelected(splines[_selectedSpline])) {
			return _selectedSpline;
		}
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (isSelected(splines[i])) {
				return i;
			}
		}
		return -1;
	}

	const SmallVectorImpl<TerrainSpline>& GroundLineTool::GetSplines()
	{
		// Compared byte by byte, which is much cheaper than reading them every frame
		const Formats::ChunkRecord* chunk = _host.GetDocument().FindChunk(TerrainSpline::Chunk);
		std::size_t size = (chunk != nullptr ? chunk->Data.size() : 0);
		bool same = (_cacheValid && size == _cachedChunk.size() && (size == 0 || std::memcmp(chunk->Data.data(), _cachedChunk.data(), size) == 0));
		if (!same) {
			_cachedChunk.clear();
			if (chunk != nullptr) {
				for (std::uint8_t byte : chunk->Data) {
					_cachedChunk.push_back(byte);
				}
			}
			ReadSplines(_host.GetDocument(), _cachedSplines);
			_cacheValid = true;
			_overlays.clear();
		}
		return _cachedSplines;
	}

	void GroundLineTool::UpdateOverlays(const SmallVectorImpl<TerrainSpline>& splines, const TileShapes& shapes)
	{
		if (_overlays.size() == splines.size() && _overlayShapesRevision == shapes.GetRevision()) {
			return;
		}
		_overlayShapesRevision = shapes.GetRevision();
		_overlays.clear();

		// The pieces of ground and of ceiling by their tiles
		auto tileKey = [](const TileRef& tile) {
			return std::uint32_t(tile.Index) | (std::uint32_t(tile.Source) << 16) | (std::uint32_t(tile.Flags & 0x03) << 24);
		};
		std::unordered_map<std::uint32_t, const TileShapes::Surface*> grounds, ceilings;
		for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
			grounds.emplace(tileKey(s.Tile), &s);
		}
		for (const TileShapes::Surface& s : shapes.GetCeilings()) {
			ceilings.emplace(tileKey(s.Tile), &s);
		}

		// How far the collision of each piece laid is from the line, measured once for all frames
		for (const TerrainSpline& spline : splines) {
			SplineOverlay& overlay = _overlays.emplace_back();
			SampleLine(spline, overlay.Fine);
			for (const TerrainSpline::OwnedTile& owned : spline.Owned) {
				OwnedOverlay& item = overlay.Owned.emplace_back();
				item = OwnedOverlay{ 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f };
				std::uint32_t key = tileKey(owned.Written);
				Vector2f origin(owned.X * 32.0f, owned.Y * 32.0f);
				auto it = grounds.find(key);
				if (it != grounds.end()) {
					item.GroundLeft = it->second->Left;
					item.GroundRight = it->second->Right;
					item.GroundMiss = std::max(DistanceToLine(overlay.Fine, origin + Vector2f(0.0f, item.GroundLeft)),
						DistanceToLine(overlay.Fine, origin + Vector2f(32.0f, item.GroundRight)));
				}
				it = ceilings.find(key);
				if (it != ceilings.end()) {
					item.CeilingLeft = it->second->Left;
					item.CeilingRight = it->second->Right;
					item.CeilingMiss = std::max(DistanceToLine(overlay.Fine, origin + Vector2f(0.0f, item.CeilingLeft)),
						DistanceToLine(overlay.Fine, origin + Vector2f(32.0f, item.CeilingRight)));
				}
			}
		}
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
			std::int32_t steps = std::clamp<std::int32_t>(std::int32_t((p2 - p1).Length() / 4.0f), 2, 64);
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
		ReadSplines(_host.GetDocument(), splines);
		std::int32_t hitSpline = -1, hitPoint = -1, hitSegment = -1;
		bool hit = (_points.empty() && HitTest(splines, view, pointer.Screen, hitSpline, hitPoint, hitSegment));
		if (hit) {
			// Its options are shown, and changed by the window
			Select(splines, hitSpline);
			_options->Kind = splines[hitSpline].Kind;
			_options->Smooth = splines[hitSpline].Smooth;
			_options->Depth = splines[hitSpline].Depth;
		}

		if (pointer.Button == MouseButton::Right) {
			if (!_points.empty()) {
				Lay();
			} else if (hit) {
				// The ground of the line goes away, what was there before comes back
				// TRANSLATORS: Name of an undo step in the history, after the ground laid by a ground line was removed
				ApplySpline(_host, splines, hitSpline, nullptr, _x("editor", "Remove ground"));
				Select(splines, -1);
			} else {
				_host.ShowMessage(_x("editor", "Click points of the surface first, the right button then lays the ground"));
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
				_host.ShowMessage(_x("editor", "A line has two points at least, a shape three - the right button removes the whole ground"));
				return;
			}
			changed.Points.erase(changed.Points.begin() + hitPoint);
			// TRANSLATORS: Name of an undo step in the history, after a ground line and its ground were changed
			if (ApplySpline(_host, splines, hitSpline, &changed, _x("editor", "Change ground"))) {
				Select(splines, hitSpline);
			}
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
		Select(splines, -1);
		_points.push_back(GetPoint(pointer));
	}

	void GroundLineTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragSpline >= 0) {
			_dragPos = GetPoint(pointer);
			if (_dragPos != _previewPos) {
				// Laid at most once a frame, the pointer can move more times in one
				_previewPos = _dragPos;
				_previewPending = true;
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
		// The ground as it would be laid now, shown in the view only, nothing where it can't be laid
		ClearPreview();
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(_host.GetDocument(), splines);
		TerrainSpline dragged, result;
		std::int32_t layerIndex;
		std::unordered_map<std::uint64_t, TileRef> changed;
		if (!GetDraggedSpline(splines, dragged) || !ComputeSpline(_host, splines, _dragSpline, &dragged, true, layerIndex, changed, result)) {
			return;
		}
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
		_previewPending = false;
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(_host.GetDocument(), splines);
		TerrainSpline changed;
		std::int32_t index = _dragSpline;
		bool valid = GetDraggedSpline(splines, changed);
		_dragSpline = -1;
		_dragPoint = -1;
		_dragInsert = false;
		if (valid && ApplySpline(_host, splines, index, &changed, _x("editor", "Change ground"))) {
			Select(splines, index);
		}
	}

	void GroundLineTool::Cancel()
	{
		ClearPreview();
		_previewPending = false;
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

		// The point dragged moved since the last frame, the ground follows it
		if (_previewPending) {
			_previewPending = false;
			UpdatePreview();
		}

		// The lines kept, of the layer being edited, read and measured again only when they change
		const SmallVectorImpl<TerrainSpline>& splines = GetSplines();
		UpdateOverlays(splines, shapes);
		std::int32_t selectedSpline = FindSelected(splines);
		std::int32_t hoverSpline = -1, hoverPoint = -1, hoverSegment = -1;
		if (hoverValid && _points.empty() && _dragSpline < 0) {
			HitTest(splines, view, hover.Screen, hoverSpline, hoverPoint, hoverSegment);
		}
		for (std::int32_t i = 0; i < std::int32_t(splines.size()); i++) {
			if (splines[i].Layer != layerIndex) {
				continue;
			}
			TerrainSpline dragged;
			bool isDragged = (i == _dragSpline && GetDraggedSpline(splines, dragged));
			const TerrainSpline& shown = (isDragged ? dragged : splines[i]);
			SmallVector<Vector2f, 0> draggedFine;
			if (isDragged) {
				SampleLine(shown, draggedFine);
			}
			const SmallVectorImpl<Vector2f>& fine = (isDragged ? draggedFine : _overlays[i].Fine);

			// Where the collision of the ground laid is, red where it misses the line
			if (i != _dragSpline && layerIndex >= 0 && layerIndex < std::int32_t(document.Layers.size())) {
				const DocumentLayer& layer = document.Layers[layerIndex];
				const SplineOverlay& overlay = _overlays[i];
				for (std::size_t j = 0; j < splines[i].Owned.size() && j < overlay.Owned.size(); j++) {
					const TerrainSpline::OwnedTile& owned = splines[i].Owned[j];
					if (!layer.Contains(owned.X, owned.Y) || layer.GetTile(owned.X, owned.Y) != owned.Written) {
						continue;
					}
					const OwnedOverlay& item = overlay.Owned[j];
					bool openAbove = !layer.Contains(owned.X, owned.Y - 1) || !IsSolidTile(layer.GetTile(owned.X, owned.Y - 1));
					bool openBelow = !layer.Contains(owned.X, owned.Y + 1) || !IsSolidTile(layer.GetTile(owned.X, owned.Y + 1));
					float left, right, miss;
					if (openAbove && item.GroundMiss >= 0.0f) {
						left = item.GroundLeft;
						right = item.GroundRight;
						miss = item.GroundMiss;
					} else if (openBelow && item.CeilingMiss >= 0.0f) {
						left = item.CeilingLeft;
						right = item.CeilingRight;
						miss = item.CeilingMiss;
					} else {
						continue;
					}
					Vector2f a(owned.X * 32.0f, owned.Y * 32.0f + left), b(owned.X * 32.0f + 32.0f, owned.Y * 32.0f + right);
					// Colors of what is right and wrong, as the theme has them, see UiTheme
					Colorf color = (miss <= 4.0f ? UiTheme::OverlayGood : UiTheme::OverlayBad);
					color.A = (miss <= 4.0f ? 0.45f : std::clamp(0.4f + miss / 32.0f, 0.4f, 0.95f));
					painter.Line(view.WorldToScreen(a + view.Origin), view.WorldToScreen(b + view.Origin), std::max(1.0f, thickness * 0.75f), color, UiLayer::Overlay);
				}
			}

			SmallVector<Vector2f, 0> screenLine;
			for (const Vector2f& p : fine) {
				screenLine.push_back(view.WorldToScreen(p + view.Origin));
			}
			bool active = (i == hoverSpline || i == _dragSpline || i == selectedSpline);
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
			return _x("editor", "Click: add a point of the surface - Ctrl: corner of a tile - Right: lay the ground - Esc: start again");
		}
		return _x("editor", "Click: start a line, or drag a point of one - click on a line: add a point - Shift+click: remove one - Right on a line: remove it");
	}

	void GroundLineTool::Lay()
	{
		std::size_t minPoints = (_options->Kind == SplineKind::Closed ? 3 : 2);
		if (_points.size() < minPoints) {
			_host.ShowMessage(_options->Kind == SplineKind::Closed ? _x("editor", "Click at least three points around the shape first") : _x("editor", "Click at least two points of the surface first"));
			return;
		}
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(_host.GetDocument(), splines);
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
		// TRANSLATORS: Name of an undo step in the history, after a ground line laid new ground
		if (ApplySpline(_host, splines, -1, &spline, _x("editor", "Lay ground"))) {
			_points.clear();
			Select(splines, std::int32_t(splines.size()) - 1);
		}
	}

	void GroundLineTool::ApplyOptionsToSelected()
	{
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(_host.GetDocument(), splines);
		// The line clicked last, as long as it's there and it's seen, an undo can take it away
		std::int32_t selected = FindSelected(splines);
		if (selected < 0 || splines[selected].Layer != _host.GetCurrentLayer()) {
			Select(splines, -1);
			return;
		}
		TerrainSpline changed = splines[selected];
		if (changed.Kind == _options->Kind && changed.Smooth == _options->Smooth && changed.Depth == _options->Depth) {
			return;
		}
		// A shape and a line go through their points in another order
		if ((changed.Kind == SplineKind::Closed) != (_options->Kind == SplineKind::Closed) && _options->Kind != SplineKind::Closed) {
			std::sort(changed.Points.begin(), changed.Points.end(), [](const Vector2f& a, const Vector2f& b) { return a.X < b.X; });
		}
		if (_options->Kind == SplineKind::Closed && changed.Points.size() < 3) {
			_host.ShowMessage(_x("editor", "A shape has three points at least"));
			_options->Kind = changed.Kind;
			return;
		}
		changed.Kind = _options->Kind;
		changed.Smooth = _options->Smooth;
		changed.Depth = _options->Depth;
		if (ApplySpline(_host, splines, selected, &changed, _x("editor", "Change ground"))) {
			// Sorted points are other points
			Select(splines, selected);
		}
	}

	void GroundLineTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		std::shared_ptr<Options> options = _options;
		// The options change the line clicked last too
		const ToolOptionItem kinds[] = {
			// TRANSLATORS: What a ground line lays, ground below it, a ceiling above it, a floating band, a closed shape
			{ _x("editor", "Ground"), EditorIcon::None, nullptr, _x("editor", "Ground below the line, down to the ground that's there - changes the line clicked last too") },
			{ _x("editor", "Ceiling"), EditorIcon::None, nullptr, _x("editor", "Ground above the line, its surface faces down - changes the line clicked last too") },
			{ _x("editor", "Band"), EditorIcon::None, nullptr, _x("editor", "A floating strip of ground below the line, as thick as the depth - changes the line clicked last too") },
			{ _x("editor", "Shape"), EditorIcon::None, nullptr, _x("editor", "Ground inside of the line, its points go around the shape - changes the line clicked last too") },
		};
		static const SplineKind KindValues[] = { SplineKind::Ground, SplineKind::Ceiling, SplineKind::Band, SplineKind::Closed };
		// TRANSLATORS: Label of the choice of what a ground line lays
		builder.AddChoice(_x("editor", "Lays"), kinds, [options]() {
			for (std::int32_t i = 0; i < std::int32_t(sizeof(KindValues) / sizeof(KindValues[0])); i++) {
				if (KindValues[i] == options->Kind) {
					return i;
				}
			}
			return std::int32_t(0);
		}, [this, options](std::int32_t index) {
			if (index >= 0 && index < std::int32_t(sizeof(KindValues) / sizeof(KindValues[0]))) {
				options->Kind = KindValues[index];
				ApplyOptionsToSelected();
			}
		});
		builder.AddSeparator();
		// TRANSLATORS: Toggle in the toolbar, the ground line is a smooth curve through its points
		builder.AddToggle({ _x("editor", "Smooth"), EditorIcon::None, nullptr, _x("editor", "Through the points in a curve, otherwise straight from one to the next") },
			[options]() { return options->Smooth; }, [this, options](bool value) {
				options->Smooth = value;
				ApplyOptionsToSelected();
			});
		// TRANSLATORS: Label of how deep the ground a ground line lays goes, in tiles
		builder.AddNumber(_x("editor", "Depth"), _x("editor", "How deep the ground goes at most in tiles, how thick a band is"), 1.0f, 32.0f, 1.0f,
			[options]() { return float(options->Depth); }, [this, options](float value, bool final) {
				options->Depth = std::clamp<std::int32_t>(std::int32_t(value), 1, 32);
				if (final) {
					ApplyOptionsToSelected();
				}
			});
	}

	bool GroundLineTool::TraceSelection(IToolHost& host)
	{
		const TileSelection& selection = host.GetSelection();
		LevelDocument& document = host.GetDocument();
		std::int32_t layerIndex = selection.GetLayer();
		if (selection.IsEmpty() || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			host.ShowMessage(_x("editor", "Select the ground to make a line of first"));
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

		// Tiles other lines of the layer wrote and still have, the new line is laid over them, so it doesn't take them away
		SmallVector<TerrainSpline, 0> splines;
		ReadSplines(document, splines);
		std::unordered_set<std::uint64_t> ownedByOthers;
		for (const TerrainSpline& other : splines) {
			if (other.Layer != layerIndex) {
				continue;
			}
			for (const TerrainSpline::OwnedTile& owned : other.Owned) {
				if (layer.Contains(owned.X, owned.Y) && layer.GetTile(owned.X, owned.Y) == owned.Written) {
					ownedByOthers.insert(ToKey(Vector2i(owned.X, owned.Y)));
				}
			}
		}

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
				// Its own tiles, with nothing before them, but those of another line, with the same tile before them
				bool other = (ownedByOthers.find(ToKey(Vector2i(x, y))) != ownedByOthers.end());
				spline.Owned.push_back(TerrainSpline::OwnedTile{ x, y, tile, other ? tile : TileRef{} });
				count = y - top + 1;
			}
			deepest = std::max(deepest, count);
		}
		if (spline.Points.size() < 2) {
			host.ShowMessage(_x("editor", "Select the ground to make a line of first"));
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

		splines.push_back(Death::move(spline));
		// TRANSLATORS: Name of an undo step in the history, after a line was made of the selected ground to change it by
		host.Execute(std::make_unique<SetTerrainSplinesCommand>(document, splines, nullptr, _x("editor", "Make ground line")));
		host.ShowMessage(_x("editor", "The ground can be changed by its line now, with the Ground Line tool"));
		return true;
	}

	bool GroundLineTool::ApplySpline(IToolHost& host, SmallVectorImpl<TerrainSpline>& splines, std::int32_t index, const TerrainSpline* spline, StringView name)
	{
		LevelDocument& document = host.GetDocument();
		std::int32_t layerIndex;
		std::unordered_map<std::uint64_t, TileRef> changed;
		TerrainSpline result;
		SmallVector<Handover, 0> handovers;
		if (!ComputeSpline(host, splines, index, spline, false, layerIndex, changed, result, &handovers)) {
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
		// The lines laid over the one taken away have what was under it under them now, they go back to that
		for (const Handover& handover : handovers) {
			std::int32_t target = (spline == nullptr && index >= 0 && handover.Spline > index ? handover.Spline - 1 : handover.Spline);
			if (target >= 0 && target < std::int32_t(newSplines.size()) && handover.Owned < std::int32_t(newSplines[target].Owned.size())) {
				newSplines[target].Owned[handover.Owned].Previous = handover.Previous;
			}
		}
		host.Execute(std::make_unique<SetTerrainSplinesCommand>(document, newSplines, tiles->IsEmpty() ? nullptr : Death::move(tiles), String(name)));
		splines.clear();
		for (TerrainSpline& s : newSplines) {
			splines.push_back(Death::move(s));
		}
		return true;
	}
}

#endif
