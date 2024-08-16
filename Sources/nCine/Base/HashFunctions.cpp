#include "HashFunctions.h"

#include <Base/Memory.h>

#if defined(DEATH_TARGET_APPLE)
#	include <libkern/OSByteOrder.h>
#endif

namespace nCine
{
	// Compression function for Merkle-Damgard construction.
	uint64_t fasthash_mix(uint64_t h)
	{
		h ^= h >> 23;
		h *= 0x2127599bf4325c37ULL;
		h ^= h >> 47;
		return h;
	}

	uint64_t fasthash64(const void* buf, size_t len, uint64_t seed)
	{
		const uint64_t m = 0x880355f21e6d1965ULL;
		const uint64_t* pos = reinterpret_cast<const uint64_t*>(buf);
		const uint64_t* end = pos + (len / 8);
		uint64_t h = seed ^ (len * m);
		uint64_t v = 0;

		while (pos <= end) {
			v = *pos++;
			h ^= fasthash_mix(v);
			h *= m;
		}

		const unsigned char* pos2 = reinterpret_cast<const unsigned char*>(pos);
		v = 0;

		switch (len & 7) {
			case 7: v ^= static_cast<uint64_t>(pos2[6]) << 48;
			case 6: v ^= static_cast<uint64_t>(pos2[5]) << 40;
			case 5: v ^= static_cast<uint64_t>(pos2[4]) << 32;
			case 4: v ^= static_cast<uint64_t>(pos2[3]) << 24;
			case 3: v ^= static_cast<uint64_t>(pos2[2]) << 16;
			case 2: v ^= static_cast<uint64_t>(pos2[1]) << 8;
			case 1: v ^= static_cast<uint64_t>(pos2[0]);
				h ^= fasthash_mix(v);
				h *= m;
		}

		return fasthash_mix(h);
	}

	uint32_t fasthash32(const void* buf, size_t len, uint32_t seed)
	{
		// The following trick converts the 64-bit hashcode to Fermat residue, which
		// shall retain information from both the higher and lower parts of hashcode.
		uint64_t h = fasthash64(buf, len, seed);
		return static_cast<uint32_t>(h - (h >> 32));
	}

	// CityHash
#if defined(__has_builtin)
#	define DEATH_HAS_BUILTIN(x) __has_builtin(x)
#else
#	define DEATH_HAS_BUILTIN(x) 0
#endif

	inline std::uint32_t ByteSwap32(std::uint32_t value)
	{
#if defined(DEATH_TARGET_MSVC)
		return _byteswap_ulong(value);
#elif defined(DEATH_TARGET_APPLE)
		return _OSSwapInt32(value);
#elif DEATH_HAS_BUILTIN(__builtin_bswap32) || defined(DEATH_TARGET_GCC)
		return __builtin_bswap32(value);
#else
		return (((value & std::uint32_t{0xFF}) << 24) |
				((value & std::uint32_t{0xFF00}) << 8) |
				((value & std::uint32_t{0xFF0000}) >> 8) |
				((value & std::uint32_t{0xFF000000}) >> 24));
#endif
	}

	inline std::uint64_t ByteSwap64(std::uint64_t value)
	{
#if defined(DEATH_TARGET_MSVC)
		return _byteswap_uint64(value);
#elif defined(DEATH_TARGET_APPLE)
		return _OSSwapInt64(value);
#elif DEATH_HAS_BUILTIN(__builtin_bswap64) || defined(DEATH_TARGET_GCC)
		return __builtin_bswap64(value);
#else
		return (((value & std::uint64_t{0xFF}) << 56) |
				((value & std::uint64_t{0xFF00}) << 40) |
				((value & std::uint64_t{0xFF0000}) << 24) |
				((value & std::uint64_t{0xFF000000}) << 8) |
				((value & std::uint64_t{0xFF00000000}) >> 8) |
				((value & std::uint64_t{0xFF0000000000}) >> 24) |
				((value & std::uint64_t{0xFF000000000000}) >> 40) |
				((value & std::uint64_t{0xFF00000000000000}) >> 56));
#endif
	}

	static std::uint64_t Fetch64(const char* p)
	{
		using Death::Memory;
#if defined(DEATH_TARGET_BIG_ENDIAN)
		return ByteSwap64(Memory::loadUnaligned<std::uint64_t>(p));
#else
		return Memory::loadUnaligned<std::uint64_t>(p);
#endif
	}

