#pragma once

#include "ContentFileTypes.h"
#include "LevelFlags.h"

#include <IO/Stream.h>

namespace Jazz2
{
	/**
		@brief Layout of the game's own level format

		A level is converted once from the original `.j2l` and then read by the game in this form, which carries
		only what the game uses: the level properties, the tile sets, the animated tiles, the layers and the events,
		all of it deflated behind a short uncompressed header.

		The header starts with the same signature and type/version fields every other file the game writes uses.
		Only @ref CurrentVersion is read --- a level of another version is refused rather than misread, and the
		converted levels in the cache are rebuilt whenever the version changes (see
		@ref Compatibility::JJ2Anims::CacheVersion).

		@par File layout
		- @cb{.cpp} u64 @ce signature, @cb{.cpp} u8 @ce file type, @cb{.cpp} u16 @ce version
		- @cb{.cpp} u16 @ce @ref LevelFlags
		- @cb{.cpp} i32 @ce size of the deflated block that follows, everything below being inside it
		- names, tile sets, music, ambient light, weather, water, palettes, overridden tiles, text strings,
		  animated tiles, layers and events, in the order @ref Compatibility::JJ2Level::Convert() writes them

		@par Version history
		- 1 --- first versioned layout; a layer may use @ref Tiles::LayerRendererType::XRay and
		  @ref Tiles::LayerRendererType::BlurBehind
	*/
	struct LevelFormat
	{
		/** @brief Signature shared by all files the game writes */
		static constexpr std::uint64_t Signature = 0x2095A59FF0BFBBEF;
		/** @brief Version this build writes, and the only one it reads */
		static constexpr std::uint16_t CurrentVersion = 1;

		/**
			@brief Reads the uncompressed header of a level

			Leaves the stream at the size of the deflated block. Returns `false` if the stream doesn't hold a level
			of @ref CurrentVersion.
		*/
		static bool ReadHeader(Death::IO::Stream& s, LevelFlags& flags)
		{
			const std::uint64_t signature = s.ReadValueAsLE<std::uint64_t>();
			const std::uint8_t fileType = s.ReadValue<std::uint8_t>();
			const std::uint16_t version = s.ReadValueAsLE<std::uint16_t>();
			if (signature != Signature || fileType != ContentFileType::Level || version != CurrentVersion) {
				return false;
			}
			flags = (LevelFlags)s.ReadValueAsLE<std::uint16_t>();
			return true;
		}
	};
}
