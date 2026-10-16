#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

#include <unordered_map>

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Draws a level small, a pixel for a tile in the average color of the tile

		The colors of the tiles are remembered, so a level is drawn again quickly. Indexed tiles are in the palette
		the level is shown in.
	*/
	class LevelThumbnail
	{
	public:
		/** @brief Size of the preview stored with a level */
		static constexpr std::int32_t PreviewMaxWidth = 128;
		/** @copydoc PreviewMaxWidth */
		static constexpr std::int32_t PreviewMaxHeight = 72;
		/** @brief Pixels of the level in a pixel of the preview, each way */
		static constexpr std::int32_t PreviewScale = 3;

		explicit LevelThumbnail(EditorSession& session);

		/** @brief Returns the average color of a tile as `R | G << 8 | B << 16 | A << 24`, its alpha is how much of it is covered */
		std::uint32_t GetTileColor(TileRef tile);
		/** @brief Draws a layer, a pixel for each of its tiles, row by row */
		void DrawLayer(std::int32_t layer, SmallVectorImpl<std::uint32_t>& pixels);
		/** @brief Forgets the colors of the tiles, when the palette or the tile sets change */
		void Clear();

		/**
		 * @brief Makes the preview of a level shown in the list of custom levels, see @ref Formats::LevelFile::PreviewChunk
		 *
		 * What the game shows around the start of the level, all layers at the parallax of the game, made
		 * @ref PreviewScale times smaller.
		 */
		static void MakePreview(EditorSession& session, SmallVectorImpl<std::uint8_t>& data);

		/** @brief Pixels of a tile as stored in its tile set */
		struct TilePixels
		{
			/** @brief Pixels in the tile set */
			const std::uint8_t* Pixels = nullptr;
			/** @brief Palette indices of the level for those of the tile set, if it's remapped */
			const std::uint8_t* Remapping = nullptr;
			/** @brief Whether the pixels are RGBA instead of palette indices */
			bool TrueColor = false;
			/** @brief Flags of the tile, see @ref Formats::TileRecord */
			std::uint8_t Flags = 0;

			/** @brief Returns the pixel as `R | G << 8 | B << 16 | A << 24`, flipped as the tile is */
			std::uint32_t GetPixel(std::int32_t x, std::int32_t y, const std::uint32_t* palette) const;
		};

		/** @brief Finds the pixels of a tile, the first frame of an animated one, returns `false` for an empty or unknown one */
		static bool GetTilePixels(EditorSession& session, TileRef tile, TilePixels& result);

	private:
		EditorSession& _session;
		std::unordered_map<std::uint32_t, std::uint32_t> _colors;

		std::uint32_t ComputeTileColor(TileRef tile);

		static std::uint32_t Blend(std::uint32_t dst, std::uint32_t src);
	};
}

#endif
