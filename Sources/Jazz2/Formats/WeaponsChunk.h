#pragma once

#include "LevelFile.h"
#include "../WeaponDefinition.h"

namespace Jazz2::Formats
{
	/**
		@brief Chunk with the weapons of a level

		How a level changes the weapons (see @ref WeaponsDescription): options of the weapons of the game, weapons of its
		own the game fires itself, weapons its script sets up, and the images its weapons use. It's an ancillary chunk, a
		reader that doesn't know it plays the level with the weapons of the game. It's kept when the level is exported.

		The payload is @cpp u8 @ce version (@ref Version), @cpp i16 @ce argument of the hook of the weapons of the script,
		@cpp varuint @ce number of weapons, and for each @cpp u8 @ce @ref WeaponType, @cpp varuint @ce size and its
		properties, then @cpp varuint @ce number of images, and for each @cpp varuint @ce size and @cpp varuint @ce frame
		width, frame height, frame count, @cpp f32 @ce speed of the animation, @cpp i16 @ce hotspot X and Y and the palette
		indices of the frames one below another, in the image content of the other images of the game with one channel (see
		@ref Compatibility::JJ2Anims::WriteImageContent()). A property is @cpp u8 @ce id, @cpp varuint @ce size and its value, one that isn't there is as the
		weapon of the game has it. A reader skips a property it doesn't know, and the editor keeps it. A version newer
		than @ref Version changes what properties mean, a reader leaves such a chunk alone.
	*/
	struct WeaponsChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('W', 'P', 'N', 'S');
		/** @brief Version this build reads and writes */
		static constexpr std::uint8_t Version = 1;
		/** @brief Most images a level can have */
		static constexpr std::uint32_t MaxImages = 64;
		/** @brief Largest width and height of a frame of an image */
		static constexpr std::int32_t MaxFrameSize = 256;
		/** @brief Most frames of an image */
		static constexpr std::int32_t MaxFrameCount = 64;

		/** @brief Result of @ref Read() */
		enum class ReadResult {
			Success,			/**< Read */
			Damaged,			/**< The payload is damaged, the weapons are left as the game has them */
			NewerVersion		/**< The payload is of a newer version, the weapons are left as the game has them */
		};

		WeaponsChunk() = delete;

		/** @brief Reads the payload */
		static ReadResult Read(ArrayView<const std::uint8_t> data, WeaponsDescription& weapons);
		/** @brief Writes the payload, nothing if the level doesn't change any weapon */
		static void Write(const WeaponsDescription& weapons, SmallVectorImpl<std::uint8_t>& data);
	};
}
