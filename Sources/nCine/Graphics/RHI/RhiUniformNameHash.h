#pragma once

#include <cstdint>

#include <Containers/StringView.h>

namespace nCine::RHI
{
	/**
		@brief Fingerprint of a uniform name, so a lookup compares integers instead of strings

		Uniforms are resolved BY NAME on the hot path: `Font::DrawString` asks for three block members
		(texRect, spriteSize and color) for every glyph it draws, so a text-heavy frame such as the main
		menu runs the lookup a couple of thousand times. Comparing each candidate as a string means chasing
		a separately heap-allocated name per entry, and on the console CPUs the fixed-function backends run
		on those scattered loads cost far more than the comparison itself. Hashing the query once and
		walking a packed array of 32-bit fingerprints keeps the whole scan within a cache line or two; the
		string comparison then runs exactly once, on the entry whose fingerprint matched, so a collision can
		never return the wrong uniform.

		Shared by every backend that resolves uniforms on the CPU (Software, GX, PVR, GU, GS, RDP), so the
		scheme can only ever change in one place.
	*/
	inline std::uint32_t HashUniformName(Death::Containers::StringView name)
	{
		// FNV-1a, 32-bit (the same constants as nCine::FNV1aHashFunc, restated so this header stays
		// free of the HashMap machinery)
		std::uint32_t hash = 0x811C9DC5u;
		for (std::size_t i = 0; i < name.size(); i++) {
			hash = (hash ^ std::uint8_t(name[i])) * 0x01000193u;
		}
		return hash;
	}
}
