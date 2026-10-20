#pragma once

#if (defined(WITH_EDITOR) && defined(DEATH_DEBUG)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Tools/EventTools.h"
#include "../Events/EventPreviews.h"
#include "../Smart/SmartTileModel.h"
#include "../Smart/TileShapes.h"

namespace Jazz2::Editor
{
	/**
		@brief Host of the tools of the level editor for tests, only over a document and its history

		Nothing is shown --- there's no shown level, camera nor interface, so previews of tiles and messages are only
		recorded. Every layer is in the view from its tile (0, 0) at the top left of the screen, a pixel of the level
		for a pixel of the screen, so a cell of any layer is at the same place, see @ref MakePointer(). Changes are
		executed as the editor executes them, the tile ID space grows with them, and the selections follow them.

		Only in debug builds, for @ref EditorSelfTest.
	*/
	class FakeToolHost : public IToolHost
	{
	public:
		/** @brief A tile shown before it's changed, see @ref PreviewTile() */
		struct PreviewedTile
		{
			/** @brief Index of the layer */
			std::int32_t Layer;
			/** @brief Position of the tile in the layer */
			Vector2i Position;
			/** @brief The tile shown */
			TileRef Tile;
		};

		/** @brief An area shown again as the level has it, see @ref RevertPreview() */
		struct RevertedArea
		{
			/** @brief Index of the layer */
			std::int32_t Layer;
			/** @brief The area */
			Recti Area;
		};

		/** @brief Tiles shown before they were changed, in the order they were */
		SmallVector<PreviewedTile, 0> Previews;
		/** @brief Areas shown again as the level has them, in the order they were */
		SmallVector<RevertedArea, 0> Reverts;
		/** @brief Messages shown, in the order they were */
		SmallVector<String, 0> Messages;

		/** @brief Creates a host of a document, the document has to live longer */
		explicit FakeToolHost(LevelDocument& document);
		~FakeToolHost() override;

		FakeToolHost(const FakeToolHost&) = delete;
		FakeToolHost& operator=(const FakeToolHost&) = delete;

		LevelDocument& GetDocument() override;
		std::int32_t GetCurrentLayer() const override;
		bool IsLayerEditable(std::int32_t layer) const override;
		LayerView GetLayerView(std::int32_t layer) override;
		TileStamp& GetStamp() override;
		void SetStamp(TileStamp&& stamp) override;
		TileSelection& GetSelection() override;
		void Execute(std::unique_ptr<EditCommand> command, bool allowMerge = false) override;
		void BeginChanges(String name) override;
		void EndChanges() override;
		void PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile) override;
		void RevertPreview(std::int32_t layer, const Recti& area) override;
		Tiles::TileMap* GetTileMap() override;
		EditorCamera* GetCamera() override;
		void ShowMessage(StringView text) override;
		EventStamp& GetEventBrush() override;
		EventSelection& GetEventSelection() override;
		EventPreviews& GetEventPreviews() override;
		float GetAnimTime() const override;
		UiSurface* GetSurface() const override;
		SmartTileModel& GetSmartTiles() override;
		const TileShapes& GetTileShapes() override;

		/** @brief Returns the history of the document */
		CommandHistory& GetHistory() {
			return _history;
		}
		/** @brief Sets the layer being edited */
		void SetCurrentLayer(std::int32_t layer) {
			_currentLayer = layer;
		}

		/** @brief Returns the pointer over the middle of a cell, see @ref FakeToolHost */
		ToolPointer MakePointer(Vector2i cell, MouseButton button = MouseButton::Left, std::int32_t modifiers = 0) const;
		/** @brief Returns the pointer at a position in pixels of the level */
		ToolPointer MakePointerAt(Vector2f world, MouseButton button = MouseButton::Left, std::int32_t modifiers = 0) const;
		/** @brief Presses the pointer over a cell and releases it there */
		void Click(EditorTool& tool, Vector2i cell, MouseButton button = MouseButton::Left, std::int32_t modifiers = 0);
		/** @brief Presses the pointer over a cell, moves it to another one and releases it there */
		void Drag(EditorTool& tool, Vector2i from, Vector2i to, MouseButton button = MouseButton::Left, std::int32_t modifiers = 0);

		/** @brief Undoes the last change, as the editor does, returns `false` if there was none */
		bool Undo();
		/** @brief Redoes the last undone change, as the editor does, returns `false` if there was none */
		bool Redo();
		/** @brief Forgets the previews, reverted areas and messages recorded */
		void ClearRecorded();

	private:
		LevelDocument& _document;
		CommandHistory _history;
		std::int32_t _currentLayer;
		TileStamp _stamp;
		TileSelection _selection;
		EventStamp _eventBrush;
		EventSelection _eventSelection;
		EventPreviews _eventPreviews;
		SmartTileModel _smartTiles;
		TileShapes _tileShapes;

		// Keeps the selections in line with the document after a change, as the editor does
		void OnChanged(const DocumentChanges& changes);
	};
}

#endif