	static std::uint32_t Fetch32(const char* p)
	{
		using Death::Memory;
#if defined(DEATH_TARGET_BIG_ENDIAN)
		return ByteSwap32(Memory::loadUnaligned<std::uint32_t>(p));
#else
		return Memory::loadUnaligned<std::uint32_t>(p);
#endif
	}

	// Some primes between 2^63 and 2^64 for various uses.
	static const std::uint64_t k0 = 0xc3a5c85c97cb3127ULL;
	static const std::uint64_t k1 = 0xb492b66fbe98f273ULL;
	static const std::uint64_t k2 = 0x9ae16a3b2f90404fULL;

	// Magic numbers for 32-bit hashing. Copied from Murmur3.
	static const std::uint32_t c1 = 0xcc9e2d51;
	static const std::uint32_t c2 = 0x1b873593;

	// A 32-bit to 32-bit integer hash copied from Murmur3.
	static std::uint32_t fmix(std::uint32_t h)
	{
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}

	static std::uint32_t Rotate32(std::uint32_t val, std::int32_t shift)
	{
		// Avoid shifting by 32: doing so yields an undefined result.
		return (shift == 0 ? val : ((val >> shift) | (val << (32 - shift))));
	}

#undef PERMUTE3
#define PERMUTE3(a, b, c)		\
	do {						\
		std::swap(a, b);		\
		std::swap(a, c);		\
	} while (0)

	static std::uint32_t Mur(std::uint32_t a, std::uint32_t h)
	{
		// Helper from Murmur3 for combining two 32-bit values.
		a *= c1;
		a = Rotate32(a, 17);
		a *= c2;
		h ^= a;
		h = Rotate32(h, 19);
		return h * 5 + 0xe6546b64;
	}

	static std::uint32_t Hash32Len13to24(const char* s, std::size_t len)
	{
		std::uint32_t a = Fetch32(s - 4 + (len >> 1));
		std::uint32_t b = Fetch32(s + 4);
		std::uint32_t c = Fetch32(s + len - 8);
		std::uint32_t d = Fetch32(s + (len >> 1));
		std::uint32_t e = Fetch32(s);
		std::uint32_t f = Fetch32(s + len - 4);
		std::uint32_t h = static_cast<std::uint32_t>(len);
		return fmix(Mur(f, Mur(e, Mur(d, Mur(c, Mur(b, Mur(a, h)))))));
	}

	static std::uint32_t Hash32Len0to4(const char* s, std::size_t len)
	{
		std::uint32_t b = 0;
		std::uint32_t c = 9;
		for (std::size_t i = 0; i < len; i++) {
			signed char v = static_cast<signed char>(s[i]);
			b = b * c1 + static_cast<std::uint32_t>(v);
			c ^= b;
		}
		return fmix(Mur(b, Mur(static_cast<std::uint32_t>(len), c)));
	}

	static std::uint32_t Hash32Len5to12(const char* s, std::size_t len)
	{
		std::uint32_t a = static_cast<std::uint32_t>(len), b = a * 5, c = 9, d = b;
		a += Fetch32(s);
		b += Fetch32(s + len - 4);
		c += Fetch32(s + ((len >> 1) & 4));
		return fmix(Mur(c, Mur(b, Mur(a, d))));
	}

