#include "IAudioPlayer.h"
#include "IAudioDevice.h"
#include "../CommonConstants.h"
#include "../ServiceLocator.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	IAudioPlayer::IAudioPlayer(ObjectType type)
		: Object(type), _sourceId(IAudioDevice::UnavailableSource), _state(PlayerState::Stopped), _flags(PlayerFlags::None),
			_gain(1.0f), _pitch(1.0f), _lowPass(1.0f), _position(0.0f, 0.0f, 0.0f)
	{
	}

	IAudioPlayer::~IAudioPlayer()
	{
	}

	std::int32_t IAudioPlayer::GetSampleOffset() const
	{
		if (_sourceId != IAudioDevice::UnavailableSource) {
			return theServiceLocator().GetAudioDevice().GetSourceSampleOffset(_sourceId);
		}
		return 0;
	}

	void IAudioPlayer::SetSampleOffset(std::int32_t offset)
	{
		if (_sourceId != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().SetSourceSampleOffset(_sourceId, offset);
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::SetSourceRelative(bool value)
	{
		if (GetFlags(PlayerFlags::SourceRelative) != value) {
			SetFlags(PlayerFlags::SourceRelative, value);
			if (_sourceId != IAudioDevice::UnavailableSource) {
				theServiceLocator().GetAudioDevice().SetSourceRelative(_sourceId, value);
			}
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::SetGain(float gain)
	{
		_gain = gain;
		if (_sourceId != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().SetSourceGain(_sourceId, _gain);
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::SetPitch(float pitch)
	{
		_pitch = pitch;
		if (_sourceId != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().SetSourcePitch(_sourceId, _pitch);
		}
	}

	void IAudioPlayer::SetLowPass(float value)
	{
		if (_lowPass != value) {
			_lowPass = value;
			if (_sourceId != IAudioDevice::UnavailableSource) {
				UpdateFilters();
			}
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::SetPosition(const Vector3f& position)
	{
		_position = position;
		if (_sourceId != IAudioDevice::UnavailableSource) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			SetPositionInternal(GetAdjustedPosition(device, _position, GetFlags(PlayerFlags::SourceRelative), GetFlags(PlayerFlags::As2D)));
		}
	}

	void IAudioPlayer::UpdateFilters()
	{
		theServiceLocator().GetAudioDevice().SetSourceLowPass(_sourceId, _lowPass);
	}

	void IAudioPlayer::SetPositionInternal(const Vector3f& position)
	{
		theServiceLocator().GetAudioDevice().SetSourcePosition(_sourceId, position);
	}

	Vector3f IAudioPlayer::GetAdjustedPosition(IAudioDevice& device, const Vector3f& pos, bool isSourceRelative, bool isAs2D)
	{
		if (isAs2D) {
			// Let's do a +/- 30° panning for 2D audio, locked to front
			Vector2f panningPos = Vector2f::FromAngleLength(30.0f * fDegToRad * pos.X, 1.0f);
			return Vector3(panningPos.X, 0.0f, -std::abs(panningPos.Y));
		}

		Vector3f listenerPos;
		Vector3f adjustedPos = Vector3f(pos.X * IAudioDevice::LengthToPhysical, pos.Y * -IAudioDevice::LengthToPhysical, pos.Z * -IAudioDevice::LengthToPhysical);

		if (!isSourceRelative) {
			listenerPos = device.GetListenerPosition();
			listenerPos.X *= IAudioDevice::LengthToPhysical;
			listenerPos.Y *= -IAudioDevice::LengthToPhysical;
			listenerPos.Z *= -IAudioDevice::LengthToPhysical;

			adjustedPos -= listenerPos;
		}

		// Flatten depth position a little, so far away sounds that can still be seen appear louder
		adjustedPos.Z *= 0.5f;

		// Normalize audio position for smooth panning when near. Do it in physical units, so this remains constant regardless of unit changes.
		constexpr float SmoothPanRadius = 26.0f;
		float listenerSpaceDist = adjustedPos.Length();
		if (listenerSpaceDist < SmoothPanRadius) {
			float panningActive = listenerSpaceDist / SmoothPanRadius;
			adjustedPos = Vector3f::Lerp(
								Vector3(0.0f, 0.0f, 1.0f + (SmoothPanRadius - 1.0f) * panningActive),
								adjustedPos,
								panningActive);
		}

		// Ensure the source is always at the front
		adjustedPos.Z = -std::abs(adjustedPos.Z);

		if (!isSourceRelative) {
			adjustedPos += listenerPos;
		}

		return adjustedPos;
	}
}
