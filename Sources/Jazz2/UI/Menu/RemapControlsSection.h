#pragma once

#include "ScrollableMenuSection.h"
#include "../ControlScheme.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Base/BitArray.h"

namespace Jazz2::UI::Menu
{
	struct RemapControlsItem {
		PlayerActions Type;
		String DisplayName;
	};

	class RemapControlsSection : public ScrollableMenuSection<RemapControlsItem>
	{
	public:
		static constexpr int32_t MaxTargetCount = 6;

		RemapControlsSection();
		~RemapControlsSection() override;

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	private:
		int32_t _selectedColumn;
		int32_t _currentPlayerIndex;
		bool _isDirty;
		bool _waitForInput;
		float _timeout;
		float _hintAnimation;
		BitArray _keysPressedLast;
		JoyMappedState _joyStatesLast[ControlScheme::MaxConnectedGamepads];

		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnHandleInput() override;
		void OnExecuteSelected() override;
		void OnTouchUp(std::int32_t newIndex, const Vector2i& viewSize, const Vector2i& touchPos) override;

		void RefreshPreviousState();
		bool HasCollision(MappingTarget target);
		static StringView KeyToName(KeySym key);
	};
}