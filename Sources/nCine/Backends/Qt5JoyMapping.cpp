#if defined(WITH_QT5)

#include "../Input/JoyMapping.h"
#include "../Input/IInputManager.h"
#include "../Input/IInputEventHandler.h"
#include "../Primitives/Vector2.h"
#include "../Base/Algorithms.h"

namespace nCine::Backends
{
	const unsigned int JoyMapping::MaxNameLength;

	const char* JoyMapping::AxesStrings[JoyMappedState::NumAxes] = {
		"leftx",
		"lefty",
		"rightx",
		"righty",
		"lefttrigger",
		"righttrigger"
	};

	const char* JoyMapping::ButtonsStrings[JoyMappedState::NumButtons] = {
		"a",
		"b",
		"x",
		"y",
		"back",
		"guide",
		"start",
		"leftstick",
		"rightstick",
		"leftshoulder",
		"rightshoulder",
		"dpup",
		"dpdown",
		"dpleft",
		"dpright"
	};

	JoyMappedStateImpl JoyMapping::_nullMappedJoyState;
	nctl::StaticArray<JoyMappedStateImpl, JoyMapping::MaxNumJoysticks> JoyMapping::_mappedJoyStates(nctl::StaticArrayMode::EXTEND_SIZE);
	JoyMappedButtonEvent JoyMapping::_mappedButtonEvent;
	JoyMappedAxisEvent JoyMapping::_mappedAxisEvent;

	JoyMapping::MappedJoystick::MappedJoystick()
	{
		name[0] = '\0';

		for (unsigned int i = 0; i < MaxNumAxes; i++)
			desc.axes[i].name = AxisName::UNKNOWN;
		for (unsigned int i = 0; i < MaxNumButtons; i++)
			buttons[i] = ButtonName::UNKNOWN;
		for (unsigned int i = 0; i < MaxHatButtons; i++)
			hats[i] = ButtonName::UNKNOWN;
	}

	JoyMapping::MappedJoystick::Guid::Guid()
	{
		for (unsigned int i = 0; i < 4; i++)
			_array[i] = 0;
	}

	JoyMapping::JoyMapping()
		: _mappings(1), _inputManager(nullptr), _inputEventHandler(nullptr)
	{
		_mappings.emplace_back();
		_mappings[0].axes[0].name = AxisName::LX;
		_mappings[0].axes[0].min = -1.0f;
		_mappings[0].axes[0].max = 1.0f;
		_mappings[0].axes[1].name = AxisName::LY;
		_mappings[0].axes[1].min = -1.0f;
		_mappings[0].axes[1].max = 1.0f;
		_mappings[0].axes[2].name = AxisName::RX;
		_mappings[0].axes[2].min = -1.0f;
		_mappings[0].axes[2].max = 1.0f;
		_mappings[0].axes[3].name = AxisName::RY;
		_mappings[0].axes[3].min = -1.0f;
		_mappings[0].axes[3].max = 1.0f;
		_mappings[0].axes[4].name = AxisName::LTRIGGER;
		_mappings[0].axes[4].min = 0.0f;
		_mappings[0].axes[4].max = 1.0f;
		_mappings[0].axes[5].name = AxisName::RTRIGGER;
		_mappings[0].axes[5].min = 0.0f;
		_mappings[0].axes[5].max = 1.0f;

		_mappings[0].buttons[0] = ButtonName::LBUMPER;
		_mappings[0].buttons[1] = ButtonName::LSTICK;
		_mappings[0].buttons[2] = ButtonName::RBUMPER;
		_mappings[0].buttons[3] = ButtonName::RSTICK;
		_mappings[0].buttons[4] = ButtonName::A;
		_mappings[0].buttons[5] = ButtonName::B;
		_mappings[0].buttons[6] = ButtonName::UNKNOWN;
		_mappings[0].buttons[7] = ButtonName::GUIDE;
		_mappings[0].buttons[8] = ButtonName::BACK;
		_mappings[0].buttons[9] = ButtonName::START;
		_mappings[0].buttons[10] = ButtonName::X;
		_mappings[0].buttons[11] = ButtonName::Y;

		_mappings[0].hats[0] = ButtonName::DPAD_UP;
		_mappings[0].hats[1] = ButtonName::DPAD_DOWN;
		_mappings[0].hats[2] = ButtonName::DPAD_RIGHT;
		_mappings[0].hats[3] = ButtonName::DPAD_LEFT;
	}

