#pragma once

#include "../../Common.h"
#include "JJ2Version.h"

#include "../../nCine/Base/HashMap.h"

#include <Containers/Pair.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

using namespace nCine;

namespace Jazz2::Compatibility
{
    enum class JJ2DefaultPalette {
        Sprite,
        Menu
    };

    class AnimSetMapping
    {
    public:
        static constexpr char Discard[] = ":discard";

        struct Entry {
            String Category;
            String Name;

            JJ2DefaultPalette Palette;
            bool SkipNormalMap;
            bool AllowRealtimePalette;
        };

        AnimSetMapping(JJ2Version version)
            :
            _version(version),
            _currentItem(0),
            _currentSet(0)
        {
        }

        Entry* Get(int32_t set, int32_t item);

        static AnimSetMapping GetAnimMapping(JJ2Version version);
        static AnimSetMapping GetSampleMapping(JJ2Version version);

    private:
        JJ2Version _version;
        HashMap<int32_t, Entry> _entries;
        int _currentItem;
        int _currentSet;

        void DiscardItems(int advanceBy, JJ2Version appliesTo = JJ2Version::All);
        void SkipItems(int advanceBy = 1);
        void NextSet(int advanceBy = 1, JJ2Version appliesTo = JJ2Version::All);
        void Add(JJ2Version appliesTo, const StringView& category, const StringView& name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
        void Add(const StringView& category, const StringView& name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
    };
}