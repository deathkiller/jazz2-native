#pragma once

#include "../Main.h"
#include "../nCine/Input/InputEvents.h"
#include "../nCine/Primitives/Vector2.h"

namespace Jazz2
{
	/** @brief Base interface of a state handler */
	class IStateHandler
	{
	public:
		IStateHandler() {}
		virtual ~IStateHandler() {}

		IStateHandler(const IStateHandler&) = delete;
		IStateHandler& operator=(const IStateHandler&) = delete;

		virtual nCine::Vector2i GetViewSize() const { return {}; }

		virtual void OnBeginFrame() {}
		virtual void OnEndFrame() {}
		virtual void OnInitializeViewport(std::int32_t width, std::int32_t height) {}

		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) {}
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) {}
		virtual void OnTextInput(const nCine::TextInputEvent& event) {}
		virtual void OnTouchEvent(const nCine::TouchEvent& event) {}
	};
}