	void JoyMapping::MappedJoystick::Guid::fromString(const char* string)
	{
	}

	bool JoyMapping::MappedJoystick::Guid::operator==(const Guid& guid) const
	{
		return false;
	}

	void JoyMapping::init(const IInputManager* inputManager)
	{
		//ASSERT(inputManager);
		_inputManager = inputManager;
	}

	bool JoyMapping::addMappingFromString(const char* mappingString)
	{
		return false;
	}

	void JoyMapping::addMappingsFromStrings(const char** mappingStrings)
	{
	}

	void JoyMapping::addMappingsFromFile(const char* filename)
	{
	}

	void JoyMapping::onJoyButtonPressed(const JoyButtonEvent& event)
	{
		if (_inputEventHandler == nullptr)
			return;

		const int idToIndex = _mappingIndex[event.joyId];
		if (idToIndex != -1 &&
			event.buttonId >= 0 && event.buttonId < static_cast<int>(MappedJoystick::MaxNumButtons)) {
			_mappedButtonEvent.joyId = event.joyId;
			_mappedButtonEvent.buttonName = _mappings[idToIndex].buttons[event.buttonId];
			if (_mappedButtonEvent.buttonName != ButtonName::UNKNOWN) {
				const int buttonId = static_cast<int>(_mappedButtonEvent.buttonName);
				_mappedJoyStates[event.joyId]._buttons[buttonId] = true;
				_inputEventHandler->OnJoyMappedButtonPressed(_mappedButtonEvent);
			}
		}
	}

	void JoyMapping::onJoyButtonReleased(const JoyButtonEvent& event)
	{
		if (_inputEventHandler == nullptr)
			return;

		const int idToIndex = _mappingIndex[event.joyId];
		if (idToIndex != -1 &&
			event.buttonId >= 0 && event.buttonId < static_cast<int>(MappedJoystick::MaxNumButtons)) {
			_mappedButtonEvent.joyId = event.joyId;
			_mappedButtonEvent.buttonName = _mappings[idToIndex].buttons[event.buttonId];
			if (_mappedButtonEvent.buttonName != ButtonName::UNKNOWN) {
				const int buttonId = static_cast<int>(_mappedButtonEvent.buttonName);
				_mappedJoyStates[event.joyId]._buttons[buttonId] = false;
				_inputEventHandler->OnJoyMappedButtonReleased(_mappedButtonEvent);
			}
		}
	}

	void JoyMapping::onJoyHatMoved(const JoyHatEvent& event)
	{
		if (_inputEventHandler == nullptr)
			return;

		const int idToIndex = _mappingIndex[event.joyId];
		// Only the first gamepad hat is mapped
		if (idToIndex != -1 && event.hatId == 0 &&
			_mappedJoyStates[event.joyId]._lastHatState != event.hatState) {
			_mappedButtonEvent.joyId = event.joyId;

			const unsigned char oldHatState = _mappedJoyStates[event.joyId]._lastHatState;
			const unsigned char newHatState = event.hatState;

			const unsigned char firstHatValue = HatState::UP;
			const unsigned char lastHatValue = HatState::LEFT;
			for (unsigned char hatValue = firstHatValue; hatValue <= lastHatValue; hatValue *= 2) {
				if ((oldHatState & hatValue) != (newHatState & hatValue)) {
					int hatIndex = hatStateToIndex(hatValue);

					_mappedButtonEvent.buttonName = _mappings[idToIndex].hats[hatIndex];
					if (_mappedButtonEvent.buttonName != ButtonName::UNKNOWN) {
						const int buttonId = static_cast<int>(_mappedButtonEvent.buttonName);
						if (newHatState & hatValue) {
							_mappedJoyStates[event.joyId]._buttons[buttonId] = true;
							_inputEventHandler->OnJoyMappedButtonPressed(_mappedButtonEvent);
						} else {
							_mappedJoyStates[event.joyId]._buttons[buttonId] = false;
							_inputEventHandler->OnJoyMappedButtonReleased(_mappedButtonEvent);
						}
					}
				}
				_mappedJoyStates[event.joyId]._lastHatState = event.hatState;
			}
		}
	}

