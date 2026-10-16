#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiWidgets.h"

namespace Jazz2::Editor
{
	class EditorSession;
	struct DocumentLayer;

	/** @brief Adds the tabs that switch what the panel with the tiles and the events shows */
	void AddPickerPanelTabs(UiElement* header, EditorSession& session);
	/**
	 * @brief Returns a name for a layer that has none, by what it is
	 *
	 * The renderer tells a textured background, effects or a masked layer, otherwise how the layer moves compared to
	 * the sprite layer tells a foreground or a background. A layer that moves with the sprite layer is known by its
	 * number, @p index is its index.
	 */
	String GetAutoLayerName(const DocumentLayer& layer, std::int32_t index = -1);
	/** @brief Returns the name of a layer, or the name it gets by what it is if it has none */
	String GetLayerTitle(const DocumentLayer& layer, std::int32_t index = -1);

	/**
		@brief List of the layers of the level, with their visibility and lock

		A layer is renamed by a double click on it, and moved in the list by dragging it.
	*/
	class LayersPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit LayersPanel(EditorSession& session);

		/** @brief Shows where a dragged layer would go, the pointer is in pixels of the screen */
		void UpdateLayerDrag(std::int32_t from, Vector2f pos);
		/** @brief Ends dragging a layer, @p drop moves it where it was shown to go */
		void EndLayerDrag(bool drop);

		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;

	private:
		EditorSession& _session;
		UiScrollView* _list;
		std::int32_t _shownCount;
		// Layer being dragged and the gap between the rows it would go into, or -1
		std::int32_t _dragFrom;
		std::int32_t _dropSlot;

		void Rebuild();
		Rectf GetSlotLine(std::int32_t slot) const;
	};

	/**
		@brief Properties of the layer being edited
	*/
	class InspectorPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit InspectorPanel(EditorSession& session);

	private:
		EditorSession& _session;
	};

	/**
		@brief Tiles of a tile set to pick the stamp from
	*/
	class TileGrid : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit TileGrid(EditorSession& session);

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

		/** @brief Whether how often the level uses each tile is shown, unused tiles darkened */
		bool ShowUsage;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool OnWheel(const UiPointerEvent& event) override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		// How many times each tile of the source is used in all layers and animations, counted again when the level changes
		mutable SmallVector<std::int32_t, 0> _usage;
		mutable std::int32_t _usageMax;
		mutable std::uint32_t _usageRevision;
		mutable std::uint8_t _usageSource;

		void UpdateUsage() const;

		EditorSession& _session;
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
		explicit TilePickerPanel(EditorSession& session);

		void Update(float timeMult) override;

	private:
		EditorSession& _session;
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
		explicit StampPreview(EditorSession& session);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		EditorSession& _session;
		mutable String _tooltip;
	};

	/**
		@brief Status bar at the bottom of the window
	*/
	class StatusBar : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit StatusBar(EditorSession& session);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;

	private:
		EditorSession& _session;
	};
}

#endif
