#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

#include <cstdint>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
#endif

	class DocumentChanges;
	class IEditorContext;
	class UiPainter;

	/**
		@brief What is shown over the level being edited of how it's played

		Where the player can get from the start, found by the route tracer of multiplayer and found again once the level
		stopped changing for a moment, and the places where the player died in the last play-tests.
	*/
	class PlayOverlays
	{
	public:
		/** @brief Creates what is shown over the level of an editor, nothing is shown yet */
		explicit PlayOverlays(IEditorContext& context);

		PlayOverlays(const PlayOverlays&) = delete;
		PlayOverlays& operator=(const PlayOverlays&) = delete;

		/**
		 * @brief Returns how far each tile of the sprite layer is from the start, for the player, row by row
		 *
		 * -1 where the player can't get. Empty while it's not shown, see @ref ToggleReachability().
		 */
		ArrayView<const std::int32_t> GetReachability() const {
			return _reachability;
		}
		/** @brief Returns the largest distance of @ref GetReachability(), at least 1, found once when it's computed */
		std::int32_t GetReachabilityMaxDistance() const {
			return _reachabilityMaxDistance;
		}
		/** @brief Returns `true` while it's shown where the player can get */
		bool IsReachabilityShown() const {
			return !_reachability.empty();
		}
		/**
		 * @brief Finds where the player can get, as the level is loaded now
		 *
		 * With the moves of only @p character, see @ref PlayerType, 0 for any, from its start, and with
		 * @p fromMultiplayerStarts from the starts of multiplayer too, each tile as far as from the nearest of them.
		 * Returns `false` if it can't be found out, as without the start or without multiplayer compiled in, whose
		 * route tracer it uses.
		 */
		bool ComputeReachability(SmallVectorImpl<std::int32_t>& distances, std::int32_t character, bool fromMultiplayerStarts);
		/** @brief Finds again where the player can get, as the settings say, with @p reload the level is loaded again first for its events */
		void RecomputeReachability(bool reload);
		/** @brief Shows where the player can get, telling how many tiles it is, or stops showing it */
		void ToggleReachability();
		/** @brief Stops showing where the player can get, as when another level is edited */
		void HideReachability();
		/** @brief Tells about changes of the level, where the player can get is found again once they stop for a moment */
		void NoteDocumentChanges(const DocumentChanges& changes);
		/** @brief Finds again where the player can get once the level stopped changing, not while @p strokeInProgress */
		void Update(float timeMult, bool strokeInProgress);
		/** @brief Draws the places where the player died in the last play-tests, if the settings say so */
		void DrawDeaths(UiPainter& painter, const Rectf& viewRect);

	private:
		// A place where the player died, those close to each other are one
		struct DeathSpot
		{
			Vector2f Center;
			std::int32_t Count;
		};

		IEditorContext& _context;
		// See GetReachability(), empty while it's not shown
		SmallVector<std::int32_t, 0> _reachability;
		std::int32_t _reachabilityMaxDistance = 1;
		// The level changed since it was found, see Update()
		bool _reachabilityDirty = false;
		bool _reachabilityNeedsReload = false;
		float _reachabilityWait = 0.0f;
		// Places where the player died in the last play-tests, made once the paths change, see DrawDeaths()
		SmallVector<DeathSpot, 0> _deathSpots;
		std::int32_t _deathSpotsMaxCount = 1;
		std::uint32_t _deathSpotsRevision = UINT32_MAX;
	};
}

#endif
