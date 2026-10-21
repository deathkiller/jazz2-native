#include "SmartFill.h"

#if defined(WITH_EDITOR)

#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Document/LevelDocument.h"
#include "../Document/Commands.h"
#include "../Imaging/TileMasks.h"
#include "../Smart/SmartTileModel.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Cryptography/xxHash.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
		// Selected cells at most, and the most cells of the rectangle around them, it's done at once
		constexpr std::int64_t MaxCells = 1 << 20;
		constexpr std::int64_t MaxAreaCells = 1 << 22;

		std::uint32_t HashCell(std::int32_t x, std::int32_t y)
		{
			std::uint32_t h = std::uint32_t(x) * 0x8DA6B343u ^ std::uint32_t(y) * 0xD8163841u;
			h ^= h >> 15;
			return h * 0x2C1B3C6Du;
		}

		bool IsSolidTile(const TileRef& tile)
		{
			return (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
		}

		// What both operations need: the layer of the selection, which can be changed, and the tiles learned
		const DocumentLayer* Prepare(IEditorContext& session, SmartTileModel*& model, std::int32_t& layerIndex)
		{
			const TileSelection& selection = session.GetSelection();
			if (selection.IsEmpty()) {
				session.ShowMessage(_x("editor", "Select the tiles first"));
				return nullptr;
			}
			model = &session.GetSmartTiles();
			if (!model->IsReady()) {
				session.ShowMessage(_fx("editor", "Still learning how the tiles fit - {}%, try it again in a moment", std::int32_t(model->GetProgress() * 100.0f)));
				return nullptr;
			}
			if (model->GetLevelCount() == 0 && model->GetRules().GetCount() == 0) {
				session.ShowMessage(_x("editor", "No level uses this tile set yet, so there's nothing to learn the tiles from"));
				return nullptr;
			}
			LevelDocument& document = session.GetDocument();
			layerIndex = selection.GetLayer();
			if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || !session.IsLayerEditable(layerIndex)) {
				session.ShowMessage(_x("editor", "The layer is locked or hidden"));
				return nullptr;
			}
			// All of it is worked out at once, the cells of the selection and of the rectangle around it are kept
			const Recti& bounds = selection.GetBounds();
			bool tooLarge = (std::int64_t(bounds.W + 2) * std::int64_t(bounds.H + 2) > MaxAreaCells);
			std::int64_t count = 0;
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H && !tooLarge; y++) {
				for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
					count += (selection.Contains(x, y) ? 1 : 0);
				}
				tooLarge = (count > MaxCells);
			}
			if (tooLarge) {
				session.ShowMessage(_fx("editor", "The selection is too large, select {} tiles at most", std::int32_t(MaxCells)));
				return nullptr;
			}
			return &document.Layers[layerIndex];
		}

		// The tiles of a layer with some of them changed, in a rectangle around the selection. A cell can be solid with its
		// tile not chosen yet: it's solid for its neighbors, but no tile for them to go well with, any tile standing for it
		// would be taken for one the levels have next to them.
		struct LayerTiles
		{
			const DocumentLayer& Layer;
			// The cells kept, the selection and the cells around it, within the layer
			Recti Area;
			SmallVector<TileRef, 0> Tiles;
			BitArray Changed;
			BitArray Unknown;

			LayerTiles(const DocumentLayer& layer, const Recti& bounds)
				: Layer(layer)
			{
				std::int32_t x1 = std::max(bounds.X - 1, 0), y1 = std::max(bounds.Y - 1, 0);
				std::int32_t x2 = std::min(bounds.X + bounds.W + 1, layer.GetWidth()), y2 = std::min(bounds.Y + bounds.H + 1, layer.GetHeight());
				Area = Recti(x1, y1, std::max(x2 - x1, 0), std::max(y2 - y1, 0));
				std::size_t size = std::size_t(Area.W) * std::size_t(Area.H);
				Tiles.reserve(size);
				for (std::int32_t y = Area.Y; y < Area.Y + Area.H; y++) {
					for (std::int32_t x = Area.X; x < Area.X + Area.W; x++) {
						Tiles.push_back(layer.GetTile(x, y));
					}
				}
				Changed = BitArray(ValueInit, size);
				Unknown = BitArray(ValueInit, size);
			}

			bool Contains(std::int32_t x, std::int32_t y) const
			{
				return (x >= 0 && y >= 0 && x < Layer.GetWidth() && y < Layer.GetHeight());
			}

			bool IsInArea(std::int32_t x, std::int32_t y) const
			{
				return (x >= Area.X && y >= Area.Y && x < Area.X + Area.W && y < Area.Y + Area.H);
			}

			std::size_t IndexOf(std::int32_t x, std::int32_t y) const
			{
				return std::size_t(y - Area.Y) * std::size_t(Area.W) + std::size_t(x - Area.X);
			}

			TileRef Get(std::int32_t x, std::int32_t y) const
			{
				return (IsInArea(x, y) ? Tiles[IndexOf(x, y)] : Layer.GetTile(x, y));
			}

			void Set(std::int32_t x, std::int32_t y, const TileRef& tile)
			{
				if (IsInArea(x, y)) {
					std::size_t index = IndexOf(x, y);
					Tiles[index] = tile;
					Changed.set(index);
					Unknown.reset(index);
				}
			}

			// Makes a cell solid without choosing its tile yet
			void SetUnknownSolid(std::int32_t x, std::int32_t y)
			{
				if (IsInArea(x, y)) {
					Unknown.set(IndexOf(x, y));
				}
			}

			bool IsUnknown(std::int32_t x, std::int32_t y) const
			{
				return (IsInArea(x, y) && Unknown[IndexOf(x, y)]);
			}

			// Gives a cell back the tile the layer has
			void Revert(std::int32_t x, std::int32_t y)
			{
				if (IsInArea(x, y)) {
					std::size_t index = IndexOf(x, y);
					Tiles[index] = Layer.GetTile(x, y);
					Changed.reset(index);
					Unknown.reset(index);
				}
			}

			// Outside of the layer counts as solid, as the levels were learned
			bool IsSolid(std::int32_t x, std::int32_t y) const
			{
				return (!Contains(x, y) || IsUnknown(x, y) || IsSolidTile(Get(x, y)));
			}

			std::uint8_t GetNeighbors(std::int32_t x, std::int32_t y) const
			{
				std::uint8_t neighbors = 0;
				for (std::int32_t i = 0; i < 8; i++) {
					if (IsSolid(x + Offsets[i][0], y + Offsets[i][1])) {
						neighbors |= std::uint8_t(1 << i);
					}
				}
				return neighbors;
			}

			// A model tile next to a cell, `false` if there's none, or it's solid without its tile chosen yet
			bool GetModelTile(std::int32_t x, std::int32_t y, TileRef& tile) const
			{
				if (!Contains(x, y) || IsUnknown(x, y)) {
					return false;
				}
				tile = Get(x, y);
				return SmartTileModel::IsModelTile(tile);
			}

			bool Pick(SmartTileModel& model, std::int32_t x, std::int32_t y, const TileRef* keep, TileRef& result) const
			{
				TileRef left, up, right, down;
				bool hasLeft = GetModelTile(x - 1, y, left), hasUp = GetModelTile(x, y - 1, up);
				bool hasRight = GetModelTile(x + 1, y, right), hasDown = GetModelTile(x, y + 1, down);
				return model.Pick(GetNeighbors(x, y), Layer.IsSpriteLayer(), hasLeft ? &left : nullptr, hasUp ? &up : nullptr,
					hasRight ? &right : nullptr, hasDown ? &down : nullptr, HashCell(x, y), result, keep);
			}
		};

		bool Commit(IEditorContext& session, std::int32_t layerIndex, const LayerTiles& tiles, StringView name)
		{
			auto command = std::make_unique<SetTilesCommand>(session.GetDocument(), layerIndex, String(name));
			const Recti& area = tiles.Area;
			for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
				for (std::int32_t x = area.X; x < area.X + area.W; x++) {
					std::size_t index = tiles.IndexOf(x, y);
					if (tiles.Changed[index]) {
						command->Set(x, y, tiles.Tiles[index]);
					}
				}
			}
			command->Seal();
			if (command->IsEmpty()) {
				return false;
			}
			session.Execute(Death::move(command));
			return true;
		}

		// Collision masks of the tiles of the default tile set as the level has them, changed in it or not, flipped every
		// way, to find tiles of the same mask
		class MaskIndex
		{
		public:
			MaskIndex(const LevelDocument& document, const Formats::TileSetData& data)
				: _document(document), _data(data)
			{
				for (std::int32_t index = 1; index < data.TileCount; index++) {
					for (std::uint8_t flags = 0; flags < 4; flags++) {
						Mask mask;
						Transform(index, flags, mask);
						if (IsEmpty(mask)) {
							break;
						}
						_byHash[Hash(mask)].push_back(TileRef{ std::uint16_t(index), TileRef::DefaultTileSet, flags });
					}
				}
			}

			// Tiles with exactly the mask of a tile, the tile itself among them; none for a tile without collision
			void Find(const TileRef& tile, SmallVectorImpl<TileRef>& result) const
			{
				result.clear();
				if (tile.Index >= _data.TileCount) {
					return;
				}
				Mask mask;
				Transform(tile.Index, tile.Flags & 0x03, mask);
				if (IsEmpty(mask)) {
					return;
				}
				auto it = _byHash.find(Hash(mask));
				if (it == _byHash.end()) {
					return;
				}
				for (const TileRef& candidate : it->second) {
					Mask other;
					Transform(candidate.Index, candidate.Flags, other);
					if (std::memcmp(mask.Bits, other.Bits, sizeof(mask.Bits)) == 0) {
						result.push_back(candidate);
					}
				}
			}

		private:
			struct Mask
			{
				std::uint8_t Bits[128];
			};

			const LevelDocument& _document;
			const Formats::TileSetData& _data;
			std::unordered_map<std::uint64_t, SmallVector<TileRef, 1>> _byHash;

			void Transform(std::int32_t index, std::uint8_t flags, Mask& mask) const
			{
				// A tile of the default tile set has its index as its tile ID, the level may change its mask
				const Formats::TileMaskOverride* changed = (index <= UINT16_MAX ? SetTileOverrideCommand::FindMask(_document, std::uint16_t(index)) : nullptr);
				const std::uint8_t* bits = &_data.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
				std::memset(mask.Bits, 0, sizeof(mask.Bits));
				for (std::int32_t y = 0; y < 32; y++) {
					for (std::int32_t x = 0; x < 32; x++) {
						std::int32_t sx = ((flags & Formats::TileRecord::FlipX) != 0 ? 31 - x : x);
						std::int32_t sy = ((flags & Formats::TileRecord::FlipY) != 0 ? 31 - y : y);
						std::int32_t from = sy * 32 + sx, to = y * 32 + x;
						bool solid = (changed != nullptr ? changed->Mask[from] != 0 : TileMasks::GetBit(bits, from));
						if (solid) {
							TileMasks::SetBit(mask.Bits, to);
						}
					}
				}
			}

			static bool IsEmpty(const Mask& mask)
			{
				for (std::uint8_t b : mask.Bits) {
					if (b != 0) {
						return false;
					}
				}
				return true;
			}

			static std::uint64_t Hash(const Mask& mask)
			{
				return Death::Cryptography::xxHash3(mask.Bits, sizeof(mask.Bits));
			}
		};
	}

	bool FillSelectionWithGround(IEditorContext& session)
	{
		SmartTileModel* model;
		std::int32_t layerIndex;
		const DocumentLayer* layer = Prepare(session, model, layerIndex);
		if (layer == nullptr) {
			return false;
		}
		const TileSelection& selection = session.GetSelection();
		const Recti& bounds = selection.GetBounds();
		LayerTiles tiles(*layer, bounds);

		// All of it solid first, so each cell knows its neighbors, without a tile until it's chosen
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y) && !IsSolidTile(tiles.Get(x, y))) {
					tiles.SetUnknownSolid(x, y);
				}
			}
		}
		// Then the tiles from the left and from the top
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				TileRef picked;
				if (selection.Contains(x, y) && tiles.Pick(*model, x, y, nullptr, picked)) {
					tiles.Set(x, y, picked);
				}
			}
		}
		// A cell no tile was found for keeps what it had, it isn't ground of any kind
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (tiles.IsUnknown(x, y)) {
					tiles.Revert(x, y);
				}
			}
		}
		// And those around that don't fit anymore, with the tiles of the selection as they're now
		const Recti& area = tiles.Area;
		for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
			for (std::int32_t x = area.X; x < area.X + area.W; x++) {
				if (selection.Contains(x, y)) {
					continue;
				}
				TileRef current = tiles.Get(x, y);
				TileRef picked;
				if (SmartTileModel::IsModelTile(current) && !model->Fits(tiles.GetNeighbors(x, y), layer->IsSpriteLayer(), current) &&
					tiles.Pick(*model, x, y, &current, picked)) {
					tiles.Set(x, y, picked);
				}
			}
		}
		// TRANSLATORS: Name of an undo step in the history, after the selected tiles were filled with ground fitting the tiles around
		if (!Commit(session, layerIndex, tiles, _x("editor", "Fill with ground"))) {
			session.ShowMessage(_x("editor", "The selection is ground already"));
			return false;
		}
		return true;
	}

	bool BeautifySelection(IEditorContext& session)
	{
		SmartTileModel* model;
		std::int32_t layerIndex;
		const DocumentLayer* layer = Prepare(session, model, layerIndex);
		if (layer == nullptr) {
			return false;
		}
		const Formats::TileSetData* data = session.GetTileSetData(session.GetDocument().Properties.DefaultTileSet);
		if (data == nullptr) {
			session.ShowMessage(_x("editor", "The tile set can't be read"));
			return false;
		}
		MaskIndex masks(session.GetDocument(), *data);
		const TileSelection& selection = session.GetSelection();
		const Recti& bounds = selection.GetBounds();
		bool spriteLayer = layer->IsSpriteLayer();
		LayerTiles tiles(*layer, bounds);

		// The tiles each selected cell can become, the most fitting of those with its mask, by the cells of the bounds,
		// one after another in a single list
		constexpr std::size_t MaxCandidates = 16;
		struct CellCandidates
		{
			std::uint32_t First = 0;
			std::uint32_t Count = 0;
		};
		SmallVector<CellCandidates, 0> cellCandidates;
		cellCandidates.resize(std::size_t(bounds.W) * std::size_t(bounds.H));
		SmallVector<TileRef, 0> allCandidates;
		auto key = [&bounds](std::int32_t x, std::int32_t y) { return std::size_t(y - bounds.Y) * std::size_t(bounds.W) + std::size_t(x - bounds.X); };
		auto hasCandidates = [&](std::int32_t x, std::int32_t y) {
			return (x >= bounds.X && y >= bounds.Y && x < bounds.X + bounds.W && y < bounds.Y + bounds.H && cellCandidates[key(x, y)].Count > 0);
		};
		auto candidatesOf = [&](std::int32_t x, std::int32_t y) {
			const CellCandidates& cell = cellCandidates[key(x, y)];
			return ArrayView<const TileRef>(allCandidates.data() + cell.First, cell.Count);
		};
		SmallVector<TileRef, 0> found;
		bool any = false;
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				TileRef tile = layer->GetTile(x, y);
				if (!selection.Contains(x, y) || !SmartTileModel::IsModelTile(tile)) {
					continue;
				}
				masks.Find(TileRef{ tile.Index, tile.Source, std::uint8_t(tile.Flags & 0x03) }, found);
				if (found.size() < 2) {
					continue;
				}
				std::uint8_t neighbors = tiles.GetNeighbors(x, y);
				std::sort(found.begin(), found.end(), [&](const TileRef& a, const TileRef& b) {
					return model->GetCount(neighbors, spriteLayer, a) > model->GetCount(neighbors, spriteLayer, b);
				});
				CellCandidates& cell = cellCandidates[key(x, y)];
				cell.First = std::uint32_t(allCandidates.size());
				bool hasCurrent = false;
				for (std::size_t i = 0; i < found.size() && i < MaxCandidates; i++) {
					// The modifier of the tile stays, translucent or not
					TileRef candidate{ found[i].Index, found[i].Source, std::uint8_t((tile.Flags & ~0x03) | found[i].Flags) };
					allCandidates.push_back(candidate);
					hasCurrent |= (candidate == tile);
				}
				// The tile there is one of them in any case
				if (!hasCurrent) {
					allCandidates.push_back(tile);
				}
				cell.Count = std::uint32_t(allCandidates.size()) - cell.First;
				any = true;
			}
		}
		if (!any) {
			session.ShowMessage(_x("editor", "No other tile of the tile set has the masks of the selected ones"));
			return false;
		}

		auto log1 = [](std::uint32_t count) { return std::log(1.0f + float(count)); };
		// How well a tile fits a cell by itself and with the tiles above and below, a little for being the one there
		auto unary = [&](std::int32_t x, std::int32_t y, const TileRef& tile, bool withSides) {
			float score = log1(model->GetCount(tiles.GetNeighbors(x, y), spriteLayer, tile));
			TileRef other;
			if (tiles.GetModelTile(x, y - 1, other)) {
				score += 0.6f * log1(model->GetDownPair(other, tile));
			}
			if (tiles.GetModelTile(x, y + 1, other)) {
				score += 0.6f * log1(model->GetDownPair(tile, other));
			}
			if (withSides) {
				if (tiles.GetModelTile(x - 1, y, other)) {
					score += 0.6f * log1(model->GetRightPair(other, tile));
				}
				if (tiles.GetModelTile(x + 1, y, other)) {
					score += 0.6f * log1(model->GetRightPair(tile, other));
				}
			}
			if (tile == layer->GetTile(x, y)) {
				score += 0.3f;
			}
			return score;
		};

		// Each run of cells in a row as a whole, the best path through their candidates, from the top
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			std::int32_t x = bounds.X;
			while (x < bounds.X + bounds.W) {
				if (!hasCandidates(x, y)) {
					x++;
					continue;
				}
				std::int32_t start = x;
				while (x < bounds.X + bounds.W && hasCandidates(x, y)) {
					x++;
				}
				std::int32_t end = x;

				// Scores of the best paths ending in each candidate, and where they came from
				SmallVector<SmallVector<float, 0>, 0> scores;
				SmallVector<SmallVector<std::int32_t, 0>, 0> from;
				for (std::int32_t cx = start; cx < end; cx++) {
					ArrayView<const TileRef> list = candidatesOf(cx, y);
					auto& cellScores = scores.emplace_back();
					auto& cellFrom = from.emplace_back();
					for (const TileRef& tile : list) {
						float score = unary(cx, y, tile, false);
						std::int32_t best = -1;
						if (cx == start) {
							TileRef left;
							if (tiles.GetModelTile(cx - 1, y, left)) {
								score += 0.6f * log1(model->GetRightPair(left, tile));
							}
						} else {
							ArrayView<const TileRef> previous = candidatesOf(cx - 1, y);
							const auto& previousScores = scores[std::size_t(cx - 1 - start)];
							float bestPrevious = -1e30f;
							for (std::size_t p = 0; p < previous.size(); p++) {
								float value = previousScores[p] + 0.6f * log1(model->GetRightPair(previous[p], tile));
								if (value > bestPrevious) {
									bestPrevious = value;
									best = std::int32_t(p);
								}
							}
							score += bestPrevious;
						}
						if (cx == end - 1) {
							TileRef right;
							if (tiles.GetModelTile(cx + 1, y, right)) {
								score += 0.6f * log1(model->GetRightPair(tile, right));
							}
						}
						cellScores.push_back(score);
						cellFrom.push_back(best);
					}
				}
				// Back from the best end
				const auto& lastScores = scores.back();
				std::int32_t index = std::int32_t(std::max_element(lastScores.begin(), lastScores.end()) - lastScores.begin());
				for (std::int32_t cx = end - 1; cx >= start && index >= 0; cx--) {
					tiles.Set(cx, y, candidatesOf(cx, y)[std::size_t(index)]);
					index = from[std::size_t(cx - start)][std::size_t(index)];
				}
			}
		}

		// Then each cell again, with all of its neighbors as they're chosen now
		for (std::int32_t sweep = 0; sweep < 2; sweep++) {
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
				for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
					if (!hasCandidates(x, y)) {
						continue;
					}
					float bestScore = -1e30f;
					TileRef best = tiles.Get(x, y);
					for (const TileRef& tile : candidatesOf(x, y)) {
						float score = unary(x, y, tile, true);
						if (score > bestScore) {
							bestScore = score;
							best = tile;
						}
					}
					tiles.Set(x, y, best);
				}
			}
		}

		// TRANSLATORS: Name of an undo step in the history, after the selected tiles were replaced by tiles with the same masks that fit their neighbors better
		if (!Commit(session, layerIndex, tiles, _x("editor", "Beautify tiles"))) {
			session.ShowMessage(_x("editor", "The selected tiles fit their neighbors the best already"));
			return false;
		}
		return true;
	}
}

#endif
