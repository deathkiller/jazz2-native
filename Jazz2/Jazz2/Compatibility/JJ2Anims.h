#pragma once

#include "../../Common.h"
#include "JJ2Version.h"
#include "AnimSetMapping.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
    class JJ2Anims // .j2a
    {
    public:
        static void Convert(const StringView& path, const StringView& targetPath, bool isPlus);

    private:
        struct AnimFrameSection {
            int16_t SizeX, SizeY;
            int16_t ColdspotX, ColdspotY;
            int16_t HotspotX, HotspotY;
            int16_t GunspotX, GunspotY;

            std::unique_ptr<uint8_t[]> ImageData;
            std::unique_ptr<uint8_t[]> MaskData;
            int ImageAddr;
            int MaskAddr;
            bool DrawTransparent;
        };

        struct AnimSection {
            uint16_t FrameCount;
            uint16_t FrameRate;
            SmallVector<AnimFrameSection, 0> Frames;
            int Set;
            uint16_t Anim;

            int16_t AdjustedSizeX, AdjustedSizeY;
            int16_t LargestOffsetX, LargestOffsetY;
            int16_t NormalizedHotspotX, NormalizedHotspotY;
            int16_t FrameConfigurationX, FrameConfigurationY;
        };

        struct SampleSection {
            int Set;
            uint16_t IdInSet;
            uint32_t SampleRate;
            std::unique_ptr<uint8_t[]> Data;
            uint16_t Multiplier;
        };

        static void ImportAnimations(const StringView& targetPath, JJ2Version version, SmallVectorImpl<AnimSection> anims);
        static void CreateAnimationMetadataFile(const StringView& filename, AnimSection currentAnim, AnimSetMapping::Entry data, JJ2Version version, int sizeX, int sizeY);
        static void ImportAudioSamples(const StringView& targetPath, JJ2Version version, SmallVectorImpl<SampleSection> samples);
    };
}