#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Events/EventGizmos.h"

namespace Jazz2::Editor
{
	class SetEventsCommand;

	/**
		@brief Events the event tool places

		One event picked from the list, or several copied from the level, each at its offset from the tile clicked.
	*/
	struct EventStamp
	{
		/** @brief An event of the stamp */
		struct Item
		{
			/** @brief Offset from the tile the stamp is placed at */
			Vector2i Offset;
			/** @brief The event */
			Formats::EventRecord Event;
		};

		/** @brief Events of the stamp */
		SmallVector<Item, 1> Items;

		/** @brief Returns `true` if the stamp places nothing */
		bool IsEmpty() const {
			return Items.empty();
		}
		/** @brief Returns `true` if the stamp is a single event */
		bool IsSingle() const {
			return (Items.size() == 1 && Items[0].Offset == Vector2i(0, 0));
		}
		/** @brief Makes a stamp of a single event */
		static EventStamp FromEvent(const Formats::EventRecord& event);

		/**
		 * @brief Gives warps and triggers that the stamp has whole an identifier the level doesn't use yet
		 *
		 * A warp is whole with both its origin and its target in the stamp, a trigger with both what changes it and
		 * the tiles it changes. A pasted copy of them then works on its own, while a part of a warp or a trigger
		 * keeps its identifier and joins the one in the level. Returns the number of identifiers changed.
		 */
		std::int32_t RemapLinkedIds(const LevelDocument& document);
	};

	/**
		@brief Selected events of the event grid, or a single event off the grid

		The two are never selected together, selecting one of them clears the other.
	*/
	class EventSelection
	{
	public:
		/** @brief Returns `true` if no event of the grid is selected, an event off it can be */
		bool IsEmpty() const {
			return _cells.empty();
		}
		/** @brief Returns the index of the selected event off the grid, `-1` if none is selected */
		std::int32_t GetOffGrid() const {
			return _offGrid;
		}
		/** @brief Returns `true` if an event off the grid is selected */
		bool HasOffGrid() const {
			return (_offGrid >= 0);
		}
		/** @brief Selects an event off the grid, replacing what was selected */
		void SetOffGrid(std::int32_t index);
		/** @brief Returns the selected tiles of the event grid as @cpp x + y * width @ce, sorted */
		ArrayView<const std::uint32_t> GetCells() const {
			return _cells;
		}
		/** @brief Returns the number of selected events */
		std::int32_t GetCount() const {
			return std::int32_t(_cells.size());
		}
		/** @brief Returns `true` if a tile is selected */
		bool Contains(std::uint32_t cell) const;

		/** @brief Clears the selection */
		void Clear();
		/** @brief Selects a tile, replacing what was selected */
		void Set(std::uint32_t cell);
		/** @brief Adds a tile to the selection */
		void Add(std::uint32_t cell);
		/** @brief Removes a tile from the selection */
		void Remove(std::uint32_t cell);
		/** @brief Replaces the selection by the given tiles, in any order */
		void Assign(SmallVectorImpl<std::uint32_t>&& cells);
		/** @brief Drops tiles that have no event anymore, as after an undo, and an event off the grid that's gone */
		void DropEmpty(const LevelDocument& document);

	private:
		SmallVector<std::uint32_t, 0> _cells;
		std::int32_t _offGrid = -1;
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
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

	private:
		enum class Mode : std::uint8_t
		{
			None,
			Paint,
			Erase,
			Move,
			Marquee,
			Gizmo,
			MoveOffGrid
		};

		Mode _mode;
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
		// The off-grid event dragged, and where it's dropped
		std::int32_t _offGridIndex;
		Vector2f _offGridTarget;

		/** @brief Returns the handle under the pointer, `-1` if there's none */
		std::int32_t FindGizmo(ArrayView<const EventGizmo> gizmos, Vector2f screen);
		/** @brief Returns the event off the grid under the pointer, the one drawn last if more are, `-1` if there's none */
		std::int32_t FindOffGrid(Vector2f screen);
		/** @brief Returns where an off-grid event dragged by the pointer is dropped */
		Vector2f GetOffGridTarget(const ToolPointer& pointer);

		/** @brief Returns the tile of the event grid under the pointer */
		Vector2i GetCell(const ToolPointer& pointer);
		/** @brief Places the stamp at a tile as a part of the stroke */
		void PaintAt(Vector2i cell);
		/** @brief Erases the event of a tile as a part of the stroke */
		void EraseAt(Vector2i cell);
		/** @brief Ends the stroke and records it */
		void CommitStroke();
		/** @brief Moves the selected events by an offset */
		void MoveSelection(Vector2i offset);
	};
}

#endif
