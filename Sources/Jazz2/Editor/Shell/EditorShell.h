#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ShellCommon.h"
#include "../Events/EventPreviews.h"
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
	class DocumentChanges;
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

		/** @brief Returns a dialog shown under an identifier, see @ref ShowSingleDialog() */
		UiElement* FindDialog(StringView id) const;
		/** @brief Remembers a dialog shown under an identifier until it's closed, with what to do when it's to be shown again */
		void TrackDialog(StringView id, UiElement* dialog, Function<void()>&& onShownAgain);
		/** @brief Forgets a dialog that was closed */
		void ForgetDialog(UiElement* dialog);
		/** @brief Does what a dialog was shown with for when it's to be shown again, see @ref TrackDialog() */
		void ShowDialogAgain(UiElement* dialog);

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
		// Handles on the frame of the layer being edited that resize it, see LayerResizeHandles
		UiElement* _resizeHandles = nullptr;
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

		// The rows of selected blocks and their edges seen, in blocks of GetCellUnit(), found again only when the
		// selection, its layer, the blocks seen or their size change, see DrawSelection()
		struct SelectionRun
		{
			std::int32_t Y;
			std::int32_t Start;
			std::int32_t End;
		};
		struct SelectionEdge
		{
			std::int32_t Y;
			std::int32_t Start;
			std::int32_t End;
			// 0 left, 1 right, 2 above, 3 below
			std::uint8_t Side;
		};
		SmallVector<SelectionRun, 0> _selectionRuns;
		SmallVector<SelectionEdge, 0> _selectionEdges;
		std::uint32_t _selectionRevision = 0;
		std::int32_t _selectionLayer = -1;
		std::int32_t _selectionUnit = 0;
		Recti _selectionUnits;
		// The size of the layer and the axes it repeats in (1 X, 2 Y), the selection is shown in every copy of a layer that repeats
		Vector2i _selectionLayerSize;
		std::int32_t _selectionRepeats = 0;

		// Dialogs that aren't modal, one of each, see ShowSingleDialog()
		struct ShownDialog
		{
			String Id;
			UiElement* Element;
			Function<void()> OnShownAgain;
		};
		SmallVector<ShownDialog, 4> _shownDialogs;

		// Told about changes of the level, the caches below are made again after them
		std::shared_ptr<Function<void(const DocumentChanges&)>> _changeObserver;

		// Events that lead to others, warps to their targets, and those that share a trigger, found again only when the
		// events change. Tiles of the same identifier next to each other are taken together, see DrawEventLinks().
		struct LinkGroup
		{
			Recti Cells;
			std::int32_t Id;
		};
		struct LinkEvent
		{
			std::int32_t Id;
			std::uint32_t Cell;
			std::int32_t Group;
		};
		SmallVector<LinkGroup, 0> _warpGroups;
		// Sorted by the identifier
		SmallVector<LinkEvent, 0> _warpTargets;
		SmallVector<LinkGroup, 0> _triggerGroups;
		// Sorted by the identifier, then by the tile
		SmallVector<LinkEvent, 0> _triggerEvents;
		// The track of a race from waypoint to waypoint and on to the finish, see EventPreviews::CollectRaceTrack()
		SmallVector<EventLink, 0> _raceLinks;
		bool _linksDirty = true;
		std::uint32_t _linksCatalogRevision = 0;
		std::size_t _linksEventCount = 0;
		// Other links of the selected events, found again when the selection or the events change
		SmallVector<EventLink, 0> _selectedLinks;
		std::uint32_t _selectedLinksKey = 0;
		std::uint32_t _eventsSerial = 0;

		AnnotationCache _annotations;

		void OnDocumentChanged(const DocumentChanges& changes);
		void UpdateEventLinks();
		// What the game shows around the pointer, see EditorSettings::ShowScreenFrame
		void DrawScreenFrame(UiPainter& painter);

		void CreateMenuBar();
		void CreateToolbar();
		void CreateToolStrip();
		UiButton* AddActionButton(UiElement* parent, StringView actionId, bool showText = false);

		void DrawGrid(UiPainter& painter, const LayerView& view);
		void DrawSelection(UiPainter& painter, const LayerView& view);
		// Returns how many tiles a side of a block drawn as one is, more than one only when tiles are a few pixels small
		static std::int32_t GetCellUnit(const LayerView& view);
		void DrawLayerBounds(UiPainter& painter, const LayerView& view);
		void DrawRepeatedLayerBounds(UiPainter& painter, const LayerView& view, const Rectf& topLeft, const Rectf& bottomRight);
		// Where a layer is on the screen, of a layer that repeats the copy that is framed, see DrawLayerBounds()
		Rectf GetLayerFrame(const LayerView& view) const;
		void DrawMasks(UiPainter& painter, const LayerView& view);
		// Over the masks of the sprite layer, symbols of what the events of its tiles make them do
		void DrawMaskGlyphs(UiPainter& painter, const LayerView& view);
		// Bookmarks and notes, see Annotation
		void DrawAnnotations(UiPainter& painter);
		// Tiles the player can get to, see PlayOverlays::GetReachability()
		void DrawReachability(UiPainter& painter);
		// Returns `true` if the masks show a symbol for an event, instead of its mark
		static bool HasMaskGlyph(EventType type);
		// Without the links and the selection if there is a hole, events that reach into it are left out
		void DrawEvents(UiPainter& painter, const Rectf& area, Vector2f holeCenter = Vector2f(), float holeRadius = -1.0f);
		// Whether the event tool drags the selected events away, they're faded where they were, a dragged handle doesn't
		bool IsMovingEvents() const;
		void DrawEventLinks(UiPainter& painter, const LayerView& view, float alpha);
		void DrawSelectedEventDetails(UiPainter& painter, const LayerView& view);
		void DrawPlayPath(UiPainter& painter);
	};
}

#endif
