#include "TouchGestures.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// A finger held still this long opens the menu, in frames at 60 FPS
		constexpr float LongPressFrames = 30.0f;
		// Fingers that were on the screen at most this long tap
		constexpr float TapFrames = 18.0f;
	}

	TouchGestures::TouchGestures()
		: _mode(Mode::None), _time(0.0f), _slop(Slop), _maxFingers(0), _maxMove(0.0f), _lastSpread(0.0f), _pointerId(-1)
	{
	}

	void TouchGestures::OnTouchEvent(const TouchEvent& event, Vector2f screenSize, float scale)
	{
		_slop = Slop * std::max(scale, 1.0f);

		// The fingers on the screen now, those that stay keep where they started
		SmallVector<Finger, 4> next;
		std::int32_t releasedId = -1;
		if (event.type == TouchEventType::PointerUp && event.actionIndex >= 0 && event.actionIndex < std::int32_t(event.count)) {
			releasedId = event.pointers[event.actionIndex].id;
		}
		if (event.type != TouchEventType::Up) {
			for (std::uint32_t i = 0; i < event.count && i < TouchEvent::MaxPointers; i++) {
				const TouchEvent::Pointer& pointer = event.pointers[i];
				if (pointer.id == releasedId) {
					continue;
				}
				Vector2f position(pointer.x * screenSize.X, pointer.y * screenSize.Y);
				Vector2f start = position;
				for (const Finger& finger : _fingers) {
					if (finger.Id == pointer.id) {
						start = finger.Start;
						break;
					}
				}
				next.push_back(Finger{ pointer.id, start, position });
			}
		}
		Vector2f lastPosition = (!_fingers.empty() ? _fingers[0].Position : Vector2f());
		std::size_t count = next.size();
		_fingers = Death::move(next);

		switch (_mode) {
			case Mode::None: {
				if (count == 1) {
					if (IsOverInterface && IsOverInterface(_fingers[0].Position)) {
						Begin(Mode::Interface);
						_pointerId = _fingers[0].Id;
						_pointerPosition = _fingers[0].Position;
						if (PointerDown) {
							PointerDown(_fingers[0].Position);
						}
					} else {
						Begin(Mode::Pending);
					}
				} else if (count >= 2) {
					Begin(Mode::Gesture);
				}
				break;
			}
			case Mode::Pending: {
				if (count >= 2) {
					// Nothing was started, the fingers move the view
					Begin(Mode::Gesture);
				} else if (count == 0) {
					// A quick touch is a click where it started
					if (PointerDown) {
						PointerDown(lastPosition);
					}
					if (PointerUp) {
						PointerUp(lastPosition);
					}
					Begin(Mode::None);
				} else if ((_fingers[0].Position - _fingers[0].Start).Length() > _slop) {
					StartPointer();
				}
				break;
			}
			case Mode::Pointer: {
				if (count >= 2) {
					// A second finger after all, what the first one started is cancelled
					if (PointerCancel) {
						PointerCancel();
					}
					Begin(Mode::Gesture);
				} else if (count == 0) {
					if (PointerUp) {
						PointerUp(lastPosition);
					}
					Begin(Mode::None);
				} else if (PointerMove) {
					PointerMove(_fingers[0].Position);
				}
				break;
			}
			case Mode::Interface: {
				// Only the finger that pressed is the pointer, another one that comes doesn't move it, and it doesn't take
				// over when the first one is lifted, the pointer would jump there
				const Finger* pointer = nullptr;
				for (const Finger& finger : _fingers) {
					if (finger.Id == _pointerId) {
						pointer = &finger;
						break;
					}
				}
				if (pointer == nullptr) {
					if (PointerUp) {
						PointerUp(_pointerPosition);
					}
					Begin(count == 0 ? Mode::None : Mode::Ignored);
				} else if (pointer->Position != _pointerPosition) {
					_pointerPosition = pointer->Position;
					if (PointerMove) {
						PointerMove(_pointerPosition);
					}
				}
				break;
			}
			case Mode::Gesture: {
				if (count >= 2) {
					_maxFingers = std::max(_maxFingers, std::int32_t(count));
					for (const Finger& finger : _fingers) {
						_maxMove = std::max(_maxMove, (finger.Position - finger.Start).Length());
					}
					Vector2f center = GetCenter();
					float spread = GetSpread(center);
					if (event.type == TouchEventType::PointerDown || event.type == TouchEventType::PointerUp) {
						// Another finger came or went, the view doesn't jump
						_lastCenter = center;
						_lastSpread = spread;
						break;
					}
					if (PanZoom) {
						float zoom = (_lastSpread > 1.0f && spread > 1.0f ? spread / _lastSpread : 1.0f);
						PanZoom(center, center - _lastCenter, zoom);
					}
					_lastCenter = center;
					_lastSpread = spread;
				} else if (count == 0) {
					if (_time <= TapFrames && _maxMove <= _slop && Tap) {
						Tap(_maxFingers);
					}
					Begin(Mode::None);
				} else {
					// One finger is left of more, it doesn't start anything
					Begin(Mode::Ignored);
				}
				break;
			}
			case Mode::Ignored: {
				if (count == 0) {
					Begin(Mode::None);
				}
				break;
			}
		}
	}

	void TouchGestures::Update(float timeMult)
	{
		_time += timeMult;
		if (_mode == Mode::Pending && _time >= LongPressFrames && !_fingers.empty()) {
			Vector2f position = _fingers[0].Position;
			std::int32_t fingerId = _fingers[0].Id;
			Begin(Mode::Ignored);
			if (LongPress) {
				LongPress(position);
			}
			// What it opened, such as a menu around the finger, is pressed by the same finger, which can slide to an item
			// and let go there
			if (_mode == Mode::Ignored && IsOverInterface && IsOverInterface(position)) {
				Begin(Mode::Interface);
				_pointerId = fingerId;
				_pointerPosition = position;
				if (PointerDown) {
					PointerDown(position);
				}
			}
		}
	}

	void TouchGestures::Begin(Mode mode)
	{
		_mode = mode;
		_time = 0.0f;
		if (mode == Mode::Gesture) {
			_maxFingers = std::int32_t(_fingers.size());
			_maxMove = 0.0f;
			_lastCenter = GetCenter();
			_lastSpread = GetSpread(_lastCenter);
		}
	}

	void TouchGestures::StartPointer()
	{
		// From where it started, then where it is
		Begin(Mode::Pointer);
		if (PointerDown) {
			PointerDown(_fingers[0].Start);
		}
		if (PointerMove) {
			PointerMove(_fingers[0].Position);
		}
	}

	Vector2f TouchGestures::GetCenter() const
	{
		Vector2f sum;
		for (const Finger& finger : _fingers) {
			sum += finger.Position;
		}
		return (_fingers.empty() ? sum : sum / float(_fingers.size()));
	}

	float TouchGestures::GetSpread(Vector2f center) const
	{
		float sum = 0.0f;
		for (const Finger& finger : _fingers) {
			sum += (finger.Position - center).Length();
		}
		return (_fingers.empty() ? 0.0f : sum / float(_fingers.size()));
	}
}

#endif
