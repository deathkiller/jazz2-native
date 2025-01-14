#pragma once

#include "../Main.h"
#include "../nCine/Input/InputEvents.h"
#include "../nCine/Primitives/Vector2.h"

namespace Jazz2
{
	/** @brief Base interface of a state handler, only one handler runs at a time */
	class IStateHandler
	{
	public:
		IStateHandler() {}
		virtual ~IStateHandler() {}

		IStateHandler(const IStateHandler&) = delete;
		IStateHandler& operator=(const IStateHandler&) = delete;

		/** @brief Returns viewport size of the handler */
		virtual nCine::Vector2i GetViewSize() const { return {}; }

		/** @brief Called at the beginning of each frame */
		virtual void OnBeginFrame() {}
		/** @brief Called at the end of each frame */
		virtual void OnEndFrame() {}
		/** @brief Called when the viewport needs to be initialized (e.g., when the resolution is changed) */
		virtual void OnInitializeViewport(std::int32_t width, std::int32_t height) {}

		/** @brief Called when a key is pressed */
		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) {}
		/** @brief Called when a key is released */
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) {}
		/** @brief Called when a text input is detected */
		virtual void OnTextInput(const nCine::TextInputEvent& event) {}
		/** @brief Called when a touch event is triggered */
		virtual void OnTouchEvent(const nCine::TouchEvent& event) {}
	};
}