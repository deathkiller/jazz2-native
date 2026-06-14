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

	/** @brief Converted extended event description */
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

	/** @brief Maps original events to extended event descriptions */
	class EventConverter
	{
	public:
		/** @brief Signature of a function that converts a single original event */
		using ConversionFunction = Function<ConversionResult(JJ2Level* level, std::uint32_t e)>;

		/** @brief Creates a new instance */
		EventConverter();

		/** @brief Converts the specified original event to an extended event description */
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