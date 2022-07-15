#pragma once

#include "../../Common.h"

#include <string>

namespace Jazz2::Compatibility
{
    class JJ2Level // .j2l
    {
    public:
        struct LevelToken {
            std::string Episode;
            std::string Level;
        };

        enum class WeatherType {
            None,
            Snow,
            Flowers,
            Rain,
            Leaf
        };

        struct ExtraTilesetEntry {
            std::string Name;
            uint16_t Offset;
            uint16_t Count;
        };



    };
}