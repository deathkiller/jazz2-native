#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../EventType.h"
#include "../../Formats/LevelFile.h"

#include <Containers/ArrayView.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#if !defined(N_)
/** @brief Marks a text of the event catalog for translation, it's translated where it's shown */
#	define N_(text) text
#endif

namespace Jazz2::Editor
{
	using Death::Containers::ArrayView;
	using Death::Containers::String;
	using Death::Containers::StringView;

	/** @brief Category of an event, the picker groups events by it */
	enum class EventCategory : std::uint8_t
	{
		Basic,				/**< Starts and checkpoints */
		Scenery,			/**< Destructible and collapsing tiles */
		Modifier,			/**< What a tile does --- vines, poles, hurting... */
		Area,				/**< What happens in an area */
		Trigger,			/**< Triggers */
		Warp,				/**< Warps */
		Light,				/**< Lights */
		Environment,		/**< Springs, platforms and other objects */
		Enemy,				/**< Enemies */
		Boss,				/**< Bosses */
		Collectible,		/**< Things to collect */
		Container,			/**< Crates, barrels and monitors */
		Other,				/**< Anything else */

		Count
	};

	/** @brief How a parameter of an event is edited */
	enum class ParamType : std::uint8_t
	{
		UInt,				/**< Unsigned number */
		Int,				/**< Signed number */
		Bool,				/**< Yes or no */
		Enum,				/**< One of the options */
		Flags,				/**< Any of the options, each option is a bit */
		EventType,			/**< Another event, 16 bits */
		TextId,				/**< Index of a level text */
		TriggerId,			/**< Index of a trigger, 0 to 31 */
		WarpId,				/**< Identifier of a warp, a warp origin goes to the target with the same one */
		String,				/**< Text of the given number of bytes, padded with zeros */
		Raw					/**< Bytes shown as they are */
	};

	/** @brief Unit of a parameter, shown next to its value */
	enum class ParamUnit : std::uint8_t
	{
		None,
		Pixels,
		Tiles,
		Frames,				/**< Frames at 60 FPS */
		Seconds,
		Percent,
		Degrees
	};

	/** @brief Option of a parameter of the type @ref ParamType::Enum or @ref ParamType::Flags */
	struct ParamOption
	{
		/** @brief Name shown to the user, marked by @ref N_() */
		const char* Label;
		/** @brief Value, or the mask of its bits for flags, relative to the field (before @ref EventParamField::Shift) */
		std::int32_t Value;
	};

	/**
		@brief Parameter of an event, a value stored in the 16 bytes of parameters of the event

		A value takes @ref Bits bits, starting at the bit @ref Shift of the little-endian value at the byte @ref Offset.
		A value of up to 8 bits is in one byte, up to 16 bits in two bytes, and up to 32 bits in four bytes.
	*/
	struct EventParamField
	{
		/** @brief Name shown to the user, marked by @ref N_() */
		const char* Name;
		/** @brief How the value is edited */
		ParamType Type;
		/** @brief Byte the value starts at */
		std::uint8_t Offset;
		/** @brief Number of bits of the value */
		std::uint8_t Bits;
		/** @brief First bit of the value */
		std::uint8_t Shift;
		/** @brief Whether the value is signed */
		bool Signed;
		/** @brief Smallest value */
		std::int32_t Min;
		/** @brief Largest value */
		std::int32_t Max;
		/** @brief Value of a new event */
		std::int32_t Default;
		/** @brief Options of an enumeration or flags */
		const ParamOption* Options;
		/** @brief Number of options */
		std::uint8_t OptionCount;
		/** @brief Unit */
		ParamUnit Unit;
		/** @brief Description shown as a tooltip, marked by @ref N_(), may be `nullptr` */
		const char* Description;

		/** @brief Returns the field with a unit */
		constexpr EventParamField WithUnit(ParamUnit unit) const {
			EventParamField copy = *this;
			copy.Unit = unit;
			return copy;
		}
		/** @brief Returns the field with a description */
		constexpr EventParamField WithDescription(const char* description) const {
			EventParamField copy = *this;
			copy.Description = description;
			return copy;
		}
		/** @brief Returns the field with a default value */
		constexpr EventParamField WithDefault(std::int32_t defaultValue) const {
			EventParamField copy = *this;
			copy.Default = defaultValue;
			return copy;
		}
	};

