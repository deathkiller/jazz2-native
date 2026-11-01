#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/**
	 * @brief Plays a sound sample at a position in the level
	 *
	 * The position is in level pixels and makes the sound louder or quieter depending on the distance to the
	 * players. `volume` is on a scale where 64 is full volume, and 0 or less is silent. `frequency` is the playback
	 * rate in Hz, 0 plays the sample at its own rate.
	 */
	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency);
	/**
	 * @brief Plays a looping sound sample at a position in the level and returns its channel
	 *
	 * Pass 0 as `channel` the first time, then the returned channel on every following call, once per game tick for
	 * as long as the sound should continue; the sound follows the latest position and stops when the calls stop.
	 * Returns 0 if the sample can't be played. `volume` and `frequency` work as in @ref jjSample().
	 */
	std::int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency);
	/** @brief Plays a sound sample at full volume regardless of where the players are */
	void playPrioritySample(std::int32_t sample);
	/** @brief Returns whether a sound sample is available to play */
	bool isSampleLoaded(std::int32_t sample);
	/**
	 * @brief Loads a sound sample from a file and returns whether it succeeded
	 *
	 * The loaded sound replaces the sample for every later use of that @ref SOUND::Sample value in the script.
	 */
	bool loadSample(std::int32_t sample, const String& filename);

	// TODO

	/**
	 * @brief Jumps to a position in the playing module music
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset);
	/**
	 * @brief Gradually changes the volume of a channel of the playing module music
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds);
	/**
	 * @brief Returns the current order (pattern position) of the playing module music
	 *
	 * @partialsupport Currently always returns 0.
	 */
	std::int32_t jjGetModOrder();
	/**
	 * @brief Returns the current row of the playing module music
	 *
	 * @partialsupport Currently always returns 0.
	 */
	std::int32_t jjGetModRow();
	/**
	 * @brief Returns the tempo of the playing module music
	 *
	 * @partialsupport Currently always returns 0.
	 */
	std::int32_t jjGetModTempo();
	/**
	 * @brief Sets the tempo of the playing module music
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjSetModTempo(std::uint8_t speed);
	/**
	 * @brief Returns the speed of the playing module music
	 *
	 * @partialsupport Currently always returns 0.
	 */
	std::int32_t jjGetModSpeed();
	/**
	 * @brief Sets the speed of the playing module music
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjSetModSpeed(std::uint8_t speed);
}

#endif
