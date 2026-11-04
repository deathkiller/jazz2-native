#pragma once

#if defined(WITH_LEVEL_PACKS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::LevelPacks
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/** @brief What a file of a level pack is */
	enum class LevelPackFileKind : std::uint8_t
	{
		/** Anything else a script can load */
		Other,
		/** Level */
		Level,
		/** Tile set */
		TileSet,
		/** Episode */
		Episode,
		/** Script or a file it includes */
		Script,
		/** Music */
		Music
	};

	/** @brief One original file of a level pack */
	struct LevelPackFile
	{
		/** @brief File name in lowercase, without directories */
		String Name;
		/** @brief Name of the stored file, `<name>~<hash>.<extension>`, empty if it's the same as the file of the original game */
		String Key;
		/** @brief SHA-256 of the content in hexadecimal */
		String Sha256;
		/** @brief Size in bytes */
		std::uint64_t Size;
		/** @brief What the file is */
		LevelPackFileKind Kind;
	};

	/** @brief A converted level of a level pack */
	struct LevelPackLevel
	{
		/** @brief Name to load the level by, e.g. `+1234/battle1` */
		String Name;
		/** @brief Name shown to the player */
		String DisplayName;
	};

	/** @brief A converted episode of a level pack */
	struct LevelPackEpisode
	{
		/** @brief Name of the episode, e.g. `+1234_myepisode` */
		String Name;
		/** @brief Name shown to the player */
		String DisplayName;
		/** @brief First level of the episode, without the episode */
		String FirstLevel;
	};

	/**
		@brief Record of an installed level pack

		Stored as `Packs/<id>.json` in the directory of level packs, written last when a pack is installed, so a pack
		without it isn't installed. The original files are stored once for all packs, see @ref LevelPackLibrary.
	*/
	struct LevelPackManifest
	{
		/** @brief Version of the format of the file */
		static constexpr std::int32_t FormatVersion = 1;

		/** @brief Identifier of the pack, the number in the level database or `local-<hash>` */
		String Id;
		/** @brief Where the pack came from, `digiex` or `local` */
		String Source;
		/** @brief Title of the pack */
		String Title;
		/** @brief Authors of the pack */
		String Authors;
		/** @brief Description of the pack */
		String Description;
		/** @brief Version of the pack in the level database */
		String Version;
		/** @brief File name of the archive the pack was installed from */
		String ArchiveName;
		/** @brief SHA-256 of the archive in hexadecimal */
		String ArchiveSha256;
		/** @brief Time of the installation, in seconds since 1970 */
		std::int64_t InstalledAt = 0;
		/** @brief Version of the cache the levels were converted with */
		std::uint32_t CacheVersion = 0;
		/** @brief Original files of the pack */
		SmallVector<LevelPackFile, 0> Files;
		/** @brief Converted levels */
		SmallVector<LevelPackLevel, 0> Levels;
		/** @brief Converted episodes */
		SmallVector<LevelPackEpisode, 0> Episodes;
		/** @brief Tile sets and music the levels need, but neither the pack nor the game has */
		SmallVector<String, 0> MissingAssets;

		/** @brief Reads the record from a file */
		bool Read(StringView path);
		/** @brief Writes the record into a file, the previous one is replaced only once it's complete */
		bool Write(StringView path) const;

		/** @brief Returns the episode of the levels of no episode, `+<id>` */
		String GetEpisodePrefix() const;
		/** @brief Returns the total size of the original files, including those shared with other packs */
		std::uint64_t GetTotalSize() const;
		/** @brief Returns a file by its name, or `nullptr` */
		const LevelPackFile* FindFile(StringView name) const;
	};
}

#endif
