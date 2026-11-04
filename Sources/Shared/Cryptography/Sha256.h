#pragma once

/** @file
	@brief Class @ref Death::Cryptography::Sha256
*/

#include "../Common.h"

namespace Death { namespace Cryptography {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Computes **SHA-256** digest of data fed in parts

		Unlike @ref xxHash3(), it's a cryptographic hash, so it's suitable for verifying downloaded files and for
		content-addressed storage of files that came from outside. Data can be passed by @ref Update() in parts of
		any size, the digest is then returned by @ref Finish().
	*/
	class Sha256
	{
	public:
		/** @brief Length of the digest in bytes */
		static constexpr std::size_t DigestLength = 32;

		Sha256();

		/** @brief Starts a new digest, everything passed so far is forgotten */
		void Reset();
		/** @brief Adds data to the digest */
		void Update(const void* data, std::size_t length);
		/** @brief Completes the digest and writes it to @p digest, the instance has to be reset before reuse */
		void Finish(std::uint8_t (&digest)[DigestLength]);

		/** @brief Computes the digest of a single buffer */
		static void Compute(const void* data, std::size_t length, std::uint8_t (&digest)[DigestLength]);

	private:
		std::uint32_t _state[8];
		std::uint64_t _totalLength;
		std::uint8_t _buffer[64];
		std::size_t _bufferLength;

		void ProcessBlock(const std::uint8_t* block);
	};

}}
