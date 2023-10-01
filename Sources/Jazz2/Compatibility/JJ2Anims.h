#pragma once

#include "../../Common.h"
#include "JJ2Version.h"
#include "AnimSetMapping.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Compatibility
{
	class JJ2Anims // .j2a
	{
	public:
		static constexpr uint16_t CacheVersion = 9;

		static bool Convert(const StringView& path, const StringView& targetPath, bool isPlus);

		static void WriteImageToFileInternal(std::unique_ptr<Stream>& so, const uint8_t* data, int32_t width, int32_t height, int32_t channelCount);

	private:
		static constexpr int32_t AddBorder = 1;

		struct AnimFrameSection {
			int16_t SizeX, SizeY;
			int16_t ColdspotX, ColdspotY;
			int16_t HotspotX, HotspotY;
			int16_t GunspotX, GunspotY;

			std::unique_ptr<uint8_t[]> ImageData;
			// TODO: Sprite mask
			//std::unique_ptr<uint8_t[]> MaskData;
			int32_t ImageAddr;
			int32_t MaskAddr;
			bool DrawTransparent;
		};

		struct AnimSection {
			uint16_t FrameCount;
			uint16_t FrameRate;
			SmallVector<AnimFrameSection, 0> Frames;
			int32_t Set;
			uint16_t Anim;

			int16_t AdjustedSizeX, AdjustedSizeY;
			int16_t LargestOffsetX, LargestOffsetY;
			int16_t NormalizedHotspotX, NormalizedHotspotY;
			int8_t FrameConfigurationX, FrameConfigurationY;
		};

		struct SampleSection {
			int32_t Set;
			uint16_t IdInSet;
			uint32_t SampleRate;
			uint32_t DataSize;
			std::unique_ptr<uint8_t[]> Data;
			uint16_t Multiplier;
		};

		JJ2Anims();

		static void ImportAnimations(const StringView& targetPath, JJ2Version version, SmallVectorImpl<AnimSection>& anims);
		static void ImportAudioSamples(const StringView& targetPath, JJ2Version version, SmallVectorImpl<SampleSection>& samples);

		static void WriteImageToFile(const StringView& targetPath, const uint8_t* data, int32_t width, int32_t height, int32_t channelCount, AnimSection* anim, AnimSetMapping::Entry* entry);
	};
}