#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Grabs a block of tiles as the stamp by its two corners marked with a key, as <kbd>B</kbd> of Jazz Creation Station

		The first press marks the corner under the pointer, the second one the other corner, and the tiles between them
		become the stamp --- of the layer being edited, with the events on them if it's the sprite layer, or of a tile
		set in the picker (see @ref PressTileSet()). Where a block of the level is painted, its empty tiles leave the
		tiles there as they are, unless it's painted with <kbd>Shift</kbd> (see @ref TileStamp::EmptyIsTransparent),
		and on the sprite layer its events replace those under its tiles, as the editors of the original game do.

		With @ref Mode::Add or @ref Mode::Subtract the rectangle changes the selection of the layer instead and all the
		selected tiles are grabbed, so a block can have any shape. A block of the level ends the selection of the layer,
		so the stamp paints anywhere.
	*/
	class BlockGrab
	{
	public:
		/** @brief What the rectangle between the corners does */
		enum class Mode : std::uint8_t
		{
			Replace,		/**< It's the block */
			Add,			/**< It's added to the selection of the layer, the selected tiles are the block */
			Subtract		/**< It's taken out of the selection of the layer, the selected tiles left are the block */
		};

		/** @brief Creates a new instance */
		explicit BlockGrab(IEditorContext& session);

		/**
		 * @brief Marks a corner of a block of the layer being edited at the pointer, or grabs the block if a corner is marked
		 *
		 * The mode is the one of the first press. Returns `false` if the pointer isn't over the level.
		 */
		bool PressLevel(Mode mode);
		/**
		 * @brief Marks a corner of a block of a tile set at a cell of the picker, or returns the other corner if one is marked
		 *
		 * The tiles of the tile set are in rows of @p columns. Returns `true` with @p start set to the corner marked
		 * before once the block is complete, the picker picks it then as a drag over it would.
		 */
		bool PressTileSet(std::uint8_t source, std::int32_t columns, Vector2i cell, Vector2i& start);
		/** @brief Returns `true` if a corner is marked */
		bool IsActive() const;
		/** @brief Returns the corner marked in the picker of the tiles of a source, `false` if none is */
		bool GetTileSetCorner(std::uint8_t source, std::int32_t columns, Vector2i& corner) const;
		/** @brief Forgets the corner marked */
		void Cancel();
		/** @brief Draws the block from the corner marked to the pointer, over the level */
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid);

		/**
		 * @brief Makes a stamp of a rectangle of cells of the view of a layer, with the events on them if it's the sprite layer
		 *
		 * The cells are wrapped as the layer repeats. Those outside of it are left out, as are those not selected if
		 * @p selection is given. Its empty tiles are transparent, see @ref TileStamp::EmptyIsTransparent.
		 */
		static TileStamp MakeStamp(const LevelDocument& document, std::int32_t layerIndex, const LayerView& view, const Recti& cells,
			const TileSelection* selection = nullptr);
		/**
		 * @brief Makes the selected tiles of the layer being edited the stamp, with the events on them, as a block grabbed
		 *
		 * With @p cut they're erased, with their events, as one step. Returns `false` if nothing is selected on the layer.
		 */
		static bool GrabSelection(IEditorContext& session, bool cut);

	private:
		enum class Zone : std::uint8_t
		{
			None,
			Level,
			TileSet
		};

		IEditorContext& _session;
		Zone _zone;
		Mode _mode;
		// The corner marked, a cell of the view of the layer, or the index of a tile of the tile set
		Vector2i _start;
		std::int32_t _startTile;
		std::int32_t _layer;
		std::uint8_t _source;

		// Grabs the block of the level from the corner marked to another one
		void FinishLevel(Vector2i end);
		// Tells how to finish the block, by the key of the action
		void ShowStartMessage();
	};
}

#endif
