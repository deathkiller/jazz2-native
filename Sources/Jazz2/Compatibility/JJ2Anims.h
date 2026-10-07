#pragma once

#include "../../Main.h"
#include "ConversionProgress.h"
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
		static constexpr std::uint16_t CacheVersion = 39;
#endif

		/**
			@brief Largest sheet @ref PackRectangles() lays out, per axis

			The smallest texture limit among the supported platforms decides it, so a sheet that fits here
			needs no per-platform variant of the converted assets.
		*/
		static constexpr std::int32_t MaxSheetSize = 1024;

		/**
			@brief Size of the pages a sheet may be split into, per axis - nothing is placed across one

			The PSP's GE cannot address more than 512 texels per axis, so its backend splits a larger texture
			into pages of this size and draws every primitive from the page its texture rectangle starts in
			(the legacy GL backend does the same on a device limited to 512). A frame lying across a page line
			would be cut off at it, so @ref PackRectangles() never places one there.
		*/
		static constexpr std::int32_t SheetPageSize = 512;

		/**
		 * @brief Converts the specified animation file and writes the result to a `.pak` file
		 *
		 * @param progress	Reports how far the conversion has got, see @ref ConversionProgress
		 */
		static JJ2Version Convert(StringView path, PakWriter& pakWriter, bool isPlus = false, ConversionProgress progress = {});

		/** @brief Writes raw image content to the specified stream */
		static void WriteImageContent(Stream& so, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount);

		/**
			@brief Reads raw image content from the specified stream

			The counterpart of @ref WriteImageContent, and the only decoder for it - the game reads its sprites,
			tilesets and fonts through this. `data` is written `channelCount` bytes per pixel, but each pixel is
			stored with a single four byte write, so the buffer needs three bytes of slack past the last pixel
			when fewer channels are read.
		*/
		static void ReadImageContent(Stream& s, std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount);

		/**
			@brief Incremental decoder for the image content written by @ref WriteImageContent

			Keeps the decoder state between calls, so an image can be read a band of rows at a time into
			separate buffers instead of one allocation the size of the whole sheet - which for the larger
			tilesets is close to a megabyte, the single biggest block the game asks for, and the first one to
			fail on a console heap that a previous level has already fragmented. The bands have to be decoded
			in order and together cover exactly `width * height` pixels.
		*/
		class ImageContentDecoder
		{
		public:
			ImageContentDecoder();

			/** @brief Decodes the next @p pixelCount pixels of the image into @p data */
			void Decode(Stream& s, std::uint8_t* data, std::int32_t pixelCount, std::int32_t channelCount);
			/**
				@brief Decodes the next @p pixelCount pixels from an in-memory copy of the content, advancing @p src

				Reading past @p end yields zero bytes. A byte read from memory is a load; through a stream it is a
				virtual call per byte, which on the consoles' in-order CPUs was most of the decode.
			*/
			void Decode(const std::uint8_t*& src, const std::uint8_t* end, std::uint8_t* data, std::int32_t pixelCount, std::int32_t channelCount);

		private:
			std::uint32_t _index[64];
			std::uint8_t _px[4];
			std::int32_t _run;
		};

		/**
			@brief Compression of the image content of the sprite sheets and tilesets a conversion writes

			Every platform reads the game's own format (@ref WriteImageContent). A tree prepared for one of
			the consoles that cannot convert on the device - and so has to carry its content ready-made anyway -
			can carry LZ4 instead (@ref WriteImageContentLz4), which decodes faster (a tileset 1.6 times as fast
			on the Nintendo 64, measured) and takes less than half the space. Only the asset packer changes this, for those
			targets; a build without LZ4 support refuses such a file rather than misreading it.
		*/
		enum class ImageCompression {
			Default,		/**< The game's own format, see @ref WriteImageContent */
			Lz4				/**< LZ4 blocks of the raw pixels, see @ref WriteImageContentLz4 */
		};

		/** @brief Image compression that sprite sheets and tilesets are converted with, see @ref ImageCompression */
		static ImageCompression PreferredImageCompression;

		/**
			@brief Header flag of a sprite sheet (`.aura`) or a tileset (`.j2t`) whose image content is LZ4

			The flags byte follows the version in both headers, and this bit is free in both.
		*/
		static constexpr std::uint8_t ImageContentLz4Flag = 0x08;

		/** @brief Rows of a tileset that go into one LZ4 block, one band of tiles - see @ref WriteImageContentLz4 */
		static constexpr std::int32_t TilesetLz4BandRows = 32;

