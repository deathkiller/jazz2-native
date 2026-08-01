#pragma once

#include "JJ2Version.h"

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

		/** @brief Converts @cb{.cpp} Anims.j2a @ce (and @cb{.cpp} Data.j2d @ce, if present) into @cb{.cpp} Source.pak @ce */
		static Result ConvertSourceAssets(StringView animsPath, StringView sourcePath, StringView targetPath, JJ2Version& version);

		/** @brief Converts episodes, levels and the tilesets they use into the output directory */
		static void ConvertLevels(StringView sourcePath, StringView targetPath, bool recreateAll);
	};
}
