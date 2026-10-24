#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Common/EventStamp.h"
#include "../Events/EventGizmos.h"

namespace Jazz2::Editor
{
	class SetEventsCommand;

	/**
	 * @brief Gives links that an event stamp has whole, as warps and triggers, an identifier the level doesn't use yet
	 *
	 * Links are those of the channels of the catalog (see @ref EventParamField::LinkChannel), of the events of the
	 * stamp and of those inside of them. A link is whole with both a source and a target in the stamp, as a warp
	 * with its origin and its target, or a trigger with what changes it and the tiles it changes, or with more
	 * peers. A pasted copy of it then works on its own, while a part of a link keeps its identifier and joins the
	 * one in the level. Only identifiers the level uses already change, so calling it before each placement gives
	 * each copy its own. Returns the number of identifiers changed.
	 */
	std::int32_t RemapLinkedIds(EventStamp& stamp, const LevelDocument& document);

	/** @brief Returns a copy of the events off the grid of a level, to be changed and set back as one step */
	SmallVector<Formats::OffGridEventRecord, 0> CopyOffGridEvents(const LevelDocument& document);

	/**
		@brief Selected events, of the event grid by their tiles and off the grid by their indices

		Both kinds can be selected together, any number of each. An event off the grid is known by its index in
		@ref LevelDocument::OffGridEvents. A change that adds or removes some of them, as an undo, moves the indices of
		the others, so @ref DropEmpty() finds the selected ones again by the events they were before the change, and
		drops those that are gone. The selection is changed only while the indices are of the events as they are, so
		those that change them call @ref DropEmpty() before they change the selection.
	*/
	class EventSelection
	{
	public:
		/** @brief Returns a number that changes whenever the selection changes */
		std::uint32_t GetRevision() const {
			return _revision;
		}

		/** @brief Returns `true` if nothing is selected, neither on the grid nor off it */
		bool IsEmpty() const {
			return (_cells.empty() && _offGrid.empty());
		}
		/** @brief Returns `true` if an event of the grid is selected */
		bool HasCells() const {
			return !_cells.empty();
		}
		/** @brief Returns `true` if an event off the grid is selected */
		bool HasOffGrid() const {
			return !_offGrid.empty();
		}
		/** @brief Returns the selected tiles of the event grid as @cpp x + y * width @ce, sorted */
		ArrayView<const std::uint32_t> GetCells() const {
			return _cells;
		}
		/** @brief Returns the indices of the selected events off the grid, sorted */
		ArrayView<const std::int32_t> GetOffGrid() const {
			return _offGrid;
		}
		/** @brief Returns the number of selected events of the grid */
		std::int32_t GetCellCount() const {
			return std::int32_t(_cells.size());
		}
		/** @brief Returns the number of all selected events, on the grid and off it */
		std::int32_t GetTotalCount() const {
			return std::int32_t(_cells.size() + _offGrid.size());
		}
		/** @brief Returns `true` if a tile is selected */
		bool Contains(std::uint32_t cell) const;
		/** @brief Returns `true` if an event off the grid is selected */
		bool ContainsOffGrid(std::int32_t index) const;

		/** @brief Clears the selection */
		void Clear();
		/** @brief Selects a tile, replacing what was selected */
		void Set(std::uint32_t cell);
		/** @brief Selects an event off the grid, replacing what was selected */
		void SetOffGrid(std::int32_t index);
		/** @brief Adds a tile to the selection */
		void Add(std::uint32_t cell);
		/** @brief Adds an event off the grid to the selection */
		void AddOffGrid(std::int32_t index);
		/** @brief Removes a tile from the selection */
		void Remove(std::uint32_t cell);
		/** @brief Removes an event off the grid from the selection */
		void RemoveOffGrid(std::int32_t index);
		/** @brief Replaces the selection by the given tiles, in any order, no event off the grid stays selected */
		void Assign(SmallVectorImpl<std::uint32_t>&& cells);
		/** @brief Replaces the selected events off the grid by the given ones, in any order, the tiles stay selected */
		void AssignOffGrid(SmallVectorImpl<std::int32_t>&& indices);
		/**
		 * @brief Drops tiles that have no event anymore, as after an undo, and finds the selected events off the grid again
		 *
		 * Called after every change of the events. Events off the grid added or removed since the last call move the
		 * indices of the others, the selected ones are found again by what they were then, those that are gone are
		 * dropped.
		 */
		void DropEmpty(const LevelDocument& document);

	private:
		SmallVector<std::uint32_t, 0> _cells;
		SmallVector<std::int32_t, 0> _offGrid;
		// The events off the grid as they were at the last DropEmpty(), what the selected indices are of
		SmallVector<Formats::OffGridEventRecord, 0> _known;
		bool _knownValid = false;
		std::uint32_t _revision = 0;
	};

