#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EventCatalog.h"
#include "EventPreviewResolver.h"
#include "../UI/UiPainter.h"
#include "../../../nCine/Primitives/AABB.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Resources
{
	struct GraphicResource;
	struct Metadata;
}

namespace Jazz2::UI
{
	class Canvas;
}

namespace Jazz2::Editor
{
	using Death::Containers::String;
	using Resources::GraphicResource;
	using Resources::Metadata;

	/**
		@brief Draws events as the level editor shows them

		An event is drawn as the object it spawns looks with its parameters (see @ref ResolveEventPreview()), or as
		the catalog shows its type (see @ref EventDescriptor::PreviewMetadata). Any other event is drawn as a box of
		the color of its category with its name in it. Sprites are loaded the first time they are drawn, and kept
		loaded while the editor is open --- even across loading the level to play it, which would release whatever
		the loaded level doesn't use.
	*/
	class EventPreviews
	{
	public:
		EventPreviews();
		~EventPreviews();

		EventPreviews(const EventPreviews&) = delete;
		EventPreviews& operator=(const EventPreviews&) = delete;

		/**
		 * @brief Returns `true` if a rectangle of the level, in pixels, has no solid tile in it
		 *
		 * Some objects turn by the tiles around them when they're spawned, a horizontal spring away from a wall. Without
		 * it, the tiles are taken as empty.
		 */
		Death::Containers::Function<bool(const nCine::AABBf&)> IsAreaEmpty;

		/** @brief Returns how an event is drawn, `false` if it's drawn as a box */
		static bool GetPreview(const Formats::EventRecord& event, EventPreviewSpec& spec);

		/**
		 * @brief Draws an event placed at a tile
		 *
		 * @param painter    Painter
		 * @param event      The event, unknown types are drawn too
		 * @param tileRect   Rectangle the tile takes on the screen
		 * @param scale      Pixels of the screen a pixel of the level takes
		 * @param alpha      Opacity
		 * @param animTime   Time for animated sprites
		 * @param spawnPos   Where the object is spawned in the level, some objects face the way their position says
		 * @param inScene    Whether its sprite is drawn in the level instead (see @ref DrawInScene()), only the mark of
		 *                   the tile it's at is drawn then
		 */
		void DrawPlaced(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime,
			Vector2f spawnPos, bool inScene = false);
		/**
		 * @brief Draws the sprite of an event placed in the level into a canvas of the level, in pixels of the level
		 *
		 * It's drawn at @p z among the layers of the level, so layers in front of it cover it as they cover the objects
		 * of the game. Returns `false` for an event that has no sprite there, see @ref HasSceneSprite().
		 */
		bool DrawInScene(UI::Canvas& canvas, const Formats::EventRecord& event, Vector2f spawnPos, float alpha, float animTime, std::uint16_t z);
		/** @brief Returns `true` if the sprite of an event is drawn by @ref DrawInScene(), a start of the level isn't */
		bool HasSceneSprite(const Formats::EventRecord& event);
		/** @brief Draws an event fitted into a rectangle, as an item of a list */
		void DrawFitted(UiPainter& painter, const Formats::EventRecord& event, const Rectf& rect, float animTime, float alpha = 1.0f);
		/** @brief Draws a new event of a type fitted into a rectangle */
		void DrawFitted(UiPainter& painter, std::uint16_t type, const Rectf& rect, float animTime, float alpha = 1.0f);
		/** @brief Releases all loaded sprites */
		void Clear();

		/** @brief Returns the color of a category */
		static Colorf GetCategoryColor(EventCategory category);
		/** @brief Returns the translated name of an event type, or its number if it isn't known */
		static String GetEventName(std::uint16_t type);

	private:
		struct SpriteEntry
		{
			// Copied, a description of an event from outside of the game can go away
			String Metadata;
			std::uint32_t Animation;
			GraphicResource* Resource;
		};

		// Sprites of each event type, an event type has a few at most; the last one for types the game doesn't know
		SmallVector<SmallVector<SpriteEntry, 1>, 0> _sprites;
		SmallVector<Metadata*, 0> _pinned;

		// Where sprites go: the painter of the interface, a position in the level shown at Origin + position * Scale, or
		// a canvas of the level, in its pixels at a depth
		struct DrawTarget
		{
			UiPainter* Painter = nullptr;
			UI::Canvas* Canvas = nullptr;
			std::uint16_t Z = 0;
			Vector2f Origin;
			float Scale = 1.0f;
		};

		// A part an object adds to itself when it's spawned, drawn with it at a depth relative to it as the game does
		struct ExtraPart
		{
			EventPreviewSpec Spec;
			std::int32_t Depth;
		};

		GraphicResource* GetSprite(std::uint16_t type, const EventPreviewSpec& spec);
		/** @brief Appends the parts drawn with the sprite of an event, sorted from the furthest one */
		static void GetExtraParts(const Formats::EventRecord& event, const EventPreviewSpec& spec, float animTime, SmallVectorImpl<ExtraPart>& parts);
		/** @brief Finds how an event at a position is drawn, as @ref GetPreview() with what its position changes */
		bool GetPlacedPreview(const Formats::EventRecord& event, Vector2f spawnPos, EventPreviewSpec& spec);
		/** @brief Draws the characters a start is for, returns `false` if it isn't a start */
		bool DrawStart(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime);
		/** @brief Draws the sprite of an event at its spawn position, all pieces of a bridge and the parts an object adds to itself */
		void DrawPlacedSprite(const DrawTarget& target, const Formats::EventRecord& event, GraphicResource* resource, const EventPreviewSpec& spec,
			Vector2f spawnPos, float alpha, float animTime);
		void DrawExtraPart(const DrawTarget& target, std::uint16_t type, const ExtraPart& part, Vector2f spawnPos, float alpha, float animTime);
		/** @brief Draws the sprite of an event and its parts fitted into a rectangle together */
		void DrawFittedWithParts(UiPainter& painter, std::uint16_t type, GraphicResource* resource, const EventPreviewSpec& spec,
			ArrayView<const ExtraPart> parts, const Rectf& rect, float animTime, float alpha);
		/** @brief Draws a frame at its hotspot, or centered at the position, the current frame for -1 */
		void DrawSprite(const DrawTarget& target, GraphicResource* resource, const EventPreviewSpec& spec, Vector2f pos, float alpha, float animTime,
			std::int32_t frame = -1, bool centered = false);
		void DrawBox(UiPainter& painter, std::uint16_t type, const Rectf& rect, float alpha, bool withLabel);
	};
}

#endif
