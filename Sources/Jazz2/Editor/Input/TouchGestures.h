#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::SmallVector;
#endif

	/**
		@brief Tells what the fingers on a touch screen do in the level editor

		One finger is the pointer: on the interface right away, over the level only once it moved a little or stayed a
		moment, so a second finger that comes soon after doesn't paint anything --- a quick touch is a click. Two
		fingers move the view and zoom it by pinching, a short tap of two fingers undoes, of three redoes, and a finger
		held still opens a menu around it.

		Fingers are told apart by their identifiers as the events list them, not by the kind of the event, which
		backends report in different ways.
	*/
	class TouchGestures
	{
	public:
		/** @brief Most pixels a finger moves for a touch to be still a tap, at the scale of the interface 1 */
		static constexpr float Slop = 10.0f;

		/** @brief Returns whether a position is over the interface, a finger that starts there is a pointer only */
		Function<bool(Vector2f)> IsOverInterface;
		/** @brief Presses the pointer */
		Function<void(Vector2f)> PointerDown;
		/** @brief Moves the pointer */
		Function<void(Vector2f)> PointerMove;
		/** @brief Releases the pointer */
		Function<void(Vector2f)> PointerUp;
		/** @brief Cancels what the pointer started, a second finger came */
		Function<void()> PointerCancel;
		/** @brief Moves the view by a distance in pixels of the screen and zooms it by a factor around a point */
		Function<void(Vector2f, Vector2f, float)> PanZoom;
		/** @brief Several fingers tapped together, with their number */
		Function<void(std::int32_t)> Tap;
		/** @brief A finger was held still at a position */
		Function<void(Vector2f)> LongPress;

		TouchGestures();

		/** @brief Passes a touch event, with the size of the screen the positions are relative to */
		void OnTouchEvent(const TouchEvent& event, Vector2f screenSize, float scale);
		/** @brief Advances time, for the moment a finger waits and a long press */
		void Update(float timeMult);
		/** @brief Returns `true` while a finger is on the screen */
		bool IsActive() const {
			return !_fingers.empty();
		}

	private:
		enum class Mode : std::uint8_t
		{
			None,			// No finger
			Pending,		// One finger over the level, not yet known what it does
			Pointer,		// One finger is the pointer
			Interface,		// One finger on the interface
			Gesture,		// More fingers move the view
			Ignored			// Waiting until all fingers are up
		};

		struct Finger
		{
			std::int32_t Id;
			Vector2f Start;
			Vector2f Position;
		};

		SmallVector<Finger, 4> _fingers;
		Mode _mode;
		float _time;
		float _slop;
		// Of a gesture, for a tap and for the movement since the last event
		std::int32_t _maxFingers;
		float _maxMove;
		Vector2f _lastCenter;
		float _lastSpread;
		// The finger that presses the interface, and where it was last
		std::int32_t _pointerId;
		Vector2f _pointerPosition;

		void Begin(Mode mode);
		void StartPointer();
		Vector2f GetCenter() const;
		float GetSpread(Vector2f center) const;
	};
}

#endif
