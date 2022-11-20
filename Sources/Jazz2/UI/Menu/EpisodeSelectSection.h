#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class EpisodeSelectSection : public MenuSection
	{
	public:
		EpisodeSelectSection();

		Recti GetClipRectangle(const Vector2i& viewSize) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class ItemFlags {
			None = 0x00,

			IsAvailable = 0x01,
			IsMissing = 0x02,
			IsCompleted = 0x04,
			CanContinue = 0x08,
			CheatsUsed = 0x10,
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(ItemFlags);

		struct ItemData {
			Episode Description;
			ItemFlags Flags;
			float TouchY;
		};

		static constexpr float ItemHeight = 40.0f;
		static constexpr float TopLine = 131.0f;
		static constexpr float BottomLine = 42.0f;

		SmallVector<ItemData> _items;
		int _selectedIndex;
		float _animation;
		float _expandedAnimation;
		bool _expanded;
		float _transitionTime;
		bool _shouldStart;
		float _y;
		float _height;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		bool _scrollable;

		void ExecuteSelected();
		void EnsureVisibleSelected();
		void OnAfterTransition();
		void AddEpisode(const StringView& episodeFile);
	};
}