#include "RumbleProcessor.h"
#include "PreferencesCache.h"
#include "UI/ControlScheme.h"
#include "../nCine/Application.h"
#include "../nCine/Base/FrameTimer.h"
#include "../nCine/Input/IInputManager.h"

using namespace nCine;

namespace Jazz2
{
	RumbleProcessor::RumbleProcessor()
	{

	}

	RumbleProcessor::~RumbleProcessor()
	{
		CancelAllEffects();
	}

	void RumbleProcessor::CancelAllEffects()
	{
		if (_activeRumble.empty()) {
			return;
		}

		auto& inputManager = theApplication().GetInputManager();
		for (std::int32_t j = 0; j < UI::ControlScheme::MaxConnectedGamepads; j++) {
			inputManager.joystickRumble(j, 0.0f, 0.0f, 0);
			inputManager.joystickRumbleTriggers(j, 0.0f, 0.0f, 0);
		}
	}

	void RumbleProcessor::ExecuteEffect(std::int32_t joyId, const std::shared_ptr<RumbleDescription>& desc)
	{
		if (PreferencesCache::GamepadRumble == 0) {
			return;
		}

		auto& inputManager = theApplication().GetInputManager();

		if (!inputManager.isJoyPresent(joyId)) {
			return;
		}

		for (auto& rumble : _activeRumble) {
			if (rumble.JoyId == joyId) {
				// Already playing
				return;
			}
		}

		_activeRumble.emplace_back(joyId, desc);
	}

	void RumbleProcessor::OnEndFrame(float timeMult)
	{
		if (PreferencesCache::GamepadRumble == 0) {
			_activeRumble.clear();
			return;
		}

		auto& inputManager = theApplication().GetInputManager();

		if (_activeRumble.empty()) {
			return;
		}

		for (std::int32_t j = 0; j < UI::ControlScheme::MaxConnectedGamepads; j++) {
			if (!inputManager.isJoyPresent(j)) {
				// Allow rumble only for connected gamepads
				for (std::size_t i = 0; i < _activeRumble.size(); i++) {
					if (_activeRumble[i].JoyId == j) {
						// Remove playing effects for disconnected gamepads
						_activeRumble.eraseUnordered(i);
						i--;
					}
				}
				continue;
			}

			float gains[4] = {};
			float timeLeft = 0.0f;

			for (std::size_t i = 0; i < _activeRumble.size(); i++) {
				auto& rumble = _activeRumble[i];
				if (rumble.JoyId != j) {
					continue;
				}

				std::size_t timelineIndex = 0;
				while (timelineIndex < rumble.Desc->_timeline.size() && rumble.Desc->_timeline[timelineIndex].EndTime <= rumble.Progress) {
					timelineIndex++;
				}

				if (timelineIndex >= rumble.Desc->_timeline.size()) {
					_activeRumble.eraseUnordered(i);
					i--;
					continue;
				}

				auto& t = rumble.Desc->_timeline[timelineIndex];
				gains[0] = std::max(gains[0], t.LowFrequency);
				gains[1] = std::max(gains[1], t.HighFrequency);
				gains[2] = std::max(gains[2], t.LeftTrigger);
				gains[3] = std::max(gains[3], t.RightTrigger);

				auto& last = rumble.Desc->_timeline[rumble.Desc->_timeline.size() - 1];
				float tl = (last.EndTime - rumble.Progress);
				timeLeft = std::max(timeLeft, tl);

				rumble.Progress += timeMult;
			}

			if (timeLeft > 0.0f) {
				std::uint32_t timeLeftMs = (std::uint32_t)(timeLeft * 1000 * FrameTimer::SecondsPerFrame);
				if (timeLeftMs > 10) {
					// Reduce gain if weak is selected
					if (PreferencesCache::GamepadRumble == 1) {
						for (auto& gain : gains) {
							gain *= 0.666f;
						}
					}

					if (gains[0] > 0.0f || gains[1] > 0.0f) {
						inputManager.joystickRumble(j, gains[0], gains[1], timeLeftMs);
					}
					if (gains[2] > 0.0f || gains[3] > 0.0f) {
						inputManager.joystickRumbleTriggers(j, gains[2], gains[3], timeLeftMs);
					}
				}
			}
		}
	}

	RumbleProcessor::ActiveRumble::ActiveRumble(std::int32_t joyId, std::shared_ptr<RumbleDescription> desc)
		: Desc(std::move(desc)), JoyId(joyId), Progress(0.0f)
	{
	}
}