	/**
		@brief Places, selects, moves and erases events

		Events are always on the sprite layer, whichever layer is being edited. A click on an empty tile places the
		events of the stamp and dragging goes on placing them; a click on an event selects it and dragging moves
		the selected events. Ctrl adds to or removes from the selection and a drag over empty tiles with it selects
		the events in a rectangle, Shift places the stamp over events, Alt picks an event as the stamp and the right
		button erases events. A single selected event shows handles of its parameters that can be dragged in the
		view, see @ref EventGizmos. Events off the grid are dragged to another position, with Shift to the center of
		a tile, erased with the right button and picked with Alt the same way.

		With @ref EventPlaceMode::Select nothing is placed, events are selected as the select tool selects tiles: a
		drag from an empty spot selects those in a rectangle, with Shift adds them to the selection and with Ctrl takes
		them out of it. A click selects an event, with Shift adds it and with Ctrl adds it or takes it away, a click
		on an empty spot ends the selection.
	*/
	class EventTool : public EditorTool
	{
	public:
		explicit EventTool(IToolHost& host);
		~EventTool() override;

		EditorToolType GetType() const override;
		void OnDeactivate() override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		/** @brief While events off the grid are dragged, <kbd>X</kbd> or <kbd>Y</kbd> held snaps that axis, see @ref GetOffGridTarget() */
		bool OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;

		/**
		 * @brief Places the single event of the stamp on tiles of the event grid as one step, returns how many it was placed on
		 *
		 * Tiles with an event keep it unless @p overwrite.
		 */
		std::int32_t PlaceOnCells(ArrayView<const Vector2i> cells, bool overwrite);

		/** @brief Whether the selected events are being dragged to another place, where they were is faded meanwhile */
		bool IsMovingEvents() const;

	private:
		enum class Mode : std::uint8_t
		{
			None,
			Paint,
			Erase,
			Move,
			Marquee,
			Gizmo,
			MoveOffGrid,
			PlaceRect
		};

		// How a rectangle dragged changes the selection
		enum class MarqueeKind : std::uint8_t
		{
			Replace,
			Add,
			Subtract
		};

		Mode _mode;
		MarqueeKind _marquee;
		Vector2i _start;
		Vector2i _current;
		bool _overwrite;
		std::unique_ptr<SetEventsCommand> _stroke;
		EventGizmo _gizmo;
		Vector2i _gizmoCell;
		// From the pointer to the handle when it was grabbed, so the handle doesn't jump to the pointer
		Vector2f _gizmoGrab;

		/** @brief Returns the handles of the selected event, if a single one is selected, and its tile */
		bool GetGizmos(SmallVectorImpl<EventGizmo>& gizmos, Vector2i& cell);
		// The off-grid event dragged, or clicked to be added to the selection or taken out of it, and where it's dropped
		std::int32_t _offGridIndex;
		Vector2f _offGridTarget;
		// The pointer dragging it last, where it's dropped is found again when a key snaps it
		ToolPointer _offGridPointer;
		// X or Y held while it's dragged, that axis snaps to the middle of the tile, with Shift to half tiles, as in MLLE
		bool _snapX;
		bool _snapY;

		// Returns `true` if the tool only selects events, see EventPlaceMode::Select
		bool IsSelectOnly() const;
		// Makes the tool place events again, once an event to place was picked
		void PlaceEventsAgain();
		// Starts a rectangle dragged to change the selection, `offGrid` is the event off the grid clicked, or -1
		void BeginMarquee(MarqueeKind kind, std::int32_t offGrid);
		// Changes the selection by the rectangle dragged, or by the event clicked if it wasn't dragged
		void EndMarquee();
		// Draws marching ants around the selected events seen in the view
		void DrawSelectedEvents(UiPainter& painter, const LayerView& view);

		/** @brief Returns the handle under the pointer, `-1` if there's none */
		std::int32_t FindGizmo(ArrayView<const EventGizmo> gizmos, Vector2f screen);
		/** @brief Returns the event off the grid under the pointer, the one drawn last if more are, `-1` if there's none */
		std::int32_t FindOffGrid(Vector2f screen);
		/**
		 * @brief Returns where an off-grid event dragged by the pointer is dropped
		 *
		 * With <kbd>Shift</kbd> in the middle of a tile. With <kbd>X</kbd> or <kbd>Y</kbd> held, only that axis is
		 * snapped, to the middle of the tile, or with <kbd>Shift</kbd> to the nearest edge or middle of a tile.
		 */
		Vector2f GetOffGridTarget(const ToolPointer& pointer);

		/** @brief Returns the tile of the event grid under the pointer */
		Vector2i GetCell(const ToolPointer& pointer);
		/** @brief Places the stamp at a tile as a part of the stroke */
		void PaintAt(Vector2i cell);
		/** @brief Erases the event of a tile as a part of the stroke */
		void EraseAt(Vector2i cell);
		/** @brief Places the event of the stamp on the empty tiles of the event grid flood-filled from one */
		void FillAt(Vector2i cell);
		/** @brief Ends the stroke and records it, with the events off the grid of the stamp as one step */
		void CommitStroke();
		/** @brief Moves the selected events, those of the grid by tiles and those off it by pixels, as one step */
		void MoveSelection(Vector2i cellOffset, Vector2i pixelOffset);
		/** @brief Returns how far a drag of an event off the grid moves the selected events, `false` if it doesn't */
		bool GetOffGridDragOffset(Vector2i& cellOffset, Vector2i& pixelOffset);
		// Events off the grid of the stamp placed by the stroke, recorded with it
		SmallVector<Formats::OffGridEventRecord, 0> _pendingOffGrid;
		// Selected tiles whose events the stroke erased, selected again if it's canceled
		SmallVector<std::uint32_t, 0> _erasedSelected;
	};
}

#endif
