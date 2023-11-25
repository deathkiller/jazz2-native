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

		virtual void OnBeginFrame() { }
		virtual void OnEndFrame() { }
		virtual void OnInitializeViewport(int width, int height) { }

		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) { }
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) { }
		virtual void OnTouchEvent(const nCine::TouchEvent& event) { }
		
	private:
		IStateHandler(const IStateHandler&) = delete;
		IStateHandler& operator=(const IStateHandler&) = delete;
	};
}