	void JoyMapping::onJoyAxisMoved(const JoyAxisEvent& event)
	{
		if (_inputEventHandler == nullptr)
			return;

		const int idToIndex = _mappingIndex[event.joyId];
		if (idToIndex != -1 &&
			event.axisId >= 0 && event.axisId < static_cast<int>(MappedJoystick::MaxNumAxes)) {
			const MappedJoystick::Axis& axis = _mappings[idToIndex].axes[event.axisId];

			_mappedAxisEvent.joyId = event.joyId;
			_mappedAxisEvent.axisName = axis.name;
			if (_mappedAxisEvent.axisName != AxisName::UNKNOWN) {
				const float value = (event.value + 1.0f) * 0.5f;
				_mappedAxisEvent.value = axis.min + value * (axis.max - axis.min);
				_mappedJoyStates[event.joyId]._axesValues[static_cast<int>(axis.name)] = _mappedAxisEvent.value;
				_inputEventHandler->OnJoyMappedAxisMoved(_mappedAxisEvent);
			}
		}
	}

	bool JoyMapping::onJoyConnected(const JoyConnectionEvent& event)
	{
		const char* joyName = _inputManager->joyName(event.joyId);

		// There is only one mapping for QGamepad
		_mappingIndex[event.joyId] = 0;
		LOGI("Joystick mapping found for \"{}\" ({})", joyName, event.joyId);

		return (_mappingIndex[event.joyId] != -1);
	}

	void JoyMapping::onJoyDisconnected(const JoyConnectionEvent& event)
	{
		_mappingIndex[event.joyId] = -1;
	}

	bool JoyMapping::isJoyMapped(int joyId) const
	{
		return true;
	}

	const JoyMappedStateImpl& JoyMapping::joyMappedState(int joyId) const
	{
		if (joyId < 0 || joyId > MaxNumJoysticks)
			return _nullMappedJoyState;
		else
			return _mappedJoyStates[joyId];
	}

	void JoyMapping::deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const
	{
		deadZoneValue = nctl::clamp(deadZoneValue, 0.0f, 1.0f);

		if (joyVector.length() <= deadZoneValue)
			joyVector = Vector2f::Zero;
		else {
			float normalizedLength = (joyVector.length() - deadZoneValue) / (1.0f - deadZoneValue);
			normalizedLength = nctl::clamp(normalizedLength, 0.0f, 1.0f);
			joyVector = joyVector.normalize() * normalizedLength;
		}
	}

	void JoyMapping::checkConnectedJoystics()
	{
	}

	int JoyMapping::findMappingByGuid(const MappedJoystick::Guid& guid) const
	{
		return 0;
	}

	int JoyMapping::findMappingByName(const char* name) const
	{
		return 0;
	}

	bool JoyMapping::parseMappingFromString(const char* mappingString, MappedJoystick& map)
	{
		return false;
	}

	bool JoyMapping::parsePlatformKeyword(const char* start, const char* end) const
	{
		return false;
	}

	bool JoyMapping::parsePlatformName(const char* start, const char* end) const
	{
		return false;
	}

	int JoyMapping::parseAxisName(const char* start, const char* end) const
	{
		return -1;
	}

	int JoyMapping::parseButtonName(const char* start, const char* end) const
	{
		return -1;
	}

	int JoyMapping::parseAxisMapping(const char* start, const char* end, MappedJoystick::Axis& axis) const
	{
		return -1;
	}

	int JoyMapping::parseButtonMapping(const char* start, const char* end) const
	{
		return -1;
	}

	int JoyMapping::parseHatMapping(const char* start, const char* end) const
	{
		return -1;
	}

	int JoyMapping::hatStateToIndex(unsigned char hatState) const
	{
		switch (hatState) {
			case HatState::UP: return 0;
			case HatState::DOWN: return 1;
			case HatState::RIGHT: return 2;
			case HatState::LEFT: return 3;
			default: return 0;
		}
	}

	void JoyMapping::trimSpaces(const char** start, const char** end) const
	{
	}
}

#endif