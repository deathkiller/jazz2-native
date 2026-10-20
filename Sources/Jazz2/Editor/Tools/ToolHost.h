#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Primitives/Rect.h"

#include <Containers/Containers.h>

#include <memory>

using namespace nCine;

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	class EditCommand;
	class EditorCamera;
	class EventPreviews;
	class EventSelection;
	class LevelDocument;
	class SmartTileModel;
	class TileSelection;
	class TileShapes;
	class UiSurface;
	struct EventStamp;
	struct LayerView;
	struct TileRef;
	struct TileStamp;

	/**
		@brief Changes the level being edited, see @ref IToolHost
	*/
	class IDocumentEditor
	{
	public:
		virtual ~IDocumentEditor() = default;

		/** @brief Returns the level being edited */
		virtual LevelDocument& GetDocument() = 0;
		/** @brief Returns the layer being edited */
		virtual std::int32_t GetCurrentLayer() const = 0;
		/** @brief Returns `true` if a layer can be changed, it's not locked or hidden */
		virtual bool IsLayerEditable(std::int32_t layer) const = 0;
		/** @brief Executes an edit command */
		virtual void Execute(std::unique_ptr<EditCommand> command, bool allowMerge = false) = 0;
		/** @brief Starts changes that are undone as one step, until @ref EndChanges(), see @ref CommandHistory::BeginTransaction() */
		virtual void BeginChanges(String name) = 0;
		/** @brief Ends changes started by @ref BeginChanges() */
		virtual void EndChanges() = 0;
		/** @brief Returns what knows how the tiles of the default tile set of the level fit together, it starts learning it */
		virtual SmartTileModel& GetSmartTiles() = 0;
		/** @brief Returns the shapes of the masks of the default tile set of the level */
		virtual const TileShapes& GetTileShapes() = 0;
	};

	/**
		@brief Shows the level being edited, see @ref IToolHost
	*/
	class ILevelView
	{
	public:
		virtual ~ILevelView() = default;

		/** @brief Returns how a layer is shown in the view */
		virtual LayerView GetLayerView(std::int32_t layer) = 0;
		/** @brief Shows a tile in the view before it's changed in the level */
		virtual void PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile) = 0;
		/** @brief Shows tiles of an area as the level has them, undoing a preview */
		virtual void RevertPreview(std::int32_t layer, const Recti& area) = 0;
		/** @brief Returns the shown level, or `nullptr` */
		virtual Tiles::TileMap* GetTileMap() = 0;
		/** @brief Returns the camera, or `nullptr` */
		virtual EditorCamera* GetCamera() = 0;
		/** @brief Returns what draws events */
		virtual EventPreviews& GetEventPreviews() = 0;
		/** @brief Returns the time animations of the interface are at */
		virtual float GetAnimTime() const = 0;
	};

	/**
		@brief Keeps what is selected and what the tools paint with, see @ref IToolHost
	*/
	class ISelectionHost
	{
	public:
		virtual ~ISelectionHost() = default;

		/** @brief Returns the stamp of the brush */
		virtual TileStamp& GetStamp() = 0;
		/** @brief Sets the stamp of the brush */
		virtual void SetStamp(TileStamp&& stamp) = 0;
		/** @brief Returns the selected tiles */
		virtual TileSelection& GetSelection() = 0;
		/** @brief Returns the events the event tool places */
		virtual EventStamp& GetEventBrush() = 0;
		/** @brief Returns the selected events */
		virtual EventSelection& GetEventSelection() = 0;
	};

	/**
		@brief Tells the user about what happened, see @ref IToolHost
	*/
	class IFeedback
	{
	public:
		virtual ~IFeedback() = default;

		/** @brief Shows a short message */
		virtual void ShowMessage(StringView text) = 0;
		/** @brief Returns the surface of the interface, for a tool that shows something over it, or `nullptr` */
		virtual UiSurface* GetSurface() const = 0;
	};

	/**
		@brief What tools need from the level editor

		All of the narrower interfaces together, a part of the editor that needs only some of them takes only those.
	*/
	class IToolHost : public IDocumentEditor, public ILevelView, public ISelectionHost, public IFeedback
	{
	public:
		~IToolHost() override = default;
	};
}

#endif
