#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Document/Commands.h"

namespace Jazz2::Editor
{
	/**
		@brief Base of the tools that paint tiles

		What is painted is shown right away, but it's a single change of the level only once the pointer is released.
		Only selected tiles are painted while something is selected on the layer. A stamp with events (see
		@ref TileStamp::Events) places them with its tiles on the sprite layer, in the same step.
	*/
	class StrokeTool : public EditorTool
	{
	public:
		explicit StrokeTool(IToolHost& host);

		void OnDeactivate() override;
		void Cancel() override;
		bool IsBusy() const override;

	protected:
		std::unique_ptr<SetTilesCommand> _stroke;
		// The events of the stamp the stroke places with its tiles, made once the first of them is
		std::unique_ptr<SetEventsCommand> _eventStroke;
		// The events off the grid of the stamp the stroke places, added to those of the level once it's released
		SmallVector<Formats::OffGridEventRecord, 0> _pendingOffGrid;
		std::int32_t _strokeLayer;
		// How the layer of the stroke wraps, which doesn't change while the view moves
		LayerView _strokeView;
		// A stroke completed at once isn't shown first, the change of the level shows it
		bool _strokePreview;
		// Whether the options of the tools apply to the stroke, see ToolSettings::ResolveTile(), not when erasing
		bool _strokeOptions;
		// Whether the stroke paints only empty tiles
		bool _strokeOnlyEmpty;
		// Whether the stroke paints the empty tiles of a stamp whose empty tiles are transparent too, as with Shift
		bool _strokeEmptyToo;
		// Where PaintStamp() painted the stamp the last time in the stroke, the cells it covered aren't painted again
		const TileStamp* _lastStamp;
		Recti _lastStampArea;
		Vector2i _lastStampOrigin;

		/**
		 * @brief Starts a stroke on the current layer, returns `false` if the layer can't be changed
		 *
		 * A stroke without @p preview is completed right away, its tiles are only shown as a change of the level.
		 */
		bool BeginStroke(String name, bool preview = true);
		/** @brief Paints a tile of the layer, given a cell of the view, returns `false` if it's outside of the layer or the selection */
		bool PaintCell(Vector2i cell, const TileRef& tile);
		/** @brief Places an event on the sprite layer with the stroke, given a tile of the layer, see @ref TileStamp::Events */
		void PaintEvent(Vector2i layerTile, const Formats::EventRecord& event);
		/** @brief Places the events off the grid on a tile of a stamp with the stroke, the tile painted at a tile of the sprite layer, see @ref TileStamp::OffGridEvents */
		void PaintOffGrid(const TileStamp& stamp, std::size_t index, Vector2i layerTile);
		/** @brief Paints the tile a stamp repeated from an origin has at a cell of the view, with the options of the tools */
		void PaintStampCell(const TileStamp& stamp, Vector2i cell, Vector2i patternOrigin);
		/** @brief Paints a stamp at a cell of the view, repeated from an origin, the cells it covered the last time in the stroke are left as they are */
		void PaintStamp(const TileStamp& stamp, Vector2i cell, Vector2i anchor, Vector2i patternOrigin);
		/** @brief Completes the stroke as a change of the level */
		void EndStroke();
	};

	/**
		@brief Paints the stamp, or erases
	*/
	class BrushTool : public StrokeTool
	{
	public:
		BrushTool(IToolHost& host, bool erase);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;

	private:
		bool _erase;
		Vector2i _lastCell;
		Vector2i _patternOrigin;
		// A straight line dragged with Shift, painted when it's let go
		bool _lineMode;
		Vector2i _lineStart;
		Vector2i _lineEnd;

		const TileStamp& GetBrushStamp() const;
		Vector2i GetAnchor() const;
	};

	/**
		@brief Fills a rectangle with the stamp
	*/
	class RectangleTool : public StrokeTool
	{
	public:
		explicit RectangleTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;

	private:
		bool _dragging;
		bool _erase;
		Vector2i _start;
		Vector2i _end;
	};

