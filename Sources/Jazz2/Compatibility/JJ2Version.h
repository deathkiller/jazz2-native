#pragma once

#include "../../Common.h"

namespace Jazz2::Compatibility
{
    enum class JJ2Version : uint16_t {
        Unknown = 0x0000,

        BaseGame = 0x0001,
        TSF = 0x0002,
        HH = 0x0004,
        CC = 0x0008,

        PlusExtension = 0x0100,
        SharewareDemo = 0x0200,

        All = 0xffff
    };

    DEFINE_ENUM_OPERATORS(JJ2Version);
}