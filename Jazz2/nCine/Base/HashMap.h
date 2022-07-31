#pragma once

#include "HashFunctions.h"
#include "ParallelHashMap/phmap.h"

namespace nCine
{
	// Use flat_hash_map from Parallel Hashmap library
	template <class K, class V,
		class Hash = FNV1aHashFunc<K>,
		class Eq = phmap::priv::hash_default_eq<K>>
	using HashMap = phmap::flat_hash_map<K, V, Hash, Eq>;
}

