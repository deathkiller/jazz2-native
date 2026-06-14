#pragma once

#include "HashFunctions.h"
#include "parallel_hashmap/phmap.h"

namespace nCine
{
	/**
		@brief Generic hash map
		
		Alias for `phmap::flat_hash_map` from the Parallel Hashmap library, defaulting to the xxHash3
		hash function sized to the target architecture.
	*/
	template <class K, class V,
#if defined(DEATH_TARGET_32BIT)
		class Hash = xxHash32Func<K>,
#else
		class Hash = xxHash64Func<K>,
#endif
		class Eq = phmap::priv::hash_default_eq<K>>
	using HashMap = phmap::flat_hash_map<K, V, Hash, Eq>;
}

