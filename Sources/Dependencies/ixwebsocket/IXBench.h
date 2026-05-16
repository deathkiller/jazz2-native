/*
 *  IXBench.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2020 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace ix
{
	/**
		@brief Simple benchmarking utility for measuring code execution time.

		Records start, stop, and duration for code blocks, and can report results.
	*/
	class Bench
	{
	public:
		/**
		 * @brief Construct a new Bench object.
		 * @param description Description of the benchmarked code.
		 */
		Bench(const std::string& description);
		/** @brief Destructor. */
		~Bench();

		/** @brief Reset the benchmark timer. */
		void reset();
		/** @brief Record the current time as a checkpoint. */
		void record();
		/** @brief Report the benchmark result. */
		void report();
		/** @brief Mark the benchmark as reported. */
		void setReported();
		/** @brief Get the measured duration in microseconds. */
		uint64_t getDuration() const;

	private:
		/** @brief Description of the benchmark. */
		std::string _description;
		/** @brief Start time point. */
		std::chrono::time_point<std::chrono::high_resolution_clock> _start;
		/** @brief Measured duration in microseconds. */
		uint64_t _duration;
		/** @brief True if the result has been reported. */
		bool _reported;
	};
}
