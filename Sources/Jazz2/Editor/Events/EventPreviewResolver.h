#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"
#include "../../../nCine/Primitives/Vector2.h"

namespace Jazz2::Editor
{
	using nCine::Vector2f;

	/**
		@brief How an event is drawn, as the object it spawns looks with its parameters
	*/
	struct EventPreviewSpec
	{
		/** @brief Path of the metadata, a string that lives as long as the program, `nullptr` to draw the event as a box */
		const char* Metadata = nullptr;
		/** @brief Animation of the metadata (see @ref AnimState) */
		std::uint32_t Animation = 0;
		/** @brief Whether the sprite is flipped horizontally */
		bool FlipX = false;
		/** @brief Whether the sprite is flipped vertically */
		bool FlipY = false;
		/** @brief How many quarter turns clockwise the sprite is drawn rotated by, around its hotspot */
		std::uint8_t QuarterTurns = 0;
		/** @brief Where the hotspot of the sprite is, in pixels from the center of the tile of the event */
		Vector2f Offset;
	};

	/**
	 * @brief Finds how an event is drawn with its parameters
	 *
	 * Mirrors what the object the event spawns chooses when it's activated --- its metadata, animation, which way
	 * it faces and where it moves itself. Returns `false` for an event whose look doesn't depend on its parameters,
	 * it's drawn as the catalog says then (see @ref EventDescriptor::PreviewMetadata).
	 */
	bool ResolveEventPreview(const Formats::EventRecord& event, EventPreviewSpec& spec);
}

#endif
