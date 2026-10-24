#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Common/StampClipboard.h"

namespace Jazz2::Editor
{
	class IEditorContext;
	struct TilePrefab;

	/**
		@brief Actions of the tools and of the selection, and what they do

		Shapes, options of the tile tools (the modifier painted, random flips, the fill and the magic wand), the event
		placement modes, operations on the selected tiles (invert, grow, shrink, select all of the same tiles, move,
		mirror, set the modifier), copying and pasting all layers with their events, the weights of a random stamp and
		the jumps the measure tool shows. Their options are kept with the settings of the editor.
	*/
	namespace ToolActions
	{
		/** @brief Adds the actions to the actions of the editor and loads the options of the tools */
		void Register(IEditorContext& session);

		/** @brief Copies the same rectangle of all layers that are shown and not locked with the events in it, returns `false` if nothing was copied */
		bool CopyAllLayers(IEditorContext& session);
		/** @brief Pastes tiles of several layers with their events, at the pointer or where they were copied, as one step */
		bool PasteAllLayers(IEditorContext& session);
		/** @brief Places tiles of several layers with their events, the top left corner at a tile of the layer being edited, as one step */
		bool PlaceLayered(IEditorContext& session, const LayeredStamp& stamp, Vector2i topLeft);
		/** @brief Returns the tiles of several layers copied last in the editor */
		const LayeredStamp& GetLayeredClipboard();
		/** @brief Makes tiles of several layers the ones pasted next, see @ref PasteAllLayers() */
		void SetLayeredClipboard(const LayeredStamp& stamp);
		/** @brief Returns the position of the layer being edited from the sprite layer, see @ref LayeredStamp::Layer::FromSpriteLayer */
		std::int32_t GetCurrentLayerFromSprite(IEditorContext& session);
		/** @brief Uses a prefab: its tiles become the brush, and a prefab with more layers or events is pasted with them next */
		bool UsePrefab(IEditorContext& session, const TilePrefab& prefab);

		/** @brief Makes the event under the pointer the one the event tool places and puts it in the clipboard, the tool chosen stays, as <kbd>Ctrl</kbd>+<kbd>E</kbd> of the editors of the original game */
		bool TakeEventAtPointer(IEditorContext& session);
		/** @brief Places the events the event tool places at the tile under the pointer, replacing the one there, as one step */
		bool PlaceEventAtPointer(IEditorContext& session);
		/** @brief Lets the next event chosen in the list be placed at the tile under the pointer, and be the one placed from then on */
		bool ChooseEventAtPointer(IEditorContext& session);
		/** @brief Makes the tile under the pointer the brush, with @p withEvent with the event there, placed with it on the sprite layer */
		bool PickTileAtPointer(IEditorContext& session, bool withEvent);

		/** @brief Mirrors the selected tiles, or events, in place, with their flips and the parameters of the events */
		bool MirrorSelection(IEditorContext& session, bool horizontal);
		/** @brief Sets the modifier of the selected tiles, see @ref TileModifierMode */
		bool SetSelectionModifier(IEditorContext& session, TileModifierMode modifier);
		/** @brief Places the event to place on the selected tiles of the sprite layer */
		bool FillSelectionWithEvent(IEditorContext& session);
		/** @brief Selects all tiles of the layer that are like the selected ones, or the one under the pointer */
		bool SelectSameTiles(IEditorContext& session);
		/** @brief Shows a dialog with how often each tile of the brush is chosen when it paints at random */
		void ShowStampWeights(IEditorContext& session);

#if defined(DEATH_DEBUG) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Tests what the tools do that doesn't need a level, returns the number of problems */
		std::int32_t SelfTest();
#endif
	}
}

#endif
