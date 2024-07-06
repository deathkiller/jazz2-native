#pragma once

#include "../Common.h"
#include "../nCine/Input/InputEvents.h"

namespace Jazz2
{
	class IStateHandler
	{
	public:
		IStateHandler() { }
		virtual ~IStateHandler() { }

		IStateHandler(const IStateHandler&) = delete;
		IStateHandler& operator=(const IStateHandler&) = delete;

		virtual void OnBeginFrame() { }
		virtual void OnEndFrame() { }
		virtual void OnInitializeViewport(std::int32_t width, std::int32_t height) { }

		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) { }
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) { }
		virtual void OnTouchEvent(const nCine::TouchEvent& event) { }
	};
}