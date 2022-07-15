#pragma once

#include "../../Common.h"
#include "JJ2Event.h"
#include "JJ2Level.h"
#include "../EventType.h"

#include <map>
#include <functional>
#include <array>

namespace Jazz2::Compatibility
{
    class EventConverter
    {
    public:
        struct ConversionResult {
            EventType Type;
            std::array<uint8_t, 16> Params;
        };

        enum class JJ2EventParamType {
            None,
            Bool,
            UInt,
            Int
        };

        using ConversionFunction = std::function<ConversionResult(JJ2Level& level, uint32_t e)>;

        EventConverter();

        ConversionResult TryConvert(JJ2Level& level, JJ2Event old, uint32_t eventParams);
        void Add(JJ2Event originalEvent, ConversionFunction converter);
        void Override(JJ2Event originalEvent, ConversionFunction converter);

        ConversionFunction NoParamList(EventType ev);
        ConversionFunction ConstantParamList(EventType ev, std::array<uint8_t, 16> eventParams);
        ConversionFunction ParamIntToParamList(EventType ev, std::array<std::pair<JJ2EventParamType, int>, 5> paramDefs);

        static std::array<uint8_t, 16> ConvertParamInt(uint32_t paramInt, std::array<std::pair<JJ2EventParamType, int>, 5> paramTypes);

    private:
        std::map<JJ2Event, ConversionFunction> _converters;

        void AddDefaultConverters();

        static ConversionFunction GetSpringConverter(uint16_t type, bool horizontal, bool frozen);
        static ConversionFunction GetPlatformConverter(uint8_t type);
        static ConversionFunction GetPoleConverter(uint8_t theme);
        static ConversionFunction GetAmmoCrateConverter(uint8_t type);
        static ConversionFunction GetBossConverter(EventType ev, uint16_t customParam = 0);
    };
}