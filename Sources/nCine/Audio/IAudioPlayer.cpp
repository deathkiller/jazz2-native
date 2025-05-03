#if defined(WITH_AUDIO)
#	define NCINE_INCLUDE_OPENAL
#	include "../CommonHeaders.h"
#endif

#include "IAudioPlayer.h"
#include "IAudioDevice.h"
#include "../CommonConstants.h"
#include "../ServiceLocator.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	IAudioPlayer::IAudioPlayer(ObjectType type)
		: Object(type), sourceId_(IAudioDevice::UnavailableSource), state_(PlayerState::Stopped), flags_(PlayerFlags::None),
			gain_(1.0f), pitch_(1.0f), lowPass_(1.0f), position_(0.0f, 0.0f, 0.0f), filterHandle_(0)
	{
	}

	IAudioPlayer::~IAudioPlayer()
	{
#if defined(WITH_AUDIO) && defined(OPENAL_FILTERS_SUPPORTED)
		if (filterHandle_ != 0) {
			alDeleteFilters(1, &filterHandle_);
			filterHandle_ = 0;
		}
#endif
	}

	std::int32_t IAudioPlayer::sampleOffset() const
	{
#if defined(WITH_AUDIO)
		ALint byteOffset = 0;
		alGetSourcei(sourceId_, AL_SAMPLE_OFFSET, &byteOffset);
		return byteOffset;
#else
		return 0;
#endif
	}

	void IAudioPlayer::setSampleOffset(std::int32_t byteOffset)
	{
#if defined(WITH_AUDIO)
		alSourcei(sourceId_, AL_SAMPLE_OFFSET, byteOffset);
#endif
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setSourceRelative(bool value)
	{
		if (GetFlags(PlayerFlags::SourceRelative) != value) {
			SetFlags(PlayerFlags::SourceRelative, value);
#if defined(WITH_AUDIO)
			if (state_ == PlayerState::Playing) {
				alSourcei(sourceId_, AL_SOURCE_RELATIVE, value ? AL_TRUE : AL_FALSE);
			}
#endif
		}
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setGain(float gain)
	{
		gain_ = gain;
#if defined(WITH_AUDIO)
		if (state_ == PlayerState::Playing) {
			alSourcef(sourceId_, AL_GAIN, gain_);
		}
#endif
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPitch(float pitch)
	{
		pitch_ = pitch;
#if defined(WITH_AUDIO)
		if (state_ == PlayerState::Playing) {
			alSourcef(sourceId_, AL_PITCH, pitch_);
		}
#endif
	}

	void IAudioPlayer::setLowPass(float value)
	{
		if (lowPass_ != value) {
			lowPass_ = value;
			if (state_ == PlayerState::Playing) {
				updateFilters();
			}
		}
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPosition(const Vector3f& position)
	{
		position_ = position;
		if (state_ == PlayerState::Playing) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			setPositionInternal(getAdjustedPosition(device, position_, GetFlags(PlayerFlags::SourceRelative), GetFlags(PlayerFlags::As2D)));
		}
	}

	void IAudioPlayer::updateFilters()
	{
#if defined(WITH_AUDIO) && defined(OPENAL_FILTERS_SUPPORTED)
		if (lowPass_ < 1.0f) {
			if (filterHandle_ == 0) {
				alGenFilters(1, &filterHandle_);
				alFilteri(filterHandle_, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
				alFilterf(filterHandle_, AL_LOWPASS_GAIN, 1.0f);
			}
			if (filterHandle_ != 0) {
				alFilterf(filterHandle_, AL_LOWPASS_GAINHF, lowPass_);
				alSourcei(sourceId_, AL_DIRECT_FILTER, filterHandle_);
			}
		} else {
			if (filterHandle_ != 0) {
				alFilterf(filterHandle_, AL_LOWPASS_GAINHF, 1.0f);
			}
			alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
		}
#endif
	}

	void IAudioPlayer::setPositionInternal(const Vector3f& position)
	{
#if defined(WITH_AUDIO)
		alSource3f(sourceId_, AL_POSITION, position.X, position.Y, position.Z);
#endif
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
