#include "TileStamp.h"

#if defined(WITH_EDITOR)

#include "ToolMath.h"

#include <algorithm>

namespace Jazz2::Editor
{
	namespace
	{
		using ToolMath::PositiveModulo;
	}

	TileStamp TileStamp::FromTile(const TileRef& tile)
	{
		TileStamp stamp;
		stamp.Width = 1;
		stamp.Height = 1;
		stamp.Tiles.push_back(tile);
		stamp.Present.push_back(true);
		return stamp;
	}

	TileStamp TileStamp::FromLayer(const DocumentLayer& layer, const Recti& area)
	{
		TileStamp stamp;
		stamp.Width = std::max(area.W, 1);
		stamp.Height = std::max(area.H, 1);
		stamp.Tiles.reserve(std::size_t(stamp.Width) * stamp.Height);
		stamp.Present.reserve(std::size_t(stamp.Width) * stamp.Height);
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				std::int32_t lx = area.X + x, ly = area.Y + y;
				if (layer.Contains(lx, ly)) {
					stamp.Tiles.push_back(layer.GetTile(lx, ly));
					stamp.Present.push_back(true);
				} else {
					stamp.Tiles.push_back(TileRef{});
					stamp.Present.push_back(false);
				}
			}
		}
		return stamp;
	}

	TileStamp TileStamp::FromTileSet(std::uint8_t source, std::int32_t columns, const Recti& area)
	{
		TileStamp stamp;
		stamp.Width = std::max(area.W, 1);
		stamp.Height = std::max(area.H, 1);
		stamp.Tiles.reserve(std::size_t(stamp.Width) * stamp.Height);
		stamp.Present.reserve(std::size_t(stamp.Width) * stamp.Height);
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				TileRef tile{};
				tile.Source = source;
				tile.Index = std::uint16_t((area.Y + y) * columns + area.X + x);
				stamp.Tiles.push_back(tile);
				stamp.Present.push_back(true);
			}
		}
		return stamp;
	}

	bool TileStamp::GetPatternTile(std::int32_t x, std::int32_t y, Vector2i origin, TileRef& tile) const
	{
		if (IsEmpty()) {
			return false;
		}
		if (Random) {
			// The same choice for the same position, as varied as a hash
			std::uint32_t h = std::uint32_t(x) * 0x8DA6B343u ^ std::uint32_t(y) * 0xD8163841u ^ Seed * 0xCB1AB31Fu;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			std::size_t count = std::min<std::size_t>(Tiles.size(), Present.size());
			if (count == 0) {
				return false;
			}
			std::size_t index = std::size_t(h % std::uint32_t(count));
			if (Weights.size() == count) {
				// A tile with a larger weight takes a larger part of the range the hash falls in
				float total = 0.0f;
				for (float weight : Weights) {
					total += std::max(weight, 0.0f);
				}
				if (total <= 0.0f) {
					return false;
				}
				float point = float(h >> 8) / 16777216.0f * total;
				index = count - 1;
				for (std::size_t i = 0; i < count; i++) {
					point -= std::max(Weights[i], 0.0f);
					if (point < 0.0f) {
						index = i;
						break;
					}
				}
			}
			if (!Present[index]) {
				return false;
			}
			tile = Tiles[index];
			return true;
		}
		std::int32_t sx = PositiveModulo(x - origin.X, Width);
		std::int32_t sy = PositiveModulo(y - origin.Y, Height);
		if (!IsPresent(sx, sy)) {
			return false;
		}
		tile = GetTile(sx, sy);
		return true;
	}

	void TileStamp::FlipX()
	{
		for (std::int32_t y = 0; y < Height; y++) {
			for (std::int32_t x = 0; x < Width / 2; x++) {
				std::size_t a = std::size_t(y) * Width + x;
				std::size_t b = std::size_t(y) * Width + (Width - 1 - x);
				std::swap(Tiles[a], Tiles[b]);
				bool present = Present[a];
				Present[a] = Present[b];
				Present[b] = present;
				if (Weights.size() == Tiles.size()) {
					std::swap(Weights[a], Weights[b]);
				}
			}
		}
		for (TileRef& tile : Tiles) {
			if (!tile.IsEmpty()) {
				tile.Flags ^= Formats::TileRecord::FlipX;
			}
		}
	}

	void TileStamp::FlipY()
	{
		for (std::int32_t y = 0; y < Height / 2; y++) {
			for (std::int32_t x = 0; x < Width; x++) {
				std::size_t a = std::size_t(y) * Width + x;
				std::size_t b = std::size_t(Height - 1 - y) * Width + x;
				std::swap(Tiles[a], Tiles[b]);
				bool present = Present[a];
				Present[a] = Present[b];
				Present[b] = present;
				if (Weights.size() == Tiles.size()) {
					std::swap(Weights[a], Weights[b]);
				}
			}
		}
		for (TileRef& tile : Tiles) {
			if (!tile.IsEmpty()) {
				tile.Flags ^= Formats::TileRecord::FlipY;
			}
		}
	}

	void TileStamp::Rotate(bool clockwise)
	{
		if (IsEmpty() || Tiles.size() != std::size_t(Width) * std::size_t(Height) || Present.size() != Tiles.size()) {
			return;
		}
		bool hasWeights = (Weights.size() == Tiles.size());
		SmallVector<TileRef, 0> tiles;
		SmallVector<bool, 0> present;
		SmallVector<float, 0> weights;
		tiles.resize(Tiles.size());
		present.resize(Present.size());
		if (hasWeights) {
			weights.resize(Weights.size());
		}
		// The new stamp is as wide as the old one was high
		std::int32_t newWidth = Height, newHeight = Width;
		for (std::int32_t y = 0; y < Height; y++) {
			for (std::int32_t x = 0; x < Width; x++) {
				std::int32_t nx = (clockwise ? Height - 1 - y : y);
				std::int32_t ny = (clockwise ? x : Width - 1 - x);
				std::size_t from = std::size_t(y) * Width + x, to = std::size_t(ny) * newWidth + nx;
				tiles[to] = Tiles[from];
				present[to] = Present[from];
				if (hasWeights) {
					weights[to] = Weights[from];
				}
			}
		}
		Width = newWidth;
		Height = newHeight;
		Tiles = Death::move(tiles);
		Present = Death::move(present);
		if (hasWeights) {
			Weights = Death::move(weights);
		}
	}

	bool TileSelection::Contains(std::int32_t x, std::int32_t y) const
	{
		if (IsEmpty() || x < _bounds.X || y < _bounds.Y || x >= _bounds.X + _bounds.W || y >= _bounds.Y + _bounds.H) {
			return false;
		}
		return _mask[std::size_t(y) * _layerSize.X + x];
	}

	void TileSelection::Clear()
	{
		Touch();
		_layer = -1;
		_layerSize = Vector2i();
		_bounds = Recti();
		_mask = BitArray();
	}

	void TileSelection::Select(std::int32_t layer, Vector2i layerSize, const Recti& area, bool add)
	{
		Touch();
		if (!add || layer != _layer || layerSize != _layerSize) {
			Reset(layer, layerSize);
		}

		std::int32_t x1 = std::max(area.X, 0), y1 = std::max(area.Y, 0);
		std::int32_t x2 = std::min(area.X + area.W, layerSize.X), y2 = std::min(area.Y + area.H, layerSize.Y);
		for (std::int32_t y = y1; y < y2; y++) {
			for (std::int32_t x = x1; x < x2; x++) {
				_mask.set(std::size_t(y) * layerSize.X + x);
			}
		}
		// The whole rectangle is selected, so the bounds only grow by it
		if (x2 > x1 && y2 > y1) {
			IncludeInBounds(Recti(x1, y1, x2 - x1, y2 - y1));
		}
	}

	void TileSelection::Select(std::int32_t layer, Vector2i layerSize, const BitArray& mask, bool add)
	{
		Touch();
		if (!add || layer != _layer || layerSize != _layerSize) {
			Reset(layer, layerSize);
		}

		// The bounds of what's added are found while it's added, the mask is gone through once
		std::size_t count = std::min(mask.size(), _mask.size());
		std::int32_t width = std::max(_layerSize.X, 1);
		std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
		for (std::size_t i = 0; i < count; i++) {
			if (mask[i]) {
				_mask.set(i);
				std::int32_t x = std::int32_t(i % std::size_t(width)), y = std::int32_t(i / std::size_t(width));
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
		if (maxX >= minX) {
			IncludeInBounds(Recti(minX, minY, maxX - minX + 1, maxY - minY + 1));
		}
	}

	void TileSelection::Invert(std::int32_t layer, Vector2i layerSize)
	{
		Touch();
		BitArray previous;
		bool same = (layer == _layer && layerSize == _layerSize);
		if (same) {
			previous = Death::move(_mask);
		}
		Reset(layer, layerSize);
		for (std::size_t i = 0; i < _mask.size(); i++) {
			if (!same || !previous[i]) {
				_mask.set(i);
			}
		}
		UpdateBounds(Recti(0, 0, layerSize.X, layerSize.Y));
	}

	void TileSelection::Grow(bool shrink)
	{
		if (IsEmpty()) {
			return;
		}
		Touch();
		// The 8 tiles around, so a rectangle stays a rectangle
		BitArray result(ValueInit, _mask.size());
		std::int32_t width = _layerSize.X, height = _layerSize.Y;
		std::int32_t x1 = std::max<std::int32_t>(_bounds.X - 1, 0), y1 = std::max<std::int32_t>(_bounds.Y - 1, 0);
		std::int32_t x2 = std::min<std::int32_t>(_bounds.X + _bounds.W, width - 1), y2 = std::min<std::int32_t>(_bounds.Y + _bounds.H, height - 1);
		for (std::int32_t y = y1; y <= y2; y++) {
			for (std::int32_t x = x1; x <= x2; x++) {
				bool selected = _mask[std::size_t(y) * width + x];
				if (selected != shrink) {
					// Selected when growing stays selected, not selected when shrinking stays so
					if (selected) {
						result.set(std::size_t(y) * width + x);
					}
					continue;
				}
				// Growing selects a tile next to a selected one, shrinking keeps only a tile with all around selected, the
				// outside of the layer isn't
				bool otherAround = false;
				for (std::int32_t dy = -1; dy <= 1 && !otherAround; dy++) {
					for (std::int32_t dx = -1; dx <= 1; dx++) {
						if (dx == 0 && dy == 0) {
							continue;
						}
						std::int32_t nx = x + dx, ny = y + dy;
						bool neighbor = (nx >= 0 && ny >= 0 && nx < width && ny < height && _mask[std::size_t(ny) * width + nx]);
						if (neighbor != selected) {
							otherAround = true;
							break;
						}
					}
				}
				if (otherAround != shrink) {
					result.set(std::size_t(y) * width + x);
				}
			}
		}
		_mask = Death::move(result);
		// Nothing outside of the tiles gone through is selected
		UpdateBounds(Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1));
	}

	void TileSelection::UpdateBounds(const Recti& area)
	{
		std::int32_t x1 = std::max(area.X, 0), y1 = std::max(area.Y, 0);
		std::int32_t x2 = std::min(area.X + area.W, _layerSize.X), y2 = std::min(area.Y + area.H, _layerSize.Y);
		std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
		for (std::int32_t y = y1; y < y2; y++) {
			for (std::int32_t x = x1; x < x2; x++) {
				if (_mask[std::size_t(y) * _layerSize.X + x]) {
					minX = std::min(minX, x);
					minY = std::min(minY, y);
					maxX = std::max(maxX, x);
					maxY = std::max(maxY, y);
				}
			}
		}
		_bounds = (maxX >= minX ? Recti(minX, minY, maxX - minX + 1, maxY - minY + 1) : Recti());
	}

	void TileSelection::IncludeInBounds(const Recti& area)
	{
		if (IsEmpty()) {
			_bounds = area;
			return;
		}
		std::int32_t x1 = std::min(_bounds.X, area.X), y1 = std::min(_bounds.Y, area.Y);
		std::int32_t x2 = std::max(_bounds.X + _bounds.W, area.X + area.W), y2 = std::max(_bounds.Y + _bounds.H, area.Y + area.H);
		_bounds = Recti(x1, y1, x2 - x1, y2 - y1);
	}

	void TileSelection::Reset(std::int32_t layer, Vector2i layerSize)
	{
		_layer = layer;
		_layerSize = layerSize;
		_mask = BitArray(ValueInit, std::size_t(std::max(layerSize.X, 0)) * std::size_t(std::max(layerSize.Y, 0)));
		_bounds = Recti();
	}

	void TileSelection::Touch()
	{
		// Only the main thread changes selections
		static std::uint32_t lastRevision = 0;
		lastRevision++;
		if (lastRevision == 0) {
			// Zero is what a selection that never changed has
			lastRevision++;
		}
		_revision = lastRevision;
	}
}

#endif
