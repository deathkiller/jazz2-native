#pragma once

#include "../../Main.h"
#include "JJ2Version.h"
#include "AnimSetMapping.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>
#include <IO/PakFile.h>

using namespace Death::Containers;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Compatibility
{
	/**
		@brief Parses original `.j2a` animation files
		
		Reads the original game's combined animation archive (`Anims.j2a`), decoding its animation sets,
		frames and embedded audio samples, and writes the converted sprites and sounds into the engine's
		`.pak` format. Asset naming and palette handling are driven by @ref AnimSetMapping.
	*/
	class JJ2Anims
	{
	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::uint16_t CacheVersion = 37;
#endif

		/** @brief Converts the specified animation file and writes the result to a `.pak` file */
		static JJ2Version Convert(StringView path, PakWriter& pakWriter, bool isPlus = false);

		/** @brief Writes raw image content to the specified stream */
		static void WriteImageContent(Stream& so, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount);

		/** @brief Where one frame ends up in a tightly packed sheet */
		struct PackedFrame
		{
			std::int32_t X, Y, W, H;
			/** @brief Where the frame's area begins inside its logical cell, so the hotspot can follow it */
			std::int32_t OffsetX, OffsetY;
		};

		/** @brief Describes a tightly packed sheet, or nothing when the frames form a regular grid */
		struct PackedSheet
		{
			const SmallVectorImpl<PackedFrame>* Frames = nullptr;
			std::int32_t Width = 0;
			std::int32_t Height = 0;
		};

	private:
		static constexpr int32_t AddBorder = 2;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct AnimFrameSection {
			std::int16_t SizeX, SizeY;
			std::int16_t ColdspotX, ColdspotY;
			std::int16_t HotspotX, HotspotY;
			std::int16_t GunspotX, GunspotY;

			std::unique_ptr<std::uint8_t[]> ImageData;
			// TODO: Sprite mask
			//std::unique_ptr<std::uint8_t[]> MaskData;
			std::int32_t ImageAddr;
			std::int32_t MaskAddr;
			bool DrawTransparent;
		};

		struct AnimSection {
			std::uint16_t FrameCount;
			std::uint16_t FrameRate;
			SmallVector<AnimFrameSection, 0> Frames;
			std::int32_t Set;
			std::uint16_t Anim;

			std::int16_t AdjustedSizeX, AdjustedSizeY;
			std::int16_t LargestOffsetX, LargestOffsetY;
			std::int16_t NormalizedHotspotX, NormalizedHotspotY;
			std::int8_t FrameConfigurationX, FrameConfigurationY;
		};

		struct SampleSection {
			std::int32_t Set;
			std::uint16_t IdInSet;
			std::uint32_t SampleRate;
			std::uint32_t DataSize;
			std::unique_ptr<std::uint8_t[]> Data;
			std::uint16_t Multiplier;
		};
#endif

		JJ2Anims();

		/**
			@brief Packs the frames of one animation so each keeps only the space it needs

			A frame's own extent is usually much smaller than the largest frame of its animation, and a grid of
			equal cells pays for that difference in every single frame. Returns `false` when the frames cannot
			be packed within the texture size limit, in which case the regular grid is used instead.
		*/
		static bool PackFramesTightly(const AnimSection& anim, std::int32_t border,
			SmallVector<PackedFrame, 0>& packed, std::int32_t& sheetWidth, std::int32_t& sheetHeight);

		static void ImportAnimations(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<AnimSection>& anims);
		static void ImportAudioSamples(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<SampleSection>& samples);

		static void WriteImageToFile(StringView targetPath, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry);
		static void WriteImageToStream(Stream& targetStream, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry, const PackedSheet& packedSheet);
	};
}