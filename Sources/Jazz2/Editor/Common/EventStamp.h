#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Editor
{
	/**
		@brief Events the event tool places

		One event picked from the list, or several copied from the level, each at its offset from the tile clicked.
		Links the stamp has whole get identifiers of their own when it's placed, see @ref RemapLinkedIds().
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

		/** @brief An event of the stamp off the grid */
		struct OffGridItem
		{
			/** @brief Offset in pixels from the top left corner of the tile the stamp is placed at */
			Vector2i Offset;
			/** @brief The event */
			Formats::EventRecord Event;
		};

		/** @brief Events of the stamp on the grid */
		SmallVector<Item, 1> Items;
		/** @brief Events of the stamp off the grid, placed with those on it */
		SmallVector<OffGridItem, 0> OffGrid;

		/** @brief Returns `true` if the stamp places nothing */
		bool IsEmpty() const {
			return (Items.empty() && OffGrid.empty());
		}
		/** @brief Returns `true` if the stamp is a single event on the grid */
		bool IsSingle() const {
			return (Items.size() == 1 && OffGrid.empty() && Items[0].Offset == Vector2i(0, 0));
		}
		/** @brief Returns the first event of the stamp, on the grid or off it, or `nullptr` if it's empty */
		const Formats::EventRecord* GetFirst() const {
			return (!Items.empty() ? &Items[0].Event : (!OffGrid.empty() ? &OffGrid[0].Event : nullptr));
		}
		/** @brief Makes a stamp of a single event */
		static EventStamp FromEvent(const Formats::EventRecord& event) {
			EventStamp stamp;
			Item& item = stamp.Items.emplace_back();
			item.Offset = Vector2i(0, 0);
			item.Event = event;
			return stamp;
		}
	};
}

#endif
