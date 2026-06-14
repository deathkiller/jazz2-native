#pragma once

#include "MenuSection.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::UI::Menu
{
	/**
		@brief Section showing a simple message
		
		Displays a single centered message and waits for the player to confirm or go back, dismissing the screen.
	*/
	class SimpleMessageSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param message         Message to be shown
		 * @param withTransition  Whether a fade-in transition should be played
		 */
		SimpleMessageSection(StringView message, bool withTransition = false);
		/** @overload */
		SimpleMessageSection(String&& message, bool withTransition = false);

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		String _message;
		float _transitionTime;
	};
}