	std::uint32_t CityHash32(const char* s, std::size_t len)
	{
		if (len <= 24) {
			return len <= 12
				? (len <= 4 ? Hash32Len0to4(s, len) : Hash32Len5to12(s, len))
				: Hash32Len13to24(s, len);
		}

		// len > 24
		std::uint32_t h = static_cast<std::uint32_t>(len), g = c1 * h, f = g;

		std::uint32_t a0 = Rotate32(Fetch32(s + len - 4) * c1, 17) * c2;
		std::uint32_t a1 = Rotate32(Fetch32(s + len - 8) * c1, 17) * c2;
		std::uint32_t a2 = Rotate32(Fetch32(s + len - 16) * c1, 17) * c2;
		std::uint32_t a3 = Rotate32(Fetch32(s + len - 12) * c1, 17) * c2;
		std::uint32_t a4 = Rotate32(Fetch32(s + len - 20) * c1, 17) * c2;
		h ^= a0;
		h = Rotate32(h, 19);
		h = h * 5 + 0xe6546b64;
		h ^= a2;
		h = Rotate32(h, 19);
		h = h * 5 + 0xe6546b64;
		g ^= a1;
		g = Rotate32(g, 19);
		g = g * 5 + 0xe6546b64;
		g ^= a3;
		g = Rotate32(g, 19);
		g = g * 5 + 0xe6546b64;
		f += a4;
		f = Rotate32(f, 19);
		f = f * 5 + 0xe6546b64;
		std::size_t iters = (len - 1) / 20;
		do {
			std::uint32_t b0 = Rotate32(Fetch32(s) * c1, 17) * c2;
			std::uint32_t b1 = Fetch32(s + 4);
			std::uint32_t b2 = Rotate32(Fetch32(s + 8) * c1, 17) * c2;
			std::uint32_t b3 = Rotate32(Fetch32(s + 12) * c1, 17) * c2;
			std::uint32_t b4 = Fetch32(s + 16);
			h ^= b0;
			h = Rotate32(h, 18);
			h = h * 5 + 0xe6546b64;
			f += b1;
			f = Rotate32(f, 19);
			f = f * c1;
			g += b2;
			g = Rotate32(g, 18);
			g = g * 5 + 0xe6546b64;
			h ^= b3 + b1;
			h = Rotate32(h, 19);
			h = h * 5 + 0xe6546b64;
			g ^= b4;
			g = ByteSwap32(g) * 5;
			h += b4 * 5;
			h = ByteSwap32(h);
			f += b0;
			PERMUTE3(f, h, g);
			s += 20;
		} while (--iters != 0);
		g = Rotate32(g, 11) * c1;
		g = Rotate32(g, 17) * c1;
		f = Rotate32(f, 11) * c1;
		f = Rotate32(f, 17) * c1;
		h = Rotate32(h + g, 19);
		h = h * 5 + 0xe6546b64;
		h = Rotate32(h, 17) * c1;
		h = Rotate32(h + f, 19);
		h = h * 5 + 0xe6546b64;
		h = Rotate32(h, 17) * c1;
		return h;
	}

	// Bitwise right rotate.  Normally this will compile to a single
	// instruction, especially if the shift is a manifest constant.
	static std::uint64_t Rotate(std::uint64_t val, std::int32_t shift)
	{
		// Avoid shifting by 64: doing so yields an undefined result.
		return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
	}

	static std::uint64_t ShiftMix(std::uint64_t val)
	{
		return val ^ (val >> 47);
	}

	static std::uint64_t HashLen16(std::uint64_t u, std::uint64_t v, std::uint64_t mul)
	{
		// Murmur-inspired hashing.
		std::uint64_t a = (u ^ v) * mul;
		a ^= (a >> 47);
		std::uint64_t b = (v ^ a) * mul;
		b ^= (b >> 47);
		b *= mul;
		return b;
	}

	static std::uint64_t HashLen16(std::uint64_t u, std::uint64_t v)
	{
		const std::uint64_t kMul = 0x9ddfea08eb382d69ULL;
		return HashLen16(u, v, kMul);
	}

	static std::uint64_t HashLen0to16(const char* s, std::size_t len)
	{
		if (len >= 8) {
			std::uint64_t mul = k2 + len * 2;
			std::uint64_t a = Fetch64(s) + k2;
			std::uint64_t b = Fetch64(s + len - 8);
			std::uint64_t c = Rotate(b, 37) * mul + a;
			std::uint64_t d = (Rotate(a, 25) + b) * mul;
			return HashLen16(c, d, mul);
		}
		if (len >= 4) {
			std::uint64_t mul = k2 + len * 2;
			std::uint64_t a = Fetch32(s);
			return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
		}
		if (len > 0) {
			std::uint8_t a = static_cast<std::uint8_t>(s[0]);
			std::uint8_t b = static_cast<std::uint8_t>(s[len >> 1]);
			std::uint8_t c = static_cast<std::uint8_t>(s[len - 1]);
			std::uint32_t y = static_cast<std::uint32_t>(a) + (static_cast<std::uint32_t>(b) << 8);
			std::uint32_t z = static_cast<std::uint32_t>(len) + (static_cast<std::uint32_t>(c) << 2);
			return ShiftMix(y * k2 ^ z * k0) * k2;
		}
		return k2;
	}

