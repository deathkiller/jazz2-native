#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusEvents.h"
#include "LevelScriptLoader.h"
#include "../LevelHandler.h"
#include "../Events/EventMap.h"

#include <algorithm>
#include <cstring>

namespace Jazz2::Scripting
{
	Legacy::EventTranslator& LevelScriptLoader::GetEventTranslator()
	{
		if (_eventTranslator == nullptr) {
			_eventTranslator = std::make_unique<Legacy::EventTranslator>();
		}
		return *_eventTranslator;
	}

	std::uint32_t LevelScriptLoader::GetOriginalEvent(std::int32_t x, std::int32_t y)
	{
		Events::EventMap* eventMap = _levelHandler->EventMap();
		if (eventMap == nullptr) {
			return 0;
		}
		Vector2i size = eventMap->GetSize();
		if (x < 0 || y < 0 || x >= size.X || y >= size.Y) {
			return 0;
		}

		const Events::EventMap::EventTile& tile = eventMap->GetEventTile(x, y);
		if (tile.Event == EventType::Empty) {
			// The fast way out, most tiles have no event
			if (!_writtenEvents.empty()) {
				auto written = _writtenEvents.find(x + y * size.X);
				if (written != _writtenEvents.end() && written->second.Type == EventType::Empty) {
					return written->second.Word;
				}
			}

			// The starts and the warp targets aren't kept in the tiles
			std::uint8_t markerParams[Formats::LevelFile::EventParamsSize];
			EventType markerType = eventMap->GetStartOrWarpTarget(x, y, markerParams);
			if (markerType != EventType::Empty) {
				return GetEventTranslator().ToOriginal(markerType, markerParams);
			}
			return 0;
		}

		if (!_writtenEvents.empty()) {
			auto written = _writtenEvents.find(x + y * size.X);
			if (written != _writtenEvents.end()) {
				if (written->second.Type == tile.Event && std::memcmp(written->second.Params, tile.EventParams, sizeof(written->second.Params)) == 0) {
					return written->second.Word;
				}
				// The tile changed since, e.g. its object was collected
				_writtenEvents.erase(written);
			}
		}

		std::uint32_t word;
		EventType generatedType; const std::uint8_t* generatedParams; std::uint8_t delay;
		if (eventMap->GetGeneratorEvent(x, y, generatedType, generatedParams, delay)) {
			// The generator says the event it spawns and its delay, the event itself has the same parameters
			std::uint32_t generatedWord = GetEventTranslator().ToOriginal(generatedType, generatedParams);
			word = std::uint32_t(Compatibility::JJ2Event::MODIFIER_GENERATOR) |
				(((generatedWord & 0xFF) | (std::uint32_t(delay) << 8)) << Legacy::EventTranslator::ParamsShift);
		} else {
			word = GetEventTranslator().ToOriginal(tile.Event, tile.EventParams);
		}

		if ((tile.EventFlags & Actors::ActorState::Illuminated) == Actors::ActorState::Illuminated) {
			word |= Legacy::EventTranslator::IlluminateBit;
		}
		return word;
	}

	void LevelScriptLoader::SetOriginalEvent(std::int32_t x, std::int32_t y, std::uint32_t eventWord)
	{
		Events::EventMap* eventMap = _levelHandler->EventMap();
		if (eventMap == nullptr) {
			return;
		}
		Vector2i size = eventMap->GetSize();
		if (x < 0 || y < 0 || x >= size.X || y >= size.Y) {
			return;
		}

		std::uint8_t eventId = std::uint8_t(eventWord & 0xFF);
		if (eventId == std::uint8_t(Compatibility::JJ2Event::MODIFIER_GENERATOR)) {
			// A generator has to be set up when the level is loaded, a new one can't be made
			LOGW("Cannot set a generator at [{}, {}] while the level is played", x, y);
			return;
		}

		Compatibility::ConversionResult converted = GetEventTranslator().FromOriginal(eventId, eventWord >> Legacy::EventTranslator::ParamsShift);
		// As the converter keeps them, see GetOriginalEvent()
		Compatibility::EventConverter::KeepOriginalParams(converted.Type, converted.Params, eventWord >> Legacy::EventTranslator::ParamsShift);
		Actors::ActorState flags = ((eventWord & Legacy::EventTranslator::IlluminateBit) != 0 ? Actors::ActorState::Illuminated : Actors::ActorState::None);
		eventMap->StoreTileEvent(x, y, converted.Type, flags, converted.Params);

		// What the script wrote is read back as it is, see GetOriginalEvent()
		WrittenEvent& written = _writtenEvents[x + y * size.X];
		written.Word = eventWord;
		written.Type = converted.Type;
		std::memcpy(written.Params, converted.Params, sizeof(written.Params));
	}
}

