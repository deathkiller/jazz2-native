#pragma once

#include "../../Common.h"

#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
    class JJ2Level // .j2l
    {
    public:
        struct LevelToken {
            String Episode;
            String Level;
        };

        enum class WeatherType {
            None,
            Snow,
            Flowers,
            Rain,
            Leaf
        };

        struct ExtraTilesetEntry {
            String Name;
            uint16_t Offset;
            uint16_t Count;
        };



    };
}