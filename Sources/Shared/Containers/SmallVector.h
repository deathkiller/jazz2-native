// Contains parts of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.

#pragma once

#include "ArrayView.h"
#include "Tags.h"
#include "../Asserts.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <type_traits>

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Base class of @ref SmallVector
		@tparam SizeT   Type of the size parameter

		The template parameter specifies the type which should be used to hold the Size
		and Capacity of `SmallVector`, so it can be adjusted. Using 32-bit size is desirable
		to shrink the size of `SmallVector`. Using 64-bit size is desirable for cases like
		@cpp SmallVector<char> @ce, where a 32 bit size would limit the vector to ~4GB.
	*/
	template<class SizeT> class SmallVectorBase
	{
	protected:
		/** @brief Pointer to the first element */
		void* BeginX;
		/** @brief Number of elements in the vector */
		SizeT Size = 0;
		/** @brief Capacity of the vector */
		SizeT Capacity;

		/** @brief Maximum value of the `SizeT` type used */
		static constexpr std::size_t getSizeTypeMax() {
			return std::numeric_limits<SizeT>::max();
		}

		SmallVectorBase() = delete;
		SmallVectorBase(void* firstEl, std::size_t totalCapacity)
			: BeginX(firstEl), Capacity(static_cast<SizeT>(totalCapacity)) {}

		/** @brief Creates a new allocation big enough for @p minSize and pass back its size in @p newCapacity */
		void* mallocForGrow(void* firstEl, std::size_t minSize, std::size_t typeSize, std::size_t& newCapacity);

		/** @brief Creates a new allocation for shrinking operation */
		void* mallocForShrink(void* firstEl, std::size_t newCapacity, std::size_t typeSize);

		/** @brief Grows the allocated memory (without initializing new elements) for trivial types */
		void growTrivial(void* firstEl, std::size_t minSize, std::size_t typeSize);

	public:
		/** @brief Returns the number of elements */
		std::size_t size() const {
			return Size;
		}
		
		/** @brief Returns the number of elements that can be held in currently allocated storage */
		std::size_t capacity() const {
			return Capacity;
		}

		/** @brief Returns whether the container is empty */
		bool empty() const {
			return !Size;
		}

	protected:
		/** @brief Sets internal vector size */
		void setSize(std::size_t n) {
			DEATH_DEBUG_ASSERT(n <= capacity());
			Size = (SizeT)n;
		}

		/** @brief Sets internal allocation range */
		void setAllocationRange(void* begin, size_t n) {
			DEATH_DEBUG_ASSERT(n <= getSizeTypeMax());
			BeginX = begin;
			Capacity = static_cast<SizeT>(n);
		}
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<class T>
	using SmallVectorSizeType = typename std::conditional<sizeof(T) < 4 && sizeof(void*) >= 8,
		std::uint64_t, std::uint32_t>::type;
	
	/** @brief Figure out the offset of the first element */
	template<class T, typename = void> struct SmallVectorAlignmentAndSize {
		alignas(SmallVectorBase<SmallVectorSizeType<T>>) char Base[sizeof(
			SmallVectorBase<SmallVectorSizeType<T>>)];
		alignas(T) char FirstElement[sizeof(T)];
	};
#endif

	/**
		@brief Common template of @ref SmallVector which does not depend on whether the type is trivial or not
		@tparam T   Element type
	*/
	template<typename T>
	class SmallVectorTemplateCommon : public SmallVectorBase<SmallVectorSizeType<T>>
	{
		using Base = SmallVectorBase<SmallVectorSizeType<T>>;

	protected:
		/**
		 * @brief Finds the address of the first element
		 *
		 * For this pointer math to be valid with small-size of 0 for `T` with lots of alignment,
		 * it's important that @p SmallVectorStorage is properly-aligned even for small-size of 0
		 */
		void* getFirstElement() const {
			return const_cast<void*>(reinterpret_cast<const void*>(
				reinterpret_cast<const char*>(this) +
				offsetof(SmallVectorAlignmentAndSize<T>, FirstElement)));
		}
		// Space after 'FirstElement' is clobbered, do not add any instance vars after it

		SmallVectorTemplateCommon(std::size_t size) : Base(getFirstElement(), size) {}

		/** @brief Grows the allocated memory (without initializing new elements) for trivial types */
		void growTrivial(std::size_t minSize, std::size_t typeSize) {
			Base::growTrivial(getFirstElement(), minSize, typeSize);
		}

		/** @brief Returns `true` if this is a vector which has not had dynamic memory allocated for it */
		bool isSmall() const {
			return this->BeginX == getFirstElement();
		}

		/** @brief Puts this vector in a state of being small */
		void resetToSmall() {
			this->BeginX = getFirstElement();
			this->Size = this->Capacity = 0;
		}

		/** @brief Returns `true` if @p v is an internal reference to the given range */
		bool isReferenceToRange(const void* v, const void* first, const void* last) const {
			// Use std::less to avoid UB
			std::less<> LessThan;
			return !LessThan(v, first) && LessThan(v, last);
		}

		/** @brief Return `true` if @p v is an internal reference to this vector */
		bool isReferenceToStorage(const void* v) const {
			return isReferenceToRange(v, this->begin(), this->end());
		}

		/** @brief Returns `true` if @p first and @p last form a valid (possibly empty) range in this vector's storage */
		bool isRangeInStorage(const void* first, const void* last) const {
			// Use std::less to avoid UB.
			std::less<> LessThan;
			return !LessThan(first, this->begin()) && !LessThan(last, first) && !LessThan(this->end(), last);
		}

		/** @brief Returns `true` unless @p elt will be invalidated by resizing the vector to @p newSize */
		bool isSafeToReferenceAfterResize(const void* elt, std::size_t newSize) {
			// Past the end.
			if DEATH_LIKELY(!isReferenceToStorage(elt)) {
				return true;
			}
			// Return false if Elt will be destroyed by shrinking
			if (newSize <= this->size()) {
				return elt < this->begin() + newSize;
			}
			// Return false if we need to grow
			return newSize <= this->capacity();
		}

		/** @brief Checks whether @p elt will be invalidated by resizing the vector to @p newSize */
		void assertSafeToReferenceAfterResize(const void* elt, std::size_t newSize) {
			DEATH_DEBUG_ASSERT(isSafeToReferenceAfterResize(elt, newSize),
				"Attempting to reference an element of the vector in an operation that invalidates it", );
		}

		/** @brief Checks whether @p elt will be invalidated by increasing the size of the vector by @p n */
		void assertSafeToAdd(const void* elt, std::size_t n = 1) {
			this->assertSafeToReferenceAfterResize(elt, this->size() + n);
		}

		/** @brief Checks whether any part of the range will be invalidated by clearing */
		void assertSafeToReferenceAfterClear(const T* from, const T* to) {
			if (from == to) {
				return;
			}
			this->assertSafeToReferenceAfterResize(from, 0);
			this->assertSafeToReferenceAfterResize(to - 1, 0);
		}
		/** @overload */
		template<class ItTy, std::enable_if_t<!std::is_same<std::remove_const_t<ItTy>, T*>::value, int> = 0>
		void assertSafeToReferenceAfterClear(ItTy, ItTy) {}

		/** @brief Checks whether any part of the range will be invalidated by growing */
		void assertSafeToAddRange(const T* from, const T* to) {
			if (from == to) {
				return;
			}
			this->assertSafeToAdd(from, to - from);
			this->assertSafeToAdd(to - 1, to - from);
		}
		/** @overload */
		template<class ItTy, std::enable_if_t<!std::is_same<std::remove_const_t<ItTy>, T*>::value, int> = 0>
		void assertSafeToAddRange(ItTy, ItTy) {}

		/** @brief Reserves enough space to add one element, and return the updated element pointer in case it was a reference to the storage */
		template<class U>
		static const T* reserveForParamAndGetAddressImpl(U* _this, const T& elt, std::size_t n) {
			std::size_t newSize = _this->size() + n;
			if DEATH_LIKELY(newSize <= _this->capacity()) {
				return &elt;
			}
			bool referencesStorage = false;
			std::int64_t index = -1;
			if (!U::TakesParamByValue) {
				if DEATH_UNLIKELY(_this->isReferenceToStorage(&elt)) {
					referencesStorage = true;
					index = &elt - _this->begin();
				}
			}
			_this->grow(newSize);
			return (referencesStorage ? _this->begin() + index : &elt);
		}

	public:
		/** @brief Size type */
		using size_type = std::size_t;
		/** @brief Difference type */
		using difference_type = std::ptrdiff_t;
		/** @brief Value type */
		using value_type = T;
		/** @brief Iterator type */
		using iterator = T*;
		/** @brief Const iterator type */
		using const_iterator = const T*;

		/** @brief Const reverse iterator type */
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;
		/** @brief Reverse iterator type */
		using reverse_iterator = std::reverse_iterator<iterator>;

		/** @brief Reference type */
		using reference = T&;
		/** @brief Const reference type */
		using const_reference = const T&;
		/** @brief Pointer type */
		using pointer = T*;
		/** @brief Const pointer type */
		using const_pointer = const T*;

		using Base::capacity;
		using Base::empty;
		using Base::size;

		// Forward iterator creation methods
		/** @brief Returns an iterator to the beginning */
		iterator begin() {
			return (iterator)this->BeginX;
		}
		/** @overload */
		const_iterator begin() const {
			return (const_iterator)this->BeginX;
		}
		/** @brief Returns an iterator to the end */
		iterator end() {
			return begin() + size();
		}
		/** @overload */
		const_iterator end() const {
			return begin() + size();
		}

		// Reverse iterator creation methods
		/** @brief Returns a reverse iterator to the beginning */
		reverse_iterator rbegin() {
			return reverse_iterator(end());
		}
		/** @overload */
		const_reverse_iterator rbegin() const {
			return const_reverse_iterator(end());
		}
		/** @brief Returns a reverse iterator to the end */
		reverse_iterator rend() {
			return reverse_iterator(begin());
		}
		/** @overload */
		const_reverse_iterator rend() const {
			return const_reverse_iterator(begin());
		}
		/** @brief Returns total size in bytes */
		size_type size_in_bytes() const {
			return size() * sizeof(T);
		}
		/** @brief Returns maximum number of elements */
		size_type max_size() const {
			return std::min(this->getSizeTypeMax(), size_type(-1) / sizeof(T));
		}
		/** @brief Returns capacity in bytes */
		std::size_t capacity_in_bytes() const {
			return capacity() * sizeof(T);
		}

		/** @brief Returns a pointer to the vector's buffer */
		pointer data() {
			return pointer(begin());
		}
		/** @overload */
		const_pointer data() const {
			return const_pointer(begin());
		}
		
		/** @brief Access specified element */
		reference operator[](size_type idx) {
			DEATH_DEBUG_ASSERT(idx < size(), ("Index {} out of range for {} elements", std::size_t(idx), size()));
			return begin()[idx];
		}
		/** @overload */
		const_reference operator[](size_type idx) const {
			DEATH_DEBUG_ASSERT(idx < size(), ("Index {} out of range for {} elements", std::size_t(idx), size()));
			return begin()[idx];
		}

		/** @brief Access the first element */
		reference front() {
			DEATH_DEBUG_ASSERT(!empty(), "View is empty");
			return begin()[0];
		}
		/** @overload */
		const_reference front() const {
			DEATH_DEBUG_ASSERT(!empty(), "View is empty");
			return begin()[0];
		}

		/** @brief Access the last element */
		reference back() {
			DEATH_DEBUG_ASSERT(!empty(), "View is empty");
			return end()[-1];
		}
		/** @overload */
		const_reference back() const {
			DEATH_DEBUG_ASSERT(!empty(), "View is empty");
			return end()[-1];
		}
	};

	/**
		@brief Template method specializations of @ref SmallVector depending on whether type is trivial or not
		@tparam T   Element type
	*/
	template<typename T, bool = std::is_trivially_copy_constructible<T>::value &&
		std::is_trivially_move_constructible<T>::value && std::is_trivially_destructible<T>::value>
	class SmallVectorTemplate : public SmallVectorTemplateCommon<T>
	{
		friend class SmallVectorTemplateCommon<T>;

	protected:
		/** @brief Whether it's cheap enough to take parameters by value, always `false` for non-trivial types */
		static constexpr bool TakesParamByValue = false;
		/** @brief Either `const T&` or `T`, depending on whether it's cheap enough to take parameters by value, always `const T&` for non-trivial types */
		using ValueParamT = const T&;

		SmallVectorTemplate(std::size_t size) : SmallVectorTemplateCommon<T>(size) {}

		/** @brief Calls destructor on every element in the specified range if needed */
		static void destroyRange(T* s, T* e) {
			while (s != e) {
				--e;
				e->~T();
			}
		}

		/** @brief Moves the range [I, E) into the uninitialized memory starting with @p dest, constructing elements as needed */
		template<typename It1, typename It2>
		static void uninitializedMove(It1 i, It1 e, It2 dest) {
			std::uninitialized_move(i, e, dest);
		}

		/** @brief Copies the range [I, E) onto the uninitialized memory starting with @p dest, constructing elements as needed */
		template<typename It1, typename It2>
		static void uninitializedCopy(It1 i, It1 e, It2 dest) {
			std::uninitialized_copy(i, e, dest);
		}

		/** @brief Grows the allocated memory (without initializing new elements), doubling the size of the allocated memory */
		void grow(std::size_t minSize = 0);

		/** @brief Creates a new allocation big enough for @p minSize and pass back its size in @p newCapacity */
		T* mallocForGrow(std::size_t minSize, std::size_t& newCapacity);

		/** @brief Move existing elements over to the new allocation @p newElts */
		void moveElementsForGrow(T* newElts);

		/** @brief Transfers ownership of the allocation */
		void takeAllocationForGrow(T* newElts, std::size_t newCapacity);

		/** @brief Reserves enough space to add one element, and returns the updated element pointer in case it was a reference to the storage */
		const T* reserveForParamAndGetAddress(const T& elt, std::size_t n = 1) {
			return this->reserveForParamAndGetAddressImpl(this, elt, n);
		}

		/** @overload */
		T* reserveForParamAndGetAddress(T& elt, std::size_t n = 1) {
			return const_cast<T*>(
				this->reserveForParamAndGetAddressImpl(this, elt, n));
		}

		/** @brief Forwards a value */
		static T&& forwardValueParam(T&& v) {
			return Death::move(v);
		}
		/** @overload */
		static const T& forwardValueParam(const T& v) {
			return v;
		}

		/** @brief Grows the allocated memory and assigns the specified element */
		void growAndAssign(std::size_t numElts, const T& elt) {
			// Grow manually in case Elt is an internal reference
			std::size_t newCapacity;
			T* newElts = mallocForGrow(numElts, newCapacity);
			std::uninitialized_fill_n(newElts, numElts, elt);
			destroyRange(this->begin(), this->end());
			takeAllocationForGrow(newElts, newCapacity);
			this->setSize(numElts);
		}

		/** @brief Grows the allocated memory and emplaces the specified elements back */
		template<typename ...ArgTypes>
		T& growAndEmplaceBack(ArgTypes&&... args) {
			// Grow manually in case one of Args is an internal reference
			std::size_t newCapacity;
			T* newElts = mallocForGrow(0, newCapacity);
			::new ((void*)(newElts + this->size())) T(Death::forward<ArgTypes>(args)...);
			moveElementsForGrow(newElts);
			takeAllocationForGrow(newElts, newCapacity);
			this->setSize(this->size() + 1);
			return this->back();
		}

	public:
		/** @brief Adds an element to the end */
		void push_back(const T& elt) {
			const T* eltPtr = reserveForParamAndGetAddress(elt);
			::new ((void*)this->end()) T(*eltPtr);
			this->setSize(this->size() + 1);
		}
		/** @overload */
		void push_back(T&& elt) {
			T* eltPtr = reserveForParamAndGetAddress(elt);
			::new ((void*)this->end()) T(Death::move(*eltPtr));
			this->setSize(this->size() + 1);
		}

		/** @brief Removes the last element */
		void pop_back() {
			this->setSize(this->size() - 1);
			this->end()->~T();
		}
	};

	// Define this out-of-line to dissuade the C++ compiler from inlining it
	template<typename T, bool TriviallyCopyable>
	void SmallVectorTemplate<T, TriviallyCopyable>::grow(std::size_t minSize) {
		std::size_t newCapacity;
		T* newElts = mallocForGrow(minSize, newCapacity);
		moveElementsForGrow(newElts);
		takeAllocationForGrow(newElts, newCapacity);
	}

	template<typename T, bool TriviallyCopyable>
	T* SmallVectorTemplate<T, TriviallyCopyable>::mallocForGrow(std::size_t minSize, std::size_t& newCapacity) {
		return static_cast<T*>(
			SmallVectorBase<SmallVectorSizeType<T>>::mallocForGrow(
				this->getFirstElement(), minSize, sizeof(T), newCapacity));
	}

	// Define this out-of-line to dissuade the C++ compiler from inlining it
	template<typename T, bool TriviallyCopyable>
	void SmallVectorTemplate<T, TriviallyCopyable>::moveElementsForGrow(T* newElts) {
		// Move the elements over
		uninitializedMove(this->begin(), this->end(), newElts);

		// Destroy the original elements
		destroyRange(this->begin(), this->end());
	}

	// Define this out-of-line to dissuade the C++ compiler from inlining it
	template<typename T, bool TriviallyCopyable>
	void SmallVectorTemplate<T, TriviallyCopyable>::takeAllocationForGrow(T* newElts, std::size_t newCapacity) {
		// If this wasn't grown from the inline copy, deallocate the old space
		if (!this->isSmall()) {
			std::free(this->begin());
		}
		this->setAllocationRange(newElts, newCapacity);
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@brief Template method specializations of @ref SmallVector designed to work with trivially copyable types
		@tparam T   Element type
	*/
	template<typename T>
	class SmallVectorTemplate<T, true> : public SmallVectorTemplateCommon<T>
	{
		friend class SmallVectorTemplateCommon<T>;

	protected:
		/** @brief Whether it's cheap enough to take parameters by value */
		static constexpr bool TakesParamByValue = sizeof(T) <= 2 * sizeof(void*);

		/** @brief Either `const T&` or `T`, depending on whether it's cheap enough to take parameters by value */
		using ValueParamT = std::conditional_t<TakesParamByValue, T, const T&>;

		SmallVectorTemplate(std::size_t size) : SmallVectorTemplateCommon<T>(size) {}

		// No need to do a destroy loop for trivial types
		/** @brief Calls destructor on every element in the specified range if needed */
		static void destroyRange(T*, T*) {}

		/** @brief Move the range [I, E) onto the uninitialized memory starting with @p dest, constructing elements into it as needed */
		template<typename It1, typename It2>
		static void uninitializedMove(It1 i, It1 e, It2 dest) {
			// Just do a copy
			uninitializedCopy(i, e, dest);
		}

		/** @brief Copy the range [I, E) onto the uninitialized memory starting with @p dest, constructing elements into it as needed */
		template<typename It1, typename It2>
		static void uninitializedCopy(It1 i, It1 e, It2 dest) {
			// Arbitrary iterator types; just use the basic implementation
			std::uninitialized_copy(i, e, dest);
		}

		/** @brief Copies the range [I, E) onto the uninitialized memory starting with @p dest, constructing elements into it as needed */
		template<typename T1, typename T2>
		static void uninitializedCopy(T1* i, T1* e, T2* dest, std::enable_if_t<std::is_same<std::remove_const_t<T1>, T2>::value>* = nullptr) {
			// Use memcpy for PODs iterated by pointers (which includes SmallVector iterators):
			// std::uninitialized_copy optimizes to memmove, but we can use memcpy here. Note that
			// I and E are iterators and thus might be invalid for memcpy if they are equal
			if (i != e) {
				std::memcpy(reinterpret_cast<void*>(dest), i, (e - i) * sizeof(T));
			}
		}

		/** @brief Doubles the size of the allocated memory, guaranteeing space for at least one more element or @p minSize if specified */
		void grow(std::size_t minSize = 0) {
			this->growTrivial(minSize, sizeof(T));
		}

		/** @brief Reserves enough space to add one element, and return the updated element pointer in case it was a reference to the storage */
		const T* reserveForParamAndGetAddress(const T& elt, std::size_t n = 1) {
			return this->reserveForParamAndGetAddressImpl(this, elt, n);
		}

		/** @overload */
		T* reserveForParamAndGetAddress(T& elt, std::size_t n = 1) {
			return const_cast<T*>(this->reserveForParamAndGetAddressImpl(this, elt, n));
		}

		/** @brief Copies @p v or return a reference, depending on @a ValueParamT */
		static ValueParamT forwardValueParam(ValueParamT v) {
			return v;
		}

		/** @brief Grows the allocated memory and assigns the specified element */
		void growAndAssign(std::size_t numElts, T elt) {
			// Elt has been copied in case it's an internal reference, side-stepping
			// reference invalidation problems without losing the realloc optimization
			this->setSize(0);
			this->grow(numElts);
			std::uninitialized_fill_n(this->begin(), numElts, elt);
			this->setSize(numElts);
		}

		/** @brief Grows the allocated memory and emplaces the specified elements back */
		template<typename ...ArgTypes>
		T& growAndEmplaceBack(ArgTypes&&... args) {
			// Use push_back with a copy in case Args has an internal reference,
			// side-stepping reference invalidation problems without losing the realloc
			// optimization
			push_back(T(Death::forward<ArgTypes>(args)...));
			return this->back();
		}

	public:
		/** @brief Adds an element to the end */
		void push_back(ValueParamT elt) {
			const T* eltPtr = reserveForParamAndGetAddress(elt);
			std::memcpy(reinterpret_cast<void*>(this->end()), eltPtr, sizeof(T));
			this->setSize(this->size() + 1);
		}

		/** @brief Removes the last element */
		void pop_back() {
			this->setSize(this->size() - 1);
		}
	};
#endif

	/**
		@brief Common method implementations of @ref SmallVector class to reduce code duplication based on `N` template parameter
		@tparam T   Element type
	*/
	template<typename T>
	class SmallVectorImpl : public SmallVectorTemplate<T>
	{
		using BaseClass = SmallVectorTemplate<T>;

	public:
		/** @brief Iterator type */
		using iterator = typename BaseClass::iterator;
		/** @brief Const iterator type */
		using const_iterator = typename BaseClass::const_iterator;
		/** @brief Reference type */
		using reference = typename BaseClass::reference;
		/** @brief Size type */
		using size_type = typename BaseClass::size_type;

	protected:
		using SmallVectorTemplate<T>::TakesParamByValue;
		/** @brief Either `const T&` or `T`, depending on whether it's cheap enough to take parameters by value, always `const T&` for non-trivial types */
		using ValueParamT = typename BaseClass::ValueParamT;

		explicit SmallVectorImpl(unsigned n)
			: SmallVectorTemplate<T>(n) {}

#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Assigns the content of the specified vector */
		void assignRemote(SmallVectorImpl&& other) {
			this->destroyRange(this->begin(), this->end());
			if (!this->isSmall()) {
				std::free(this->begin());
			}
			this->BeginX = other.BeginX;
			this->Size = other.Size;
			this->Capacity = other.Capacity;
			other.resetToSmall();
		}
#endif

		~SmallVectorImpl() {
			// Subclass has already destructed this vector's elements
			// If this wasn't grown from the inline copy, deallocate the old space
			if (!this->isSmall()) {
				std::free(this->begin());
			}
		}

	public:
		SmallVectorImpl(const SmallVectorImpl&) = delete;
		
		/** @brief Clears the vector */
		void clear() {
			this->destroyRange(this->begin(), this->end());
			this->Size = 0;
		}

	private:
		// Make setSize() and setAllocationRange() private to avoid misuse in subclasses.
		using BaseClass::setSize;
		using BaseClass::setAllocationRange;

		template<bool ForOverwrite>
		void resizeImpl(size_type n) {
			if (n == this->size())
				return;

			if (n < this->size()) {
				this->truncate(n);
				return;
			}

			this->reserve(n);
			for (auto i = this->end(), e = this->begin() + n; i != e; ++i) {
				if (ForOverwrite) {
					new (&*i) T;
				} else {
					new (&*i) T();
				}
			}
			this->setSize(n);
		}

	public:
		/** @brief Resizes the vector to given size, value-initializing new elements */
		void resize(size_type n) {
			resizeImpl<false>(n);
		}

		/** @brief Resizes the vector to given size, default-initializing new elements */
		void resize_for_overwrite(size_type n) {
			resizeImpl<true>(n);
		}

		/** @brief Resizes the vector to given size, constructing new elements using provided argument */
		void resize(size_type n, ValueParamT nv) {
			if (n == this->size())
				return;

			if (n < this->size()) {
				this->truncate(n);
				return;
			}

			// N > this->size() - defer to append
			this->append(n - this->size(), nv);
		}

		/** @brief Like resize, but requires that @p n is less than @ref size() */
		void truncate(size_type n) {
			DEATH_DEBUG_ASSERT(this->size() >= n, "Cannot increase size with truncate", );
			this->destroyRange(this->begin() + n, this->end());
			this->setSize(n);
		}

		/** @brief Reserve given capacity in the vector */
		void reserve(size_type n) {
			if (this->capacity() < n)
				this->grow(n);
		}

		/** @brief Try to shrink the vector to given capacity without discarding any elements */
		void shrink(size_type newCapacity) {
			if (newCapacity < this->Size) {
				newCapacity = this->Size;
			}
			if (newCapacity >= this->capacity() || this->isSmall()) {
				return;
			}

			void* newElts = this->mallocForShrink(this->BeginX, newCapacity, sizeof(T));
			this->setAllocationRange(newElts, newCapacity);
		}

		/** @brief Removes the last @p n elements */
		void pop_back_n(size_type n) {
			DEATH_DEBUG_ASSERT(this->size() >= n);
			truncate(this->size() - n);
		}

		/** @brief Removes the last element and returns it */
		DEATH_NODISCARD T pop_back_val() {
			T result = Death::move(this->back());
			this->pop_back();
			return result;
		}

		/** @brief Swaps the contents */
		void swap(SmallVectorImpl& other);

		/** @brief Adds the specified range to the end of the vector */
		template<typename in_iter, std::enable_if_t<std::is_convertible<typename std::iterator_traits<in_iter>::iterator_category, std::input_iterator_tag>::value, int> = 0>
		void append(in_iter inStart, in_iter inEnd) {
			this->assertSafeToAddRange(inStart, inEnd);
			size_type numInputs = std::distance(inStart, inEnd);
			this->reserve(this->size() + numInputs);
			this->uninitializedCopy(inStart, inEnd, this->end());
			this->setSize(this->size() + numInputs);
		}

		/** @brief Appends @p n copies of @p elt to the end */
		void append(size_type n, ValueParamT elt) {
			const T* eltPtr = this->reserveForParamAndGetAddress(elt, n);
			std::uninitialized_fill_n(this->end(), n, *eltPtr);
			this->setSize(this->size() + n);
		}

		/** @brief Appends the specified list to the end */
		void append(std::initializer_list<T> il) {
			append(il.begin(), il.end());
		}

		/** @brief Appends the specified view to the end */
		void append(ArrayView<const T> other) {
			append(other.begin(), other.end());
		}

		/** @brief Assigns @p n copies of @p elt */
		void assign(size_type n, ValueParamT elt) {
			// Note that Elt could be an internal reference
			if (n > this->capacity()) {
				this->growAndAssign(n, elt);
				return;
			}

			// Assign over existing elements
			std::fill_n(this->begin(), std::min(n, this->size()), elt);
			if (n > this->size()) {
				std::uninitialized_fill_n(this->end(), n - this->size(), elt);
			} else if (n < this->size()) {
				this->destroyRange(this->begin() + n, this->end());
			}
			this->setSize(n);
		}

		/** @brief Assigns the specified range */
		template<typename in_iter, std::enable_if_t<std::is_convertible<typename std::iterator_traits<in_iter>::iterator_category, std::input_iterator_tag>::value, int> = 0>
		void assign(in_iter inStart, in_iter inEnd) {
			this->assertSafeToReferenceAfterClear(inStart, inEnd);
			clear();
			append(inStart, inEnd);
		}

		/** @brief Assigns the specified list */
		void assign(std::initializer_list<T> il) {
			clear();
			append(il);
		}

		/** @brief Assigns the specified vector */
		void assign(const SmallVectorImpl& other) {
			assign(other.begin(), other.end());
		}

		/** @brief Removes elements from the vector */
		iterator erase(const_iterator ci) {
			// Just cast away constness because this is a non-const member function
			iterator i = const_cast<iterator>(ci);

			DEATH_DEBUG_ASSERT(this->isReferenceToStorage(ci), "Iterator to erase is out of bounds", i);

			iterator n = i;
			// Shift all elts down one
			std::move(i + 1, this->end(), i);
			// Drop the last elt
			this->pop_back();
			return n;
		}
		/** @overload */
		iterator erase(const_iterator cs, const_iterator ce) {
			// Just cast away constness because this is a non-const member function
			iterator s = const_cast<iterator>(cs);
			iterator e = const_cast<iterator>(ce);

			DEATH_DEBUG_ASSERT(this->isRangeInStorage(s, e), "Range to erase is out of bounds", s);

			iterator n = s;
			// Shift all elts down
			iterator i = std::move(e, this->end(), s);
			// Drop the last elts
			this->destroyRange(i, this->end());
			this->setSize(i - this->begin());
			return n;
		}
		/** @overload */
		void erase(size_type index) {
			erase(this->begin() + index);
		}

		/** @brief Removes an element from the unordered vector */
		iterator eraseUnordered(const_iterator ci) {
			// Just cast away constness because this is a non-const member function
			iterator i = const_cast<iterator>(ci);

			DEATH_DEBUG_ASSERT(this->isReferenceToStorage(ci), "Iterator to erase is out of bounds", i);

			iterator il = this->end() - 1;
			if (i != il) {
				*i = Death::move(*il);
			}
			// Drop the last elt
			this->pop_back();
			return i;
		}
		/** @overload */
		void eraseUnordered(size_type index) {
			eraseUnordered(this->begin() + index);
		}

	private:
		template<class ArgType> iterator insert_one_impl(iterator i, ArgType&& elt) {
			// Callers ensure that ArgType is derived from T
			static_assert(std::is_same<std::remove_const_t<std::remove_reference_t<ArgType>>, T>::value, "ArgType must be derived from T");

			if (i == this->end()) {  // Important special case for empty vector
				this->push_back(Death::forward<ArgType>(elt));
				return this->end() - 1;
			}

			DEATH_DEBUG_ASSERT(this->isReferenceToStorage(i), "Insertion iterator is out of bounds", i);

			// Grow if necessary
			std::size_t index = i - this->begin();
			std::remove_reference_t<ArgType>* eltPtr = this->reserveForParamAndGetAddress(elt);
			i = this->begin() + index;

			::new ((void*)this->end()) T(Death::move(this->back()));
			// Push everything else over
			std::move_backward(i, this->end() - 1, this->end());
			this->setSize(this->size() + 1);

			// If we just moved the element we're inserting, be sure to update the reference (never happens if TakesParamByValue)
			static_assert(!TakesParamByValue || std::is_same<ArgType, T>::value,
						  "ArgType must be 'T' when taking by value");
			if (!TakesParamByValue && this->isReferenceToRange(eltPtr, i, this->end())) {
				++eltPtr;
			}
			*i = Death::forward<ArgType>(*eltPtr);
			return i;
		}

	public:
		/** @brief Inserts elements */
		iterator insert(iterator i, T&& elt) {
			return insert_one_impl(i, this->forwardValueParam(Death::move(elt)));
		}
		/** @overload */
		iterator insert(iterator i, const T& elt) {
			return insert_one_impl(i, this->forwardValueParam(elt));
		}
		/** @overload */
		iterator insert(iterator i, size_type numToInsert, ValueParamT elt) {
			// Convert iterator to elt# to avoid invalidating iterator when we reserve()
			std::size_t insertElt = i - this->begin();

			if (i == this->end()) {  // Important special case for empty vector
				append(numToInsert, elt);
				return this->begin() + insertElt;
			}

			DEATH_DEBUG_ASSERT(this->isReferenceToStorage(i), "Insertion iterator is out of bounds", i);

			// Ensure there is enough space, and get the (maybe updated) address of Elt
			const T* eltPtr = this->reserveForParamAndGetAddress(elt, numToInsert);

			// Uninvalidate the iterator
			i = this->begin() + insertElt;

			// If there are more elements between the insertion point and the end of the
			// range than there are being inserted, we can use a simple approach to
			// insertion. Since we already reserved space, we know that this won't
			// reallocate the vector
			if (std::size_t(this->end() - i) >= numToInsert) {
				T* oldEnd = this->end();
				append(std::move_iterator<iterator>(this->end() - numToInsert),
					   std::move_iterator<iterator>(this->end()));

				// Copy the existing elements that get replaced.
				std::move_backward(i, oldEnd - numToInsert, oldEnd);

				// If we just moved the element we're inserting, be sure to update the reference (never happens if TakesParamByValue)
				if (!TakesParamByValue && i <= eltPtr && eltPtr < this->end()) {
					eltPtr += numToInsert;
				}
				std::fill_n(i, numToInsert, *eltPtr);
				return i;
			}

			// Otherwise, we're inserting more elements than exist already, and we're not inserting at the end

			// Move over the elements that we're about to overwrite
			T* oldEnd = this->end();
			this->setSize(this->size() + numToInsert);
			std::size_t numOverwritten = oldEnd - i;
			this->uninitializedMove(i, oldEnd, this->end() - numOverwritten);

			// If we just moved the element we're inserting, be sure to update the reference (never happens if TakesParamByValue)
			if (!TakesParamByValue && i <= eltPtr && eltPtr < this->end()) {
				eltPtr += numToInsert;
			}
			// Replace the overwritten part
			std::fill_n(i, numOverwritten, *eltPtr);

			// Insert the non-overwritten middle part
			std::uninitialized_fill_n(oldEnd, numToInsert - numOverwritten, *eltPtr);
			return i;
		}
		/** @overload */
		template<typename ItTy, std::enable_if_t<std::is_convertible<typename std::iterator_traits<ItTy>::iterator_category, std::input_iterator_tag>::value, int> = 0>
		iterator insert(iterator i, ItTy from, ItTy to) {
			// Convert iterator to elt# to avoid invalidating iterator when we reserve()
			std::size_t insertElt = i - this->begin();

			if (i == this->end()) {  // Important special case for empty vector
				append(from, to);
				return this->begin() + insertElt;
			}

			DEATH_DEBUG_ASSERT(this->isReferenceToStorage(i), "Insertion iterator is out of bounds", i);

			// Check that the reserve that follows doesn't invalidate the iterators
			this->assertSafeToAddRange(from, to);

			std::size_t numToInsert = std::distance(from, to);

			// Ensure there is enough space
			reserve(this->size() + numToInsert);

			// Uninvalidate the iterator
			i = this->begin() + insertElt;

			// If there are more elements between the insertion point and the end of the range than there
			// are being inserted, we can use a simple approach to insertion.  Since we already reserved
			// space, we know that this won't reallocate the vector
			if (std::size_t(this->end() - i) >= numToInsert) {
				T* oldEnd = this->end();
				append(std::move_iterator<iterator>(this->end() - numToInsert),
					   std::move_iterator<iterator>(this->end()));

				// Copy the existing elements that get replaced.
				std::move_backward(i, oldEnd - numToInsert, oldEnd);

				std::copy(from, to, i);
				return i;
			}

			// Otherwise, we're inserting more elements than exist already, and we're not inserting at the end

			// Move over the elements that we're about to overwrite
			T* oldEnd = this->end();
			this->setSize(this->size() + numToInsert);
			std::size_t numOverwritten = oldEnd - i;
			this->uninitializedMove(i, oldEnd, this->end() - numOverwritten);

			// Replace the overwritten part
			for (T* j = i; numOverwritten > 0; --numOverwritten) {
				*j = *from;
				++j; ++from;
			}

			// Insert the non-overwritten middle part
			this->uninitializedCopy(from, to, oldEnd);
			return i;
		}
		/** @overload */
		void insert(iterator i, std::initializer_list<T> il) {
			insert(i, il.begin(), il.end());
		}

		/** @brief Constructs elements in-place at the end */
		template<typename ...ArgTypes>
		reference emplace_back(ArgTypes&&... args) {
			if (this->size() >= this->capacity()) {
				return this->growAndEmplaceBack(Death::forward<ArgTypes>(args)...);
			}

			::new ((void*)this->end()) T(Death::forward<ArgTypes>(args)...);
			this->setSize(this->size() + 1);
			return this->back();
		}

		SmallVectorImpl& operator=(const SmallVectorImpl& other);

		SmallVectorImpl& operator=(SmallVectorImpl&& other);

		bool operator==(const SmallVectorImpl& other) const {
			if (this->size() != other.size()) return false;
			return std::equal(this->begin(), this->end(), other.begin());
		}
		bool operator!=(const SmallVectorImpl& other) const {
			return !(*this == other);
		}

		bool operator<(const SmallVectorImpl& other) const {
			return std::lexicographical_compare(this->begin(), this->end(),
												other.begin(), other.end());
		}
		bool operator>(const SmallVectorImpl& other) const {
			return other < *this;
		}
		bool operator<=(const SmallVectorImpl& other) const {
			return !(*this > other);
		}
		bool operator>=(const SmallVectorImpl& other) const {
			return !(*this < other);
		}
	};

	template<typename T>
	void SmallVectorImpl<T>::swap(SmallVectorImpl<T>& other) {
		if (this == &other) return;

		// We can only avoid copying elements if neither vector is small
		if (!this->isSmall() && !other.isSmall()) {
			std::swap(this->BeginX, other.BeginX);
			std::swap(this->Size, other.Size);
			std::swap(this->Capacity, other.Capacity);
			return;
		}
		this->reserve(other.size());
		other.reserve(this->size());

		// Swap the shared elements
		std::size_t numShared = this->size();
		if (numShared > other.size()) numShared = other.size();
		for (size_type i = 0; i != numShared; ++i) {
			std::swap((*this)[i], other[i]);
		}

		// Copy over the extra elts
		if (this->size() > other.size()) {
			std::size_t eltDiff = this->size() - other.size();
			this->uninitializedCopy(this->begin() + numShared, this->end(), other.end());
			other.setSize(other.size() + eltDiff);
			this->destroyRange(this->begin() + numShared, this->end());
			this->setSize(numShared);
		} else if (other.size() > this->size()) {
			std::size_t eltDiff = other.size() - this->size();
			this->uninitializedCopy(other.begin() + numShared, other.end(), this->end());
			this->setSize(this->size() + eltDiff);
			this->destroyRange(other.begin() + numShared, other.end());
			other.setSize(numShared);
		}
	}

	template<typename T>
	SmallVectorImpl<T>& SmallVectorImpl<T>::operator=(const SmallVectorImpl<T>& other) {
		// Avoid self-assignment
		if (this == &other) return *this;

		// If we already have sufficient space, assign the common elements, then destroy any excess
		std::size_t otherSize = other.size();
		std::size_t currentSize = this->size();
		if (currentSize >= otherSize) {
			// Assign common elements
			iterator newEnd;
			if (otherSize) {
				newEnd = std::copy(other.begin(), other.begin() + otherSize, this->begin());
			} else {
				newEnd = this->begin();
			}

			// Destroy excess elements
			this->destroyRange(newEnd, this->end());

			// Trim
			this->setSize(otherSize);
			return *this;
		}

		// If we have to grow to have enough elements, destroy the current elements.
		// This allows us to avoid copying them during the grow.
		if (this->capacity() < otherSize) {
			// Destroy current elements
			this->clear();
			currentSize = 0;
			this->grow(otherSize);
		} else if (currentSize) {
			// Otherwise, use assignment for the already-constructed elements
			std::copy(other.begin(), other.begin() + currentSize, this->begin());
		}

		// Copy construct the new elements in place
		this->uninitializedCopy(other.begin() + currentSize, other.end(),
								 this->begin() + currentSize);

		this->setSize(otherSize);
		return *this;
	}

	template<typename T>
	SmallVectorImpl<T>& SmallVectorImpl<T>::operator=(SmallVectorImpl<T>&& other) {
		// Avoid self-assignment
		if (this == &other) return *this;

		// If the RHS isn't small, clear this vector and then steal its buffer
		if (!other.isSmall()) {
			this->assignRemote(Death::move(other));
			return *this;
		}

		// If we already have sufficient space, assign the common elements, then destroy any excess
		std::size_t otherSize = other.size();
		std::size_t currentSize = this->size();
		if (currentSize >= otherSize) {
			// Assign common elements
			iterator newEnd = this->begin();
			if (otherSize) {
				newEnd = std::move(other.begin(), other.end(), newEnd);
			}

			// Destroy excess elements and trim the bounds
			this->destroyRange(newEnd, this->end());
			this->setSize(otherSize);

			// Clear the RHS
			other.clear();

			return *this;
		}

		// If we have to grow to have enough elements, destroy the current elements.
		// This allows us to avoid copying them during the grow
		if (this->capacity() < otherSize) {
			// Destroy current elements.
			this->clear();
			currentSize = 0;
			this->grow(otherSize);
		} else if (currentSize) {
			// Otherwise, use assignment for the already-constructed elements.
			std::move(other.begin(), other.begin() + currentSize, this->begin());
		}

		// Move-construct the new elements in place
		this->uninitializedMove(other.begin() + currentSize, other.end(), this->begin() + currentSize);

		this->setSize(otherSize);

		other.clear();
		return *this;
	}

	/**
		@brief Storage for @ref SmallVector in-place elements
		@tparam T   Element type
		@tparam N   Number of in-place elements
	*/
	template<typename T, unsigned N>
	struct SmallVectorStorage {
		/** @brief Array of in-place elements */
		alignas(T) char InplaceElements[N * sizeof(T)];
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<typename T> struct alignas(T) SmallVectorStorage<T, 0> {};

	template<typename T, unsigned N> class SmallVector;

	/**
		@brief Helper class for calculating the default number of in-place elements for `SmallVector<T>`
		@tparam T   Element type
	*/
	template<typename T> struct CalculateSmallVectorDefaultInlinedElements {
		// Parameter controlling the default number of inlined elements
		// for `SmallVector<T>`.
		//
		// The default number of inlined elements ensures that
		// 1. There is at least one inlined element.
		// 2. `sizeof(SmallVector<T>) <= kPreferredSmallVectorSizeof` unless
		// it contradicts 1.
		static constexpr std::size_t PreferredSmallVectorSizeof = 64;

		// static_assert that sizeof(T) is not "too big".
		//
		// Because our policy guarantees at least one inlined element, it is possible
		// for an arbitrarily large inlined element to allocate an arbitrarily large
		// amount of inline storage. We generally consider it an antipattern for a
		// SmallVector to allocate an excessive amount of inline storage, so we want
		// to call attention to these cases and make sure that users are making an
		// intentional decision if they request a lot of inline storage.
		//
		// We want this assertion to trigger in pathological cases, but otherwise
		// not be too easy to hit. To accomplish that, the cutoff is actually somewhat
		// larger than kPreferredSmallVectorSizeof (otherwise,
		// `SmallVector<SmallVector<T>>` would be one easy way to trip it, and that
		// pattern seems useful in practice).
		//
		// One wrinkle is that this assertion is in theory non-portable, since
		// sizeof(T) is in general platform-dependent. However, we don't expect this
		// to be much of an issue, because most LLVM development happens on 64-bit
		// hosts, and therefore sizeof(T) is expected to *decrease* when compiled for
		// 32-bit hosts, dodging the issue. The reverse situation, where development
		// happens on a 32-bit host and then fails due to sizeof(T) *increasing* on a
		// 64-bit host, is expected to be very rare.
		static_assert(
			sizeof(T) <= 256,
			"You are trying to use a default number of inlined elements for "
			"`SmallVector<T>` but `sizeof(T)` is really big! Please use an "
			"explicit number of inlined elements with `SmallVector<T, N>` to make "
			"sure you really want that much inline storage.");

		// Discount the size of the header itself when calculating the maximum inline bytes.
		static constexpr std::size_t PreferredInlineBytes = PreferredSmallVectorSizeof - sizeof(SmallVector<T, 0>);
		static constexpr std::size_t NumElementsThatFit = PreferredInlineBytes / sizeof(T);
		static constexpr std::size_t value = (NumElementsThatFit == 0 ? 1 : NumElementsThatFit);
	};
#endif

	/**
		@brief Memory-optimized vector
		@tparam T   Element type
		@tparam N   Number of in-place elements

		A variable-sized array optimized for the case when the array is small. It contains
		some number of elements in-place, which allows it to avoid heap allocation when
		the actual number of elements is below that threshold. This allows normal "small"
		cases to be fast without losing generality for large inputs.

		@note In the absence of a well-motivated choice for the number of inlined elements @p N,
			it is recommended to use @cpp SmallVector<T> @ce (that is, omitting the @p N).
			This will choose a default number of inlined elements reasonable for allocation on
			the stack (for example, trying to keep @cpp sizeof(SmallVector<T>) @ce around 64 bytes).
	*/
	template<typename T, unsigned N = CalculateSmallVectorDefaultInlinedElements<T>::value>
	class SmallVector : public SmallVectorImpl<T>, SmallVectorStorage<T, N>
	{
	public:
		/** @brief Default constructor */
		SmallVector() : SmallVectorImpl<T>(N) {}

		/**
		 * @brief Construct a default-initialized vector
		 *
		 * Creates a vector of given size, the contents are default-initialized
		 * (i.e. trivial types are not initialized, default constructor called
		 * otherwise). If the size is zero, no allocation is done.
		 */
		explicit SmallVector(DefaultInitT, std::size_t size)
			: SmallVectorImpl<T>(N) {
			this->resize_for_overwrite(size);
		}

		/**
		 * @brief Construct a value-initialized vector
		 *
		 * Creates a vector of given size, the contents are value-initialized
		 * (i.e. trivial types are zero-initialized, default constructor called
		 * otherwise). This is the same as @ref SmallVector(std::size_t). If the size
		 * is zero, no allocation is done.
		 */
		explicit SmallVector(ValueInitT, std::size_t size)
			: SmallVectorImpl<T>(N) {
			this->resize(size);
		}

		/**
		 * @brief Construct a list-initialized vector
		 *
		 * Copy-initializes each element with placement new using values from @p list.
		 */
		/*implicit*/ SmallVector(InPlaceInitT, ArrayView<const T> list) : SmallVectorImpl<T>(N) {
			this->append(list.begin(), list.end());
		}

		/** @overload */
		template<typename ItTy, std::enable_if_t<std::is_convertible<typename std::iterator_traits<ItTy>::iterator_category, std::input_iterator_tag>::value, int> = 0>
		/*implicit*/ SmallVector(InPlaceInitT, ItTy s, ItTy e) : SmallVectorImpl<T>(N) {
			this->append(s, e);
		}

		/** @overload */
		/*implicit*/ SmallVector(InPlaceInitT, std::initializer_list<T> il) : SmallVectorImpl<T>(N) {
			this->assign(il);
		}

		/**
		 * @brief Construct a value-initialized vector
		 *
		 * Alias to @ref SmallVector(ValueInitT, std::size_t).
		 */
		explicit SmallVector(std::size_t size)
			: SmallVector{ValueInit, size} {
		}

		/** @brief Copy constructor */
		SmallVector(const SmallVector& other) : SmallVectorImpl<T>(N) {
			if (!other.empty()) {
				SmallVectorImpl<T>::operator=(other);
			}
		}

		/** @brief Move constructor */
		SmallVector(SmallVector&& other) : SmallVectorImpl<T>(N) {
			if (!other.empty()) {
				SmallVectorImpl<T>::operator=(Death::move(other));
			}
		}

		/** @overload */
		SmallVector(SmallVectorImpl<T>&& other) : SmallVectorImpl<T>(N) {
			if (!other.empty()) {
				SmallVectorImpl<T>::operator=(Death::move(other));
			}
		}

		/** @brief Destructor */
		~SmallVector() {
			// Destroy the constructed elements in the vector.
			this->destroyRange(this->begin(), this->end());
		}

		/** @brief Copy assignment */
		SmallVector& operator=(const SmallVector& other) {
			SmallVectorImpl<T>::operator=(other);
			return *this;
		}

		/** @overload */
		SmallVector& operator=(std::initializer_list<T> il) {
			this->assign(il);
			return *this;
		}

		/** @brief Move assignment */
		SmallVector& operator=(SmallVector&& other) {
			if (N) {
				SmallVectorImpl<T>::operator=(Death::move(other));
				return *this;
			}
			if (this == &other) {
				return *this;
			}
			if (other.empty()) {
				this->destroyRange(this->begin(), this->end());
				this->Size = 0;
			} else {
				this->assignRemote(Death::move(other));
			}
			return *this;
		}

		/** @overload */
		SmallVector& operator=(SmallVectorImpl<T>&& other) {
			SmallVectorImpl<T>::operator=(Death::move(other));
			return *this;
		}

#ifdef DOXYGEN_GENERATING_OUTPUT
		// Pull documentation of all public methods in base classes
		using SmallVectorBase::size;
		using SmallVectorBase::capacity;
		using SmallVectorBase::empty;
		using SmallVectorTemplateCommon::begin;
		using SmallVectorTemplateCommon::end;
		using SmallVectorTemplateCommon::rbegin;
		using SmallVectorTemplateCommon::rend;
		using SmallVectorTemplateCommon::size_in_bytes;
		using SmallVectorTemplateCommon::max_size;
		using SmallVectorTemplateCommon::capacity_in_bytes;
		using SmallVectorTemplateCommon::data;
		using SmallVectorTemplateCommon::operator[];
		using SmallVectorTemplateCommon::front;
		using SmallVectorTemplateCommon::back;
		using SmallVectorImpl::clear;
		using SmallVectorImpl::resize;
		using SmallVectorImpl::resize_for_overwrite;
		using SmallVectorImpl::truncate;
		using SmallVectorImpl::reserve;
		using SmallVectorImpl::shrink;
		using SmallVectorTemplate::push_back;
		using SmallVectorTemplate::pop_back;
		using SmallVectorImpl::pop_back_n;
		using SmallVectorImpl::pop_back_val;
		using SmallVectorImpl::swap;
		using SmallVectorImpl::append;
		using SmallVectorImpl::assign;
		using SmallVectorImpl::erase;
		using SmallVectorImpl::eraseUnordered;
		using SmallVectorImpl::insert;
#endif
	};

	// Explicit instantiations
	extern template class SmallVectorBase<std::uint32_t>;
#if SIZE_MAX > UINT32_MAX
	extern template class SmallVectorBase<std::uint64_t>;
#endif

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<typename T>
	inline void swap(SmallVectorImpl<T>& lhs, SmallVectorImpl<T>& rhs) {
		lhs.swap(rhs);
	}

	template<typename T, unsigned N>
	inline void swap(SmallVector<T, N>& lhs, SmallVector<T, N>& rhs) {
		lhs.swap(rhs);
	}
#endif

	namespace Implementation
	{
		template<class T, unsigned N> struct ArrayViewConverter<T, SmallVector<T, N>> {
			static ArrayView<T> from(SmallVector<T, N>& other) {
				return {other.data(), other.size()};
			}
			static ArrayView<T> from(SmallVector<T, N>&& other) {
				return {other.data(), other.size()};
			}
		};
		template<class T, unsigned N> struct ArrayViewConverter<const T, SmallVector<T, N>> {
			static ArrayView<const T> from(const SmallVector<T, N>& other) {
				return {other.data(), other.size()};
			}
		};
		template<class T, unsigned N> struct ArrayViewConverter<const T, SmallVector<const T, N>> {
			static ArrayView<const T> from(const SmallVector<const T, N>& other) {
				return {other.data(), other.size()};
			}
		};
		template<class T, unsigned N> struct ErasedArrayViewConverter<SmallVector<T, N>> : ArrayViewConverter<T, SmallVector<T, N>> {};
		template<class T, unsigned N> struct ErasedArrayViewConverter<const SmallVector<T, N>> : ArrayViewConverter<const T, SmallVector<T, N>> {};
	}

}}