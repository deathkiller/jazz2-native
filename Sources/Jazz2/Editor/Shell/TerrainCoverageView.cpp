#include "TerrainCoverageView.h"

#if defined(WITH_EDITOR)

#include "../Tools/EditorContext.h"
#include "../Common/ToolMath.h"
#include "../Smart/SmartTileModel.h"
#include "../UI/UiPainter.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t Columns = 8;
		constexpr float CellSize = 26.0f;
		// Smallest cell in units, a narrower dialog makes them smaller down to this
		constexpr float MinCellSize = 12.0f;
		constexpr std::int32_t LegendRows = 3;

		// Of a pattern with a rule of the terrain, with tiles learned from the levels only, and with neither
		const Colorf ColorByHand(0.3f, 0.75f, 0.35f, 0.55f);
		const Colorf ColorLearned(0.5f, 0.5f, 0.5f, 0.4f);
		const Colorf ColorMissing(0.95f, 0.3f, 0.25f, 0.6f);

		std::int32_t GetRows(std::int32_t patternCount)
		{
			return (patternCount + Columns - 1) / Columns;
		}
	}

	TerrainCoverageView::TerrainCoverageView(IEditorContext& session)
		: _session(session), _patternCount(0), _missing(0), _refresh(0.0f)
	{
		std::memset(_coverage, 0, sizeof(_coverage));
		SmallVector<std::uint8_t, 0> patterns;
		TerrainRules::GetPatterns(patterns);
		for (std::uint8_t pattern : patterns) {
			if (_patternCount < std::int32_t(sizeof(_patterns))) {
				_patterns[_patternCount++] = pattern;
			}
		}
	}

	Vector2f TerrainCoverageView::Measure(const UiContext& context, Vector2f available)
	{
		// Smaller where there isn't room for the cells, with the legend below them
		float cell = context.Px(CellSize);
		if (available.X > 0.0f && cell * Columns > available.X) {
			cell = std::max(std::floor(available.X / Columns), context.Px(MinCellSize));
		}
		float legend = context.Px(UiTheme::Spacing) + context.Px(UiTheme::RowHeight) * LegendRows;
		return Vector2f(cell * Columns, cell * GetRows(_patternCount) + legend);
	}

	void TerrainCoverageView::Update(float timeMult)
	{
		UiElement::Update(timeMult);
		_refresh -= timeMult;
		if (_refresh > 0.0f) {
			return;
		}
		_refresh = 60.0f;
		LevelDocument& document = _session.GetDocument();
		std::int32_t layer = _session.GetCurrentLayer();
		bool spriteLayer = (layer >= 0 && layer < std::int32_t(document.Layers.size()) && document.Layers[layer].IsSpriteLayer());
		_session.GetSmartTiles().GetCoverage(spriteLayer, _coverage);
		_missing = 0;
		for (std::int32_t i = 0; i < _patternCount; i++) {
			_missing += (_coverage[_patterns[i]] == 0 ? 1 : 0);
		}
	}

	void TerrainCoverageView::Draw(UiPainter& painter)
	{
		// Within the bounds, whatever the parent gives, a wider dialog doesn't make the cells taller than the room for them
		std::int32_t rows = GetRows(_patternCount);
		float rowHeight = painter.Px(UiTheme::RowHeight);
		float legendHeight = painter.Px(UiTheme::Spacing) + rowHeight * LegendRows;
		float cell = std::floor(std::min(Bounds.W / Columns, (Bounds.H - legendHeight) / std::max(rows, 1)));
		if (cell < 6.0f) {
			return;
		}
		float left = Bounds.X + std::floor((Bounds.W - cell * Columns) * 0.5f);
		float pad = std::max(1.0f, std::round(cell * 0.12f));
		float mini = std::floor((cell - pad * 2.0f) / 3.0f);
		for (std::int32_t i = 0; i < _patternCount; i++) {
			std::uint8_t pattern = _patterns[i];
			Rectf rect(left + (i % Columns) * cell, Bounds.Y + (i / Columns) * cell, cell, cell);
			const Colorf& background = (_coverage[pattern] == 2 ? ColorByHand : (_coverage[pattern] == 1 ? ColorLearned : ColorMissing));
			painter.FillRect(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f), background, UiLayer::Fill);
			// The cell in the middle is the one the tile is for, the 8 around are solid or not as the pattern says
			for (std::int32_t y = 0; y < 3; y++) {
				for (std::int32_t x = 0; x < 3; x++) {
					bool solid = true;
					if (x != 1 || y != 1) {
						for (std::int32_t bit = 0; bit < 8; bit++) {
							if (ToolMath::NeighborOffsets[bit][0] == x - 1 && ToolMath::NeighborOffsets[bit][1] == y - 1) {
								solid = ((pattern >> bit) & 1) != 0;
								break;
							}
						}
					}
					Rectf square(rect.X + pad + x * mini, rect.Y + pad + y * mini, mini - 1.0f, mini - 1.0f);
					Colorf color = (x == 1 && y == 1 ? Colorf(1.0f, 1.0f, 1.0f, 0.95f) : (solid ? Colorf(0.85f, 0.85f, 0.85f, 0.85f) : Colorf(0.0f, 0.0f, 0.0f, 0.35f)));
					painter.FillRect(square, color, UiLayer::Content);
				}
			}
		}

		// What the colors mean, a row each, so it's never wider than the cells
		const Colorf* legendColors[LegendRows] = { &ColorByHand, &ColorLearned, &ColorMissing };
		// TRANSLATORS: Legend of the colors in the overview of the terrain rules, where the tile of each pattern of neighbors comes from
		StringView legendTexts[LegendRows] = { _x("editor", "Chosen by hand"), _x("editor", "Learned from the levels"), _x("editor", "No tile, a rule is needed") };
		float y = Bounds.Y + cell * rows + painter.Px(UiTheme::Spacing);
		float swatch = std::max(4.0f, rowHeight - painter.Px(4.0f));
		for (std::int32_t i = 0; i < LegendRows; i++) {
			Rectf box(left, std::round(y + (rowHeight - swatch) * 0.5f), swatch, swatch);
			painter.FillRect(box, *legendColors[i], UiLayer::Fill);
			float textX = box.X + swatch + painter.Px(UiTheme::Spacing);
			painter.TextInRect(legendTexts[i], Rectf(textX, y, std::max(0.0f, Bounds.X + Bounds.W - textX), rowHeight), UiTheme::TextDim);
			y += rowHeight;
		}
	}
}

#endif
