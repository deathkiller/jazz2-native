#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../EventType.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Finds tiles or events of a kind and replaces them, or selects them

		A tile is taken from the brush, and matches the same tile of the same tile set --- flipped the same way, or
		any way. A replaced tile keeps how it was flipped and whether it was translucent. Events are found by their
		kind and the values of the parameters chosen, edited as the event properties edit them. A replaced one gets
		the parameters set for the new kind, or keeps those of the same name, and keeps its difficulties and flags.
	*/
	void ShowFindReplaceDialog(IEditorContext& session);

	/**
		@brief Replaces a tile with another one in the layers of a level, as a step that can be undone

		All layers if @p layer is -1, with @p onlySelected only the selected tiles. Returns the number of tiles
		replaced.
	*/
	std::int32_t ReplaceTiles(IEditorContext& session, std::int32_t layer, const TileRef& find, const TileRef& replace, bool anyFlip, bool onlySelected = false);

	/** @brief Which events Find and Replace finds, see @ref ReplaceEvents() */
	struct EventFindOptions
	{
		/** @brief Kind of the events */
		EventType Type = EventType::Empty;
		/** @brief Parameters of the kind whose values have to be those of @ref Values, a bit for each of the first 32, none for any values */
		std::uint32_t FieldMask = 0;
		/** @brief Event of the kind with the values of the parameters of @ref FieldMask */
		Formats::EventRecord Values = {};
		/** @brief Only the selected events */
		bool OnlySelected = false;
	};

	/** @brief Returns whether an event, at a tile of the grid or off it by its index, is found */
	bool MatchesEventFind(IEditorContext& session, const EventFindOptions& find, const Formats::EventRecord& event, std::uint32_t cell, std::int32_t offGrid);
	/** @brief Returns the number of events found, on the grid and off it */
	std::int32_t CountFoundEvents(IEditorContext& session, const EventFindOptions& find);

	/**
		@brief Replaces the events found with another event, as a step that can be undone

		They become @p replace and keep their difficulties and flags, with @p keepParams also the parameters of the
		same name and kind as the new kind has. Returns how many, or -1 if the sprite layer is locked or hidden, the
		events can't be changed then.
	*/
	std::int32_t ReplaceEvents(IEditorContext& session, const EventFindOptions& find, const Formats::EventRecord& replace, bool keepParams = true);
	/** @overload The new events get the parameters of a new event of the kind they're replaced with */
	std::int32_t ReplaceEvents(IEditorContext& session, const EventFindOptions& find, EventType replace, bool keepParams = true);
	/** @overload */
	std::int32_t ReplaceEvents(IEditorContext& session, EventType find, EventType replace);
}

#endif
