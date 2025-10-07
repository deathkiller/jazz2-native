#pragma once

#include "../Common.h"

namespace Death { namespace Cryptography {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Computes 64-bit digest of given data using **xxHash3** algorithm */
	std::uint64_t xxHash3(const void* data, std::size_t length);

	/** @overload */
	std::uint64_t xxHash3(const void* data, std::size_t length, std::uint64_t seed);

}}