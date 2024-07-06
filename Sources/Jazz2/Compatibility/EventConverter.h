#pragma once

#include "../../Common.h"
#include "JJ2Event.h"
#include "../EventType.h"

#include <functional>
#include <array>

#include "../../nCine/Base/HashMap.h"

using namespace nCine;

#include <Containers/ArrayView.h>
#include <Containers/Pair.h>

namespace Jazz2::Compatibility
{
    class JJ2Level;

    struct ConversionResult {
        EventType Type;
		std::uint8_t Params[16];
    };

    constexpr std::int32_t JJ2ParamNone = 0;
    constexpr std::int32_t JJ2ParamBool = 1;
    constexpr std::int32_t JJ2ParamUInt = 2;
    constexpr std::int32_t JJ2ParamInt = 3;

    class EventConverter
    {
    public:
        using ConversionFunction = std::function<ConversionResult(JJ2Level* level, std::uint32_t e)>;

        EventConverter();

        ConversionResult TryConvert(JJ2Level* level, JJ2Event old, std::uint32_t eventParams) const;
        void Add(JJ2Event originalEvent, const ConversionFunction& converter);
        void Override(JJ2Event originalEvent, const ConversionFunction& converter);

        ConversionFunction NoParamList(EventType ev);
        ConversionFunction ConstantParamList(EventType ev, const std::array<std::uint8_t, 16>& eventParams);
        ConversionFunction ParamIntToParamList(EventType ev, const std::array<Pair<std::int32_t, std::int32_t>, 6>& paramDefs);

        static void ConvertParamInt(std::uint32_t paramInt, const ArrayView<const Pair<std::int32_t, std::int32_t>>& paramTypes, std::uint8_t eventParams[16]);

        static void ConvertParamInt(std::uint32_t paramInt, const std::initializer_list<const Pair<std::int32_t, std::int32_t>> paramTypes, std::uint8_t eventParams[16])
        {
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