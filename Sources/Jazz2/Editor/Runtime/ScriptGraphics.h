#pragma once

#if (defined(WITH_EDITOR) && defined(WITH_ANGELSCRIPT)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../Resources.h"

#include <Containers/SmallVector.h>

#include <memory>

namespace Jazz2
{
	class IRootController;
}

namespace Jazz2::Scripting::Legacy
{
	class EventTranslator;
}

namespace Jazz2::Scripting
{
	class LevelScriptLoader;
}

namespace Jazz2::Editor
{
	/**
		@brief The sprites the script of a level gives the objects of the events, so the editor shows them

		The script runs in a level of its own, loaded as the game loads it but with no players and nothing shown of it,
		only until `onLevelLoad()` returns. The templates of the objects (`jjObjectPresets`) it gave another animation
		then, or whose frames it changed, are taken with the frames of their animations, and the level goes away with
		whatever else the script did; the palette of the sprites is put back. An event is matched to a template by the
		event of the original game it converts from.
	*/
	class ScriptGraphics
	{
	public:
		ScriptGraphics();
		~ScriptGraphics();

		ScriptGraphics(const ScriptGraphics&) = delete;
		ScriptGraphics& operator=(const ScriptGraphics&) = delete;

		/** @brief Runs the script of a level and takes the sprites it gives the objects, returns `false` if there are none */
		bool Capture(IRootController* root, const LevelDocument& document, StringView levelName, StringView fullPath);
		/** @brief Forgets all sprites */
		void Clear();
		/** @brief Returns `true` if there are no sprites */
		bool IsEmpty() const;
		/**
		 * @brief Returns the sprite the script gives the object of an event, or `nullptr`
		 *
		 * With @p animated its animation plays as a behavior of JJ2 plays it, otherwise it shows the frame the object starts
		 * with, as an object of the game that doesn't animate (crates) shows it. A template that keeps the animation of
		 * the game, only with other frames, plays it only if @p ownAnimated says the sprite of the game does too.
		 */
		Resources::GraphicResource* Find(const Formats::EventRecord& event, bool animated, bool ownAnimated);

	private:
		struct Look
		{
			std::uint32_t FirstFrame;
			std::int32_t FrameCount;
			std::uint32_t Frame;
			bool Restyled;

			bool operator==(const Look& other) const {
				return FirstFrame == other.FirstFrame && FrameCount == other.FrameCount && Frame == other.Frame;
			}
		};

		struct Sprite
		{
			std::unique_ptr<Resources::GenericGraphicResource> Base;
			Resources::GraphicResource Animated;
			Resources::GraphicResource Still;
			bool Restyled;
		};

		struct Variant
		{
			// See MakeKey()
			std::uint32_t Key;
			std::unique_ptr<Sprite> Look;
		};

		// By the event of the original
		std::unique_ptr<Sprite> _sprites[256];
		// Events whose parameters give them another look (an enemy that is a bomb by a parameter), sorted by key
		SmallVector<Variant, 0> _variants;
		// Types of the game the events with sprites convert to, sorted, the events of other types aren't looked up
		SmallVector<std::uint16_t, 0> _types;
		// Made on first use, it probes all converters of events
		std::unique_ptr<Scripting::Legacy::EventTranslator> _translator;

		static std::uint32_t MakeKey(std::uint8_t eventId, std::uint32_t params);

		void TakeSprites(Scripting::LevelScriptLoader& scripts, Vector2i levelSize);
		bool GetLook(Scripting::LevelScriptLoader& scripts, std::uint8_t eventId, std::int32_t tileX, std::int32_t tileY, Look& look);
		std::unique_ptr<Sprite> MakeSprite(Scripting::LevelScriptLoader& scripts, std::uint8_t eventId, const Look& look, SmallVectorImpl<std::uint32_t>& colors);
		Sprite* FindSprite(const Formats::EventRecord& event);
	};
}

#endif
