#pragma once

#include "HashFunctions.h"
#include "ParallelHashMap/phmap.h"

namespace nCine
{
	// Use `flat_hash_map` from Parallel Hashmap library
	template <class K, class V,
#if defined(DEATH_TARGET_32BIT)
		class Hash = CityHash32Func<K>,
#else
		class Hash = CityHash64Func<K>,
#endif
		class Eq = phmap::priv::hash_default_eq<K>>
	using HashMap = phmap::flat_hash_map<K, V, Hash, Eq>;
}

