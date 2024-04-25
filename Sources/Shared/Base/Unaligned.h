#pragma once

#include "../Common.h"

#include <cstring>

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides unaligned operations for pointers
	*/
	class Unaligned
	{
	public:
		Unaligned() = delete;

		static std::uint16_t Load16(const void* p);
		static std::uint32_t Load32(const void* p);
		static std::uint64_t Load64(const void* p);

		static void Store16(void* p, std::uint16_t v);
		static void Store32(void* p, std::uint32_t v);
		static void Store64(void* p, std::uint64_t v);
	};

	DEATH_ALWAYS_INLINE std::uint16_t Unaligned::Load16(const void* p)
	{
		std::uint16_t v;
		std::memcpy(&v, p, sizeof(v));
		return v;
	}

	DEATH_ALWAYS_INLINE std::uint32_t Unaligned::Load32(const void* p)
	{
		std::uint32_t v;
		std::memcpy(&v, p, sizeof(v));
		return v;
	}

	DEATH_ALWAYS_INLINE std::uint64_t Unaligned::Load64(const void* p)
	{
		std::uint64_t v;
		std::memcpy(&v, p, sizeof(v));
		return v;
	}

	DEATH_ALWAYS_INLINE void Unaligned::Store16(void* p, std::uint16_t v)
	{
		std::memcpy(p, &v, sizeof(v));
	}

	DEATH_ALWAYS_INLINE void Unaligned::Store32(void* p, std::uint32_t v)
	{
		std::memcpy(p, &v, sizeof(v));
	}

	DEATH_ALWAYS_INLINE void Unaligned::Store64(void* p, std::uint64_t v)
	{
		std::memcpy(p, &v, sizeof(v));
	}
}