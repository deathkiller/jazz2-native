#pragma once

#include "StaticHashMap.h"
#include "Iterator.h"

namespace nCine
{
	/**
		@brief Primary template for the @ref StaticHashMapIterator type traits helper
		
		Specialized on the `IsConst` flag to expose the hashmap pointer and node reference types with
		the appropriate constness.
	*/
	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	struct StaticHashMapHelperTraits
	{};

	/**
		@brief Type traits used by the non-constant @ref StaticHashMapIterator
	*/
	template<class K, class T, class HashFunc, std::uint32_t Capacity>
	struct StaticHashMapHelperTraits<K, T, HashFunc, Capacity, false>
	{
		using HashMapPtr = StaticHashMap<K, T, Capacity, HashFunc>*;
		using NodeReference = typename StaticHashMap<K, T, Capacity, HashFunc>::Node&;
	};

	/**
		@brief Type traits used by the constant @ref StaticHashMapIterator
	*/
	template<class K, class T, class HashFunc, std::uint32_t Capacity>
	struct StaticHashMapHelperTraits<K, T, HashFunc, Capacity, true>
	{
		using HashMapPtr = const StaticHashMap<K, T, Capacity, HashFunc>*;
		using NodeReference = const typename StaticHashMap<K, T, Capacity, HashFunc>::Node&;
	};

	/**
		@brief Bidirectional iterator over the elements of a @ref StaticHashMap
		
		Iterates over occupied buckets, skipping empty ones. The `IsConst` template parameter selects
		between a mutable and a read-only iterator.
	*/
	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	class StaticHashMapIterator
	{
	public:
		/** @brief Reference type that respects the iterator constness */
		using Reference = typename IteratorTraits<StaticHashMapIterator>::Reference;

		/**
		 * @brief Sentinel tag used to initialize the iterator before the first or past the last element
		 */
		enum class SentinelTagInit
		{
			BEGINNING,	/**< Iterator before the first element; the next element is the first one */
			END			/**< Iterator past the last element; the previous element is the last one */
		};

		StaticHashMapIterator(typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::HashMapPtr hashMap, std::uint32_t bucketIndex)
			: hashMap_(hashMap), bucketIndex_(bucketIndex), tag_(SentinelTag::REGULAR) {}

		StaticHashMapIterator(typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::HashMapPtr hashMap, SentinelTagInit tag);

		/** @brief Implicitly converts a non-constant iterator to a constant one */
		StaticHashMapIterator(const StaticHashMapIterator<K, T, HashFunc, Capacity, false>& it)
			: hashMap_(it.hashMap_), bucketIndex_(it.bucketIndex_), tag_(SentinelTag(it.tag_)) {}

		/** @brief Dereferences the iterator, returning the value of the pointed element */
		Reference operator*() const;

		/** @brief Advances to the next element (prefix) */
		StaticHashMapIterator& operator++();
		/** @brief Advances to the next element (postfix) */
		StaticHashMapIterator operator++(int);

		/** @brief Moves to the previous element (prefix) */
		StaticHashMapIterator& operator--();
		/** @brief Moves to the previous element (postfix) */
		StaticHashMapIterator operator--(int);

		/** @brief Returns `true` if both iterators point to the same element */
		friend inline bool operator==(const StaticHashMapIterator& lhs, const StaticHashMapIterator& rhs)
		{
			if (lhs.tag_ == SentinelTag::REGULAR && rhs.tag_ == SentinelTag::REGULAR) {
				return (lhs.hashMap_ == rhs.hashMap_ && lhs.bucketIndex_ == rhs.bucketIndex_);
			} else {
				return (lhs.tag_ == rhs.tag_);
			}
		}

		/** @brief Returns `true` if the iterators point to different elements */
		friend inline bool operator!=(const StaticHashMapIterator& lhs, const StaticHashMapIterator& rhs)
		{
			if (lhs.tag_ == SentinelTag::REGULAR && rhs.tag_ == SentinelTag::REGULAR) {
				return (lhs.hashMap_ != rhs.hashMap_ || lhs.bucketIndex_ != rhs.bucketIndex_);
			} else {
				return (lhs.tag_ != rhs.tag_);
			}
		}

		/** @brief Returns the hashmap node currently pointed to by the iterator */
		typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::NodeReference node() const;
		/** @brief Returns the value of the currently pointed node */
		const T& value() const;
		/** @brief Returns the key of the currently pointed node */
		const K& key() const;
		/** @brief Returns the hash of the currently pointed node */
		hash_t hash() const;

	private:
		// Sentinel tag used to detect the begin and end conditions
		enum SentinelTag {
			REGULAR,	// Iterator pointing to a real element
			BEGINNING,	// Iterator before the first element
			END			// Iterator past the last element
		};

		typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::HashMapPtr hashMap_;
		std::uint32_t bucketIndex_;
		SentinelTag tag_;

		// Makes the iterator point to the next non-empty element in the hashmap
		void next();
		// Makes the iterator point to the previous non-empty element in the hashmap
		void previous();

		// Grants the constant iterator access for the non-constant to constant conversion
		friend class StaticHashMapIterator<K, T, HashFunc, Capacity, true>;
	};

#ifndef DOXYGEN_GENERATING_OUTPUT

