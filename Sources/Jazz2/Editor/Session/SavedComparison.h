#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/DocumentChanges.h"
#include "../../../nCine/Primitives/Rect.h"

#include <Containers/SmallVector.h>

#include <memory>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
#endif

	class IEditorContext;
	class LevelDocument;
	class UiPainter;

	/**
		@brief Shows where the level being edited differs from its file saved on disk

		While it's shown, the saved level is kept read, and the differences --- tiles of the layer being edited, and
		events --- are found again only where the level changed since, the whole level is gone through only when its
		layers or tile sets changed. The saved file is read again once it was saved, also by another program.
	*/
	class SavedComparison
	{
	public:
		/** @brief Creates the comparison of the level of an editor, it isn't shown yet */
		explicit SavedComparison(IEditorContext& context);
		~SavedComparison();

		SavedComparison(const SavedComparison&) = delete;
		SavedComparison& operator=(const SavedComparison&) = delete;

		/** @brief Returns `true` while the differences are shown */
		bool IsShown() const {
			return (_savedDocument != nullptr);
		}
		/**
		 * @brief Shows the differences, or stops showing them
		 *
		 * With @p strokeInProgress, a stroke of a tool goes on, the whole level is compared only once it's done.
		 */
		void Toggle(bool strokeInProgress);
		/** @brief Stops showing the differences, as when another level is edited */
		void Hide();
		/** @brief Tells about changes of the level, the differences are found again where they are */
		void NoteDocumentChanges(const DocumentChanges& changes);
		/** @brief Reads the saved level again if its file changed since it was read */
		void ReloadIfSavedChanged();
		/** @brief Draws the differences over the view, with @p strokeInProgress as in @ref Toggle() */
		void Draw(UiPainter& painter, const Rectf& viewRect, bool strokeInProgress);

	private:
		struct SavedLayerDifference
		{
			std::int32_t Layer = 0;
			// The layer was resized, added or removed
			bool Whole = false;
			SmallVector<std::uint32_t, 0> Cells;
		};

		IEditorContext& _context;
		// The level as saved, while it's compared with it
		std::unique_ptr<LevelDocument> _savedDocument;
		SmallVector<SavedLayerDifference, 0> _savedDifferences;
		SmallVector<std::uint32_t, 0> _savedEventDifferences;
		std::int64_t _savedFileTime = 0;
		std::uint32_t _compareRevision = 0;
		// The differences are to be found again for the whole level, otherwise only where _compareChanges are, the
		// changes since they were found, see UpdateDifferences()
		bool _compareFull = true;
		DocumentChanges _compareChanges;

		// Finds the differences again where the level changed, or for the whole level
		void UpdateDifferences(bool strokeInProgress);
		// Finds the differences again only in an area of a layer, or of the event grid, the rest stays as it was found
		void UpdateTileDifferences(std::int32_t layerIndex, const Recti& area);
		void UpdateEventDifferences(const Recti& area);
	};
}

#endif
