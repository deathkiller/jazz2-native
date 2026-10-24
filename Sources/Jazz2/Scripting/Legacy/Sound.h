#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Plays a sound sample at the given position */
	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency);
	/** @brief Plays a looped sound sample on a channel and returns the channel */
	std::int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency);
	/** @brief Plays a sound sample with priority, ignoring position */
	void playPrioritySample(std::int32_t sample);
	/** @brief Returns whether the given sample is loaded */
	bool isSampleLoaded(std::int32_t sample);
	/** @brief Loads a sound sample from a file */
	bool loadSample(std::int32_t sample, const String& filename);

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset);
	/** @brief Slides a music channel's volume to the given level over time */
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds);
	/** @brief Returns the current music order */
	std::int32_t jjGetModOrder();
	/** @brief Returns the current music row */
	std::int32_t jjGetModRow();
	/** @brief Returns the current music tempo */
	std::int32_t jjGetModTempo();
	/** @brief Sets the music tempo */
	void jjSetModTempo(std::uint8_t speed);
	/** @brief Returns the current music speed */
	std::int32_t jjGetModSpeed();
	/** @brief Sets the music speed */
	void jjSetModSpeed(std::uint8_t speed);
}

#endif
