#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EventCatalog.h"
#include "EventPreviewResolver.h"
#include "../UI/UiPainter.h"
#include "../../../nCine/Primitives/AABB.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>
#include <unordered_map>

namespace nCine
{
	class AudioBufferPlayer;
}

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
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Resources::GraphicResource;
	using Resources::Metadata;
#endif

	class LevelDocument;

	/**
		@brief A line between events linked by the values of their parameters, see @ref EventPreviews::CollectLinks()
	*/
	struct EventLink
	{
		/** @brief Where the line starts, in pixels of the level --- tiles next to each other of the same event taken together */
		Rectf From;
		/** @brief Where the line ends, in pixels of the level, empty for a source with no target */
		Rectf To;
		/** @brief Channel of the link, as @ref EventParamField::LinkChannel names it, e.g. `"Warp"` or `"Trigger"` */
		const char* Channel;
		/** @brief The value the events share */
		std::int32_t Id;
		/** @brief Whether it leads from a source to a target, not between peers */
		bool Directed;
		/** @brief Whether the source has no target, @ref To is empty then */
		bool Missing;
	};

	/**
		@brief An arrow of an event along the values of its parameters, see @ref EventPreviews::CollectDirections()
	*/
	struct EventDirection
	{
		/** @brief The direction and the strength, in the units of the parameters, X to the right and Y down */
		Vector2f Vector;
		/** @brief Group of the arrow, see @ref EventParamField::DirectionGroup --- of a horizontal force 0 is the belt, 1 the
			accelerating belt and 2 the wind */
		std::uint8_t Group;
	};

	/**
		@brief How a light event lights up its surroundings, see @ref EventPreviews::GetLight()
	*/
	struct EventLightShape
	{
		/** @brief How much it lights up the darkness, 0 to 1 */
		float Intensity = 0.0f;
		/** @brief How much it glows above the normal light, 0 to 1 */
		float Brightness = 0.0f;
		/** @brief Fully lit up to this distance, in pixels */
		float RadiusNear = 0.0f;
		/** @brief Fades out up to this distance, in pixels */
		float RadiusFar = 0.0f;
		/** @brief How much the near radius grows and shrinks, both radii move with it, in pixels */
		float Pulse = 0.0f;
		/** @brief How fast it grows and shrinks, in radians per frame of the game, see @ref EventPreviews::GetLightPulse() */
		float PulseSpeed = 0.0f;
		/** @brief Where the pulse starts as the level starts, in radians */
		float PulsePhase = 0.0f;
		/** @brief Whether the light flickers, its far radius goes between the near and the far one */
		bool Flickers = false;
	};

	/**
		@brief Draws events as the level editor shows them

		An event is drawn as the object it spawns looks with its parameters (see @ref ResolveEventPreview()), or as
		the catalog shows its type (see @ref EventDescriptor::PreviewMetadata). An event the game knows that is
		described from outside of the game (see @ref EventCatalog::IsExtension()) is drawn only as its description
		says, its parameters may mean something else there. Any other event is drawn as a box of the color of its
		category with its name in it. Sprites are loaded the first time they are drawn, and kept
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
		 * @param offGrid    Whether it's an event off the grid, centered in @p tileRect, its mark is a triangle instead of
		 *                   a square then
		 */
		void DrawPlaced(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime,
			Vector2f spawnPos, bool inScene = false, bool offGrid = false);
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

		/**
		 * @brief Finds the lines between events linked by their parameters
		 *
		 * Events whose fields of the same channel (see @ref EventParamField::LinkChannel) have the same value are linked
		 * --- warp origins to the warp targets of their identifier, crates and zones to the tiles of their trigger,
		 * rolling rock triggers to the rocks of their ID, and whatever event packs describe. A source links to every
		 * target, a source without any target gets a link with @ref EventLink::Missing set, peers are linked in a chain.
		 * Events on tiles next to each other with the same value are one end. Off-grid events are an end of their own.
		 *
		 * With @p onlyCells, sorted tiles of the event grid, only the links of the events on them are found. At most
		 * @p maxLinks links are found, a level linking everything to everything would draw too many lines.
		 */
		static void CollectLinks(const LevelDocument& document, SmallVectorImpl<EventLink>& links,
			ArrayView<const std::uint32_t> onlyCells = {}, std::size_t maxLinks = 4096);
		/**
		 * @brief Finds the arrows of the track of a race, from each race waypoint to the next one and from the last one to the finish
		 *
		 * The waypoints of a group are run in ascending order, those of the same order and group are one, as the game merges
		 * them into their center, see `MpLevelHandler::ConsolidateOrderedRaceCheckpoints()`. An order that's skipped breaks
		 * the track there, as on the minimap of the game. The last waypoint of each group leads to what ends a lap ---
		 * a warp or a coin warp with Set Lap, or a level exit, the game counts a lap at each; those on tiles next to each other are one
		 * end. Off-grid waypoints are at their own position. The links are of the channel `"Race"` with the group as
		 * @ref EventLink::Id, at most @p maxLinks are found.
		 */
		static void CollectRaceTrack(const LevelDocument& document, SmallVectorImpl<EventLink>& links, std::size_t maxLinks = 4096);
		/**
		 * @brief Finds the arrows of an event, the values of its fields along their axes (see @ref EventParamField::Direction)
		 *
		 * Returns how many arrows there are, at most 4; an arrow of no strength isn't counted. The parameters of an event
		 * the game knows that is described from outside of the game point as its description says.
		 */
		static std::int32_t CollectDirections(const Formats::EventRecord& event, EventDirection (&directions)[4]);
		/** @brief Finds how a light event lights up its surroundings, returns `false` if it isn't a light */
		static bool GetLight(const Formats::EventRecord& event, EventLightShape& light);
		/**
		 * @brief Returns how much a light lights up a point at a distance, 0 to 1, as the game draws it
		 *
		 * Full up to the near radius, then falling off with the cube of the distance left to the far one, see the
		 * `LightingFs.inc` shader.
		 */
		static float GetLightStrength(float distance, float radiusNear, float radiusFar);
		/**
		 * @brief Returns how much a pulsing light has grown at a time, in pixels, negative while it's smaller
		 *
		 * Both of its radii grow by it, as the game pulses it, see @ref EventLightShape::Pulse. @p animTime is the time of
		 * the animations of the interface, as sprites are drawn at. A light that doesn't pulse doesn't grow.
		 */
		static float GetLightPulse(const EventLightShape& light, float animTime);
		/**
		 * @brief Returns the length of the arrow of a direction of a strength (see @ref CollectDirections()), in pixels of the level
		 *
		 * Longer for a stronger one, but less and less, so a strong one doesn't reach far across the level. Its tip is
		 * where it's dragged by, see @ref EventGizmoKind::Direction.
		 */
		static float GetDirectionLength(float strength);
		/**
		 * @brief Returns where the arrow of a direction (see @ref CollectDirections()) of an event at a position ends, in pixels of the level
		 *
		 * The arrow starts at @p center, the position of the event in the level, not the middle of its tile as the sprite
		 * layer shows it, and its handle is at the tip (see @ref EventGizmoKind::Direction), so both are found here. A
		 * direction of no strength ends at @p center.
		 */
		static Vector2f GetDirectionTip(Vector2f center, Vector2f direction);
		/** @brief Returns the strength of a direction whose arrow is of a length in pixels of the level, the inverse of @ref GetDirectionLength() */
		static float GetDirectionStrength(float length);
		/**
		 * @brief Describes the level an exit goes to when it doesn't name one
		 *
		 * The next level of an exit of the game is a text, 0 goes to the next level of the properties of the level, a
		 * bonus exit to the secret level, see `LevelHandler::ResolveNextLevelName()`. Returns `false` if @p field of the
		 * event isn't such a name, or a text is given. With @p brief, it's short enough to fit under the field.
		 */
		static bool DescribeDefaultLevel(const LevelDocument& document, const Formats::EventRecord& event, const EventParamField& field, String& text,
			bool brief = false);

		/** @brief Returns `true` if an event plays a sound that can be heard in the editor, see @ref UpdateSound() */
		static bool HasSound(const Formats::EventRecord& event);
		/**
		 * @brief Plays the sound of an event over and over, as loud as the game plays it, or stops it with `nullptr`
		 *
		 * Called every frame with the event that's heard. The sound already playing goes on, it's changed as soon as the
		 * event plays another one or at another volume. Only an ambient sound has one, nothing is played while sounds are
		 * muted in the options of the game.
		 */
		void UpdateSound(const Formats::EventRecord* event);
		/** @brief Stops the sound played by @ref UpdateSound(), as the level starts to be played */
		void StopSound();

		/** @brief Returns the color of a category */
		static Colorf GetCategoryColor(EventCategory category);
		/** @brief Returns the translated name of an event type, or its number if it isn't known */
		static String GetEventName(std::uint16_t type, const std::uint8_t* params = nullptr);

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
		// The sound of the event heard, see UpdateSound(); its name is a literal, it's kept even if it can't be played
		std::shared_ptr<nCine::AudioBufferPlayer> _sound;
		Death::Containers::StringView _soundIdentifier;
		float _soundGain = 0.0f;

		// How the name of an event fits into a box of a size, its one or two lines at a size of the text, a size of 0
		// if nothing fits
		struct BoxLabel
		{
			String Name;
			String First;
			String Second;
			float Size;
		};

		// Layouts of labels by the type, the size of the box a few percent apart and the scale of the interface; a few
		// sizes of boxes are drawn at a time, a long zoom goes through many, so it's emptied once it gets large
		static constexpr std::size_t MaxBoxLabels = 4096;
		std::unordered_map<std::uint64_t, BoxLabel> _boxLabels;

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
		bool DrawStart(UiPainter& painter, const Formats::EventRecord& event, const Rectf& tileRect, float scale, float alpha, float animTime, bool offGrid);
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
		void DrawBox(UiPainter& painter, std::uint16_t type, const std::uint8_t* params, const Rectf& rect, float alpha, bool withLabel);
		static void LayoutBoxLabel(UiPainter& painter, StringView name, const Rectf& inner, float boxWidth, BoxLabel& label);
	};
}

#endif
