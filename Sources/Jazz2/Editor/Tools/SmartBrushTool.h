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
		// A tile a click would paint, shown before it, by the cell of the view
		struct GhostTile
		{
			Vector2i Cell;
			TileRef Tile;
		};

		bool _erase;
		Vector2i _lastCell;
		// Tiles of the layer set by the stroke, by their position in the layer
		std::unordered_map<std::uint64_t, TileRef> _pending;
		mutable String _hint;
		// The layer painted and how it wraps, of the stroke or of what a click would paint
		std::int32_t _paintLayer;
		LayerView _paintView;
		// Everything what a click would paint depends on, the tiles around the cell, which of them can change and what the
		// model knows, see DrawOverlay()
		static constexpr std::int32_t KeyRadius = 2;
		static constexpr std::int32_t KeyTiles = (KeyRadius * 2 + 1) * (KeyRadius * 2 + 1);
		struct GhostKey
		{
			Vector2i Cell;
			std::int32_t Layer = -1;
			Vector2i Size;
			bool RepeatX = false;
			bool RepeatY = false;
			std::uint64_t ModelRevision = 0;
			// A bit for each of the tiles around that can be changed, by their index in Tiles
			std::uint32_t Changeable = 0;
			TileRef Tiles[KeyTiles];

			bool operator==(const GhostKey& other) const;
		};

		// What a click would paint is worked out without changing the level, see DrawOverlay()
		bool _simulating;
		SmallVector<GhostTile, 0> _ghost;
		// What the ghost was worked out for
		GhostKey _ghostKey;
		bool _ghostValid;

		void MakeGhostKey(std::int32_t layerIndex, const LayerView& view, Vector2i cell, std::uint64_t modelRevision, GhostKey& key) const;
		TileRef GetTile(const DocumentLayer& layer, Vector2i tile) const;
		bool IsSolid(const DocumentLayer& layer, const LayerView& view, Vector2i cell) const;
		void SetTile(Vector2i tile, Vector2i cell, const TileRef& value);
		void PaintAt(Vector2i cell);
	};
}

#endif