	/**
		@brief Fills an area of the same tiles with the stamp
	*/
	class FillTool : public StrokeTool
	{
	public:
		explicit FillTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;
	};

	/**
		@brief Picks tiles of the layer as the stamp
	*/
	class EyedropperTool : public EditorTool
	{
	public:
		explicit EyedropperTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

	private:
		bool _dragging;
		Vector2i _start;
		Vector2i _end;
	};

	/**
		@brief Selects a rectangle of tiles
	*/
	class SelectTool : public EditorTool
	{
	public:
		explicit SelectTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

	private:
		bool _dragging;
		bool _add;
		Vector2i _start;
		Vector2i _end;
		// The selected tiles dragged to another place, and where they are in the view
		bool _moving;
		TileStamp _moveStamp;
		Vector2i _moveOrigin;
	};

	/** @brief Marks the tiles of a layer seen in a rectangle of the screen that are translucent or invisible */
	void DrawTileModifiers(UiPainter& painter, const DocumentLayer& layer, const LayerView& view, const Rectf& viewRect);

	/**
	 * @brief Draws an edge of a selection as marching ants, short dashes moving along a dark line, as a bitmap editor has them
	 *
	 * The edge goes right or down from @p x, @p y, as thick as a pixel of the interface. The dashes follow the diagonal
	 * of the screen, so they go on around corners, and move on with @p animTime, see @ref IToolHost::GetAnimTime().
	 */
	void DrawSelectionEdge(UiPainter& painter, float x, float y, float length, bool horizontal, float animTime);

	/**
	 * @brief Splits a rectangle of cells of the view into rectangles of a layer, returns how many there are, at most 4
	 *
	 * A rectangle dragged over a layer that repeats is of the copy of the layer it was dragged over, while a selection
	 * is of the layer itself, and so of every copy. On an axis the layer repeats in, the rectangle is wrapped into the
	 * layer --- into two parts if it goes over the end of a copy into the next one, all of it if it's as long as the
	 * layer. On the other axes it's as it is, what's outside of the layer is left to the one who takes it.
	 */
	std::int32_t WrapViewRect(const Recti& rect, Vector2i layerSize, const LayerView& view, Recti (&parts)[4]);

	/**
	 * @brief Moves the selected tiles of the layer being edited by an offset, as one step, the selection with them
	 *
	 * The tiles left behind become empty. Around a layer that repeats they wrap, otherwise nothing is moved if any of
	 * them would be outside of the layer. On the sprite layer the events on them go with them, see
	 * @ref CarrySelectedEvents(). Returns `false` if nothing was moved.
	 */
	bool MoveSelectedTiles(IToolHost& host, Vector2i offset);

	/**
	 * @brief Moves the events on the selected tiles of the sprite layer where the tiles go, as moving or mirroring them does
	 *
	 * @p mapCell returns the tile a selected one goes to, @p mapPixel the position in pixels a position on one goes to,
	 * for the events off the grid, which stay within the level. @p transform changes each event moved, as mirroring
	 * turns it, or is empty. Events at the tiles they go to are replaced, as the tiles there are. To be called between
	 * @ref IToolHost::BeginChanges() and @ref IToolHost::EndChanges() with the change of the tiles, before the
	 * selection moves. Returns `false` if there were no events to move or the selection isn't of the sprite layer.
	 */
	bool CarrySelectedEvents(IToolHost& host, const TileSelection& selection, const String& name, Function<Vector2i(Vector2i)>&& mapCell,
		Function<Vector2i(Vector2i)>&& mapPixel, Function<void(Formats::EventRecord&)>&& transform);

	/**
		@brief Selects an area of the same tiles
	*/
	class WandTool : public EditorTool
	{
	public:
		explicit WandTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;
	};

	/**
		@brief Moves the view
	*/
	class HandTool : public EditorTool
	{
	public:
		explicit HandTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		StringView GetHint() const override;

	private:
		bool _dragging;
	};
}

#endif