namespace Jazz2::Scripting::Legacy
{
	EventTranslator::EventTranslator()
	{
		for (std::int32_t eventId = 1; eventId < 256; eventId++) {
			EventProbe& probe = _probes[eventId];
			probe.Base = FromOriginal(std::uint8_t(eventId), 0);

			// The types the event converts to, most give always the same one; an event kept as it is is found without them,
			// see ToOriginal()
			SmallVector<std::uint16_t, 4> types;
			auto isConverted = [](EventType type) {
				return (type != EventType::Empty && type != EventType::PassthroughEvent);
			};
			if (isConverted(probe.Base.Type)) {
				types.push_back(std::uint16_t(probe.Base.Type));
			}

			for (std::int32_t bit = 0; bit < ParamBits; bit++) {
				Compatibility::ConversionResult result = FromOriginal(std::uint8_t(eventId), 1u << bit);
				std::uint32_t outputs = GetDifferences(probe.Base, result);
				if (outputs == 0) {
					// The bit isn't used
					continue;
				}

				if (isConverted(result.Type) && std::find(types.begin(), types.end(), std::uint16_t(result.Type)) == types.end()) {
					types.push_back(std::uint16_t(result.Type));
				}

				// The bit joins every group that decides one of its outputs, which merges them into one
				BitGroup merged = { 1u << bit, outputs };
				for (std::size_t i = 0; i < probe.Groups.size(); ) {
					if ((probe.Groups[i].Outputs & merged.Outputs) != 0) {
						merged.Bits |= probe.Groups[i].Bits;
						merged.Outputs |= probe.Groups[i].Outputs;
						probe.Groups.erase(probe.Groups.begin() + i);
					} else {
						i++;
					}
				}
				probe.Groups.push_back(merged);
			}

			for (std::uint16_t type : types) {
				auto it = _candidates.find(type);
				if (it == _candidates.end()) {
					it = _candidates.emplace(type, SmallVector<std::uint8_t, 4>()).first;
				}
				it->second.push_back(std::uint8_t(eventId));
			}
		}
	}

