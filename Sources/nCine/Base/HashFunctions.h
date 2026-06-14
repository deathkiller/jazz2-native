#pragma once

#include <cstdint>
#include <cstring>

#include <Containers/Pair.h>
#include <Containers/String.h>
#include <Cryptography/xxHash.h>

using namespace Death::Containers;
using namespace Death::Cryptography;

namespace nCine
{
	/** @brief 32-bit hash value */
	using hash_t = std::uint32_t;
	/** @brief 64-bit hash value */
	using hash64_t = std::uint64_t;

	/** @brief Reserved hash value marking an empty or invalid entry */
	const hash_t NullHash = static_cast<hash_t>(~0);

	/**
		@brief Fowler-Noll-Vo (FNV-1a) hash function
		
		Computes a 32-bit hash by iterating over the raw bytes of the key. Specializations are provided
		for C strings, @ref Death::Containers::String and @ref Death::Containers::Pair.

		@see http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	*/
	template<class K>
	class FNV1aHashFunc
	{
	public:
		hash_t operator()(const K& key) const
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
			hash_t hash = static_cast<hash_t>(Seed);
			for (unsigned int i = 0; i < sizeof(K); i++) {
				hash = fnv1a(bytes[i], hash);
			}
			return hash;
		}

	private:
		static const hash_t Prime = 0x01000193; //  16777619
		static const hash_t Seed = 0x811C9DC5; // 2166136261

		inline hash_t fnv1a(const unsigned char oneByte, hash_t hash = Seed) const
		{
			return (oneByte ^ hash) * Prime;
		}
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<>
	class FNV1aHashFunc<char*>
	{
	public:
		hash_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			hash_t hash = static_cast<hash_t>(Seed);
			for (unsigned int i = 0; i < length; i++) {
				hash = fnv1a(key[i], hash);
			}
			return hash;
		}

	private:
		static const hash_t Prime = 0x01000193; //  16777619
		static const hash_t Seed = 0x811C9DC5; // 2166136261

		inline hash_t fnv1a(const char oneByte, hash_t hash = Seed) const
		{
			return (oneByte ^ hash) * Prime;
		}
	};

	template<>
	class FNV1aHashFunc<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			const unsigned int length = (unsigned int)string.size();
			hash_t hash = static_cast<hash_t>(Seed);
			for (unsigned int i = 0; i < length; i++) {
				hash = fnv1a(static_cast<hash_t>(string[i]), hash);
			}
			return hash;
		}

	private:
		static const hash_t Prime = 0x01000193; //  16777619
		static const hash_t Seed = 0x811C9DC5; // 2166136261

		inline hash_t fnv1a(unsigned char oneByte, hash_t hash = Seed) const
		{
			return (oneByte ^ hash) * Prime;
		}
	};

	template<class F, class S>
	class FNV1aHashFunc<Pair<F, S>>
	{
	public:
		hash_t operator()(const Pair<F, S>& pair) const
		{
			return FNV1aHashFunc<F>()(pair.first()) ^ FNV1aHashFunc<S>()(pair.second());
		}
	};
#endif

	/**
		@brief xxHash3 hash function producing a 32-bit value
		
		Hashes the raw bytes of the key with xxHash3 and truncates the result to 32 bits.
		Specializations are provided for C strings, @ref Death::Containers::String and
		@ref Death::Containers::Pair.
	*/
	template<class K>
	class xxHash32Func
	{
	public:
		hash_t operator()(const K& key) const
		{
			return hash_t(xxHash3(reinterpret_cast<const char*>(&key), sizeof(K)));
		}
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<>
	class xxHash32Func<char*>
	{
	public:
		hash64_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			return hash_t(xxHash3(key, length));
		}
	};

	template<>
	class xxHash32Func<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			return hash_t(xxHash3(string.data(), string.size()));
		}
	};

	template<class F, class S>
	class xxHash32Func<Pair<F, S>>
	{
	public:
		hash_t operator()(const Pair<F, S>& pair) const
		{
			return xxHash32Func<F>()(pair.first()) ^ xxHash32Func<S>()(pair.second());
		}
	};
#endif

	/**
		@brief xxHash3 hash function producing a 64-bit value
		
		Hashes the raw bytes of the key with xxHash3. Specializations are provided for C strings,
		@ref Death::Containers::String and @ref Death::Containers::Pair.
	*/
	template<class K>
	class xxHash64Func
	{
	public:
		hash64_t operator()(const K& key) const
		{
			return hash64_t(xxHash3(reinterpret_cast<const char*>(&key), sizeof(K)));
		}
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<>
	class xxHash64Func<char*>
	{
	public:
		hash64_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			return hash64_t(xxHash3(key, length));
		}
	};

	template<>
	class xxHash64Func<String>
	{
	public:
		hash64_t operator()(const String& string) const
		{
			return hash64_t(xxHash3(string.data(), string.size()));
		}
	};

	template<class F, class S>
	class xxHash64Func<Pair<F, S>>
	{
	public:
		hash64_t operator()(const Pair<F, S>& pair) const
		{
			return xxHash64Func<F>()(pair.first()) ^ xxHash64Func<S>()(pair.second());
		}
	};
#endif
}