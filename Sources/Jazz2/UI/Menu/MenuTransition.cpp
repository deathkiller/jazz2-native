#include "MenuTransition.h"
#include "MenuSection.h"
#include "../Canvas.h"

#include <algorithm>
#include <utility>

namespace Jazz2::UI::Menu
{
	MenuTransition::MenuTransition()
		: _state(State::Idle), _outgoingBorrowed(nullptr), _progress(0.0f)
	{
	}

	MenuSection* MenuTransition::GetOutgoing() const
	{
		return (_outgoingOwned != nullptr ? _outgoingOwned.get() : _outgoingBorrowed);
	}

	void MenuTransition::BeginForward(MenuSection* outgoing, const TransitionInfo& info)
	{
		// Finish any previous transition first (hides its outgoing section)
		Finalize();

		_state = State::Forward;
		_outgoingBorrowed = outgoing;
		_progress = 0.0f;
		_info = info;

		if (_info.Style == TransitionStyle::None || _info.Duration <= 0.0f) {
			// Instant switch - hide the outgoing section right away
			Finalize();
		}
	}

	void MenuTransition::BeginBackward(std::unique_ptr<MenuSection> outgoing, const TransitionInfo& info)
	{
		Finalize();

		_state = State::Backward;
		_outgoingOwned = std::move(outgoing);
		_progress = 0.0f;
		_info = info;

		if (_info.Style == TransitionStyle::None || _info.Duration <= 0.0f) {
			// Instant switch - destroy the outgoing section right away
			Finalize();
		}
	}

	void MenuTransition::Skip()
	{
		if (_state != State::Idle) {
			Finalize();
		}
	}

	bool MenuTransition::Update(float timeMult)
	{
		if (_state == State::Idle) {
			return false;
		}

		_progress += timeMult / _info.Duration;
		if (_progress >= 1.0f) {
			Finalize();
			return true;
		}

		return false;
	}

	void MenuTransition::Finalize()
	{
		if (_state == State::Forward && _outgoingBorrowed != nullptr) {
			// The outgoing section is still owned by the container (it remains underneath in the stack); just hide it
			_outgoingBorrowed->OnHide();
		}

		// A backward transition owns the popped section, so resetting destroys it (matching the previous pop behavior)
		_outgoingOwned = nullptr;
		_outgoingBorrowed = nullptr;
		_progress = 0.0f;
		_state = State::Idle;
	}

	void MenuTransition::ApplyTransform(Canvas* canvas, bool incoming, Vector2i viewSize) const
	{
		float p = _info.Ease(std::clamp(_progress, 0.0f, 1.0f));
		float w = (float)viewSize.X;
		float h = (float)viewSize.Y;
		bool forward = (_state == State::Forward);

		Vector2f offset = Vector2f::Zero;
		float scale = 1.0f;
		float alpha = 1.0f;

		switch (_info.Style) {
			case TransitionStyle::None:
				break;
			case TransitionStyle::Fade:
				alpha = (incoming ? p : 1.0f - p);
				break;
			case TransitionStyle::SlideHorizontal: {
				// Gentle slide combined with a cross-fade
				float dir = (forward ? 1.0f : -1.0f);
				offset.X = (incoming ? dir * (1.0f - p) : -dir * p) * w * 0.18f;
				alpha = (incoming ? p : 1.0f - p);
				break;
			}
			case TransitionStyle::SlideVertical: {
				// Slides down when going forward (e.g., the scoreboard dropping in from the top), up when going back
				float dir = (forward ? -1.0f : 1.0f);
				offset.Y = (incoming ? dir * (1.0f - p) : -dir * p) * h * 0.18f;
				alpha = (incoming ? p : 1.0f - p);
				break;
			}
			case TransitionStyle::Scale: {
				if (incoming) {
					scale = 0.9f + 0.1f * p;
					alpha = p;
				} else {
					scale = 1.0f + 0.1f * p;
					alpha = 1.0f - p;
				}
				Vector2f pivot = Vector2f(w * 0.5f, h * 0.5f);
				offset = pivot * (1.0f - scale);
				break;
			}
		}

		canvas->LayerOffset = offset;
		canvas->LayerScale = scale;
		canvas->LayerColor = Colorf(1.0f, 1.0f, 1.0f, alpha);
	}

	void MenuTransition::ResetTransform(Canvas* canvas)
	{
		canvas->LayerOffset = Vector2f::Zero;
		canvas->LayerScale = 1.0f;
		canvas->LayerColor = Colorf::White;
	}

	Recti MenuTransition::CombineClip(const Recti& a, const Recti& b, Vector2i viewSize)
	{
		bool emptyA = (a.W <= 0 || a.H <= 0);
		bool emptyB = (b.W <= 0 || b.H <= 0);
		if (emptyA && emptyB) {
			// Neither section clips its content - don't clip anything during the transition
			return Recti(0, 0, viewSize.X, viewSize.Y);
		}
		if (emptyA) {
			return b;
		}
		if (emptyB) {
			return a;
		}

		std::int32_t x0 = std::min(a.X, b.X);
		std::int32_t y0 = std::min(a.Y, b.Y);
		std::int32_t x1 = std::max(a.X + a.W, b.X + b.W);
		std::int32_t y1 = std::max(a.Y + a.H, b.Y + b.H);
		return Recti(x0, y0, x1 - x0, y1 - y0);
	}
}