	/// Iterator traits structure specialization for `HashMapIterator` class
	template<class K, class T, class HashFunc, std::uint32_t Capacity>
	struct IteratorTraits<StaticHashMapIterator<K, T, HashFunc, Capacity, false>>
	{
		/// Type of the values deferenced by the iterator
		using ValueType = T;
		/// Pointer to the type of the values deferenced by the iterator
		using Pointer = T*;
		/// Reference to the type of the values deferenced by the iterator
		using Reference = T&;
		/// Type trait for iterator category
		static inline BidirectionalIteratorTag IteratorCategory() {
			return BidirectionalIteratorTag();
		}
	};

	/// Iterator traits structure specialization for constant `HashMapIterator` class
	template<class K, class T, class HashFunc, std::uint32_t Capacity>
	struct IteratorTraits<StaticHashMapIterator<K, T, HashFunc, Capacity, true>>
	{
		/// Type of the values deferenced by the iterator (never const)
		using ValueType = T;
		/// Pointer to the type of the values deferenced by the iterator
		using Pointer = const T*;
		/// Reference to the type of the values deferenced by the iterator
		using Reference = const T&;
		/// Type trait for iterator category
		static inline BidirectionalIteratorTag IteratorCategory() {
			return BidirectionalIteratorTag();
		}
	};

#endif

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::StaticHashMapIterator(typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::HashMapPtr hashMap, SentinelTagInit tag)
		: hashMap_(hashMap), bucketIndex_(0)
	{
		switch (tag) {
			case SentinelTagInit::BEGINNING: tag_ = SentinelTag::BEGINNING; break;
			case SentinelTagInit::END: tag_ = SentinelTag::END; break;
		}
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	typename StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::Reference StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::operator*() const
	{
		return node().value;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>& StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::operator++()
	{
		next();
		return *this;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst> StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::operator++(int)
	{
		// Create an unmodified copy to return
		StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst> iterator = *this;
		next();
		return iterator;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>& StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::operator--()
	{
		previous();
		return *this;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst> StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::operator--(int)
	{
		// Create an unmodified copy to return
		StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst> iterator = *this;
		previous();
		return iterator;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	typename StaticHashMapHelperTraits<K, T, HashFunc, Capacity, IsConst>::NodeReference StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::node() const
	{
		return hashMap_->nodes_[bucketIndex_];
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	const T& StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::value() const
	{
		return node().value;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	const K& StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::key() const
	{
		return node().key;
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	hash_t StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::hash() const
	{
		return hashMap_->hashes_[bucketIndex_];
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	void StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::next()
	{
		if (tag_ == SentinelTag::REGULAR) {
			if (bucketIndex_ >= hashMap_->capacity() - 1) {
				tag_ = SentinelTag::END;
				return;
			} else {
				bucketIndex_++;
			}
		} else if (tag_ == SentinelTag::BEGINNING) {
			tag_ = SentinelTag::REGULAR;
			bucketIndex_ = 0;
		} else if (tag_ == SentinelTag::END) {
			return;
		}
		// Search the first non empty index starting from the current one
		while (bucketIndex_ < hashMap_->capacity() - 1 && hashMap_->hashes_[bucketIndex_] == NullHash) {
			bucketIndex_++;
		}
		if (hashMap_->hashes_[bucketIndex_] == NullHash) {
			tag_ = SentinelTag::END;
		}
	}

	template<class K, class T, class HashFunc, std::uint32_t Capacity, bool IsConst>
	void StaticHashMapIterator<K, T, HashFunc, Capacity, IsConst>::previous()
	{
		if (tag_ == SentinelTag::REGULAR) {
			if (bucketIndex_ == 0) {
				tag_ = SentinelTag::BEGINNING;
				return;
			} else {
				bucketIndex_--;
			}
		} else if (tag_ == SentinelTag::END) {
			tag_ = SentinelTag::REGULAR;
			bucketIndex_ = hashMap_->capacity() - 1;
		} else if (tag_ == SentinelTag::BEGINNING) {
			return;
		}
		// Search the first non empty index starting from the current one
		while (bucketIndex_ > 0 && hashMap_->hashes_[bucketIndex_] == NullHash) {
			bucketIndex_--;
		}
		if (hashMap_->hashes_[bucketIndex_] == NullHash) {
			tag_ = SentinelTag::BEGINNING;
		}
	}
}
