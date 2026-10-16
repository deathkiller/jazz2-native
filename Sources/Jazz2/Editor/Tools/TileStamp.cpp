#include "TileStamp.h"

#if defined(WITH_EDITOR)

#include <algorithm>

namespace Jazz2::Editor
{
	namespace
	{
		std::int32_t PositiveModulo(std::int32_t value, std::int32_t divisor)
		{
			std::int32_t result = value % divisor;
			return (result < 0 ? result + divisor : result);
		}
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
			std::size_t index = std::size_t(h % std::uint32_t(Width * Height));
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
			}
		}
		for (TileRef& tile : Tiles) {
			if (!tile.IsEmpty()) {
				tile.Flags ^= Formats::TileRecord::FlipY;
			}
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
		_layer = -1;
		_layerSize = Vector2i();
		_bounds = Recti();
		_mask = BitArray();
	}

	void TileSelection::Select(std::int32_t layer, Vector2i layerSize, const Recti& area, bool add)
	{
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
		UpdateBounds();
	}

	void TileSelection::Select(std::int32_t layer, Vector2i layerSize, const BitArray& mask, bool add)
	{
		if (!add || layer != _layer || layerSize != _layerSize) {
			Reset(layer, layerSize);
		}

		std::size_t count = std::min(mask.size(), _mask.size());
		for (std::size_t i = 0; i < count; i++) {
			if (mask[i]) {
				_mask.set(i);
			}
		}
		UpdateBounds();
	}

	void TileSelection::UpdateBounds()
	{
		std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
		for (std::int32_t y = 0; y < _layerSize.Y; y++) {
			for (std::int32_t x = 0; x < _layerSize.X; x++) {
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

	void TileSelection::Reset(std::int32_t layer, Vector2i layerSize)
	{
		_layer = layer;
		_layerSize = layerSize;
		_mask = BitArray(ValueInit, std::size_t(std::max(layerSize.X, 0)) * std::size_t(std::max(layerSize.Y, 0)));
		_bounds = Recti();
	}
}

#endif
