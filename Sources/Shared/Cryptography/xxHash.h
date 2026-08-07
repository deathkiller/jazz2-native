#pragma once

/** @file
	@brief Function @ref Death::Cryptography::xxHash3
*/

#include "../Common.h"

namespace Death { namespace Cryptography {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Computes 64-bit digest of given data using the **xxHash3** algorithm
		@param data     Pointer to the beginning of the input buffer
		@param length   Length of the input buffer in bytes
		@returns 64-bit hash of the input

		xxHash3 is a fast, non-cryptographic hash function well suited for hash tables, checksums and
		content deduplication --- it is not intended for security-sensitive use. The result depends only on
		the byte contents of the buffer, so hashing the same bytes always yields the same value. Passing
		@cpp nullptr @ce with zero @p length is valid and returns the hash of an empty input.
	*/
	std::uint64_t xxHash3(const void* data, std::size_t length);

	/**
		@brief Computes 64-bit digest of given data using the **xxHash3** algorithm with a custom seed
		@param data     Pointer to the beginning of the input buffer
		@param length   Length of the input buffer in bytes
		@param seed     Seed value that alters the resulting hash

		Unlike @ref xxHash3(const void*, std::size_t), this overload mixes @p seed into the computation, so
		different seeds produce independent hashes of the same input --- useful for seeded hash tables or to
		reduce the chance of collisions being exploited.
	*/
	std::uint64_t xxHash3(const void* data, std::size_t length, std::uint64_t seed);

}}