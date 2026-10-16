#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../UI/UiPainter.h"
#include "../../../nCine/Input/InputEvents.h"

#include <memory>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
	class EditorCamera;
	class EventPreviews;
	class SmartTileModel;
	class TileShapes;
	class UiSurface;
	class EventSelection;
	struct EventStamp;

	/**
		@brief How a layer is shown in the view

		Layers other than the sprite layer move at their own speed, so a position in the view lies on another tile of
		each layer. Cells here are counted from the tile (0, 0) of the layer as it's shown, without repeating ---
		@ref Wrap() finds the tile of the layer a cell shows.
	*/
	struct LayerView
	{
		/** @brief Position of the tile (0, 0) of the layer in the level */
		Vector2f Origin;
		/** @brief Size of the layer in tiles */
		Vector2i Size;
		/** @brief Whether the layer repeats horizontally */
		bool RepeatX = false;
		/** @brief Whether the layer repeats vertically */
		bool RepeatY = false;
		/** @brief Pixels of the screen one pixel of the level takes */
		float Scale = 1.0f;
		/** @brief Position of the top left corner of the view in pixels of the level multiplied by the scale */
		Vector2f ViewOrigin;
		/** @brief Position of the view on the screen */
		Vector2f ScreenOffset;

		/** @brief Converts a position on the screen to a position in the level */
		Vector2f ScreenToWorld(Vector2f screen) const;
		/** @brief Converts a position in the level to a position on the screen */
		Vector2f WorldToScreen(Vector2f world) const;
		/** @brief Returns the cell at a position in the level */
		Vector2i WorldToCell(Vector2f world) const;
		/** @brief Returns the rectangle a cell takes on the screen */
		Rectf CellToScreen(Vector2i cell) const;
		/** @brief Returns the tile of the layer a cell shows, `false` if it shows none */
		bool Wrap(Vector2i cell, Vector2i& tile) const;
	};

	/** @brief Pointer over the view, as a tool sees it */
	struct ToolPointer
	{
		/** @brief Position on the screen */
		Vector2f Screen;
		/** @brief Position in the level */
		Vector2f World;
		/** @brief Cell of the current layer, see @ref LayerView */
		Vector2i Cell;
		/** @brief Button pressed or released */
		MouseButton Button = MouseButton::Left;
		/** @brief Modifier keys held */
		std::int32_t Modifiers = 0;
	};

	/**
		@brief What tools need from the level editor
	*/
	class IToolHost
	{
	public:
		virtual ~IToolHost() = default;

		/** @brief Returns the level being edited */
		virtual LevelDocument& GetDocument() = 0;
		/** @brief Returns the layer being edited */
		virtual std::int32_t GetCurrentLayer() const = 0;
		/** @brief Returns `true` if a layer can be changed, it's not locked or hidden */
		virtual bool IsLayerEditable(std::int32_t layer) const = 0;
		/** @brief Returns how a layer is shown in the view */
		virtual LayerView GetLayerView(std::int32_t layer) = 0;
		/** @brief Returns the stamp of the brush */
		virtual TileStamp& GetStamp() = 0;
		/** @brief Sets the stamp of the brush */
		virtual void SetStamp(TileStamp&& stamp) = 0;
		/** @brief Returns the selected tiles */
		virtual TileSelection& GetSelection() = 0;
		/** @brief Executes an edit command */
		virtual void Execute(std::unique_ptr<EditCommand> command, bool allowMerge = false) = 0;
		/** @brief Shows a tile in the view before it's changed in the level */
		virtual void PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile) = 0;
		/** @brief Shows tiles of an area as the level has them, undoing a preview */
		virtual void RevertPreview(std::int32_t layer, const Recti& area) = 0;
		/** @brief Returns the shown level, or `nullptr` */
		virtual Tiles::TileMap* GetTileMap() = 0;
		/** @brief Returns the camera, or `nullptr` */
		virtual EditorCamera* GetCamera() = 0;
		/** @brief Shows a short message */
		virtual void ShowMessage(StringView text) = 0;
		/** @brief Returns the events the event tool places */
		virtual EventStamp& GetEventBrush() = 0;
		/** @brief Returns the selected events */
		virtual EventSelection& GetEventSelection() = 0;
		/** @brief Returns what draws events */
		virtual EventPreviews& GetEventPreviews() = 0;
		/** @brief Returns the time animations of the interface are at */
		virtual float GetAnimTime() const = 0;
		/** @brief Returns the surface of the interface, for a tool that shows a window of its options, or `nullptr` */
		virtual UiSurface* GetSurface() const = 0;
		/** @brief Returns what knows how the tiles of the default tile set of the level fit together, it starts learning it */
		virtual SmartTileModel& GetSmartTiles() = 0;
		/** @brief Returns the shapes of the masks of the default tile set of the level */
		virtual const TileShapes& GetTileShapes() = 0;
	};

	/** @brief Tool of the level editor */
	enum class EditorToolType : std::uint8_t
	{
		Brush,
		Eraser,
		Rectangle,
		Fill,
		Eyedropper,
		Select,
		Wand,
		Hand,
		Event,
		Measure,
		Path,
		Smart,
		Ground,
		TilePixels,

		Count
	};

	/**
		@brief Tool used in the view of the level editor
	*/
	class EditorTool
	{
	public:
		/** @brief Creates a new instance */
		explicit EditorTool(IToolHost& host);
		virtual ~EditorTool();

		/** @brief Returns the type */
		virtual EditorToolType GetType() const = 0;

		/** @brief Called when the tool is chosen */
		virtual void OnActivate();
		/** @brief Called when another tool is chosen, anything in progress is cancelled */
		virtual void OnDeactivate();
		/** @brief Called when the pointer is pressed over the view */
		virtual void OnPointerDown(const ToolPointer& pointer);
		/** @brief Called when the pointer moves over the view, pressed or not */
		virtual void OnPointerMove(const ToolPointer& pointer);
		/** @brief Called when the pointer is released */
		virtual void OnPointerUp(const ToolPointer& pointer);
		/** @brief Cancels what the tool is doing */
		virtual void Cancel();
		/** @brief Returns `true` if the tool is in the middle of something */
		virtual bool IsBusy() const;
		/** @brief Draws what the tool shows over the view */
		virtual void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid);
		/** @brief Returns a hint of how the tool is used */
		virtual StringView GetHint() const;

	protected:
		IToolHost& _host;

		/** @brief Draws the outline of cells of a layer on the screen */
		void DrawCellsOutline(UiPainter& painter, const LayerView& view, Vector2i from, Vector2i to, const Colorf& color);
		/** @brief Draws a stamp on the screen, its cell at the anchor at a cell of a layer */
		void DrawStampGhost(UiPainter& painter, const LayerView& view, const TileStamp& stamp, Vector2i cell, Vector2i anchor, float alpha);
	};

	/**
		@brief Keeps the tools and which of them is used

		A tool can be chosen for a while only, as the hand while Space is held, after which the one used before
		comes back.
	*/
	class ToolManager
	{
	public:
		/** @brief Creates all tools */
		explicit ToolManager(IToolHost& host);
		~ToolManager();

		/** @brief Returns the tool in use */
		EditorTool* GetActive() const;
		/** @brief Returns the type of the tool in use */
		EditorToolType GetActiveType() const {
			return _active;
		}
		/** @brief Returns the type of the tool chosen, not just for a while */
		EditorToolType GetChosenType() const {
			return _chosen;
		}
		/** @brief Chooses a tool */
		void SetActive(EditorToolType type);
		/** @brief Uses a tool for a while */
		void BeginTemporary(EditorToolType type);
		/** @brief Returns to the tool used before the one used for a while */
		void EndTemporary(EditorToolType type);
		/** @brief Returns a tool */
		EditorTool* Get(EditorToolType type) const;

	private:
		std::unique_ptr<EditorTool> _tools[std::int32_t(EditorToolType::Count)];
		EditorToolType _active;
		EditorToolType _chosen;
		bool _temporary;
	};
}

#endif
