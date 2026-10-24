#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Compatibility/EventConverter.h"
#include "../../Compatibility/JJ2Level.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Scripting::Legacy
{
	/**
		@brief Translates the events of a level between the game and the original, for the level scripts of JJ2+

		Scripts of JJ2+ see the events of a level as the original stores them (`jjEventGet()`, `jjParameterGet()`), a
		converted level keeps only what the game made of them, so the original event and its parameters are worked out
		again. Every converter of @ref Compatibility::EventConverter is probed bit by bit for which bytes of the converted
		event each bit of the original parameters decides; the bits that decide the same bytes form a group, and each
		group is searched for the values that give the converted event. When no original event gives it exactly - the
		converter dropped a parameter the game doesn't use, or read something of the level - the event that comes closest
		is taken. The results are cached by the converted event, so a script that looks at every tile of the level pays
		a lookup per tile.

		An event word is laid out as in the original: the event ID in the low 8 bits, the difficulty in bits 8 and 9,
		the illuminate flag in bit 10 and the parameters from bit 12. The difficulty isn't kept by a converted level.
	*/
	class EventTranslator
	{
	public:
		/** @brief Bit of the event word the parameters begin at */
		static constexpr std::int32_t ParamsShift = 12;
		/** @brief Number of bits of the parameters */
		static constexpr std::int32_t ParamBits = 20;
		/** @brief Bit of the event word that makes the object of the event illuminated */
		static constexpr std::uint32_t IlluminateBit = (1u << 10);
		/** @brief Bit of the event word that tells the object of the event exists, so the level doesn't make another */
		static constexpr std::uint32_t ActiveBit = (1u << 11);

		/** @brief Creates a new instance, probing all the converters */
		EventTranslator();

		EventTranslator(const EventTranslator&) = delete;
		EventTranslator& operator=(const EventTranslator&) = delete;

		/** @brief Returns the event word of the original event that converts to an event of the game, 0 if there's none */
		std::uint32_t ToOriginal(EventType type, const std::uint8_t* params);
		/**
			@brief Converts an event of the original as the converter of levels does, but without the level

			The converters that read the level get an empty one.
		*/
		Compatibility::ConversionResult FromOriginal(std::uint8_t eventId, std::uint32_t params);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// Bits of the parameters that decide the same outputs, bit `i` of the outputs is byte `i` of the converted
		// parameters, bit 16 is the type
		struct BitGroup {
			std::uint32_t Bits;
			std::uint32_t Outputs;
		};

		struct EventProbe {
			Compatibility::ConversionResult Base;
			SmallVector<BitGroup, 4> Groups;
		};

		struct CachedEvent {
			std::uint16_t Type;
			std::uint8_t Params[16];
			std::uint32_t Word;
		};
#endif

		// Largest group of bits that is searched, 2^n conversions
		static constexpr std::int32_t MaxGroupBits = 16;
		static constexpr std::uint32_t TypeOutput = (1u << 16);

		Compatibility::EventConverter _converter;
		// Stands in for the level, the converters that read it get an empty one
		Compatibility::JJ2Level _level;
		EventProbe _probes[256];
		// The original events by the type of the game they convert to
		HashMap<std::uint16_t, SmallVector<std::uint8_t, 4>> _candidates;
		// What the converted events were found to be, by a hash of them
		HashMap<std::uint64_t, CachedEvent> _cache;

		// Returns which outputs differ between two converted events
		static std::uint32_t GetDifferences(const Compatibility::ConversionResult& a, const Compatibility::ConversionResult& b);
		// Returns the parameters of an original event that give the converted event, `matching` is the number of outputs
		// that come out the same (17 = exactly)
		std::uint32_t Solve(std::uint8_t eventId, EventType type, const std::uint8_t* params, std::int32_t& matching);
	};
}

#endif
