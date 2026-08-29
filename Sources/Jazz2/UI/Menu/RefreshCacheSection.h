#pragma once

#include "../../../Main.h"

#if defined(NCINE_HAS_WRITABLE_CACHE)

#include "MenuSection.h"

#include "../../../nCine/Threading/Thread.h"

#include <atomic>

namespace Jazz2::UI::Menu
{
	/**
		@brief Refresh cache menu section
		
		Rebuilds the asset cache by re-indexing the cached levels and pruning the binary shader cache, then returns
		once finished.
	*/
	class RefreshCacheSection : public MenuSection
	{
	public:
		/** @brief Creates a new instance */
		RefreshCacheSection();
		~RefreshCacheSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		float _animation;
		std::atomic_bool _done;
#if defined(WITH_THREADS)
		Thread _thread;
#endif
	};
}

#endif
