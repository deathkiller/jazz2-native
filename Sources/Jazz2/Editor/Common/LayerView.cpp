#include "LayerView.h"

#if defined(WITH_EDITOR)

#include "ToolMath.h"

#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;

		using ToolMath::PositiveModulo;
	}

	Vector2f LayerView::ScreenToWorld(Vector2f screen) const
	{
		return (screen - ScreenOffset + ViewOrigin) / Scale;
	}

	Vector2f LayerView::WorldToScreen(Vector2f world) const
	{
		return world * Scale - ViewOrigin + ScreenOffset;
	}

	Vector2i LayerView::WorldToCell(Vector2f world) const
	{
		return Vector2i(std::int32_t(std::floor((world.X - Origin.X) / TileSize)), std::int32_t(std::floor((world.Y - Origin.Y) / TileSize)));
	}

	Rectf LayerView::CellToScreen(Vector2i cell) const
	{
		Vector2f topLeft = WorldToScreen(Vector2f(Origin.X + cell.X * TileSize, Origin.Y + cell.Y * TileSize));
		Vector2f bottomRight = WorldToScreen(Vector2f(Origin.X + (cell.X + 1) * TileSize, Origin.Y + (cell.Y + 1) * TileSize));
		return Rectf(std::round(topLeft.X), std::round(topLeft.Y), std::round(bottomRight.X) - std::round(topLeft.X), std::round(bottomRight.Y) - std::round(topLeft.Y));
	}

	bool LayerView::Wrap(Vector2i cell, Vector2i& tile) const
	{
		if (Size.X <= 0 || Size.Y <= 0) {
			return false;
		}
		if (RepeatX) {
			tile.X = PositiveModulo(cell.X, Size.X);
		} else if (cell.X < 0 || cell.X >= Size.X) {
			return false;
		} else {
			tile.X = cell.X;
		}
		if (RepeatY) {
			tile.Y = PositiveModulo(cell.Y, Size.Y);
		} else if (cell.Y < 0 || cell.Y >= Size.Y) {
			return false;
		} else {
			tile.Y = cell.Y;
		}
		return true;
	}
}

#endif
