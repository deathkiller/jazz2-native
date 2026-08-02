#pragma once

#include "JJ2Version.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

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

				Note that this cannot narrow `Source.pak`: sprites and sounds are shared across every episode, so
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

		/** @brief Converts @cb{.cpp} Anims.j2a @ce (and @cb{.cpp} Data.j2d @ce, if present) into @cb{.cpp} Source.pak @ce */
		static Result ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath, JJ2Version& version);

		/** @brief Converts every episode, level and used tileset into the output directory */
		static void ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll);
		/** @brief Converts the episodes, levels and used tilesets that @p options allow into the output directory */
		static void ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll, const ConversionOptions& options);
	};
}