	std::uint32_t EventTranslator::ToOriginal(EventType type, const std::uint8_t* params)
	{
		if (type == EventType::Empty) {
			return 0;
		}

		// FNV-1a of the converted event
		std::uint64_t hash = 14695981039346656037ull;
		auto mix = [&hash](std::uint8_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(std::uint8_t(std::uint16_t(type) & 0xFF));
		mix(std::uint8_t(std::uint16_t(type) >> 8));
		for (std::int32_t i = 0; i < 16; i++) {
			mix(params[i]);
		}

		auto cached = _cache.find(hash);
		if (cached != _cache.end() && cached->second.Type == std::uint16_t(type) && std::memcmp(cached->second.Params, params, 16) == 0) {
			return cached->second.Word;
		}

		if (type == EventType::PassthroughEvent) {
			// The game doesn't use it, it's kept as it is
			Compatibility::JJ2Event eventId;
			std::uint32_t eventParams;
			Compatibility::EventConverter::ReadPassthroughEvent(params, eventId, eventParams);
			return std::uint32_t(eventId) | (eventParams << ParamsShift);
		}

		// The parameters of the original the converted event keeps in its last bytes (see EventConverter::KeepOriginalParams()),
		// they aren't any of its own, the converters never give them
		std::uint8_t converted[16];
		std::memcpy(converted, params, sizeof(converted));
		std::uint32_t keptParams = 0;
		const bool hasKeptParams = Compatibility::EventConverter::ReadOriginalParams(type, params, keptParams);
		if (Compatibility::EventConverter::CanKeepOriginalParams(type)) {
			std::memset(converted + Formats::EventRecord::OriginalParamsOffset, 0, sizeof(converted) - Formats::EventRecord::OriginalParamsOffset);
		}

		std::uint32_t word = 0;
		auto candidates = _candidates.find(std::uint16_t(type));
		bool found = false;
		if (candidates != _candidates.end() && hasKeptParams) {
			// The event that gives the converted one of the kept parameters is the one of the original
			for (std::uint8_t eventId : candidates->second) {
				Compatibility::ConversionResult result = FromOriginal(eventId, keptParams);
				if (result.Type == type && std::memcmp(result.Params, converted, sizeof(converted)) == 0) {
					word = eventId | (keptParams << ParamsShift);
					found = true;
					break;
				}
			}
		}
		if (candidates != _candidates.end() && !found) {
			// The original event that gives the converted one exactly, or else the one that comes closest. Several events can
			// give the same one (an ammo crate of the toaster, and an ammo crate with the toaster in its parameters), the one
			// with the fewest bits of the parameters set is the one levels use.
			std::int32_t bestMatching = -1;
			std::int32_t bestBitCount = INT32_MAX;
			for (std::uint8_t eventId : candidates->second) {
				std::int32_t matching;
				std::uint32_t eventParams = Solve(eventId, type, converted, matching);
				std::int32_t bitCount = 0;
				for (std::uint32_t bits = eventParams; bits != 0; bits &= bits - 1) {
					bitCount++;
				}
				if (matching > bestMatching || (matching == bestMatching && bitCount < bestBitCount)) {
					bestMatching = matching;
					bestBitCount = bitCount;
					word = eventId | ((hasKeptParams ? keptParams : eventParams) << ParamsShift);
				}
			}
		}

		CachedEvent& entry = _cache[hash];
		entry.Type = std::uint16_t(type);
		std::memcpy(entry.Params, params, 16);
		entry.Word = word;
		return word;
	}

	Compatibility::ConversionResult EventTranslator::FromOriginal(std::uint8_t eventId, std::uint32_t params)
	{
		Compatibility::ConversionResult result = _converter.TryConvert(&_level, Compatibility::JJ2Event(eventId), params);
		if (result.Type == EventType::Empty) {
			// The converters that give nothing leave the parameters as they are
			std::memset(result.Params, 0, sizeof(result.Params));
		}
		return result;
	}

	std::uint32_t EventTranslator::GetDifferences(const Compatibility::ConversionResult& a, const Compatibility::ConversionResult& b)
	{
		std::uint32_t outputs = (a.Type != b.Type ? TypeOutput : 0);
		for (std::int32_t i = 0; i < 16; i++) {
			if (a.Params[i] != b.Params[i]) {
				outputs |= (1u << i);
			}
		}
		return outputs;
	}

	std::uint32_t EventTranslator::Solve(std::uint8_t eventId, EventType type, const std::uint8_t* params, std::int32_t& matching)
	{
		Compatibility::ConversionResult target;
		target.Type = type;
		std::memcpy(target.Params, params, sizeof(target.Params));

		// Every group on its own, with the other bits clear - the group decides only its outputs, so the values found
		// for the groups go together
		const EventProbe& probe = _probes[eventId];
		std::uint32_t result = 0;
		for (const BitGroup& group : probe.Groups) {
			std::int32_t bitCount = 0;
			for (std::uint32_t bits = group.Bits; bits != 0; bits &= bits - 1) {
				bitCount++;
			}
			if (bitCount > MaxGroupBits) {
				continue;
			}

			// All the values of the bits of the group, by the usual trick for the subsets of a mask
			std::uint32_t value = 0;
			do {
				Compatibility::ConversionResult converted = FromOriginal(eventId, value);
				if ((GetDifferences(converted, target) & group.Outputs) == 0) {
					result |= value;
					break;
				}
				value = (value - group.Bits) & group.Bits;
			} while (value != 0);
		}

		Compatibility::ConversionResult converted = FromOriginal(eventId, result);
		std::uint32_t differences = GetDifferences(converted, target);
		matching = 17;
		for (; differences != 0; differences &= differences - 1) {
			matching--;
		}
		return result;
	}
}

#endif
