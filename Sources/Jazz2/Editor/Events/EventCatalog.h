#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../EventType.h"
#include "../../Formats/LevelFile.h"

#include <Containers/ArrayView.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <cstddef>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Text of a description of an event, a literal is shown as it is, a text translated by `_x()` is kept

		The descriptions of the game are built on their first use, after the language is loaded, so only what's
		translated there by `_x("editor", ...)` is translated. Such a text is copied, it stays valid when the language
		is unloaded --- the editor keeps the language it was first opened in.
	*/
	struct CatalogText
	{
		/** @brief Text, `nullptr` for none */
		const char* Text;

		/** @brief No text */
		constexpr CatalogText(std::nullptr_t) noexcept : Text(nullptr) {}
		/** @brief Text shown as it is */
		constexpr CatalogText(const char* text) noexcept : Text(text) {}
		/** @brief Translated text, it's copied */
		CatalogText(StringView translated);
	};

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
		Raw,				/**< Bytes shown as they are */
		Contents			/**< Parameters of another event inside this one, as a crate has them, see @ref EventParamField::ContentsTypeField */
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
		/** @brief Name shown to the user, see @ref CatalogText */
		const char* Label = nullptr;
		/** @brief Value, or the mask of its bits for flags, relative to the field (before @ref EventParamField::Shift) */
		std::int32_t Value = 0;

		constexpr ParamOption() noexcept = default;
		/** @brief Option with a name and a value */
		constexpr ParamOption(CatalogText label, std::int32_t value) noexcept : Label(label.Text), Value(value) {}
	};

	/** @brief How the value of another field decides whether a field is shown, see @ref EventParamField::VisibleIfField */
	enum class FieldCondition : std::uint8_t
	{
		Equals,				/**< The value is the given one */
		NotEquals,			/**< The value isn't the given one */
		AnyBits				/**< Any of the given bits is set */
	};

	/** @brief What mirroring an event does to a value, see @ref EventCatalog::MirrorParams() */
	enum class MirrorMode : std::uint8_t
	{
		None,				/**< The value stays */
		Negate,				/**< The value turns negative, as a speed or an offset; a value whose negation is out of the range stays, so mirroring twice always gives it back */
		Toggle,				/**< A yes becomes a no and the other way, as facing left */
		Swap,				/**< Values, or bits of flags, are swapped in pairs, see @ref FieldMirror::Pairs */
		SwapField			/**< The value is swapped with another field, as a belt to the left with one to the right */
	};

	/** @brief How a value changes when its event is mirrored one way */
	struct FieldMirror
	{
		/** @brief What happens */
		MirrorMode Mode = MirrorMode::None;
		/** @brief Index of the other field of @ref MirrorMode::SwapField */
		std::int8_t Field = -1;
		/** @brief Pairs of values of @ref MirrorMode::Swap, `{ a, b, c, d }` swaps a with b and c with d; bit masks for flags */
		const std::int32_t* Pairs = nullptr;
		/** @brief Number of values in @ref Pairs */
		std::uint8_t PairCount = 0;
	};

	/** @brief A handle in the level that changes a value, see @ref EventGizmos */
	enum class FieldGizmo : std::uint8_t
	{
		None,
		Radius,				/**< A distance from the event in any direction */
		LengthDown,			/**< A distance below the event, as a chain hangs */
		WidthRight,			/**< A distance to the right of the event, as a bridge goes */
		OffsetX,			/**< A horizontal offset from the center of the tile, paired with the next @ref OffsetY field */
		OffsetY				/**< The vertical offset paired with an @ref OffsetX field */
	};

	/** @brief What a link of an event to others with the same value is, see @ref EventPreviews::CollectLinks() */
	enum class EventLinkRole : std::uint8_t
	{
		None,
		Source,				/**< It acts on the targets, as a warp origin or a trigger crate */
		Target,				/**< It's acted on, as a warp target or a trigger area */
		Peer				/**< It's linked to the others without a direction */
	};

	/** @brief Axis a value points an arrow along, see @ref EventPreviews::CollectDirections() */
	enum class DirectionAxis : std::uint8_t
	{
		None,
		X,					/**< Horizontal, positive to the right */
		Y					/**< Vertical, positive down */
	};

	/**
		@brief Parameter of an event, a value stored in the 16 bytes of parameters of the event

		A value takes @ref Bits bits, starting at the bit @ref Shift of the little-endian value at the byte @ref Offset.
		A value of up to 8 bits is in one byte, up to 16 bits in two bytes, and up to 32 bits in four bytes.
	*/
	struct EventParamField
	{
		/** @brief Name shown to the user, see @ref CatalogText */
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
		/** @brief Description shown as a tooltip, see @ref CatalogText, may be `nullptr` */
		const char* Description;

		// What follows has defaults, the functions making fields above leave it as it is

		/** @brief Index of the field whose value decides whether this one is shown, -1 for always, see @ref EventCatalog::IsFieldVisible() */
		std::int8_t VisibleIfField = -1;
		/** @brief How the value of @ref VisibleIfField is compared with @ref VisibleIfValue */
		FieldCondition VisibleIfMode = FieldCondition::Equals;
		/** @brief Value the one of @ref VisibleIfField is compared with, a mask of bits for @ref FieldCondition::AnyBits */
		std::int32_t VisibleIfValue = 0;
		/** @brief Never shown, the value is only kept as it is */
		bool Hidden = false;
		/** @brief The value is shown multiplied by this, the steps of a bridge as pixels, see @ref EventCatalog::ToDisplayValue() */
		std::int32_t DisplayScale = 1;
		/** @brief Of @ref ParamType::Contents, index of the field with the type of the event inside */
		std::int8_t ContentsTypeField = -1;
		/** @brief What mirroring the event horizontally does to the value */
		FieldMirror MirrorX;
		/** @brief What mirroring the event vertically does to the value */
		FieldMirror MirrorY;
		/** @brief The value is a team, see @ref EventCatalog::SwapTeamParams() */
		bool IsTeam = false;
		/** @brief Handle in the level that changes the value */
		FieldGizmo Gizmo = FieldGizmo::None;
		/** @brief Pixels of the level a unit of the value takes for its handle */
		std::int16_t GizmoStep = 1;
		/** @brief Events with the same value of a field of the same channel are linked, `nullptr` for none */
		const char* LinkChannel = nullptr;
		/** @brief What the event is in its links */
		EventLinkRole LinkRole = EventLinkRole::None;
		/** @brief Axis the value points an arrow along */
		DirectionAxis Direction = DirectionAxis::None;
		/** @brief Whether a positive value points the other way along the axis */
		bool DirectionNegative = false;
		/** @brief Arrow the value adds to, the values of an arrow are summed up --- a belt, an accelerating belt and a wind */
		std::uint8_t DirectionGroup = 0;

		/** @brief Returns the field with a unit */
		constexpr EventParamField WithUnit(ParamUnit unit) const {
			EventParamField copy = *this;
			copy.Unit = unit;
			return copy;
		}
		/** @brief Returns the field with a description */
		constexpr EventParamField WithDescription(CatalogText description) const {
			EventParamField copy = *this;
			copy.Description = description.Text;
			return copy;
		}
		/** @brief Returns the field with a default value */
		constexpr EventParamField WithDefault(std::int32_t defaultValue) const {
			EventParamField copy = *this;
			copy.Default = defaultValue;
			return copy;
		}
		/** @brief Returns the field shown only while another one, by its index, has a value */
		constexpr EventParamField ShowIf(std::int8_t field, std::int32_t value) const {
			EventParamField copy = *this;
			copy.VisibleIfField = field;
			copy.VisibleIfMode = FieldCondition::Equals;
			copy.VisibleIfValue = value;
			return copy;
		}
		/** @brief Returns the field shown only while another one, by its index, doesn't have a value */
		constexpr EventParamField ShowIfNot(std::int8_t field, std::int32_t value) const {
			EventParamField copy = *this;
			copy.VisibleIfField = field;
			copy.VisibleIfMode = FieldCondition::NotEquals;
			copy.VisibleIfValue = value;
			return copy;
		}
		/** @brief Returns the field never shown, its value is kept */
		constexpr EventParamField Hide() const {
			EventParamField copy = *this;
			copy.Hidden = true;
			return copy;
		}
		/** @brief Returns the field shown multiplied by a scale */
		constexpr EventParamField WithScale(std::int32_t scale) const {
			EventParamField copy = *this;
			copy.DisplayScale = scale;
			return copy;
		}
		/** @brief Returns the field changed by mirroring the event horizontally */
		constexpr EventParamField MirroredX(MirrorMode mode, std::int8_t otherField = -1) const {
			EventParamField copy = *this;
			copy.MirrorX.Mode = mode;
			copy.MirrorX.Field = otherField;
			return copy;
		}
		/** @brief Returns the field whose values are swapped in pairs by mirroring the event horizontally */
		template<std::size_t N>
		constexpr EventParamField MirroredXPairs(const std::int32_t (&pairs)[N]) const {
			EventParamField copy = *this;
			copy.MirrorX.Mode = MirrorMode::Swap;
			copy.MirrorX.Pairs = pairs;
			copy.MirrorX.PairCount = std::uint8_t(N);
			return copy;
		}
		/** @brief Returns the field changed by mirroring the event vertically */
		constexpr EventParamField MirroredY(MirrorMode mode, std::int8_t otherField = -1) const {
			EventParamField copy = *this;
			copy.MirrorY.Mode = mode;
			copy.MirrorY.Field = otherField;
			return copy;
		}
		/** @brief Returns the field whose values are swapped in pairs by mirroring the event vertically */
		template<std::size_t N>
		constexpr EventParamField MirroredYPairs(const std::int32_t (&pairs)[N]) const {
			EventParamField copy = *this;
			copy.MirrorY.Mode = MirrorMode::Swap;
			copy.MirrorY.Pairs = pairs;
			copy.MirrorY.PairCount = std::uint8_t(N);
			return copy;
		}
		/** @brief Returns the field marked as a team */
		constexpr EventParamField AsTeam() const {
			EventParamField copy = *this;
			copy.IsTeam = true;
			return copy;
		}
		/** @brief Returns the field with a handle in the level, @p step pixels for a unit */
		constexpr EventParamField WithGizmo(FieldGizmo gizmo, std::int16_t step = 1) const {
			EventParamField copy = *this;
			copy.Gizmo = gizmo;
			copy.GizmoStep = step;
			return copy;
		}
		/** @brief Returns the field linking the event to others with the same value in a channel */
		constexpr EventParamField WithLink(const char* channel, EventLinkRole role) const {
			EventParamField copy = *this;
			copy.LinkChannel = channel;
			copy.LinkRole = role;
			return copy;
		}
		/** @brief Returns the field pointing an arrow along an axis, values of the same group are summed */
		constexpr EventParamField WithDirection(DirectionAxis axis, bool negative = false, std::uint8_t group = 0) const {
			EventParamField copy = *this;
			copy.Direction = axis;
			copy.DirectionNegative = negative;
			copy.DirectionGroup = group;
			return copy;
		}
	};

	/** @brief Unsigned number of @p bits bits at the byte @p offset, starting at the bit @p shift */
	constexpr EventParamField UIntField(CatalogText name, std::uint8_t offset, std::uint8_t bits, std::int32_t min, std::int32_t max, std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name.Text, ParamType::UInt, offset, bits, shift, false, min, max, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Signed number of @p bits bits at the byte @p offset */
	constexpr EventParamField IntField(CatalogText name, std::uint8_t offset, std::uint8_t bits, std::int32_t min, std::int32_t max, std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name.Text, ParamType::Int, offset, bits, shift, true, min, max, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Yes or no, the bit @p bit of the byte @p offset */
	constexpr EventParamField BoolField(CatalogText name, std::uint8_t offset, std::uint8_t bit = 0, bool defaultValue = false)
	{
		return { name.Text, ParamType::Bool, offset, 1, bit, false, 0, 1, defaultValue ? 1 : 0, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief One of the options, @p bits bits at the byte @p offset */
	template<std::size_t N>
	constexpr EventParamField EnumField(CatalogText name, std::uint8_t offset, std::uint8_t bits, const ParamOption (&options)[N], std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name.Text, ParamType::Enum, offset, bits, shift, false, 0, (1 << bits) - 1, defaultValue, options, std::uint8_t(N), ParamUnit::None, nullptr };
	}
	/** @brief Any of the options, each a bit of @p bits bits at the byte @p offset */
	template<std::size_t N>
	constexpr EventParamField FlagsField(CatalogText name, std::uint8_t offset, std::uint8_t bits, const ParamOption (&options)[N], std::int32_t defaultValue = 0, std::uint8_t shift = 0)
	{
		return { name.Text, ParamType::Flags, offset, bits, shift, false, 0, (1 << bits) - 1, defaultValue, options, std::uint8_t(N), ParamUnit::None, nullptr };
	}
	/** @brief Another event, 16 bits at the byte @p offset */
	constexpr EventParamField EventTypeField(CatalogText name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name.Text, ParamType::EventType, offset, 16, 0, false, 0, 0xFFFF, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Index of a level text, 8 bits at the byte @p offset */
	constexpr EventParamField TextIdField(CatalogText name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name.Text, ParamType::TextId, offset, 8, 0, false, 0, 255, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Index of a trigger, 8 bits at the byte @p offset */
	constexpr EventParamField TriggerIdField(CatalogText name, std::uint8_t offset, std::int32_t defaultValue = 0)
	{
		return { name.Text, ParamType::TriggerId, offset, 8, 0, false, 0, 31, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Identifier of a warp, @p bits bits at the byte @p offset */
	constexpr EventParamField WarpIdField(CatalogText name, std::uint8_t offset, std::uint8_t bits = 8, std::int32_t defaultValue = 0)
	{
		return { name.Text, ParamType::WarpId, offset, bits, 0, false, 0, (1 << bits) - 1, defaultValue, nullptr, 0, ParamUnit::None, nullptr };
	}
	/** @brief Text of @p length bytes starting at the byte @p offset */
	constexpr EventParamField StringField(CatalogText name, std::uint8_t offset, std::uint8_t length)
	{
		return { name.Text, ParamType::String, offset, std::uint8_t(length * 8), 0, false, 0, 0, 0, nullptr, 0, ParamUnit::None, nullptr };
	}
	/**
	 * @brief Parameters of another event inside this one, @p length bytes from the byte @p offset
	 *
	 * The type of the event inside is in the field of the index @p typeField, its own parameters are edited shifted by
	 * @p offset bytes, see @ref EventCatalog::GetContents().
	 */
	constexpr EventParamField ContentsField(CatalogText name, std::uint8_t offset, std::uint8_t length, std::int8_t typeField)
	{
		EventParamField field = { name.Text, ParamType::Contents, offset, std::uint8_t(length * 8), 0, false, 0, 0, 0, nullptr, 0, ParamUnit::None, nullptr };
		field.ContentsTypeField = typeField;
		return field;
	}

	/**
		@brief How an event looks for a value of one of its parameters, see @ref EventDescriptor::PreviewCases
	*/
	struct EventPreviewCase
	{
		/** @brief Value the case is for, @ref AnyValue for all the others */
		std::int32_t Value;
		/** @brief Path of the metadata, `nullptr` to show the event as a box */
		const char* Metadata;
		/** @brief Animation of the metadata (see @ref AnimState) */
		std::uint32_t Animation;
		/** @brief Whether the sprite is mirrored horizontally */
		bool FlipX = false;
		/** @brief Horizontal offset of the sprite from where the object is spawned, in pixels */
		std::int16_t OffsetX = 0;
		/** @brief Vertical offset of the sprite from where the object is spawned, in pixels */
		std::int16_t OffsetY = 0;

		/** @brief @ref Value of the case for the values that no other case is for */
		static constexpr std::int32_t AnyValue = INT32_MIN;
	};

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
		/** @brief Name shown to the user, see @ref CatalogText */
		const char* Name;
		/** @brief Category */
		EventCategory Category;
		/** @brief Description, see @ref CatalogText, may be `nullptr` */
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

		// What follows has defaults, DescribeEvent() leaves it as it is

		/**
		 * @brief How the event looks by the value of one of its parameters, `nullptr` to look as @ref PreviewMetadata says
		 *
		 * The case of the value of the field @ref PreviewField is taken, or the one for @ref EventPreviewCase::AnyValue,
		 * or @ref PreviewMetadata if there's neither. Without a field (-1), the case for any value is taken.
		 */
		const EventPreviewCase* PreviewCases = nullptr;
		/** @brief Number of @ref PreviewCases */
		std::uint8_t PreviewCaseCount = 0;
		/** @brief Index of the field @ref PreviewCases are chosen by, -1 for none */
		std::int8_t PreviewField = -1;
		/** @brief Number of the event of the original game it describes, which @ref EventType::PassthroughEvent keeps, 0 for none */
		std::uint8_t OriginalEvent = 0;

		/** @brief Returns the description with the looks for the values of a field, by its index */
		template<std::size_t N>
		constexpr EventDescriptor WithPreviewCases(std::int8_t field, const EventPreviewCase (&cases)[N]) const {
			EventDescriptor copy = *this;
			copy.PreviewCases = cases;
			copy.PreviewCaseCount = std::uint8_t(N);
			copy.PreviewField = field;
			return copy;
		}
	};

	/** @brief Description of an event without parameters */
	constexpr EventDescriptor DescribeEvent(EventType type, const char* key, CatalogText name, EventCategory category, CatalogText description,
		const char* previewMetadata, std::uint32_t previewAnimation, EventTraits traits)
	{
		return { type, key, name.Text, category, description.Text, nullptr, 0, previewMetadata, previewAnimation, traits };
	}
	/** @brief Description of an event with parameters */
	template<std::size_t N>
	constexpr EventDescriptor DescribeEvent(EventType type, const char* key, CatalogText name, EventCategory category, CatalogText description,
		const EventParamField (&fields)[N], const char* previewMetadata, std::uint32_t previewAnimation, EventTraits traits)
	{
		return { type, key, name.Text, category, description.Text, fields, std::uint8_t(N), previewMetadata, previewAnimation, traits };
	}

	/** @brief Where descriptions of events from outside of the game come from, see @ref EventCatalog::LoadExtensions() */
	enum class EventExtensionScope : std::uint8_t
	{
		/** Event packs in `Editor/Events/` of the content, for all levels */
		Global,
		/** The script of the level being edited */
		Level
	};

	/** @brief A description of an event from outside of the game that couldn't be used, see @ref EventCatalog::GetExtensionErrors() */
	struct EventExtensionError
	{
		/** @brief Where the description came from */
		EventExtensionScope Scope;
		/** @brief File the description is in */
		String Source;
		/** @brief What's wrong, translated */
		String Message;
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
		 * `"Events"`, the format is described in @ref editor-events-extensions-format. Keys are case-sensitive, the
		 * names given as values aren't.
		 *
		 * An event with an `"Original"` in place of the `"Type"` is an event of the original game the game has no use
		 * for and keeps as @ref EventType::PassthroughEvent for the scripts of the level, by its number there.
		 *
		 * The range of a number is cut to -16777216 to 16777216, the number box edits it as a float, which holds whole
		 * numbers exactly only up to that. A value of a wrong type is taken as missing. What's wrong is logged with
		 * @p source, kept in @ref GetExtensionErrors() and left out. Returns the number of events added.
		 */
		static std::int32_t LoadExtensions(StringView json, StringView source, EventExtensionScope scope);
		/**
		 * @brief Adds the descriptions of events of the original game that MLLE keeps in a level script
		 *
		 * Each is a line `///@Event <number>=<name>|<+ or ->|<category>|<label>|<label>|<parameters>...` as in JCS.ini,
		 * a parameter `Name:8` of 8 bits, `Name:-8` signed, `Name:c1` a check box or `Name:{A,B,C}2` with named values.
		 * They're added as with `"Original"` (see @ref LoadExtensions()), so they describe the passthrough events with
		 * their numbers, and they're offered in the list as well (see @ref GetPickedOriginal()). A line `///@Event <number>=`
		 * alone hides the event of the original game from the list, as MLLE does, so the event it becomes here is found
		 * only by searching (see @ref IsHidden()). Returns the number of events added.
		 */
		static std::int32_t LoadMlleEvents(StringView script, StringView source, EventExtensionScope scope);
		/**
		 * @brief Returns the number of the event of the original game a type of the list stands for, or 0
		 *
		 * The events of the original game the script of the level describes are offered by types of their own, from
		 * @ref FirstOriginalPickType on. @ref MakeDefault() makes one as it's placed.
		 */
		static std::uint8_t GetPickedOriginal(EventType type);
		/** @brief Returns `true` if the script of the level hides an event from the list, see @ref LoadMlleEvents() */
		static bool IsHidden(const EventDescriptor* descriptor);

		/** @brief First type of the list that stands for an event of the original game, see @ref GetPickedOriginal() */
		static constexpr std::uint16_t FirstOriginalPickType = 0xFF00;
		/** @brief Removes all descriptions of events added in a scope, and the errors found in them */
		static void ClearExtensions(EventExtensionScope scope);
		/** @brief Returns what was wrong with the descriptions of events from outside of the game, until they're cleared */
		static ArrayView<const EventExtensionError> GetExtensionErrors();
		/** @brief Returns a number that changes whenever events are described or not anymore, or errors are found, descriptions found before are invalid then */
		static std::uint32_t GetRevision();
		/**
		 * @brief Returns whether an event is described from outside of the game
		 *
		 * The editor knows how the game uses the parameters of its own events --- how they look, what handles they
		 * have in the level --- that's used only while an event isn't described from outside of the game.
		 */
		static bool IsExtension(const EventDescriptor* descriptor);
		/** @brief Returns `true` if an event is described by the level being edited, by its script (see @ref LoadMlleEvents()) */
		static bool IsLevelExtension(const EventDescriptor* descriptor);

		/** @brief Returns the description of an event, or `nullptr` if it's not known */
		static const EventDescriptor* Find(EventType type);
		/**
		 * @brief Returns the description of a placed event, or `nullptr` if it's not known
		 *
		 * A passthrough event (@ref EventType::PassthroughEvent) is described as the event of the original game it keeps,
		 * if it is (see `"Original"` in @ref LoadExtensions()). So is an event of the game with the parameters the converter
		 * makes of a described event of the original game, the parameters of the original are in its last bytes then.
		 */
		static const EventDescriptor* Find(EventType type, const std::uint8_t* params);
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
		/**
		 * @brief Makes a new event of a type with the default values of its parameters
		 *
		 * A type that stands for an event of the original game (see @ref GetPickedOriginal()) makes the event as the
		 * converter makes it, the passthrough event that keeps it or the event of the game it becomes.
		 */
		static Formats::EventRecord MakeDefault(EventType type);
		/** @brief Reads a text parameter */
		static StringView ReadString(const Formats::EventRecord& event, const EventParamField& field, char* buffer, std::size_t bufferSize);

		/** @brief Returns how an event looks by the value of the field of @ref EventDescriptor::PreviewCases, or `nullptr` if no case is for it */
		static const EventPreviewCase* FindPreviewCase(const EventDescriptor& descriptor, const Formats::EventRecord& event);
		/** @brief Returns whether a field, by its index, is shown for an event --- it isn't hidden and the condition on another field holds */
		static bool IsFieldVisible(const EventDescriptor& descriptor, std::size_t fieldIndex, const Formats::EventRecord& event);
		/** @brief Returns a value as it's shown, multiplied by @ref EventParamField::DisplayScale */
		static std::int32_t ToDisplayValue(const EventParamField& field, std::int32_t value);
		/** @brief Returns the value of a shown one, the closest step of @ref EventParamField::DisplayScale */
		static std::int32_t FromDisplayValue(const EventParamField& field, std::int32_t displayValue);

		/** @brief Returns the description of the event inside of a @ref ParamType::Contents field, or `nullptr` if there's none or it isn't known */
		static const EventDescriptor* GetContentsDescriptor(const EventDescriptor& descriptor, const EventParamField& contentsField, const Formats::EventRecord& event);
		/**
		 * @brief Returns the event inside of a @ref ParamType::Contents field as an event of its own
		 *
		 * Its type is the one of the field @ref EventParamField::ContentsTypeField, its parameters are the bytes of the
		 * field moved to the start, the rest of them zero. Its fields are read and written as of any event then, and
		 * stored back by @ref SetContents().
		 */
		static Formats::EventRecord GetContents(const EventDescriptor& descriptor, const EventParamField& contentsField, const Formats::EventRecord& event);
		/** @brief Stores the parameters of the event inside back into a @ref ParamType::Contents field */
		static void SetContents(const EventParamField& contentsField, Formats::EventRecord& event, const Formats::EventRecord& contents);
		/** @brief Returns whether a field of the event inside fits into the bytes of a @ref ParamType::Contents field */
		static bool FitsInContents(const EventParamField& contentsField, const EventParamField& innerField);
		/**
		 * @brief Writes the type of the event inside, and gives it the parameters of a new event of the type
		 *
		 * The parameters of the event inside mean something else for another type, so they are reset only when the
		 * type changes. A field that no @ref ParamType::Contents field takes its type from is just written.
		 */
		static void SetContentsType(const EventDescriptor& descriptor, const EventParamField& typeField, Formats::EventRecord& event, std::uint16_t type);

		/** @brief How deep events inside of events are followed, as a crate in a crate, see @ref ForEachContents() */
		static constexpr std::int32_t MaxContentsDepth = 8;

		/**
		 * @brief Calls a function for an event and for each event inside of it, as a crate has one, down to @ref MaxContentsDepth
		 *
		 * The function is called as @cpp bool(const EventDescriptor&, Formats::EventRecord&) @ce with an event inside
		 * as an event of its own (see @ref GetContents()), and returns `true` if it changed it; a changed event inside
		 * is stored back into the one it's in. The events inside are taken as they were before the function changed
		 * the event they're in. Descriptions of events from outside of the game can name each other as what's inside,
		 * the depth is what ends it then. Returns `true` if anything changed.
		 */
		template<class Callback>
		static bool ForEachContents(const EventDescriptor& descriptor, Formats::EventRecord& event, Callback&& callback, std::int32_t depth = 0);

		/**
		 * @brief Changes the parameters of an event as mirroring it horizontally or vertically does
		 *
		 * Each field changes as its @ref EventParamField::MirrorX or @ref EventParamField::MirrorY says --- the way a
		 * spring launches, the side a paddle is mounted on, speeds, offsets. The event inside a crate is mirrored too.
		 * Positions measured from the edges of the level don't change. Returns `true` if anything changed.
		 */
		static bool MirrorParams(const EventDescriptor* descriptor, std::uint8_t* params, bool horizontal);
		/** @overload */
		static bool MirrorParams(const EventDescriptor* descriptor, Formats::EventRecord& event, bool horizontal);
		/** @brief Returns whether mirroring changes any parameter of an event of a description */
		static bool CanMirror(const EventDescriptor* descriptor);
		/** @brief Swaps two teams, by their numbers, in the fields marked as teams, returns `true` if anything changed */
		static bool SwapTeamParams(const EventDescriptor* descriptor, std::uint8_t* params, std::int32_t first = 0, std::int32_t second = 1);
	};

	template<class Callback>
	bool EventCatalog::ForEachContents(const EventDescriptor& descriptor, Formats::EventRecord& event, Callback&& callback, std::int32_t depth)
	{
		Formats::EventRecord original = event;
		bool changed = callback(descriptor, event);
		if (depth >= MaxContentsDepth) {
			return changed;
		}
		for (std::uint8_t i = 0; i < descriptor.FieldCount; i++) {
			const EventParamField& field = descriptor.Fields[i];
			if (field.Type != ParamType::Contents) {
				continue;
			}
			const EventDescriptor* inner = GetContentsDescriptor(descriptor, field, original);
			if (inner == nullptr) {
				continue;
			}
			Formats::EventRecord contents = GetContents(descriptor, field, original);
			if (ForEachContents(*inner, contents, callback, depth + 1)) {
				SetContents(field, event, contents);
				changed = true;
			}
		}
		return changed;
	}

	// The parts of the catalog, one for each group of categories, each is built on its first call, the language must be loaded by then
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
