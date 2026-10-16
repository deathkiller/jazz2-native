#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiDock.h"
#include "../UI/UiMenu.h"
#include "../../EventType.h"

#include <memory>

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::Editor
{
	class EditorSession;
	struct LayerView;

	/**
		@brief Interface of the level editor on a desktop

		The menu bar and the toolbar at the top, the tools on the left with the tiles and the events to pick from next
		to them, the layers and their properties on the right and the status bar at the bottom. The level is shown
		behind all of it, the part the panels leave uncovered is the view (see @ref GetViewRect()). The panel on the
		left with the tiles and the one on the right can float over the level instead, see @ref UiDockFrame. The shell
		also draws what is shown over the level --- the grid, the selection and what the tool in use shows.
	*/
	class EditorShell : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		explicit EditorShell(EditorSession& session);
		~EditorShell() override;

		/** @brief Returns the part of the screen the level is seen in, between the panels */
		Rectf GetViewRect() const {
			return _viewRect;
		}
		/** @brief Returns `true` if the interface is laid out for a small screen, with the panels in drawers */
		bool IsCompact() const {
			return _compact;
		}
		/** @brief Opens or closes the drawer of the panel with the tiles and the events, or of the panel on the right */
		void ToggleDrawer(bool picker);
		/** @brief Returns `true` if the drawer of a panel is open */
		bool IsDrawerOpen(bool picker) const;

		/** @brief Returns `true` if an action is offered only to experts, see @ref EditorSettings::Expert */
		static bool IsExpertAction(StringView actionId);

		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		UiElement* HitTest(Vector2f pos) override;

		/** @brief Draws the sprites of the events into the level, among its layers, their marks are drawn over it */
		void DrawSceneEvents(UI::Canvas& canvas);

	private:
		EditorSession& _session;
		UiMenuBar* _menuBar;
		UiPanel* _toolbar;
		UiPanel* _toolStrip;
		UiDockFrame* _rightFrame;
		UiPanel* _rightDock;
		UiElement* _layersPanel;
		UiDockFrame* _pickerFrame;
		UiPanel* _pickerDock;
		// Panels that float, or are on their way, are drawn over the others in this order, the last one on top
		SmallVector<UiDockFrame*, 2> _floatOrder;
		UiElement* _statusBar;
		UiPanel* _playBar;
		UiSplitter* _rightSplitter;
		UiSplitter* _layersSplitter;
		UiSplitter* _pickerSplitter;
		float _rightDockWidth;
		float _layerListHeight;
		// How far the bar of the level being played has come in, 0 to 1
		float _playBarAppear = 0.0f;
		float _pickerDockWidth;
		float _dragStartSize;
		Rectf _viewRect;
		// Laid out for a small screen, with the panels in drawers, and for a very narrow one with the tools at the bottom
		bool _compact = false;
		bool _narrow = false;

		// The path of the last play-test, simplified and smoothed once, with the speed of the player along it
		struct CachedPathSegment
		{
			SmallVector<Vector2f, 0> Points;
			SmallVector<float, 0> Speeds;
			std::uint8_t End;
		};
		SmallVector<CachedPathSegment, 0> _cachedPath;
		std::int32_t _cachedPathPoints;
		std::int32_t _cachedPathSegments;
		std::unique_ptr<Rendering::PolylineRenderer> _pathRenderer;

		void CreateMenuBar();
		void CreateToolbar();
		void CreateToolStrip();
		UiButton* AddActionButton(UiElement* parent, StringView actionId, bool showText = false);

		void DrawGrid(UiPainter& painter, const LayerView& view);
		void DrawSelection(UiPainter& painter, const LayerView& view);
		void DrawLayerBounds(UiPainter& painter, const LayerView& view);
		void DrawRepeatedLayerBounds(UiPainter& painter, const LayerView& view, const Rectf& topLeft, const Rectf& bottomRight);
		void DrawMasks(UiPainter& painter, const LayerView& view);
		// Over the masks of the sprite layer, symbols of what the events of its tiles make them do
		void DrawMaskGlyphs(UiPainter& painter, const LayerView& view);
		// Bookmarks and notes, see Annotation
		void DrawAnnotations(UiPainter& painter);
		// Tiles the player can get to, see EditorSession::GetReachability()
		void DrawReachability(UiPainter& painter);
		// Returns `true` if the masks show a symbol for an event, instead of its mark
		static bool HasMaskGlyph(EventType type);
		// Without the links and the selection if there is a hole, events that reach into it are left out
		void DrawEvents(UiPainter& painter, const Rectf& area, Vector2f holeCenter = Vector2f(), float holeRadius = -1.0f);
		void DrawEventLinks(UiPainter& painter, const LayerView& view, float alpha);
		void DrawSelectedEventDetails(UiPainter& painter, const LayerView& view);
		void DrawPlayPath(UiPainter& painter);
	};
}

#endif
