#pragma once

#include "../../Main.h"

#include <Containers/SmallVector.h>
#include <IO/Stream.h>

namespace Jazz2::Formats
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::IO::Stream;
#endif

	/**
		@brief Uncompressed header of a tile set file

		Read by @ref TileSetFile::ReadHeader(), which leaves the stream at the start of the deflated block with the
		palette and the masks, which the image content follows.
	*/
	struct TileSetHeader
	{
		/** @brief Combination of @ref TileSetFile::HasMask, @ref TileSetFile::HasPalette and @ref TileSetFile::ImageLz4 */
		std::uint8_t Flags;
		/** @brief Channels of the image content, 1 for palette indices only, 4 for RGBA */
		std::uint8_t ChannelCount;
		/** @brief Width of the image content in pixels */
		std::uint32_t Width;
		/** @brief Height of the image content in pixels */
		std::uint32_t Height;
		/** @brief Number of tiles */
		std::uint16_t TileCount;
		/** @brief Size of the deflated block that follows the header */
		std::int32_t BodySize;
	};

	/**
		@brief A whole tile set as its file stores it

		Each tile is either an 8-bit one, whose pixels are palette indices (0 is transparent), or a true color one,
		whose pixels are RGBA. Both keep their pixels in a 4096-byte slot of @ref Images, an 8-bit tile in the first
		1024 bytes of it.
	*/
	struct TileSetData
	{
		/** @brief Bytes of the pixels of a tile */
		static constexpr std::int32_t ImageBytesPerTile = 32 * 32 * 4;
		/** @brief Bytes of the collision mask of a tile */
		static constexpr std::int32_t MaskBytesPerTile = 32 * 32 / 8;

		/** @brief Palette as `R | G << 8 | B << 16 | A << 24`, entry 0 is transparent */
		std::uint32_t Palette[256];
		/** @brief Number of tiles */
		std::int32_t TileCount = 0;
		/** @brief Pixels of all tiles, @ref ImageBytesPerTile each */
		SmallVector<std::uint8_t, 0> Images;
		/** @brief Non-zero for a true color tile, one byte per tile */
		SmallVector<std::uint8_t, 0> TrueColor;
		/** @brief Collision masks of all tiles, 1 bit per pixel (LSB-first, row by row), @ref MaskBytesPerTile each */
		SmallVector<std::uint8_t, 0> Masks;
		/** @brief Tiles per row of the image content, zero to choose the one that wastes the least texture memory */
		std::int32_t Columns = 0;

		/** @brief Resizes the tile set, the new tiles are empty */
		void Resize(std::int32_t tileCount);
		/** @brief Returns `true` if any tile is a true color one */
		bool HasTrueColorTiles() const;
	};

	/**
		@brief Layout of the game's own tile set format

		A tile set is converted once from the original `.j2t` (see @ref Compatibility::JJ2Tileset::Convert()), or
		saved by the tile set editor, and then read by the game in this form. The game reads it through
		@ref ContentResolver::RequestTileSet(), which builds the atlas a band of tiles at a time, the editor and the
		converter through @ref Read() and @ref Write().

		@par File layout
		All values are little-endian.
		- @cpp u64 @ce and @cpp u16 @ce signature, @cpp u8 @ce version (2), @cpp u8 @ce flags
		- @cpp u8 @ce channel count, @cpp u32 @ce width and height of the image content, @cpp u16 @ce tile count
		- @cpp i32 @ce size of the raw-deflated block that follows, which holds the palette (256 × @cpp u32 @ce),
		  a bit per tile that is set for a true color tile, and the masks (@cpp u32 @ce size, then 128 bytes per tile)
		- the image content, tiles laid out in rows of the image (see @ref Compatibility::JJ2Anims::WriteImageContent()),
		  or LZ4 blocks of a row of tiles each with @ref ImageLz4 --- an 8-bit tile in an RGBA image keeps its index in
		  all three color channels
	*/
	struct TileSetFile
	{
		/** @brief First part of the signature */
		static constexpr std::uint64_t Signature = 0xB8EF8498E2BFBBEF;
		/** @brief Second part of the signature */
		static constexpr std::uint16_t Signature2 = 0x208F;
		/** @brief Version this build writes, and the only one it reads */
		static constexpr std::uint8_t CurrentVersion = 2;

		/** @brief The masks are stored */
		static constexpr std::uint8_t HasMask = 0x20;
		/** @brief The palette is stored */
		static constexpr std::uint8_t HasPalette = 0x40;
		/** @brief The image content is in LZ4 blocks, see @ref Compatibility::JJ2Anims::ImageContentLz4Flag */
		static constexpr std::uint8_t ImageLz4 = 0x08;

		/** @brief Size of a tile in pixels */
		static constexpr std::int32_t TileSize = 32;
		/** @brief Size of the collision mask of a tile in bytes */
		static constexpr std::int32_t MaskBytesPerTile = TileSize * TileSize / 8;
		/** @brief Largest width of the image content in pixels */
		static constexpr std::uint32_t MaxImageWidth = 32768;

		/**
		 * @brief Reads the uncompressed header of a tile set
		 *
		 * Leaves the stream at the start of the deflated block. Returns `false` if the stream doesn't hold a tile set
		 * of @ref CurrentVersion, or if the header isn't valid (see @ref Validate()).
		 */
		static bool ReadHeader(Stream& s, TileSetHeader& header);

		/**
		 * @brief Returns `true` if a header describes a tile set every reader can take
		 *
		 * One or four channels, the image a whole number of tiles in both directions and not wider than
		 * @ref MaxImageWidth, at least one tile with a cell for every tile and not much more than the tiles the count
		 * can hold. With
		 * @p remainingBytes not negative, the deflated block also has to fit into them. Everything that reads
		 * a tile set relies on it, a tile set can come from a level, which can come from anywhere.
		 */
		static bool Validate(const TileSetHeader& header, std::int64_t remainingBytes = -1);

		/**
		 * @brief Returns how many tiles a row of the image content should hold
		 *
		 * Chooses the layout whose image wastes the least space once rounded up to power-of-two dimensions, within
		 * 1024x1024 where the tiles fit into it. No texture is decided by it, the game copies every tile into an
		 * atlas of its own, so it only keeps the file small.
		 */
		static std::int32_t GetBestColumnCount(std::int32_t tileCount);

		/**
		 * @brief Reads a whole tile set, including the pixels of all tiles
		 *
		 * Returns `false` if the stream doesn't hold a tile set, or holds one in LZ4 and this build can't decode it.
		 */
		static bool Read(Stream& s, TileSetData& data);

		/**
		 * @brief Writes a whole tile set
		 *
		 * The image content has a single index channel if no tile is a true color one, RGBA otherwise. With @p lz4,
		 * which requires LZ4 support in the build, it's written in LZ4 blocks.
		 */
		static bool Write(Stream& s, const TileSetData& data, bool lz4 = false);
	};
}
