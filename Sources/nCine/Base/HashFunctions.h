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
	using hash_t = std::uint32_t;
	using hash64_t = std::uint64_t;

	/** @brief Invalid hash value */
	const hash_t NullHash = static_cast<hash_t>(~0);

	/// Fowler-Noll-Vo Hash (FNV-1a)
	/*!
	 * For more information: http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
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

	/// xxHash hash function (32-bit)
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

	/// xxHash hash function (64-bit)
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