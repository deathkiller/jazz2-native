#include "IAudioPlayer.h"
#include "IAudioDevice.h"
#include "../CommonConstants.h"
#include "../ServiceLocator.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	IAudioPlayer::IAudioPlayer(ObjectType type)
		: Object(type), sourceId_(IAudioDevice::UnavailableSource), state_(PlayerState::Stopped), flags_(PlayerFlags::None),
			gain_(1.0f), pitch_(1.0f), lowPass_(1.0f), position_(0.0f, 0.0f, 0.0f)
	{
	}

	IAudioPlayer::~IAudioPlayer()
	{
	}

	std::int32_t IAudioPlayer::sampleOffset() const
	{
		if (sourceId_ != IAudioDevice::UnavailableSource) {
			return theServiceLocator().GetAudioDevice().sourceSampleOffset(sourceId_);
		}
		return 0;
	}

	void IAudioPlayer::setSampleOffset(std::int32_t offset)
	{
		if (sourceId_ != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().setSourceSampleOffset(sourceId_, offset);
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::setSourceRelative(bool value)
	{
		if (GetFlags(PlayerFlags::SourceRelative) != value) {
			SetFlags(PlayerFlags::SourceRelative, value);
			if (sourceId_ != IAudioDevice::UnavailableSource) {
				theServiceLocator().GetAudioDevice().setSourceRelative(sourceId_, value);
			}
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::setGain(float gain)
	{
		gain_ = gain;
		if (sourceId_ != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().setSourceGain(sourceId_, gain_);
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::setPitch(float pitch)
	{
		pitch_ = pitch;
		if (sourceId_ != IAudioDevice::UnavailableSource) {
			theServiceLocator().GetAudioDevice().setSourcePitch(sourceId_, pitch_);
		}
	}

	void IAudioPlayer::setLowPass(float value)
	{
		if (lowPass_ != value) {
			lowPass_ = value;
			if (sourceId_ != IAudioDevice::UnavailableSource) {
				updateFilters();
			}
		}
	}

	// The change is applied to the backend source only when a source is assigned (playing or paused)
	void IAudioPlayer::setPosition(const Vector3f& position)
	{
		position_ = position;
		if (sourceId_ != IAudioDevice::UnavailableSource) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			setPositionInternal(getAdjustedPosition(device, position_, GetFlags(PlayerFlags::SourceRelative), GetFlags(PlayerFlags::As2D)));
		}
	}

	void IAudioPlayer::updateFilters()
	{
		theServiceLocator().GetAudioDevice().setSourceLowPass(sourceId_, lowPass_);
	}

	void IAudioPlayer::setPositionInternal(const Vector3f& position)
	{
		theServiceLocator().GetAudioDevice().setSourcePosition(sourceId_, position);
	}

	Vector3f IAudioPlayer::getAdjustedPosition(IAudioDevice& device, const Vector3f& pos, bool isSourceRelative, bool isAs2D)
	{
		if (isAs2D) {
			// Let's do a +/- 30° panning for 2D audio, locked to front
			Vector2f panningPos = Vector2f::FromAngleLength(30.0f * fDegToRad * pos.X, 1.0f);
			return Vector3(panningPos.X, 0.0f, -std::abs(panningPos.Y));
		}

		Vector3f listenerPos;
		Vector3f adjustedPos = Vector3f(pos.X * IAudioDevice::LengthToPhysical, pos.Y * -IAudioDevice::LengthToPhysical, pos.Z * -IAudioDevice::LengthToPhysical);

		if (!isSourceRelative) {
			listenerPos = device.getListenerPosition();
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
