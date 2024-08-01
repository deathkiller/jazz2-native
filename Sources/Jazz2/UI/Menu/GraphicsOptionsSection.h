#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class GraphicsOptionsItemType {
		RescaleMode,
#if defined(NCINE_HAS_WINDOWS)
		Fullscreen,
#endif
		Antialiasing,
		LowWaterQuality,
		ShowPlayerTrails,
		PreferVerticalSplitscreen,
		PreferZoomOut,
		KeepAspectRatioInCinematics,
		UnalignedViewport,
		ShowPerformanceMetrics
	};

	struct GraphicsOptionsItem {
		GraphicsOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
	};

	class GraphicsOptionsSection : public ScrollableMenuSection<GraphicsOptionsItem>
	{
	public:
		GraphicsOptionsSection();
		~GraphicsOptionsSection();

		void OnDraw(Canvas* canvas) override;

	private:
		bool _isDirty;

		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}