#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiWidgets.h"

#include <memory>

namespace nCine
{
	class Texture;
}

namespace Jazz2::Editor
{
	class DocumentChanges;
	class IEditorContext;
	class LevelDocument;
	class LevelThumbnail;
	struct DocumentLayer;

	/** @brief Adds the tabs that switch what the panel with the tiles and the events shows */
	void AddPickerPanelTabs(UiElement* header, IEditorContext& session);
	/**
	 * @brief Returns a name for a layer that has none, by what it is
	 *
	 * The renderer tells a textured background, effects or a masked layer. A layer that moves with the sprite layer is
	 * known by its number, others are a foreground or a background by whether they're drawn in front of the sprite layer
	 * or behind it. @p index is the index of the layer in @p document.
	 */
	String GetAutoLayerName(const LevelDocument& document, std::int32_t index);
	/** @brief Returns the name of a layer, or the name it gets by what it is if it has none */
	String GetLayerTitle(const LevelDocument& document, std::int32_t index);

	/**
		@brief List of the layers of the level, with their visibility and lock

		A layer is renamed by a double click on it, and moved in the list by dragging it, by a finger after holding it
		for a moment. Alt+click on the eye shows only that layer. Each row has a small picture of its layer.
	*/
	class LayersPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit LayersPanel(IEditorContext& session);
		~LayersPanel() override;

		/** @brief Shows where a dragged layer would go, the pointer is in pixels of the screen */
		void UpdateLayerDrag(std::int32_t from, Vector2f pos);
		/** @brief Ends dragging a layer, @p drop moves it where it was shown to go */
		void EndLayerDrag(bool drop);
		/** @brief Returns a small picture of a layer, `nullptr` if there's none yet */
		nCine::Texture* GetThumbnail(std::int32_t index) const;

		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;

	private:
		// A small picture of a layer, found by its identity, made again a while after its tiles change
		struct Thumbnail
		{
			std::uint32_t LayerId;
			std::unique_ptr<nCine::Texture> Image;
			Vector2i Size;
			bool Dirty;
			float Cooldown;
		};

		IEditorContext& _session;
		UiScrollView* _list;
		std::int32_t _shownCount;
		// Layer being dragged and the gap between the rows it would go into, or -1
		std::int32_t _dragFrom;
		std::int32_t _dropSlot;
		// Identity of the layer being dragged, the one moved when it's dropped
		std::uint32_t _dragLayerId = 0;
		// Where the pointer dragging a layer is, the list scrolls while it's held near its top or bottom
		Vector2f _dragPos;
		SmallVector<Thumbnail, 0> _thumbnails;
		std::unique_ptr<LevelThumbnail> _thumbnailColors;
		std::shared_ptr<Function<void(const DocumentChanges&)>> _changeObserver;
		bool _revalidateColors;

		void Rebuild();
		Rectf GetSlotLine(std::int32_t slot) const;
		void UpdateThumbnails(float timeMult);
		void DrawThumbnail(Thumbnail& thumbnail, const DocumentLayer& layer);
	};

	/**
		@brief Properties of the layer being edited
	*/
	class InspectorPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit InspectorPanel(IEditorContext& session);

	private:
		IEditorContext& _session;
	};

	/**
		@brief Tiles of a tile set to pick the stamp from
	*/
	class TileGrid : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit TileGrid(IEditorContext& session);

		/** @brief Returns the source of the tiles, see @ref TileRef::Source */
		std::uint8_t GetSource() const {
			return _source;
		}
		/** @brief Sets the source of the tiles */
		void SetSource(std::uint8_t source);
		/** @brief Returns the number of tiles in a row, they're as large as the width allows */
		std::int32_t GetColumns() const;
		/** @brief Sets the number of tiles in a row, it's remembered in the settings */
		void SetColumns(std::int32_t columns);
		/** @brief Returns the number of tiles of the source */
		std::int32_t GetTileCount() const;
		/** @brief Returns the cell of the tile under the pointer, `false` if it's over no tile */
		bool GetCellUnderPointer(Vector2i& cell) const;
		/** @brief Makes the tiles between two cells the stamp, as a drag over them does, and paints with them */
		void PickBlock(Vector2i from, Vector2i to);

		/** @brief Whether how often the level uses each tile is shown, unused tiles darkened */
		bool ShowUsage;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void OnPointerCancel() override;
		/** @brief A finger dragged over the tiles scrolls them, held on a tile it picks a block of them from there */
		bool DragsAfterHold() const override;
		bool OnWheel(const UiPointerEvent& event) override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		// How many times each tile of the source is used in all layers and animations, counted again when the tiles
		// change, but not more often than a few times a second, every tile of the level is looked at
		mutable SmallVector<std::int32_t, 0> _usage;
		mutable std::int32_t _usageMax;
		mutable bool _usageDirty;
		mutable float _usageCooldown;
		mutable std::uint8_t _usageSource;
		std::shared_ptr<Function<void(const DocumentChanges&)>> _changeObserver;

		void UpdateUsage() const;

		IEditorContext& _session;
		std::uint8_t _source;
		float _tileSize;
		bool _dragging;
		Vector2i _dragStart;
		Vector2i _dragEnd;
		mutable String _tooltip;

		Vector2i GetCellAt(Vector2f pos) const;
		float GetTileSize(const UiContext& context, float availableWidth) const;
	};

	/**
		@brief Panel with the tile grid and its controls
	*/
	class TilePickerPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit TilePickerPanel(IEditorContext& session);

		void Update(float timeMult) override;

	private:
		IEditorContext& _session;
		TileGrid* _grid;

		String GetSourceName() const;
		void NextSource(std::int32_t direction);
	};

	/**
		@brief The stamp of the brush, drawn small
	*/
	class StampPreview : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit StampPreview(IEditorContext& session);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		IEditorContext& _session;
		mutable String _tooltip;
	};

	/**
		@brief Status bar at the bottom of the window
	*/
	class StatusBar : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit StatusBar(IEditorContext& session);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;

	private:
		IEditorContext& _session;
	};
}

#endif
