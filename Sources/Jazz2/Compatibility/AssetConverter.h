#pragma once

#include "ConversionProgress.h"
#include "JJ2Version.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/PakFile.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Jazz2::Compatibility
{
	/**
		@brief Converts original Jazz Jackrabbit 2 data into the formats the game loads

		The same conversion the game performs on its first run, factored out so it can also be driven offline by
		the AssetPacker tool. Everything is expressed in terms of an input and an output directory, so it does
		not depend on the resource cache (and through it, on the renderer).
	*/
	class AssetConverter
	{
	public:
		/** @brief Outcome of converting the source assets */
		enum class Result {
			Success,				/**< Everything was converted */
			CannotWriteTarget,		/**< The output .pak file could not be opened for writing */
			UnsupportedVersion		/**< The provided Jazz Jackrabbit 2 version is not supported */
		};

		/**
			@brief What is converted out of the source directory, and what of it is carried over

			Which levels are wanted

			A source directory is usually an installation that has collected levels from everywhere, and most of
			them are of no use to a build that only has to carry the game itself - a console disc especially,
			where they cost space that is not there. Both of these are off by default, so the whole directory is
			converted unless something asks otherwise.
		*/
		struct ConversionOptions
		{
			/**
				@brief Convert only what the original game and its expansions shipped

				The episodes are recognised by name and every level reachable from one of them is followed, so an
				episode does not lose a level just because it is not in the table of known ones. Levels that
				belong to no episode are matched against a list of the ones the original game came with - see
				`StockStandaloneLevels` in the implementation, and @ref SkippedLevels if it needs correcting.
			*/
			bool OriginalsOnly = false;
			/**
				@brief Convert only what the Shareware Demo came with

				A subset of @ref OriginalsOnly - its one episode and the three multiplayer levels that shipped
				alongside - and it wins if both are set. The custom-levels entry is still kept, so a build that
				lets the player bring their own data (see `ImportSection`) does not lose the menu item for it.

				Note that this cannot narrow the sprite and sound package: they are shared across every episode, so
				what ends up in there is decided by which `Anims.j2a` the source directory has.
			*/
			bool SharewareOnly = false;
			/** @brief Skip levels that belong to no episode (the ones that would land in `Episodes/unknown`) */
			bool SkipNonEpisodeLevels = false;
			/**
				@brief Copy the music the converted levels ask for into a `Music` subdirectory

				For a target whose output *is* the content tree the game loads, which is where it looks for music.
				The desktop game instead reads it from its own content directory, falling back to the original
				files, and never looks in the cache - so copying it there would only take up space.
			*/
			bool CopyUsedMusic = false;
			/** @brief Receives the name of every level that was skipped, so a caller can report them */
			SmallVectorImpl<String>* SkippedLevels = nullptr;
		};

		/**
			@brief How the levels of a level pack are converted, and what came out of it, see @ref ConvertLevelPack()

			Levels of no episode go to the episode @ref EpisodePrefix, levels of an episode `e` of the pack to
			`<EpisodePrefix>_e`, so the links between them stay inside the pack.
		*/
		struct LevelPackConversion
		{
			/** @brief A converted level */
			struct Level
			{
				/** @brief Name to load the level by, `episode/level` */
				String Name;
				/** @brief Name shown to the player */
				String DisplayName;
			};

			/** @brief A converted episode */
			struct Episode
			{
				/** @brief Name of the episode */
				String Name;
				/** @brief Name shown to the player */
				String DisplayName;
				/** @brief First level of the episode, without the episode */
				String FirstLevel;
			};

			/** @brief Episode of the levels of no episode, e.g. `+1234` */
			String EpisodePrefix;
			/** @brief Maps a tile set name, lowercase without extension, to the path written into the levels */
			Function<String(StringView)> TileSetPath;
			/** @brief Maps a music file name, lowercase with extension, to the name written into the levels */
			Function<String(StringView)> MusicPath;
			/** @brief Whether the scripts of the levels are copied next to them */
			bool CopyScripts = true;

			/** @brief Levels that were converted */
			SmallVector<Level, 0> Levels;
			/** @brief Episodes that were converted */
			SmallVector<Episode, 0> Episodes;
			/** @brief Original names of the tile sets the levels use, they're converted by the caller */
			SmallVector<String, 0> UsedTileSets;
			/** @brief Original names of the music files the levels use */
			SmallVector<String, 0> UsedMusic;
		};

		/** @brief Name of the package the first-run conversion writes its sprites and sounds into */
		static constexpr StringView SourcePackage = "Source.pak"_s;
		/**
			@brief Name of the same package in a content tree prepared ahead of time

			A tree prepared by @ref asset-packer "AssetPacker" is loaded instead of being converted, and this
			is how the game tells one apart from a `Cache` it built itself: the name is the marker, so it is
			what a prepared tree gets instead of @ref SourcePackage. See
			@relativeref{Jazz2,ContentResolver::IsContentPrebaked()}.
		*/
		static constexpr StringView PrebakedPackage = "Prebaked.pak"_s;

		/**
		 * @brief Converts @cb{.cpp} Anims.j2a @ce (and @cb{.cpp} Data.j2d @ce, if present) into a package
		 *
		 * @param packageName	Which package to write, @ref SourcePackage or @ref PrebakedPackage
		 * @param progress		Reports how far the conversion has got, see @ref ConversionProgress
		 */
		static Result ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath,
			JJ2Version& version, StringView packageName = SourcePackage, ConversionProgress progress = {});
		/**
		 * @brief Converts the same into a package that is already open, and is not finalized here
		 *
		 * For a caller that has more to put in the same package --- @ref asset-packer "AssetPacker" embeds the
		 * game's own assets alongside the converted ones, after these, so that what the original data provides
		 * is what a path present in both resolves to.
		 */
		static Result ConvertSourceAssets(StringView animsPath, StringView sourcePath,
			Death::IO::PakWriter& pakWriter, JJ2Version& version, ConversionProgress progress = {});

		/** @brief Converts every episode, level and used tileset into the output directory */
		static void ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, ConversionProgress progress = {});
		/** @brief Converts the episodes, levels and used tilesets that @p options allow into the output directory */
		static void ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, const ConversionOptions& options, ConversionProgress progress = {});
		/**
		 * @brief Converts the episodes and levels of a level pack into the `Episodes` directory of the output directory
		 *
		 * Every level gets a preview. The tile sets aren't converted, only listed in @ref LevelPackConversion::UsedTileSets.
		 */
		static void ConvertLevelPack(StringView sourcePath, StringView targetPath, LevelPackConversion& pack, ConversionProgress progress = {});

	private:
		static void ConvertLevelsInternal(StringView sourcePath, StringView targetPath, bool recreateAll, const ConversionOptions& options,
			LevelPackConversion* pack, ConversionProgress progress);
	};
}
