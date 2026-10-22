#pragma once

#include "../../Main.h"
#include "JJ2Event.h"
#include "../EventType.h"

#include <array>

#include "../../nCine/Base/HashMap.h"

using namespace nCine;

#include <Containers/ArrayView.h>
#include <Containers/Function.h>
#include <Containers/Pair.h>

namespace Jazz2::Compatibility
{
	class JJ2Level;

	/**
		@brief Converted extended event description
		
		Result of translating a single original @ref JJ2Event into the engine's native representation,
		pairing the resolved @ref Jazz2::EventType with the unpacked event parameter bytes. Produced by
		@ref EventConverter::TryConvert.
	*/
	struct ConversionResult {
		/** @brief Resulting event type */
		EventType Type;
		/** @brief Resulting event parameters */
		std::uint8_t Params[16];
	};

	/** @brief Parameter is unused */
	constexpr std::int32_t JJ2ParamNone = 0;
	/** @brief Parameter is a boolean value */
	constexpr std::int32_t JJ2ParamBool = 1;
	/** @brief Parameter is an unsigned integer value */
	constexpr std::int32_t JJ2ParamUInt = 2;
	/** @brief Parameter is a signed integer value */
	constexpr std::int32_t JJ2ParamInt = 3;

	/**
		@brief Maps original events to extended event descriptions
		
		Holds a registry of per-event conversion functions that turn an original JJ2 event ID and its
		packed parameter integer into the engine's native @ref EventType with unpacked parameters. Default
		converters are registered on construction and can be extended or overridden.
	*/
	class EventConverter
	{
	public:
		/** @brief Signature of a function that converts a single original event */
		using ConversionFunction = Function<ConversionResult(JJ2Level* level, std::uint32_t e)>;

		/** @brief Creates a new instance */
		EventConverter();

		/**
		 * @brief Converts the specified original event to an extended event description
		 *
		 * An event the game has no use for is kept as it is, see @ref MakePassthroughEvent(). Only @ref JJ2Event::EMPTY gives
		 * @ref EventType::Empty.
		 */
		ConversionResult TryConvert(JJ2Level* level, JJ2Event old, std::uint32_t eventParams);
		/** @brief Registers a converter for the specified original event */
		void Add(JJ2Event originalEvent, ConversionFunction&& converter);
		/** @brief Registers a converter for the specified original event, replacing any existing one */
		void Override(JJ2Event originalEvent, ConversionFunction&& converter);

		/** @brief Returns a converter that maps to the specified event with no parameters */
		ConversionFunction NoParamList(EventType ev);
		/** @brief Returns a converter that maps to the specified event with constant parameters */
		ConversionFunction ConstantParamList(EventType ev, const std::array<std::uint8_t, 16>& eventParams);
		/** @brief Returns a converter that maps to the specified event by extracting parameters from the original parameter integer */
		ConversionFunction ParamIntToParamList(EventType ev, const std::array<Pair<std::int32_t, std::int32_t>, 6>& paramDefs);

		/** @brief Number of bits of the parameters of an original event */
		static constexpr std::int32_t OriginalParamBits = 20;

		/** @brief Returns an original event as it is, as @ref EventType::PassthroughEvent */
		static ConversionResult MakePassthroughEvent(JJ2Event event, std::uint32_t eventParams);
		/** @brief Returns the original event and its parameters of the parameters of @ref EventType::PassthroughEvent */
		static void ReadPassthroughEvent(const std::uint8_t* params, JJ2Event& event, std::uint32_t& eventParams);

		/** @brief Returns whether the last bytes of the parameters of a converted event can keep the parameters of the original */
		static bool CanKeepOriginalParams(EventType type);
		/**
		 * @brief Keeps the parameters of the original event in the last bytes of the converted one, for the scripts of JJ2+
		 *
		 * The game reads none of the last three bytes of the parameters of any event, but the name of the music of
		 * @ref EventType::AreaActivateBoss, so the 20 bits of the original are kept there - scripts store data of their own
		 * in the bits the game ignores. The converted event's own parameters take precedence, they aren't kept if it
		 * uses those bytes. Returns `false` if they aren't kept.
		 */
		static bool KeepOriginalParams(EventType type, std::uint8_t* params, std::uint32_t eventParams);
		/**
		 * @brief Returns the parameters of the original event kept in a converted one, see @ref KeepOriginalParams()
		 *
		 * Returns `false` if there are none, a level converted before they were kept has none, and none are kept of an
		 * original event of no parameters.
		 */
		static bool ReadOriginalParams(EventType type, const std::uint8_t* params, std::uint32_t& eventParams);

		/** @brief Decodes a packed parameter integer into individual event parameters */
		static void ConvertParamInt(std::uint32_t paramInt, const ArrayView<const Pair<std::int32_t, std::int32_t>>& paramTypes, std::uint8_t eventParams[16]);

		/** @overload */
		static void ConvertParamInt(std::uint32_t paramInt, const std::initializer_list<const Pair<std::int32_t, std::int32_t>> paramTypes, std::uint8_t eventParams[16]) {
			ConvertParamInt(paramInt, arrayView(paramTypes), eventParams);
		}

	private:
		HashMap<JJ2Event, ConversionFunction> _converters;

		void AddDefaultConverters();

		static ConversionFunction GetSpringConverter(std::uint8_t type, bool horizontal, bool frozen);
		static ConversionFunction GetPlatformConverter(std::uint8_t type);
		static ConversionFunction GetPoleConverter(std::uint8_t theme);
		static ConversionFunction GetAmmoCrateConverter(std::uint8_t type);
		static ConversionFunction GetBossConverter(EventType ev, std::uint8_t customParam = 0);
	};
}