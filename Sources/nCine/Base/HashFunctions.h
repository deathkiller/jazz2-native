#pragma once

#include <cstdint>
#include <cstring>

#include <Containers/Pair.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	using hash_t = uint32_t;
	using hash64_t = uint64_t;
	const hash_t NullHash = static_cast<hash_t>(~0);

	/// Hash function returning always the first hashmap bucket, for debug purposes
	template<class K>
	class FixedHashFunc
	{
	public:
		hash_t operator()(const K& key) const {
			return static_cast<hash_t>(0);
		}
	};

	/// Hash function returning the modulo of the key, for debug purposes
	template<class K, unsigned int Value>
	class ModuloHashFunc
	{
	public:
		hash_t operator()(const K& key) const {
			return static_cast<hash_t>(key % Value);
		}
	};

	/// Hash function returning the key unchanged
	/*! The key type should be convertible to `hash_t.` */
	template<class K>
	class IdentityHashFunc
	{
	public:
		hash_t operator()(const K& key) const {
			return static_cast<hash_t>(key);
		}
	};

	/// Shift-Add-XOR hash function
	template<class K>
	class SaxHashFunc
	{
	public:
		hash_t operator()(const K& key) const
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < sizeof(K); i++)
				hash ^= (hash << 5) + (hash >> 2) + static_cast<hash_t>(bytes[i]);

			return hash;
		}
	};

	/// Shift-Add-XOR hash function
	/*!
	 * \note Specialized version of the function for C-style strings
	 */
	template<>
	class SaxHashFunc<char*>
	{
	public:
		hash_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < length; i++)
				hash ^= (hash << 5) + (hash >> 2) + static_cast<hash_t>(key[i]);

			return hash;
		}
	};

	/// Shift-Add-XOR hash function
	/*!
	 * \note Specialized version of the function for String objects
	 */
	template<>
	class SaxHashFunc<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			const unsigned int length = (unsigned int)string.size();
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < length; i++)
				hash ^= (hash << 5) + (hash >> 2) + static_cast<hash_t>(string[i]);

			return hash;
		}
	};

	/// Jenkins hash function
	/*!
	 * For more information: http://en.wikipedia.org/wiki/Jenkins_hash_function
	 */
	template<class K>
	class JenkinsHashFunc
	{
	public:
		hash_t operator()(const K& key) const
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < sizeof(K); i++) {
				hash += static_cast<hash_t>(bytes[i]);
				hash += (hash << 10);
				hash ^= (hash >> 6);
			}
			hash += (hash << 3);
			hash ^= (hash >> 11);
			hash += (hash << 15);

			return hash;
		}
	};

	/// Jenkins hash function
	/*!
	 * \note Specialized version of the function for C-style strings
	 *
	 * For more information: http://en.wikipedia.org/wiki/Jenkins_hash_function
	 */
	template<>
	class JenkinsHashFunc<char*>
	{
	public:
		hash_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < length; i++) {
				hash += static_cast<hash_t>(key[i]);
				hash += (hash << 10);
				hash ^= (hash >> 6);
			}
			hash += (hash << 3);
			hash ^= (hash >> 11);
			hash += (hash << 15);

			return hash;
		}
	};

	/// Jenkins hash function
	/*!
	 * \note Specialized version of the function for String objects
	 *
	 * For more information: http://en.wikipedia.org/wiki/Jenkins_hash_function
	 */
	template<>
	class JenkinsHashFunc<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			const unsigned int length = (unsigned int)string.size();
			hash_t hash = static_cast<hash_t>(0);
			for (unsigned int i = 0; i < length; i++) {
				hash += static_cast<hash_t>(string[i]);
				hash += (hash << 10);
				hash ^= (hash >> 6);
			}
			hash += (hash << 3);
			hash ^= (hash >> 11);
			hash += (hash << 15);

			return hash;
		}
	};

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
			for (unsigned int i = 0; i < sizeof(K); i++)
				hash = fnv1a(bytes[i], hash);

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

	/*!
	 * \note Specialized version of the function for C-style strings
	 *
	 * For more information: http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	 */
	template<>
	class FNV1aHashFunc<char*>
	{
	public:
		hash_t operator()(const char*& key) const
		{
			const unsigned int length = (unsigned int)strlen(key);
			hash_t hash = static_cast<hash_t>(Seed);
			for (unsigned int i = 0; i < length; i++)
				hash = fnv1a(key[i], hash);

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

	/// Fowler-Noll-Vo Hash (FNV-1a)
	/*!
	 * \note Specialized version of the function for String objects
	 *
	 * For more information: http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	 */
	template<>
	class FNV1aHashFunc<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			const unsigned int length = (unsigned int)string.size();
			hash_t hash = static_cast<hash_t>(Seed);
			for (unsigned int i = 0; i < length; i++)
				hash = fnv1a(static_cast<hash_t>(string[i]), hash);

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

	uint64_t fasthash64(const void* buf, size_t len, uint64_t seed);
	uint32_t fasthash32(const void* buf, size_t len, uint32_t seed);

	/// fast-hash
	/*!
	 * For more information: https://github.com/ztanml/fast-hash
	 */
	template<class K>
	class FastHashFunc
	{
	public:
		hash_t operator()(const K& key) const
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
			const hash_t hash = fasthash32(bytes, sizeof(K), Seed);

			return hash;
		}

	private:
		static const uint64_t Seed = 0x01000193811C9DC5;
	};

	/// fast-hash
	/*!
	 * \note Specialized version of the function for C-style strings
	 *
	 * For more information: https://github.com/ztanml/fast-hash
	 */
	template<>
	class FastHashFunc<const char*>
	{
	public:
		hash_t operator()(const char* key) const
		{
			return fasthash32(key, strlen(key), Seed);
		}

	private:
		static const uint32_t Seed = 0x811C9DC5;
	};

	/// fast-hash
	/*!
	 * \note Specialized version of the function for String objects
	 *
	 * For more information: https://github.com/ztanml/fast-hash
	 */
	template<>
	class FastHashFunc<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			return fasthash32(string.data(), string.size(), Seed);
		}

	private:
		static const uint32_t Seed = 0x811C9DC5;
	};

	/// CityHash
	std::uint64_t CityHash64(const char* s, std::size_t len);

	std::uint64_t CityHash64WithSeed(const char* s, std::size_t len, std::uint64_t seed);

	std::uint64_t CityHash64WithSeeds(const char* s, std::size_t len, std::uint64_t seed0, std::uint64_t seed1);

	std::uint32_t CityHash32(const char* s, std::size_t len);

	template<class K>
	class CityHash32Func
	{
	public:
		hash_t operator()(const K& key) const
		{
			return CityHash32(reinterpret_cast<const char*>(&key), sizeof(K));
		}
	};

	template<>
	class CityHash32Func<String>
	{
	public:
		hash_t operator()(const String& string) const
		{
			return CityHash32(string.data(), string.size());
		}
	};

	template<class F, class S>
	class CityHash32Func<Pair<F, S>>
	{
	public:
		hash_t operator()(const Pair<F, S>& pair) const
		{
			return CityHash32Func<F>()(pair.first()) ^ CityHash32Func<S>()(pair.second());
		}
	};

	template<class K>
	class CityHash64Func
	{
	public:
		hash64_t operator()(const K& key) const
		{
			return CityHash64(reinterpret_cast<const char*>(&key), sizeof(K));
		}
	};

	template<>
	class CityHash64Func<String>
	{
	public:
		hash64_t operator()(const String& string) const
		{
			return CityHash64(string.data(), string.size());
		}
	};

	template<class F, class S>
	class CityHash64Func<Pair<F, S>>
	{
	public:
		hash64_t operator()(const Pair<F, S>& pair) const
		{
			return CityHash64Func<F>()(pair.first()) ^ CityHash64Func<S>()(pair.second());
		}
	};
}