#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Document/Commands.h"

namespace Jazz2::Editor
{
	/**
		@brief Base of the tools that paint tiles

		What is painted is shown right away, but it's a single change of the level only once the pointer is released.
		Only selected tiles are painted while something is selected on the layer.
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
		std::int32_t _strokeLayer;

		/** @brief Starts a stroke on the current layer, returns `false` if the layer can't be changed */
		bool BeginStroke(String name);
		/** @brief Paints a tile of the layer, given a cell of the view */
		void PaintCell(Vector2i cell, const TileRef& tile);
		/** @brief Paints a stamp at a cell of the view, repeated from an origin */
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
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

	private:
		bool _erase;
		Vector2i _lastCell;
		Vector2i _patternOrigin;

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
	};

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
