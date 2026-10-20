#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

namespace Jazz2::Editor
{
	class ActionRegistry;
	class IEditorContext;

	/**
		@brief Adds the actions of the panels, the overlays and the dialogs of the interface

		Duplicating a layer, the frame of the game screen shown over the level, the list of events
		and the favourite events and tiles. Called by @ref EditorSession::CreateActions().
	*/
	void AddShellActions(IEditorContext& session, ActionRegistry& actions);

	/** @brief Adds a copy of the layer being edited after it, as an undoable step, the copy is edited then */
	bool DuplicateCurrentLayer(IEditorContext& session);
	/**
	 * @brief Shows only one layer in the editor, or all of them again
	 *
	 * If all other layers are hidden and the layer is shown, all layers are shown again, otherwise all others are
	 * hidden. Only the editor state of the layers changes, see @ref DocumentLayer::EditorHidden.
	 */
	void SoloLayer(IEditorContext& session, std::int32_t layer);
	/** @brief Sets the opacity the editor shows a layer with, 255 fully, see @ref DocumentLayer::EditorOpacity */
	void SetLayerEditorOpacity(IEditorContext& session, std::int32_t layer, std::uint8_t opacity);
}

#endif