#if defined(WITH_LZ4) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Writes image content as LZ4 blocks of @p bandRows rows each

			Every block is the raw pixels of its rows (`channelCount` bytes each), compressed at the highest
			ratio - the cost is paid once, by the converter, and decoding does not get slower for it - and
			preceded by its compressed size (32-bit little-endian). The blocks are independent, so a reader
			can decode them one at a time straight into a buffer of one band, which is how a tileset is read
			(see @ref TilesetLz4BandRows); a sprite sheet is one block.
		*/
		static void WriteImageContentLz4(Stream& so, const std::uint8_t* data, std::int32_t width, std::int32_t height,
			std::int32_t channelCount, std::int32_t bandRows);

		/**
			@brief Decodes the next LZ4 block of image content from memory, advancing @p src past it

			@p byteCount is the exact size of the block's pixels. Returns `false` if the block is damaged or
			does not have exactly that many bytes; @p data is then left undefined.
		*/
		static bool DecodeImageContentLz4(const std::uint8_t*& src, const std::uint8_t* end, std::uint8_t* data, std::int32_t byteCount);

		/** @brief Reads and decodes the next LZ4 block of image content from a stream, see @ref DecodeImageContentLz4 */
		static bool ReadImageContentLz4(Stream& s, std::uint8_t* data, std::int32_t byteCount);
#endif

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

		/**
			@brief Lays rectangles out in as small a sheet as possible

			Takes @ref PackedFrame::W and @ref PackedFrame::H of every rectangle and fills in @ref PackedFrame::X
			and @ref PackedFrame::Y, keeping @p spacing pixels between any two rectangles. The sheet is chosen
			for the least memory once its dimensions are rounded up to powers of two, which is what the
			hardware that cannot sample anything else pays for it; among layouts that round up the same, one
			that fits into a single @ref SheetPageSize page wins, and then the smallest exact area, which is
			what every other platform pays. The sheet is reported at its exact size rather than rounded up,
			the backends that need a power of two pad the texture themselves.

			No rectangle is placed across a multiple of @ref SheetPageSize on either axis (see there), unless
			there is no other way to fit them into @ref MaxSheetSize x @ref MaxSheetSize - a rectangle larger
			than a page, or so many that the gaps the page lines leave cannot be afforded; @ref LiesAcrossPageLine()
			tells when that happened. Returns `false` when the rectangles do not fit into
			@ref MaxSheetSize x @ref MaxSheetSize at all.
		*/
		static bool PackRectangles(SmallVectorImpl<PackedFrame>& rects, std::int32_t spacing, std::int32_t& sheetWidth, std::int32_t& sheetHeight);

		/** @brief Returns `true` if a rectangle lies across a multiple of @ref SheetPageSize, where a split texture is cut */
		static constexpr bool LiesAcrossPageLine(const PackedFrame& rect) {
			return (rect.W > 0 && rect.X / SheetPageSize != (rect.X + rect.W - 1) / SheetPageSize) ||
				(rect.H > 0 && rect.Y / SheetPageSize != (rect.Y + rect.H - 1) / SheetPageSize);
		}

	private:
		// Transparent margin written around every exported frame, so a frame cannot bleed into its neighbour
		// under bilinear filtering. The runtime reads it back as `GenericGraphicResource::FrameBorder` - hitboxes,
		// the tiled vine and the scripting API's spot values are all derived from it - so the two have to agree;
		// it is duplicated rather than shared because `Resources.h` would drag the renderer into the converter.
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
			equal cells pays for that difference in every single frame. The layout is @ref PackRectangles()'s.
			Returns `false` when the frames cannot be packed within the texture size limit, in which case the
			regular grid is used instead.
		*/
		static bool PackFramesTightly(const AnimSection& anim, std::int32_t border,
			SmallVector<PackedFrame, 0>& packed, std::int32_t& sheetWidth, std::int32_t& sheetHeight);

		static void ImportAnimations(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<AnimSection>& anims, ConversionProgress progress);
		static void ImportAudioSamples(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<SampleSection>& samples, ConversionProgress progress);

		static void WriteImageToFile(StringView targetPath, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry);
		static void WriteImageToStream(Stream& targetStream, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry, const PackedSheet& packedSheet);
	};
}