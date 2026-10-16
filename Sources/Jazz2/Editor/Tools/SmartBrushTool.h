#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TileTools.h"

#include <unordered_map>

namespace Jazz2::Editor
{
	/**
		@brief Paints ground, the tiles are chosen by the tiles around them

		The left button makes cells solid, the right one empty. Every painted cell and its neighbors get the tile the
		levels of the tile set use the most where the same neighbors are solid (see @ref SmartTileModel) --- so painting
		a block gets it its grass, edges and corners, and painting next to ground joins them. Neighbors with tiles of
		another tile set, animated ones, and those that are translucent or invisible are kept as they are.
	*/
	class SmartBrushTool : public StrokeTool
	{
	public:
		explicit SmartBrushTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnActivate() override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

	private:
		bool _erase;
		Vector2i _lastCell;
		// Tiles of the layer set by the stroke, by their position in the layer
		std::unordered_map<std::uint64_t, TileRef> _pending;
		mutable String _hint;

		TileRef GetTile(const DocumentLayer& layer, Vector2i tile) const;
		bool IsSolid(const DocumentLayer& layer, const LayerView& view, Vector2i cell) const;
		void SetTile(Vector2i tile, const TileRef& value);
		void PaintAt(Vector2i cell);
	};
}

#endif