	/** @brief Unsigned number of @p bits bits at the byte @p offset, starting at the bit @p shift */
	constexpr EventParamField UIntField(const char* name, std::uint8_t offset, std::uint8_t bits, std::int32_t min, std::int32_t max, std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name, ParamType::UInt, offset, bits, shift, false, min, max, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Signed number of @p bits bits at the byte @p offset */
	constexpr EventParamField IntField(const char* name, std::uint8_t offset, std::uint8_t bits, std::int32_t min, std::int32_t max, std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name, ParamType::Int, offset, bits, shift, true, min, max, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Yes or no, the bit @p bit of the byte @p offset */
	constexpr EventParamField BoolField(const char* name, std::uint8_t offset, std::uint8_t bit = 0, bool defaultValue = false)
	{
		return { name, ParamType::Bool, offset, 1, bit, false, 0, 1, defaultValue ? 1 : 0, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief One of the options, @p bits bits at the byte @p offset */
	template<std::size_t N>
	constexpr EventParamField EnumField(const char* name, std::uint8_t offset, std::uint8_t bits, const ParamOption (&options)[N], std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name, ParamType::Enum, offset, bits, shift, false, 0, (1 << bits) - 1, defaultValue, options, std::uint8_t(N), ParamUnit::None, nullptr };
	}
	/** @brief Any of the options, each a bit of @p bits bits at the byte @p offset */
	template<std::size_t N>
	constexpr EventParamField FlagsField(const char* name, std::uint8_t offset, std::uint8_t bits, const ParamOption (&options)[N], std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name, ParamType::Flags, offset, bits, shift, false, 0, (1 << bits) - 1, defaultValue, options, std::uint8_t(N), ParamUnit::None, nullptr };
	}
	/** @brief Another event, 16 bits at the byte @p offset */
	constexpr EventParamField EventTypeField(const char* name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name, ParamType::EventType, offset, 16, 0, false, 0, 0xFFFF, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Index of a level text, 8 bits at the byte @p offset */
	constexpr EventParamField TextIdField(const char* name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name, ParamType::TextId, offset, 8, 0, false, 0, 255, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Index of a trigger, 8 bits at the byte @p offset */
	constexpr EventParamField TriggerIdField(const char* name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name, ParamType::TriggerId, offset, 8, 0, false, 0, 31, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Identifier of a warp, @p bits bits at the byte @p offset */
	constexpr EventParamField WarpIdField(const char* name, std::uint8_t offset, std::uint8_t bits = 8, std::int32_t defaultValue = 0)
	{
		return { name, ParamType::WarpId, offset, bits, 0, false, 0, (1 << bits) - 1, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Text of @p length bytes starting at the byte @p offset */
	constexpr EventParamField StringField(const char* name, std::uint8_t offset, std::uint8_t length)
	{
		return { name, ParamType::String, offset, std::uint8_t(length * 8), 0, false, 0, 0, 0, nullptr, 0, ParamUnit::None, nullptr };
	}

	/** @brief What an event is, helps the editor to show and check it */
	enum class EventTraits : std::uint16_t
	{
		None = 0,
		TileModifier = 0x01,		/**< Changes the tile it's placed on, it's not an object */
		Area = 0x02,				/**< Acts on the area of its tile, it's not an object */
		Object = 0x04,				/**< Spawns an object */
		Unused = 0x08,				/**< Not used by the game, kept only so levels that have it load */
		MultiplayerOnly = 0x10		/**< Means something only in multiplayer */
	};

	DEATH_ENUM_FLAGS(EventTraits);

	/**
		@brief Description of an event, how it's shown and how its parameters are edited
	*/
	struct EventDescriptor
	{
		/** @brief Type */
		EventType Type;
		/** @brief Identifier that doesn't change between versions */
		const char* Key;
		/** @brief Name shown to the user, marked by @ref N_() */
		const char* Name;
		/** @brief Category */
		EventCategory Category;
		/** @brief Description, marked by @ref N_(), may be `nullptr` */
		const char* Description;
		/** @brief Parameters */
		const EventParamField* Fields;
		/** @brief Number of parameters */
		std::uint8_t FieldCount;
		/** @brief Path of the metadata the event is previewed with, `nullptr` to show it as a box */
		const char* PreviewMetadata;
		/** @brief Animation of the metadata the event is previewed with (see @ref AnimState) */
		std::uint32_t PreviewAnimation;
		/** @brief What the event is */
		EventTraits Traits;
	};

	/** @brief Description of an event without parameters */
	constexpr EventDescriptor DescribeEvent(EventType type, const char* key, const char* name, EventCategory category, const char* description,
		const char* previewMetadata, std::uint32_t previewAnimation, EventTraits traits)
	{
		return { type, key, name, category, description, nullptr, 0, previewMetadata, previewAnimation, traits };
	}
	/** @brief Description of an event with parameters */
	template<std::size_t N>
	constexpr EventDescriptor DescribeEvent(EventType type, const char* key, const char* name, EventCategory category, const char* description,
		const EventParamField (&fields)[N], const char* previewMetadata, std::uint32_t previewAnimation, EventTraits traits)
	{
		return { type, key, name, category, description, fields, std::uint8_t(N), previewMetadata, previewAnimation, traits };
	}

	/** @brief Where descriptions of events from outside of the game come from, see @ref EventCatalog::LoadExtensions() */
	enum class EventExtensionScope : std::uint8_t
	{
		/** Event packs in `Editor/Events/` of the content, for all levels */
		Global,
		/** The script of the level being edited */
		Level
	};

	/**
		@brief All events the editor knows, how they are shown and how their parameters are edited

		The parameters are described as the game reads them --- by the actors the events spawn and the code that
		handles the tiles and areas --- not as the converter of the original levels writes them, so they are edited
		as the game understands them.

		Events can be described from outside of the game too, in JSON (see @ref LoadExtensions()): new ones that a
		script of a level reacts to, or ones the game knows that a script uses in another way. Those of the level
		replace those of event packs, which replace those of the game.
	*/
	class EventCatalog
	{
	public:
		EventCatalog() = delete;

		/**
		 * @brief Adds descriptions of events written in JSON
		 *
		 * The text is an object describing one event, an array of them, or an object with such an array in
		 * `"events"`. An event has a `"type"` (its number) and a `"name"`, and may have a `"key"`, a `"category"`
		 * (`"basic"`, `"scenery"`, `"modifier"`, `"area"`, `"trigger"`, `"warp"`, `"light"`, `"environment"`,
		 * `"enemy"`, `"boss"`, `"collectible"`, `"container"` or `"other"`), a `"description"`, a `"preview"` with
		 * `"metadata"` and `"animation"`, `"traits"` (`"object"`, `"tileModifier"`, `"area"`, `"multiplayerOnly"`)
		 * and `"params"`. A parameter has a `"name"`, a `"type"` (`"uint"`, `"int"`, `"bool"`, `"enum"`, `"flags"`,
		 * `"event"`, `"text"`, `"trigger"`, `"warp"`, `"string"`), the `"offset"` of its first byte, `"bits"`,
		 * `"shift"`, `"min"`, `"max"`, `"default"`, `"unit"` (`"pixels"`, `"tiles"`, `"frames"`, `"seconds"`,
		 * `"percent"`, `"degrees"`), `"description"` and `"options"` --- labels for the values from 0 on, or objects
		 * of a `"label"` and a `"value"`. What's wrong is logged with @p source and left out. Returns the number of
		 * events added.
		 */
		static std::int32_t LoadExtensions(StringView json, StringView source, EventExtensionScope scope);
		/** @brief Removes all descriptions of events added in a scope */
		static void ClearExtensions(EventExtensionScope scope);
		/** @brief Returns a number that changes whenever events are described or not anymore, descriptions found before are invalid then */
		static std::uint32_t GetRevision();
		/** @brief Returns whether an event is described from outside of the game */
		static bool IsExtension(const EventDescriptor* descriptor);

		/** @brief Returns the description of an event, or `nullptr` if it's not known */
		static const EventDescriptor* Find(EventType type);
		/** @brief Returns the description of an event by its key, or `nullptr` */
		static const EventDescriptor* FindByKey(StringView key);
		/**
		 * @brief Returns a level text as a single line to show, at most @p maxLength bytes long
		 *
		 * Its parts are joined by a slash, the control characters the original texts have are left out and line breaks
		 * become spaces, so it takes a single line of the font. A longer one is cut and ends with three dots.
		 */
		static String MakeTextPreview(StringView text, std::size_t maxLength);
		/** @brief Returns all described events */
		static ArrayView<const EventDescriptor* const> GetAll();
		/** @brief Returns the name of a category, translated */
		static StringView GetCategoryName(EventCategory category);

		/** @brief Reads a parameter of an event */
		static std::int32_t ReadField(const Formats::EventRecord& event, const EventParamField& field);
		/** @brief Writes a parameter of an event, the value is clamped to the range of the field */
		static void WriteField(Formats::EventRecord& event, const EventParamField& field, std::int32_t value);
		/** @brief Makes a new event of a type with the default values of its parameters */
		static Formats::EventRecord MakeDefault(EventType type);
		/** @brief Reads a text parameter */
		static StringView ReadString(const Formats::EventRecord& event, const EventParamField& field, char* buffer, std::size_t bufferSize);
	};

	// The parts of the catalog, one for each group of categories
	namespace EventCatalogParts
	{
		ArrayView<const EventDescriptor> GetBasicEvents();			// Basic, Scenery, Modifier
		ArrayView<const EventDescriptor> GetAreaEvents();			// Area, Trigger, Warp
		ArrayView<const EventDescriptor> GetEnvironmentEvents();	// Light, Environment
		ArrayView<const EventDescriptor> GetEnemyEvents();			// Enemy, Boss
		ArrayView<const EventDescriptor> GetItemEvents();			// Collectible, Container, Other
	}
}

#endif
