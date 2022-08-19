#pragma once

#include "../../Common.h"
#include "JJ2Event.h"
#include "../EventType.h"

#include <functional>
#include <array>

#include "../../nCine/Base/HashMap.h"

using namespace nCine;

#include <Containers/Pair.h>

namespace Jazz2::Compatibility
{
    class JJ2Level;

    struct ConversionResult {
        EventType Type;
        uint8_t Params[16];
    };

    constexpr int JJ2ParamNone = 0;
    constexpr int JJ2ParamBool = 1;
    constexpr int JJ2ParamUInt = 2;
    constexpr int JJ2ParamInt = 3;

    /*struct ParamDesc {
        ParamDesc(int type, int size) : Type(type), Size(size) { }

        int Type;
        int Size;
    };*/

    class EventConverter
    {
    public:
        using ConversionFunction = std::function<ConversionResult(JJ2Level* level, uint32_t e)>;

        EventConverter();

        ConversionResult TryConvert(JJ2Level* level, JJ2Event old, uint32_t eventParams) const;
        void Add(JJ2Event originalEvent, const ConversionFunction& converter);
        void Override(JJ2Event originalEvent, const ConversionFunction& converter);

        ConversionFunction NoParamList(EventType ev);
        ConversionFunction ConstantParamList(EventType ev, const std::array<uint8_t, 16>& eventParams);
        ConversionFunction ParamIntToParamList(EventType ev, const std::array<Pair<int, int>, 6>& paramDefs);

        static void ConvertParamInt(uint32_t paramInt, const std::initializer_list<Pair<int, int>> paramTypes, uint8_t eventParams[16]);

    private:
        HashMap<JJ2Event, ConversionFunction> _converters;

        void AddDefaultConverters();

        static ConversionFunction GetSpringConverter(uint8_t type, bool horizontal, bool frozen);
        static ConversionFunction GetPlatformConverter(uint8_t type);
        static ConversionFunction GetPoleConverter(uint8_t theme);
        static ConversionFunction GetAmmoCrateConverter(uint8_t type);
        static ConversionFunction GetBossConverter(EventType ev, uint8_t customParam = 0);
    };
}