	// This probably works well for 16-byte strings as well, but it may be overkill in that case.
	static std::uint64_t HashLen17to32(const char* s, std::size_t len)
	{
		std::uint64_t mul = k2 + len * 2;
		std::uint64_t a = Fetch64(s) * k1;
		std::uint64_t b = Fetch64(s + 8);
		std::uint64_t c = Fetch64(s + len - 8) * mul;
		std::uint64_t d = Fetch64(s + len - 16) * k2;
		return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d, a + Rotate(b + k2, 18) + c, mul);
	}

	// Return a 16-byte hash for 48 bytes.  Quick and dirty.
	// Callers do best to use "random-looking" values for a and b.
	static std::pair<std::uint64_t, std::uint64_t> WeakHashLen32WithSeeds(std::uint64_t w, std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t a, std::uint64_t b)
	{
		a += w;
		b = Rotate(b + a + z, 21);
		std::uint64_t c = a;
		a += x;
		a += y;
		b += Rotate(a, 44);
		return std::make_pair(a + z, b + c);
	}

	// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
	static std::pair<std::uint64_t, std::uint64_t> WeakHashLen32WithSeeds(const char* s, std::uint64_t a, std::uint64_t b)
	{
		return WeakHashLen32WithSeeds(Fetch64(s), Fetch64(s + 8), Fetch64(s + 16), Fetch64(s + 24), a, b);
	}

	// Return an 8-byte hash for 33 to 64 bytes.
	static std::uint64_t HashLen33to64(const char* s, std::size_t len)
	{
		std::uint64_t mul = k2 + len * 2;
		std::uint64_t a = Fetch64(s) * k2;
		std::uint64_t b = Fetch64(s + 8);
		std::uint64_t c = Fetch64(s + len - 24);
		std::uint64_t d = Fetch64(s + len - 32);
		std::uint64_t e = Fetch64(s + 16) * k2;
		std::uint64_t f = Fetch64(s + 24) * 9;
		std::uint64_t g = Fetch64(s + len - 8);
		std::uint64_t h = Fetch64(s + len - 16) * mul;
		std::uint64_t u = Rotate(a + g, 43) + (Rotate(b, 30) + c) * 9;
		std::uint64_t v = ((a + g) ^ d) + f + 1;
		std::uint64_t w = ByteSwap64((u + v) * mul) + h;
		std::uint64_t x = Rotate(e + f, 42) + c;
		std::uint64_t y = (ByteSwap64((v + w) * mul) + g) * mul;
		std::uint64_t z = e + f + c;
		a = ByteSwap64((x + z) * mul + y) + b;
		b = ShiftMix((z + a) * mul + d + h) * mul;
		return b + x;
	}

	std::uint64_t CityHash64(const char* s, std::size_t len)
	{
		if (len <= 32) {
			if (len <= 16) {
				return HashLen0to16(s, len);
			} else {
				return HashLen17to32(s, len);
			}
		} else if (len <= 64) {
			return HashLen33to64(s, len);
		}

		// For strings over 64 bytes we hash the end first, and then as we
		// loop we keep 56 bytes of state: v, w, x, y, and z.
		std::uint64_t x = Fetch64(s + len - 40);
		std::uint64_t y = Fetch64(s + len - 16) + Fetch64(s + len - 56);
		std::uint64_t z = HashLen16(Fetch64(s + len - 48) + len, Fetch64(s + len - 24));
		std::pair<std::uint64_t, std::uint64_t> v = WeakHashLen32WithSeeds(s + len - 64, len, z);
		std::pair<std::uint64_t, std::uint64_t> w = WeakHashLen32WithSeeds(s + len - 32, y + k1, x);
		x = x * k1 + Fetch64(s);

		// Decrease len to the nearest multiple of 64, and operate on 64-byte chunks.
		len = (len - 1) & ~static_cast<std::size_t>(63);
		do {
			x = Rotate(x + y + v.first + Fetch64(s + 8), 37) * k1;
			y = Rotate(y + v.second + Fetch64(s + 48), 42) * k1;
			x ^= w.second;
			y += v.first + Fetch64(s + 40);
			z = Rotate(z + w.first, 33) * k1;
			v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
			w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch64(s + 16));
			std::swap(z, x);
			s += 64;
			len -= 64;
		} while (len != 0);
		return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * k1 + z, HashLen16(v.second, w.second) + x);
	}

	std::uint64_t CityHash64WithSeeds(const char* s, std::size_t len, std::uint64_t seed0, uint64_t seed1)
	{
		return HashLen16(CityHash64(s, len) - seed0, seed1);
	}

	std::uint64_t CityHash64WithSeed(const char* s, std::size_t len, std::uint64_t seed)
	{
		return CityHash64WithSeeds(s, len, k2, seed);
	}
}