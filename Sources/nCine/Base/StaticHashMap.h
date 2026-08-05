#pragma once

#include <new>

#include "Algorithms.h"
#include "HashFunctions.h"
#include "ReverseIterator.h"
#include "../../Main.h"

namespace nCine
{
	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst> class StaticHashMapIterator;
	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst> struct StaticHashMapHelperTraits;

	/**
		@brief Statically allocated hashmap with open addressing and leapfrog probing
		
		Stores up to `Capacity` key/value pairs in fixed-size buffers embedded in the object, so no
		heap allocation occurs. Collisions are resolved with open addressing using a leapfrog probing
		scheme based on per-bucket delta offsets.
	*/
	template<class K, class T, std::uint32_t Capacity, class HashFunc = xxHash32Func<K>>
	class StaticHashMap
	{
	public:
		/** @brief Iterator type */
		using Iterator = StaticHashMapIterator<K, T, HashFunc, Capacity, false>;
		/** @brief Constant iterator type */
		using ConstIterator = StaticHashMapIterator<K, T, HashFunc, Capacity, true>;
		/** @brief Reverse iterator type */
		using ReverseIterator = nCine::ReverseIterator<Iterator>;
		/** @brief Constant reverse iterator type */
		using ConstReverseIterator = nCine::ReverseIterator<ConstIterator>;

		inline StaticHashMap()
			: _size(0) {
			init();
		}
		inline ~StaticHashMap() {
			destructNodes();
		}

		StaticHashMap(const StaticHashMap& other);
		StaticHashMap(StaticHashMap&& other);
		StaticHashMap& operator=(const StaticHashMap& other);
		StaticHashMap& operator=(StaticHashMap&& other);

		/** @brief Returns an iterator to the first element */
		Iterator begin();
		/** @brief Returns a reverse iterator to the last element */
		ReverseIterator rbegin();
		/** @brief Returns an iterator past the last element */
		Iterator end();
		/** @brief Returns a reverse iterator before the first element */
		ReverseIterator rend();

		/** @brief Returns a constant iterator to the first element */
		ConstIterator begin() const;
		/** @brief Returns a constant reverse iterator to the last element */
		ConstReverseIterator rbegin() const;
		/** @brief Returns a constant iterator past the last element */
		ConstIterator end() const;
		/** @brief Returns a constant reverse iterator before the first element */
		ConstReverseIterator rend() const;

		/** @brief Returns a constant iterator to the first element */
		inline ConstIterator cbegin() const {
			return begin();
		}
		/** @brief Returns a constant reverse iterator to the last element */
		inline ConstReverseIterator crbegin() const {
			return rbegin();
		}
		/** @brief Returns a constant iterator past the last element */
		inline ConstIterator cend() const {
			return end();
		}
		/** @brief Returns a constant reverse iterator before the first element */
		inline ConstReverseIterator crend() const {
			return rend();
		}

		/** @brief Returns a reference to the value of the given key, inserting a default-constructed one if absent */
		T& operator[](const K& key);
		/** @brief Inserts a copy of the value if no element with the same key exists */
		bool insert(const K& key, const T& value);
		/** @brief Inserts the value by move if no element with the same key exists */
		bool insert(const K& key, T&& value);
		/** @brief Constructs the value in place if no element with the same key exists */
		template <typename... Args> bool emplace(const K& key, Args &&... args);

		/** @brief Returns the maximum number of elements the hashmap can hold */
		inline std::uint32_t capacity() const {
			return Capacity;
		}
		/** @brief Returns `true` if the hashmap contains no elements */
		inline bool empty() const {
			return _size == 0;
		}
		/** @brief Returns the number of elements in the hashmap */
		inline std::uint32_t size() const {
			return _size;
		}
		/** @brief Returns the ratio between used and total buckets */
		inline float loadFactor() const {
			return _size / float(Capacity);
		}
		/** @brief Returns the hash of the given key */
		inline hash_t hash(const K& key) const {
			return _hashFunc(key);
		}

		/** @brief Removes all elements from the hashmap */
		void clear();
		/** @brief Copies the value for the given key into `returnedValue` if present, returning whether it was found */
		bool contains(const K& key, T& returnedValue) const;
		/** @brief Returns a pointer to the value for the given key, or `nullptr` if not found */
		T* find(const K& key);
		/** @brief Returns a read-only pointer to the value for the given key, or `nullptr` if not found */
		const T* find(const K& key) const;
		/** @brief Removes the element with the given key if it exists */
		bool remove(const K& key);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		/// The template class for the node stored inside the hashmap
		class Node
		{
		public:
			K key;
			T value;

