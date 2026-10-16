#include "SmartFill.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Smart/SmartTileModel.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };

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
		const DocumentLayer* Prepare(EditorSession& session, SmartTileModel*& model, std::int32_t& layerIndex)
		{
			const TileSelection& selection = session.GetSelection();
			if (selection.IsEmpty()) {
				session.ShowMessage(_("Select the tiles first"));
				return nullptr;
			}
			model = &session.GetSmartTiles();
			if (!model->IsReady()) {
				session.ShowMessage(_f("Still learning how the tiles fit - {}%, try it again in a moment", std::int32_t(model->GetProgress() * 100.0f)));
				return nullptr;
			}
			if (model->GetLevelCount() == 0 && model->GetRules().GetCount() == 0) {
				session.ShowMessage(_("No level uses this tile set yet, so there's nothing to learn the tiles from"));
				return nullptr;
			}
			LevelDocument& document = session.GetDocument();
			layerIndex = selection.GetLayer();
			if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || !session.IsLayerEditable(layerIndex)) {
				session.ShowMessage(_("The layer is locked or hidden"));
				return nullptr;
			}
			return &document.Layers[layerIndex];
		}

		// The tiles of a layer with some of them changed
		struct LayerTiles
		{
			const DocumentLayer& Layer;
			std::unordered_map<std::uint32_t, TileRef> Changed;

			explicit LayerTiles(const DocumentLayer& layer)
				: Layer(layer)
			{
			}

			bool Contains(std::int32_t x, std::int32_t y) const
			{
				return (x >= 0 && y >= 0 && x < Layer.GetWidth() && y < Layer.GetHeight());
			}

			TileRef Get(std::int32_t x, std::int32_t y) const
			{
				auto it = Changed.find(std::uint32_t(y) * std::uint32_t(Layer.GetWidth()) + std::uint32_t(x));
				return (it != Changed.end() ? it->second : Layer.GetTile(x, y));
			}

			void Set(std::int32_t x, std::int32_t y, const TileRef& tile)
			{
				Changed[std::uint32_t(y) * std::uint32_t(Layer.GetWidth()) + std::uint32_t(x)] = tile;
			}

			// Outside of the layer counts as solid, as the levels were learned
			bool IsSolid(std::int32_t x, std::int32_t y) const
			{
				return (!Contains(x, y) || IsSolidTile(Get(x, y)));
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

			// A model tile next to a cell, `false` if there's none
			bool GetModelTile(std::int32_t x, std::int32_t y, TileRef& tile) const
			{
				if (!Contains(x, y)) {
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

		bool Commit(EditorSession& session, std::int32_t layerIndex, const LayerTiles& tiles, StringView name)
		{
			auto command = std::make_unique<SetTilesCommand>(session.GetDocument(), layerIndex, String(name));
			for (const auto& [cell, tile] : tiles.Changed) {
				std::int32_t width = tiles.Layer.GetWidth();
				command->Set(std::int32_t(cell % std::uint32_t(width)), std::int32_t(cell / std::uint32_t(width)), tile);
			}
			command->Seal();
			if (command->IsEmpty()) {
				return false;
			}
			session.Execute(std::move(command));
			return true;
		}

		// Collision masks of the tiles of a tile set, flipped every way, to find tiles of the same mask
		class MaskIndex
		{
		public:
			explicit MaskIndex(const Formats::TileSetData& data)
				: _data(data)
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

			const Formats::TileSetData& _data;
			std::unordered_map<std::uint64_t, SmallVector<TileRef, 1>> _byHash;

			void Transform(std::int32_t index, std::uint8_t flags, Mask& mask) const
			{
				const std::uint8_t* bits = &_data.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
				std::memset(mask.Bits, 0, sizeof(mask.Bits));
				for (std::int32_t y = 0; y < 32; y++) {
					for (std::int32_t x = 0; x < 32; x++) {
						std::int32_t sx = ((flags & Formats::TileRecord::FlipX) != 0 ? 31 - x : x);
						std::int32_t sy = ((flags & Formats::TileRecord::FlipY) != 0 ? 31 - y : y);
						std::int32_t from = sy * 32 + sx, to = y * 32 + x;
						if (((bits[from / 8] >> (from % 8)) & 1) != 0) {
							mask.Bits[to / 8] |= std::uint8_t(1 << (to % 8));
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
				std::uint64_t hash = 14695981039346656037ull;
				for (std::uint8_t b : mask.Bits) {
					hash = (hash ^ b) * 1099511628211ull;
				}
				return hash;
			}
		};
	}

	bool FillSelectionWithGround(EditorSession& session)
	{
		SmartTileModel* model;
		std::int32_t layerIndex;
		const DocumentLayer* layer = Prepare(session, model, layerIndex);
		if (layer == nullptr) {
			return false;
		}
		const TileSelection& selection = session.GetSelection();
		const Recti& bounds = selection.GetBounds();
		LayerTiles tiles(*layer);

		// All of it solid first, so each cell knows its neighbors, any solid tile stands for them until they're chosen
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y) && !IsSolidTile(tiles.Get(x, y))) {
					tiles.Set(x, y, TileRef{ 1, TileRef::DefaultTileSet, 0 });
				}
			}
		}
		// Then the tiles from the left and from the top, those around too if they don't fit anymore
		for (std::int32_t y = bounds.Y - 1; y <= bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X - 1; x <= bounds.X + bounds.W; x++) {
				if (!tiles.Contains(x, y)) {
					continue;
				}
				TileRef current = tiles.Get(x, y);
				TileRef picked;
				if (selection.Contains(x, y)) {
					if (tiles.Pick(*model, x, y, nullptr, picked)) {
						tiles.Set(x, y, picked);
					}
				} else if (SmartTileModel::IsModelTile(current) && !model->Fits(tiles.GetNeighbors(x, y), layer->IsSpriteLayer(), current)) {
					if (tiles.Pick(*model, x, y, &current, picked)) {
						tiles.Set(x, y, picked);
					}
				}
			}
		}
		if (!Commit(session, layerIndex, tiles, _("Fill with ground"))) {
			session.ShowMessage(_("The selection is ground already"));
			return false;
		}
		return true;
	}

	bool BeautifySelection(EditorSession& session)
	{
		SmartTileModel* model;
		std::int32_t layerIndex;
		const DocumentLayer* layer = Prepare(session, model, layerIndex);
		if (layer == nullptr) {
			return false;
		}
		const Formats::TileSetData* data = session.GetTileSetData(session.GetDocument().Properties.DefaultTileSet);
		if (data == nullptr) {
			session.ShowMessage(_("The tile set can't be read"));
			return false;
		}
		MaskIndex masks(*data);
		const TileSelection& selection = session.GetSelection();
		const Recti& bounds = selection.GetBounds();
		bool spriteLayer = layer->IsSpriteLayer();
		LayerTiles tiles(*layer);

		// The tiles each selected cell can become, the most fitting of those with its mask
		constexpr std::size_t MaxCandidates = 16;
		std::unordered_map<std::uint32_t, SmallVector<TileRef, 0>> candidates;
		SmallVector<TileRef, 0> found;
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
				auto& list = candidates[std::uint32_t(y) * std::uint32_t(layer->GetWidth()) + std::uint32_t(x)];
				for (std::size_t i = 0; i < found.size() && i < MaxCandidates; i++) {
					// The modifier of the tile stays, translucent or not
					list.push_back(TileRef{ found[i].Index, found[i].Source, std::uint8_t((tile.Flags & ~0x03) | found[i].Flags) });
				}
				// The tile there is one of them in any case
				bool hasCurrent = std::any_of(list.begin(), list.end(), [&tile](const TileRef& t) { return t == tile; });
				if (!hasCurrent) {
					list.push_back(tile);
				}
			}
		}
		if (candidates.empty()) {
			session.ShowMessage(_("No other tile of the tile set has the masks of the selected ones"));
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
		auto key = [layer](std::int32_t x, std::int32_t y) { return std::uint32_t(y) * std::uint32_t(layer->GetWidth()) + std::uint32_t(x); };

		// Each run of cells in a row as a whole, the best path through their candidates, from the top
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			std::int32_t x = bounds.X;
			while (x < bounds.X + bounds.W) {
				if (candidates.find(key(x, y)) == candidates.end()) {
					x++;
					continue;
				}
				std::int32_t start = x;
				while (x < bounds.X + bounds.W && candidates.find(key(x, y)) != candidates.end()) {
					x++;
				}
				std::int32_t end = x;

				// Scores of the best paths ending in each candidate, and where they came from
				SmallVector<SmallVector<float, 0>, 0> scores;
				SmallVector<SmallVector<std::int32_t, 0>, 0> from;
				for (std::int32_t cx = start; cx < end; cx++) {
					const auto& list = candidates[key(cx, y)];
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
							const auto& previous = candidates[key(cx - 1, y)];
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
					tiles.Set(cx, y, candidates[key(cx, y)][std::size_t(index)]);
					index = from[std::size_t(cx - start)][std::size_t(index)];
				}
			}
		}

		// Then each cell again, with all of its neighbors as they're chosen now
		for (std::int32_t sweep = 0; sweep < 2; sweep++) {
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
				for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
					auto it = candidates.find(key(x, y));
					if (it == candidates.end()) {
						continue;
					}
					float bestScore = -1e30f;
					TileRef best = tiles.Get(x, y);
					for (const TileRef& tile : it->second) {
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

		if (!Commit(session, layerIndex, tiles, _("Beautify tiles"))) {
			session.ShowMessage(_("The selected tiles fit their neighbors the best already"));
			return false;
		}
		return true;
	}
}

#endif
