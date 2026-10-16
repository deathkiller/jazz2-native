#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

namespace Jazz2::Editor
{
	/**
		@brief Tile set library of the game content

		Finds tile sets the same way the game does --- in the user content, the content and the cache --- and
		reads only what it's asked for.
	*/
	class ContentTileSetLibrary : public ITileSetLibrary
	{
	public:
		std::int32_t GetTileCount(StringView name) override;

		/** @brief Reads the palette of a tile set, returns `false` if it doesn't exist or has none */
		bool ReadPalette(StringView name, std::uint32_t (&palette)[256]);
		/** @brief Returns the path of a tile set file, or an empty string if it doesn't exist */
		static String FindTileSetFile(StringView name);
		/**
		 * @brief Returns the path a tile set made in the editor is saved to, in the user content
		 *
		 * Creates its directory if @p create is `true`. Returns an empty string if there's no place to save to.
		 */
		static String GetWritablePath(StringView name, bool create);
		/** @brief Returns `true` if a name can be the name of a tile set, lowercase letters, digits, dashes and underscores */
		static bool IsValidName(StringView name);

		/** @brief Forgets everything read so far, so changed tile sets are read again */
		void Invalidate();

	private:
		struct Entry
		{
			String Name;
			std::int32_t TileCount;
		};

		SmallVector<Entry, 0> _entries;
	};
}

#endif