			Node() {}
			explicit Node(K kk)
				: key(kk) {}
			Node(K kk, const T& vv)
				: key(kk), value(vv) {}
			Node(K kk, T&& vv)
				: key(kk), value(std::move(vv)) {}
			template <typename... Args>
			Node(K kk, Args &&... args)
				: key(kk), value(std::forward<Args>(args)...) {}
		};
#endif

		std::uint32_t _size;
		std::uint8_t _delta1[Capacity];
		std::uint8_t _delta2[Capacity];
		hash_t _hashes[Capacity];
		std::uint8_t _nodesBuffer[Capacity * sizeof(Node)];
		Node* _nodes = reinterpret_cast<Node*>(_nodesBuffer);
		HashFunc _hashFunc;

		void init();
		void destructNodes();
		bool findBucketIndex(const K& key, std::uint32_t& foundIndex, std::uint32_t& prevFoundIndex) const;
		inline bool findBucketIndex(const K& key, std::uint32_t& foundIndex) const;
		std::uint32_t addDelta1(std::uint32_t bucketIndex) const;
		std::uint32_t addDelta2(std::uint32_t bucketIndex) const;
		std::uint32_t calcNewDelta(std::uint32_t bucketIndex, std::uint32_t newIndex) const;
		std::uint32_t linearSearch(std::uint32_t index, hash_t hash, const K& key) const;
		bool bucketFoundOrEmpty(std::uint32_t index, hash_t hash, const K& key) const;
		bool bucketFound(std::uint32_t index, hash_t hash, const K& key) const;
		T& addNode(std::uint32_t index, hash_t hash, const K& key);
		void insertNode(std::uint32_t index, hash_t hash, const K& key, const T& value);
		void insertNode(std::uint32_t index, hash_t hash, const K& key, T&& value);
		template <typename... Args> void emplaceNode(std::uint32_t index, hash_t hash, const K& key, Args &&... args);

		friend class StaticHashMapIterator<K, T, HashFunc, Capacity, false>;
		friend class StaticHashMapIterator<K, T, HashFunc, Capacity, true>;
		friend struct StaticHashMapHelperTraits<K, T, HashFunc, Capacity, false>;
		friend struct StaticHashMapHelperTraits<K, T, HashFunc, Capacity, true>;
	};

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::Iterator StaticHashMap<K, T, Capacity, HashFunc>::begin()
	{
		Iterator iterator(this, Iterator::SentinelTagInit::Beginning);
		return ++iterator;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::ReverseIterator StaticHashMap<K, T, Capacity, HashFunc>::rbegin()
	{
		Iterator iterator(this, Iterator::SentinelTagInit::End);
		return ReverseIterator(--iterator);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::Iterator StaticHashMap<K, T, Capacity, HashFunc>::end()
	{
		return Iterator(this, Iterator::SentinelTagInit::End);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::ReverseIterator StaticHashMap<K, T, Capacity, HashFunc>::rend()
	{
		Iterator iterator(this, Iterator::SentinelTagInit::Beginning);
		return ReverseIterator(iterator);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	inline typename StaticHashMap<K, T, Capacity, HashFunc>::ConstIterator StaticHashMap<K, T, Capacity, HashFunc>::begin() const
	{
		ConstIterator iterator(this, ConstIterator::SentinelTagInit::Beginning);
		return ++iterator;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::ConstReverseIterator StaticHashMap<K, T, Capacity, HashFunc>::rbegin() const
	{
		ConstIterator iterator(this, ConstIterator::SentinelTagInit::End);
		return ConstReverseIterator(--iterator);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	inline typename StaticHashMap<K, T, Capacity, HashFunc>::ConstIterator StaticHashMap<K, T, Capacity, HashFunc>::end() const
	{
		return ConstIterator(this, ConstIterator::SentinelTagInit::End);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	typename StaticHashMap<K, T, Capacity, HashFunc>::ConstReverseIterator StaticHashMap<K, T, Capacity, HashFunc>::rend() const
	{
		ConstIterator iterator(this, ConstIterator::SentinelTagInit::Beginning);
		return ConstReverseIterator(iterator);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	StaticHashMap<K, T, Capacity, HashFunc>::StaticHashMap(const StaticHashMap<K, T, Capacity, HashFunc>& other)
		: _size(other._size)
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			if (other._hashes[i] != NullHash) {
				new (_nodes + i) Node(other._nodes[i]);
			}
			_delta1[i] = other._delta1[i];
			_delta2[i] = other._delta2[i];
			_hashes[i] = other._hashes[i];
		}
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	StaticHashMap<K, T, Capacity, HashFunc>::StaticHashMap(StaticHashMap<K, T, Capacity, HashFunc>&& other)
		: _size(other._size)
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			if (other._hashes[i] != NullHash) {
				new (_nodes + i) Node(std::move(other._nodes[i]));
			}
			_delta1[i] = other._delta1[i];
			_delta2[i] = other._delta2[i];
			_hashes[i] = other._hashes[i];
		}
		other.destructNodes();
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	StaticHashMap<K, T, Capacity, HashFunc>& StaticHashMap<K, T, Capacity, HashFunc>::operator=(const StaticHashMap<K, T, Capacity, HashFunc>& other)
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			if (other._hashes[i] != NullHash) {
				if (_hashes[i] != NullHash) {
					_nodes[i] = other._nodes[i];
				} else {
					new (_nodes + i) Node(other._nodes[i]);
				}
			} else if (_hashes[i] != NullHash) {
				destructObject(_nodes + i);
			}
			_delta1[i] = other._delta1[i];
			_delta2[i] = other._delta2[i];
			_hashes[i] = other._hashes[i];
		}
		_size = other._size;

		return *this;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	StaticHashMap<K, T, Capacity, HashFunc>& StaticHashMap<K, T, Capacity, HashFunc>::operator=(StaticHashMap<K, T, Capacity, HashFunc>&& other)
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			if (other._hashes[i] != NullHash) {
				if (_hashes[i] != NullHash) {
					_nodes[i] = std::move(other._nodes[i]);
				} else {
					new (_nodes + i) Node(std::move(other._nodes[i]));
				}
			} else if (_hashes[i] != NullHash) {
				destructObject(_nodes + i);
			}
			_delta1[i] = other._delta1[i];
			_delta2[i] = other._delta2[i];
			_hashes[i] = other._hashes[i];
		}
		_size = other._size;
		other.destructNodes();

		return *this;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	T& StaticHashMap<K, T, Capacity, HashFunc>::operator[](const K& key)
	{
		const hash_t hash = _hashFunc(key);
		std::uint32_t bucketIndex = hash % Capacity;

		if (bucketFoundOrEmpty(bucketIndex, hash, key) == false) {
			if (_delta1[bucketIndex] != 0) {
				bucketIndex = addDelta1(bucketIndex);
				if (bucketFound(bucketIndex, hash, key) == false) {
					while (_delta2[bucketIndex] != 0) {
						bucketIndex = addDelta2(bucketIndex);
						// Found at ideal index + delta1 + (n * delta2)
						if (bucketFound(bucketIndex, hash, key)) {
							return _nodes[bucketIndex].value;
						}
					}

					// Adding at ideal index + delta1 + (n * delta2)
					const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
					_delta2[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
					return addNode(newIndex, hash, key);
				} else {
					// Found at ideal index + delta1
					return _nodes[bucketIndex].value;
				}
			} else {
				// Adding at ideal index + delta1
				const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
				_delta1[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
				return addNode(newIndex, hash, key);
			}
		} else {
			// Using the ideal bucket index for the node
			if (_hashes[bucketIndex] == NullHash) {
				return addNode(bucketIndex, hash, key);
			} else {
				return _nodes[bucketIndex].value;
			}
		}
	}

	/** @return `true` if the element has been inserted */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::insert(const K& key, const T& value)
	{
		const hash_t hash = _hashFunc(key);
		std::uint32_t bucketIndex = hash % Capacity;

		if (bucketFoundOrEmpty(bucketIndex, hash, key) == false) {
			if (_delta1[bucketIndex] != 0) {
				bucketIndex = addDelta1(bucketIndex);
				if (bucketFound(bucketIndex, hash, key) == false) {
					while (_delta2[bucketIndex] != 0) {
						bucketIndex = addDelta2(bucketIndex);
						// Found at ideal index + delta1 + (n * delta2)
						if (bucketFound(bucketIndex, hash, key)) {
							return false;
						}
					}

					// Adding at ideal index + delta1 + (n * delta2)
					const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
					_delta2[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
					insertNode(newIndex, hash, key, value);
					return true;
				} else {
					// Found at ideal index + delta1
					return false;
				}
			} else {
				// Adding at ideal index + delta1
				const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
				_delta1[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
				insertNode(newIndex, hash, key, value);
				return true;
			}
		} else {
			// Using the ideal bucket index for the node
			if (_hashes[bucketIndex] == NullHash) {
				insertNode(bucketIndex, hash, key, value);
				return true;
			} else {
				return false;
			}
		}
	}

	/** @return `true` if the element has been inserted */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::insert(const K& key, T&& value)
	{
		const hash_t hash = _hashFunc(key);
		std::uint32_t bucketIndex = hash % Capacity;

		if (bucketFoundOrEmpty(bucketIndex, hash, key) == false) {
			if (_delta1[bucketIndex] != 0) {
				bucketIndex = addDelta1(bucketIndex);
				if (bucketFound(bucketIndex, hash, key) == false) {
					while (_delta2[bucketIndex] != 0) {
						bucketIndex = addDelta2(bucketIndex);
						// Found at ideal index + delta1 + (n * delta2)
						if (bucketFound(bucketIndex, hash, key)) {
							return false;
						}
					}

					// Adding at ideal index + delta1 + (n * delta2)
					const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
					_delta2[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
					insertNode(newIndex, hash, key, std::move(value));
					return true;
				} else {
					// Found at ideal index + delta1
					return false;
				}
			} else {
				// Adding at ideal index + delta1
				const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
				_delta1[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
				insertNode(newIndex, hash, key, std::move(value));
				return true;
			}
		} else {
			// Using the ideal bucket index for the node
			if (_hashes[bucketIndex] == NullHash) {
				insertNode(bucketIndex, hash, key, std::move(value));
				return true;
			} else {
				return false;
			}
		}
	}

	/** @return `true` if the element has been emplaced */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	template<typename... Args>
	bool StaticHashMap<K, T, Capacity, HashFunc>::emplace(const K& key, Args &&... args)
	{
		const hash_t hash = _hashFunc(key);
		std::uint32_t bucketIndex = hash % Capacity;

		if (bucketFoundOrEmpty(bucketIndex, hash, key) == false) {
			if (_delta1[bucketIndex] != 0) {
				bucketIndex = addDelta1(bucketIndex);
				if (bucketFound(bucketIndex, hash, key) == false) {
					while (_delta2[bucketIndex] != 0) {
						bucketIndex = addDelta2(bucketIndex);
						// Found at ideal index + delta1 + (n * delta2)
						if (bucketFound(bucketIndex, hash, key)) {
							return false;
						}
					}

					// Adding at ideal index + delta1 + (n * delta2)
					const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
					_delta2[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
					emplaceNode(newIndex, hash, key, std::forward<Args>(args)...);
					return true;
				} else {
					// Found at ideal index + delta1
					return false;
				}
			} else {
				// Adding at ideal index + delta1
				const std::uint32_t newIndex = linearSearch(bucketIndex + 1, hash, key);
				_delta1[bucketIndex] = calcNewDelta(bucketIndex, newIndex);
				emplaceNode(newIndex, hash, key, std::forward<Args>(args)...);
				return true;
			}
		} else {
			// Using the ideal bucket index for the node
			if (_hashes[bucketIndex] == NullHash) {
				emplaceNode(bucketIndex, hash, key, std::forward<Args>(args)...);
				return true;
			} else {
				return false;
			}
		}
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	void StaticHashMap<K, T, Capacity, HashFunc>::clear()
	{
		destructNodes();
		init();
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::contains(const K& key, T& returnedValue) const
	{
		std::uint32_t bucketIndex = 0;
		const bool found = findBucketIndex(key, bucketIndex);

		if (found) {
			returnedValue = _nodes[bucketIndex].value;
		}
		return found;
	}

	/** @note Prefer this method if copying `T` is expensive, but always check the validity of the returned pointer */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	T* StaticHashMap<K, T, Capacity, HashFunc>::find(const K& key)
	{
		std::uint32_t bucketIndex = 0;
		const bool found = findBucketIndex(key, bucketIndex);

		T* returnedPtr = nullptr;
		if (found) {
			returnedPtr = &_nodes[bucketIndex].value;
		}
		return returnedPtr;
	}

	/** @note Prefer this method if copying `T` is expensive, but always check the validity of the returned pointer */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	const T* StaticHashMap<K, T, Capacity, HashFunc>::find(const K& key) const
	{
		std::uint32_t bucketIndex = 0;
		const bool found = findBucketIndex(key, bucketIndex);

		const T* returnedPtr = nullptr;
		if (found) {
			returnedPtr = &_nodes[bucketIndex].value;
		}
		return returnedPtr;
	}

	/** @return `true` if the element has been found and removed */
	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::remove(const K& key)
	{
		std::uint32_t foundBucketIndex = 0;
		std::uint32_t prevFoundBucketIndex = 0;
		const bool found = findBucketIndex(key, foundBucketIndex, prevFoundBucketIndex);
		std::uint32_t bucketIndex = foundBucketIndex;

		if (found) {
			// The found bucket is the last of the chain, previous one needs a delta fix
			if (foundBucketIndex != _hashes[foundBucketIndex] % Capacity && _delta2[foundBucketIndex] == 0) {
				if (addDelta1(prevFoundBucketIndex) == foundBucketIndex) {
					_delta1[prevFoundBucketIndex] = 0;
				} else if (addDelta2(prevFoundBucketIndex) == foundBucketIndex) {
					_delta2[prevFoundBucketIndex] = 0;
				}
			}

			while (_delta1[bucketIndex] != 0 || _delta2[bucketIndex] != 0) {
				std::uint32_t lastBucketIndex = bucketIndex;
				if (_delta1[lastBucketIndex] != 0) {
					lastBucketIndex = addDelta1(lastBucketIndex);
				}
				if (_delta2[lastBucketIndex] != 0) {
					std::uint32_t secondLastBucketIndex = lastBucketIndex;
					while (_delta2[lastBucketIndex] != 0) {
						secondLastBucketIndex = lastBucketIndex;
						lastBucketIndex = addDelta2(lastBucketIndex);
					}
					_delta2[secondLastBucketIndex] = 0;
				} else {
					_delta1[bucketIndex] = 0;
				}
				if (bucketIndex != lastBucketIndex) {
					_nodes[bucketIndex].key = std::move(_nodes[lastBucketIndex].key);
					_nodes[bucketIndex].value = std::move(_nodes[lastBucketIndex].value);
					_hashes[bucketIndex] = _hashes[lastBucketIndex];
				}

				bucketIndex = lastBucketIndex;
			}

			_hashes[bucketIndex] = NullHash;
			destructObject(_nodes + bucketIndex);
			_size--;
		}

		return found;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	void StaticHashMap<K, T, Capacity, HashFunc>::init()
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			_delta1[i] = 0;
		}
		for (std::uint32_t i = 0; i < Capacity; i++) {
			_delta2[i] = 0;
		}
		for (std::uint32_t i = 0; i < Capacity; i++) {
			_hashes[i] = NullHash;
		}
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	void StaticHashMap<K, T, Capacity, HashFunc>::destructNodes()
	{
		for (std::uint32_t i = 0; i < Capacity; i++) {
			if (_hashes[i] != NullHash) {
				destructObject(_nodes + i);
				_hashes[i] = NullHash;
			}
		}
		_size = 0;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::findBucketIndex(const K& key, std::uint32_t& foundIndex, std::uint32_t& prevFoundIndex) const
	{
		if (_size == 0)
			return false;

		bool found = false;
		const hash_t hash = _hashFunc(key);
		foundIndex = hash % Capacity;
		prevFoundIndex = foundIndex;

		if (bucketFoundOrEmpty(foundIndex, hash, key) == false) {
			if (_delta1[foundIndex] != 0) {
				prevFoundIndex = foundIndex;
				foundIndex = addDelta1(foundIndex);
				if (bucketFound(foundIndex, hash, key) == false) {
					while (_delta2[foundIndex] != 0) {
						prevFoundIndex = foundIndex;
						foundIndex = addDelta2(foundIndex);
						if (bucketFound(foundIndex, hash, key)) {
							// Found at ideal index + delta1 + (n * delta2)
							found = true;
							break;
						}
					}
				} else {
					// Found at ideal index + delta1
					found = true;
				}
			}
		} else {
			if (_hashes[foundIndex] != NullHash) {
				// Found at ideal bucket index
				found = true;
			}
		}

		return found;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::findBucketIndex(const K& key, std::uint32_t& foundIndex) const
	{
		std::uint32_t prevFoundIndex = 0;
		return findBucketIndex(key, foundIndex, prevFoundIndex);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	std::uint32_t StaticHashMap<K, T, Capacity, HashFunc>::addDelta1(std::uint32_t bucketIndex) const
	{
		std::uint32_t newIndex = bucketIndex + _delta1[bucketIndex];
		if (newIndex > Capacity - 1) {
			newIndex -= Capacity;
		}
		return newIndex;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	std::uint32_t StaticHashMap<K, T, Capacity, HashFunc>::addDelta2(std::uint32_t bucketIndex) const
	{
		std::uint32_t newIndex = bucketIndex + _delta2[bucketIndex];
		if (newIndex > Capacity - 1) {
			newIndex -= Capacity;
		}
		return newIndex;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	std::uint32_t StaticHashMap<K, T, Capacity, HashFunc>::calcNewDelta(std::uint32_t bucketIndex, std::uint32_t newIndex) const
	{
		std::uint32_t delta = 0;
		if (newIndex >= bucketIndex) {
			delta = newIndex - bucketIndex;
		} else {
			delta = Capacity - bucketIndex + newIndex;
		}
		FATAL_ASSERT(delta < 256); // deltas are uint8_t
		return delta;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	std::uint32_t StaticHashMap<K, T, Capacity, HashFunc>::linearSearch(std::uint32_t index, hash_t hash, const K& key) const
	{
		for (std::uint32_t i = index; i < Capacity; i++) {
			if (bucketFoundOrEmpty(i, hash, key)) {
				return i;
			}
		}

		for (std::uint32_t i = 0; i < index; i++) {
			if (bucketFoundOrEmpty(i, hash, key)) {
				return i;
			}
		}

		return index;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::bucketFoundOrEmpty(std::uint32_t index, hash_t hash, const K& key) const
	{
		return (_hashes[index] == NullHash || (_hashes[index] == hash && _nodes[index].key == key));
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	bool StaticHashMap<K, T, Capacity, HashFunc>::bucketFound(std::uint32_t index, hash_t hash, const K& key) const
	{
		return (_hashes[index] == hash && _nodes[index].key == key);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	T& StaticHashMap<K, T, Capacity, HashFunc>::addNode(std::uint32_t index, hash_t hash, const K& key)
	{
		FATAL_ASSERT(_size < Capacity);
		FATAL_ASSERT(_hashes[index] == NullHash);

		_size++;
		_hashes[index] = hash;
		new (_nodes + index) Node(key);

		return _nodes[index].value;
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	void StaticHashMap<K, T, Capacity, HashFunc>::insertNode(std::uint32_t index, hash_t hash, const K& key, const T& value)
	{
		FATAL_ASSERT(_size < Capacity);
		FATAL_ASSERT(_hashes[index] == NullHash);

		_size++;
		_hashes[index] = hash;
		new (_nodes + index) Node(key, value);
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	void StaticHashMap<K, T, Capacity, HashFunc>::insertNode(std::uint32_t index, hash_t hash, const K& key, T&& value)
	{
		FATAL_ASSERT(_size < Capacity);
		FATAL_ASSERT(_hashes[index] == NullHash);

		_size++;
		_hashes[index] = hash;
		new (_nodes + index) Node(key, std::move(value));
	}

	template<class K, class T, std::uint32_t Capacity, class HashFunc>
	template<typename... Args>
	void StaticHashMap<K, T, Capacity, HashFunc>::emplaceNode(std::uint32_t index, hash_t hash, const K& key, Args &&... args)
	{
		FATAL_ASSERT(_size < Capacity);
		FATAL_ASSERT(_hashes[index] == NullHash);

		_size++;
		_hashes[index] = hash;
		new (_nodes + index) Node(key, std::forward<Args>(args)...);
	}
}
