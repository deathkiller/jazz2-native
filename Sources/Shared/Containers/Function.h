// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2020-2024 Dan R.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#pragma once

#include "../Asserts.h"
#include "../Base/Move.h"

#include <new>
#include <type_traits>

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief No-allocate initialization tag type

		Used to distinguish @ref Function<R(Args...)> "Function" construction that
		disallows allocating stateful function data on heap.
	*/
	struct NoAllocateInitT {
#ifndef DOXYGEN_GENERATING_OUTPUT
		struct Init {};
		constexpr explicit NoAllocateInitT(Init) {}
#endif
	};

	/**
		@brief No-allocate initialization tag

		Used to distinguish @ref Function<R(Args...)> "Function" construction that
		disallows allocating stateful function data on heap.
	*/
	constexpr NoAllocateInitT NoAllocateInit{NoAllocateInitT::Init{}};

	namespace Implementation
	{
		// Lower bound of the size is verified with a static_assert() in templated Function constructors below,
		// upper bound in particular FunctionTest::call*() cases.
		enum: std::size_t { FunctionPointerSize =
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			2 * sizeof(void*) / sizeof(std::size_t)
#else
			// On MSVC, pointers to members with a virtual base class are the biggest and have 12 bytes on 32-bit and 16 on 64-bit.
			// Pointers to incomplete class members are 16 bytes on both 32-bit and 64-bit, but such scenario is impossible
			// to happen here as there's a std::is_base_of check in Function constructor that requires the type to be complete.
#	if defined(DEATH_TARGET_32BIT)
			12 / sizeof(std::size_t)
#	else
			16 / sizeof(std::size_t)
#	endif
#endif
		};
	}

	/**
		@brief @ref Function<R(Args...)> "Function" data storage

		Type-erased base for @ref Function<R(Args...)> "Function". Suitable for storing
		type-erased function pointers in containers.
	*/
	class FunctionData
	{
	public:
		/**
		 * @brief Default constructor
		 *
		 * Creates a @cpp nullptr @ce function.
		 */
		/*implicit*/ FunctionData(std::nullptr_t = nullptr) noexcept: _storage{}, _call{nullptr} {}

		/** @brief Copying is not allowed */
		FunctionData(const FunctionData&) = delete;

		/** @brief Move constructor */
		FunctionData(FunctionData&&) noexcept;

		~FunctionData();

		/** @brief Copying is not allowed */
		FunctionData& operator=(const FunctionData&) = delete;

		/** @brief Move assignment */
		FunctionData& operator=(FunctionData&&) noexcept;

		/**
		 * @brief Whether the function pointer is non-null
		 *
		 * Returns @cpp false @ce if the instance was default-constructed,
		 * constructed from a @cpp nullptr @ce free or member function pointer
		 * or moved out, @cpp true @ce otherwise.
		 */
		explicit operator bool() const {
			return _call || _storage.functor.call;
		}

		/**
		 * @brief Whether the instance contains heap-allocated state
		 *
		 * Returns @cpp true @ce if the instance was created from a capturing
		 * lambda or a functor with a large or non-trivially copyable state,
		 * @cpp false @ce otherwise. In other words, always returns
		 * @cpp false @ce for default-constructed instances, free and member
		 * function pointers and instances constructed with the
		 * @ref NoAllocateInitT constructor.
		 */
		bool isAllocated() const { return !_call && _storage.functor.call; }

	private:
		template<class> friend class Function;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		union Storage {
			// Simple, small enough and trivial functors
			char data[Implementation::FunctionPointerSize*sizeof(std::size_t) + sizeof(void*)];

			// Free functions
			void(*function)();

			// Member functions
			struct {
				std::size_t data[Implementation::FunctionPointerSize];
				void* instance;
			} member;

			// Non-trivially-destructible/-copyable or too large functors. The data points
			// to heap-allocated memory that has to be moved and deleted.
			struct {
				void* data;
				void(*free)(Storage&);
				void(*call)();
			} functor;
		};
#endif

		constexpr explicit FunctionData(const Storage& storage, void(*call)()):
			// GCC 4.8 attempts to initialize the first member (the char array) instead of performing a copy if {} is used
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
			_storage(storage),
#else
			_storage{storage},
#endif
			_call{call} {}

		Storage _storage;
		// Calls into _storage.function, _storage.data or _storage.member. If nullptr, _storage.functor.call is used instead
		// (if not nullptr as well), and _storage.functor.data points to heap-allocated data that has to be moved and deleted.
		void(*_call)();
	};

	namespace Implementation
	{
		/* Takes the void_t trick, explained at https://stackoverflow.com/a/27688405,
		   and takes it one step further. The basic premise is that with

			template<class> using VoidT = void;
			template<class, class = void> struct HasThing {
				enum : bool { value = false };
			};
			template<class T> struct HasThing<T, VoidT<decltype(T::Thing)>> {
				enum : bool { value = true };
			};

		   when `HasThing<Foo>` is in code, `HasThing<Foo, void>` gets looked for,
		   because the second template argument wasn't specified and `void` was the
		   default. The compiler then looks for the most specialized variant of the
		   `HasThing<Foo, void>` template and falls back to the unspecialized version
		   only if nothing else matches. Thus, if `T` contains a `Thing` member, the
		   second `HasThing` from above gets picked (because it *does* specialize
		   `HasThing<Foo, void>`, although cryptically), and if it doesn't, SFINAE
		   takes care of this not being an error and a fallback to the unspecialized
		   template is picked.

		   Here, instead of having a `VoidT` that eats any type, a function overload
		   that accepts one of the four allowed member functions with desired signature
		   is picked. If the operator() doesn't exist or none of the possible overloads
		   have given signature, it again falls back to the default. */
		template<class T, class Signature, class = void> struct IsFunctor;
		template<class T, class R, class ...Args, class U> struct IsFunctor<T, R(Args...), U> {
			enum : bool { value = false };
		};
		/* Originally this was a set of
			template<class Class, class R, class ...Args> void functorSignature(R(Class::*)(Args...)) {}
			...
		   overloads because that seemed to work as well, was simpler, and this was
		   present only for MSVC 2017 (but not 2015) because it ICEs when it encounters
		   `decltype(functionSignature<T, R, Args...>())`. However, the problem with
		   function template arguments is that they get autodetected if not passed.
		   Thus, with just a function, asking for e.g.
			functionSignature<T, int>(&T::operator())
		   would match not only `int()` but also `int(float)`, `int(float, short)` and
		   just any other argument combination because they would ultimately match as
			functionSignature<T, int, float>(&T::operator())
			functionSignature<T, int, float, short>(&T::operator())
		   etc. The robust solution is to put these templates into a struct instead,
		   where they cannot be auto-filled. */
		template<class> struct FunctorSignature;
		template<class Class, class R,  class ...Args> struct FunctorSignature<R(Class::*)(Args...)> {
			static void match(R(Class::*)(Args...)) {}
			static void match(R(Class::*)(Args...) &) {}
			static void match(R(Class::*)(Args...) const) {}
			static void match(R(Class::*)(Args...) const &) {}
		};
		template<class T, class R, class ...Args> struct IsFunctor<T, R(Args...), decltype(FunctorSignature<R(T::*)(Args...)>::match(&T::operator()))> {
			enum : bool { value = !std::is_convertible<T, R(*)(Args...)>::value };
		};
	}

	/**
		@brief Function wrapper

		See the @ref Function<R(Args...)> specialization.
	*/
	template<class> class Function;

	/**
		@brief Function wrapper

		An alternative to @m_class{m-doc-external} [std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
		from C++23 or a move-only alternative to @ref std::function from C++11,
		providing a common interface for free functions, member functions, (capturing)
		lambdas and generic functors. The wrapped function is then called through @ref operator()().

		To prevent accidental type conversions and potential extra overhead coming from
		those, the wrapper only accepts functions that match the signature *exactly*.
		If a function or a functor has a set of overloads, an overload matching
		the signature is picked. If you have a function which has a different signature,
		wrap it in a lambda of a matching singature first.

		@section Containers-Function-usage-overhead Function call overhead

		On construction, a stateless wrapper lambda is created that then delegates to
		the particular free function pointer, member function pointer, or a stateful
		functor / lambda. In the common case this means that invoking a
		@ref Function<R(Args...)> "Function" always involves two function calls, one
		for the wrapper lambda and one for the actual function being called.

		If the wrapped function is inlineable, in optimized builds this can reduce to
		just a single function call to the wrapper lambda, which then has the actual
		function call inlined inside.

		@section Containers-Function-usage-stateful Stateful function storage

		The @ref Function<R(Args...)> "Function" class is internally made large enough
		to fit any free or member function pointer. But because member function
		pointers can increase in size if multiple inheritance and/or virtual
		inheritance is involved, that space can be repurposed also for saving up to 24
		bytes of stateful lambda / functor data on 64-bit platforms (and up to 16 bytes
		on 32-bit platforms). If larger, the data is allocated on heap instead. For
		implementation simplicity reasons the state is also allocated if isn't
		trivially copyable. You can use @ref isAllocated() to check whether the
		function state needed a heap allocation or not.
		If heap allocation is undesirable, the @ref Function(NoAllocateInitT, F&&)
		overload can be used to prevent wrapping any function with state that would
		need to be allocated.

		@section Containers-Function-usage-type-erased Type-erased function storage

		The class derives from a type-erased @ref FunctionData base, which owns all
		state and also takes care of proper moving and destruction for non-trivial
		functors. This allows it to be used as a type-erased storage in various
		containers for example. Cast the instance back to a concrete
		@ref Function<R(Args...)> "Function" in order to use it.
	*/
	template<class R, class ...Args> class Function<R(Args...)> : public FunctionData
	{
	public:
		/** @brief Function type */
		typedef R(Type)(Args...);

		/**
		 * @brief Default constructor
		 *
		 * Creates a @cpp nullptr @ce function.
		 */
		/*implicit*/ Function(std::nullptr_t = nullptr) noexcept : FunctionData{nullptr} {}

		/**
		 * @brief Wrap a free function pointer
		 *
		 * If @p f is @cpp nullptr @ce, the constructor is equivalent to
		 * @ref Function(std::nullptr_t).
		 */
		/*implicit*/ Function(R(*f)(Args...)) noexcept;

		/**
		 * @brief Wrap a member function pointer
		 *
		 * Default, @cpp & @ce, @cpp const @ce and @cpp const & @ce r-value
		 * overloads are supported, @cpp && @ce and @cpp const && @ce however
		 * isn't, as the member function is always called on a l-value. If @p f
		 * is @cpp nullptr @ce, the constructor is equivalent to
		 * @ref Function(std::nullptr_t).
		 */
		template<class Instance, class Class> /*implicit*/ Function(Instance& instance, R(Class::*f)(Args...)) noexcept;
		/** @overload */
		template<class Instance, class Class> /*implicit*/ Function(Instance& instance, R(Class::*f)(Args...) &) noexcept;
		/** @overload */
		template<class Instance, class Class> /*implicit*/ Function(Instance& instance, R(Class::*f)(Args...) const) noexcept;
		/** @overload */
		template<class Instance, class Class> /*implicit*/ Function(Instance& instance, R(Class::*f)(Args...) const &) noexcept;

		/**
		 * @brief Create a null member function pointer
		 *
		 * Equivalent to @ref Function(std::nullptr_t).
		 */
		template<class Instance> /*implicit*/ Function(Instance&, std::nullptr_t) noexcept : Function{nullptr} {}

		/**
		 * @brief Wrap a lambda or a functor
		 *
		 * The functor is expected to *exactly* match the signature, no
		 * implicit conversions on either the arguments or return type are
		 * allowed. If the lambda capture or functor state is small enough and
		 * trivially copyable, it's stored inline, otherwise allocated on heap.
		 */
		template<class F
#ifndef DOXYGEN_GENERATING_OUTPUT
			// Enabling this only for functors and lambdas convertible to function pointers,
			// regular function pointers should go through the R(*)(Args...) overload above.
			, typename std::enable_if<std::is_convertible<typename std::decay<F>::type, R(*)(Args...)>::value || Implementation::IsFunctor<typename std::decay<F>::type, R(Args...)>::value, int>::type = 0
#endif
		> /*implicit*/ Function(F&& f) noexcept(sizeof(typename std::decay<F>::type) <= sizeof(Storage) &&
			std::is_trivially_copyable<typename std::decay<F>::type>::value
		) : Function{nullptr, Death::forward<F>(f)} {}

		/**
		 * @brief Wrap a small enough and trivial lambda / functor
		 *
		 * Compared to @ref Function(F&&) compiles only if the lambda capture
		 * or functor state is small enough and trivially copyable to not need
		 * to be allocated on heap.
		 *
		 * Note that there's no @ref NoAllocateInit variant for free or member
		 * function pointers, as they never need to be allocated on heap.
		 */
		template<class F
#ifndef DOXYGEN_GENERATING_OUTPUT
			// Enabling this only for functors that aren't convertible to function pointers,
			// everything else never allocates so using this overload makes no sense
			, typename std::enable_if<Implementation::IsFunctor<typename std::decay<F>::type, R(Args...)>::value, int>::type = 0
#endif
		> /*implicit*/ Function(NoAllocateInitT, F&& f) noexcept;

		/**
		 * @brief Call the function pointer
		 *
		 * Expects that the pointer is not @cpp nullptr @ce.
		 */
		R operator()(Args... args);

	private:
		// Functor convertible to a function pointer, delegates to the function pointer variant
		template<class F, typename std::enable_if<std::is_convertible<typename std::decay<F>::type, R(*)(Args...)>::value, int>::type = 0> explicit Function(std::nullptr_t, F&& f) noexcept : Function{static_cast<R(*)(Args...)>(f)} {}

		// Simple, small enough and trivial functor which is not convertible to a function pointer,
		// delegates to the NoAllocateInit variant
		template<class F, typename std::enable_if<!std::is_convertible<typename std::decay<F>::type, R(*)(Args...)>::value && sizeof(typename std::decay<F>::type) <= sizeof(Storage) &&
			std::is_trivially_copyable<typename std::decay<F>::type>::value
		, int>::type = 0> explicit Function(std::nullptr_t, F&& f) noexcept :
			// Note that, as this is a trivially copyable functor, it doesn't need std::forward<F>(f)
			Function{NoAllocateInit, f} {}

		// Non-trivially-destructible/-copyable or too large functor. MSVC 2015 and 2017 has all lambdas not trivially
		// copyable, so it has to additionally check that it isn't convertible to a function pointer to avoid ambiguity
		// with the constructor taking a functor convertible to a function pointer.
		template<class F, typename std::enable_if<
#if defined(DEATH_MSVC2017_COMPATIBILITY)
			!std::is_convertible<typename std::decay<F>::type, R(*)(Args...)>::value &&
#endif
			(sizeof(typename std::decay<F>::type) > sizeof(Storage) ||
			!std::is_trivially_copyable<typename std::decay<F>::type>::value
		), int>::type = 0> explicit Function(std::nullptr_t, F&& f);
	};

	inline FunctionData::FunctionData(FunctionData&& other) noexcept :
		// GCC 4.8 attempts to initialize the first member (the char array) instead of performing a copy if {} is used
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
		_storage(other._storage),
#else
		_storage{other._storage},
#endif
		_call{other._call}
	{
		// Move out the other instance, if it's heap-allocated data
		if(!_call && _storage.functor.call)
			other._storage.functor.call = nullptr;
	}

	inline FunctionData::~FunctionData() {
		// If it's heap-allocated data, delete
		if(!_call && _storage.functor.call)
			_storage.functor.free(_storage);
	}

	inline FunctionData& FunctionData::operator=(FunctionData&& other) noexcept {
		using Death::swap;
		swap(other._storage, _storage);
		swap(other._call, _call);
		return *this;
	}

	template<class R, class ...Args> inline R Function<R(Args...)>::operator()(Args... args) {
		void(*const call)() = _call ? _call : _storage.functor.call;
		return DEATH_DEBUG_CONSTEXPR_ASSERT(call, "The function is null"),
			reinterpret_cast<R(*)(Storage&, Args...)>(call)(_storage, Death::forward<Args>(args)...);
	}

	// Free function
	template<class R, class ...Args> Function<R(Args...)>::Function(R(*f)(Args...)) noexcept {
		_storage.function = reinterpret_cast<void(*)()>(f);
		// The + is to decay the lambda to a function pointer so we can cast it. MSVC 2015 says it's "illegal on a class"
		// so there it's an explicit cast to the function pointer type (and the parentheses are for both to have only one ifdef).
		_call = !f ? nullptr : reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return reinterpret_cast<R(*)(Args...)>(storage.function)(Death::forward<Args>(args)...);
		}));
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Doxygen 1.12.0 outputs also some template specializations, so suppress it
	// Simple, small enough and trivial functor which is not convertible to a function pointer 
	template<class R, class ...Args> template<class F, typename std::enable_if<Implementation::IsFunctor<typename std::decay<F>::type, R(Args...)>::value, int>::type> Function<R(Args...)>::Function(NoAllocateInitT, F&& f) noexcept {
		static_assert(sizeof(typename std::decay<F>::type) <= sizeof(FunctionData::Storage) &&
			std::is_trivially_copyable<typename std::decay<F>::type>::value
		, "Functor too large or non-trivial to be stored without allocation");

		// GCC 4.8 attempts to initialize the first member instead of performing a copy if {} is used.
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
		new(&_storage.data) typename std::decay<F>::type(f);
#else
		// std::is_trivially_copyable can be true for classes that have deleted copy constructors
		// (but non-deleted trivial move constructors), so we need a move here.
		// On GCC 4.8 the __has_trivial_copy() is false for such classes, so the move isn't needed in the above variant.
		new(&_storage.data) typename std::decay<F>::type{Death::move(f)};
#endif
		// The + is to decay the lambda to a function pointer so we can cast it. MSVC 2015 says it's "illegal on a class"
		// so there it's an explicit cast to the function pointer type (and the parentheses are for both to have only one ifdef).
		_call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return reinterpret_cast<F&>(storage.data)(Death::forward<Args>(args)...);
		}));
	}
#endif

	// Non-trivially-destructible/-copyable or too large functor. MSVC 2017 has all lambdas not trivially copyable,
	// so it has to additionally check that it isn't convertible to a function pointer to avoid ambiguity with the
	// constructor taking a functor convertible to a function pointer.
	template<class R, class ...Args> template<class F, typename std::enable_if<
#if defined(DEATH_MSVC2017_COMPATIBILITY)
		!std::is_convertible<typename std::decay<F>::type, R(*)(Args...)>::value &&
#endif
		(sizeof(typename std::decay<F>::type) > sizeof(FunctionData::Storage) ||
		!std::is_trivially_copyable<typename std::decay<F>::type>::value
	), int>::type> Function<R(Args...)>::Function(std::nullptr_t, F&& f) {
		// GCC 4.8 attempts to initialize the first member instead of performing a copy if {} is used.
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
		reinterpret_cast<typename std::decay<F>::type*&>(_storage.functor.data) = new typename std::decay<F>::type(Death::forward<F>(f));
#else
		reinterpret_cast<typename std::decay<F>::type*&>(_storage.functor.data) = new typename std::decay<F>::type{Death::forward<F>(f)};
#endif
		_storage.functor.free = [](Storage& storage) {
			delete reinterpret_cast<typename std::decay<F>::type*>(storage.functor.data);
		};
		// The + is to decay the lambda to a function pointer so we can cast it. MSVC 2015 says it's "illegal on a class"
		// so there it's an explicit cast to the function pointer type (and the parentheses are for both to have only one ifdef).
		_storage.functor.call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return (*reinterpret_cast<typename std::decay<F>::type*>(storage.functor.data))(Death::forward<Args>(args)...);
		}));
		// The actual call is put into _storage.functor.call and _call is nullptr in order to be able
		// to tell whether it's needed to move / delete the data pointer
		_call = nullptr;
	}

	// Member function and const, &, const & variants. There can't really be any common helper for those because passing
	// lambdas to delegated constructors is broken in MSVC 2015, losing knowledge of template parameters.
	template<class R, class ...Args> template<class Instance, class Class> Function<R(Args...)>::Function(Instance& instance, R(Class::*f)(Args...)) noexcept {
		static_assert(sizeof(f) <= sizeof(_storage.member.data),
			"Size of member function pointer is incorrectly assumed to be smaller");
		// If the pointer is null, don't save anything, not even the instance, as otherwise this would
		// get confused with a heap-allocated functor (that has nullptr _call but non-null _storage.functor.call,
		// which aliases _storage.member.instance)
		if(!f) return;

		_storage.member.instance = &instance;
		reinterpret_cast<R(Class::*&)(Args...)>(_storage.member.data) = f;
		// The + is to decay the lambda to a function pointer so we can cast it. MSVC 2015 says it's "illegal on a class"
		// so there it's an explicit cast to the function pointer type (and the parentheses are for both to have only one ifdef).
		_call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return (static_cast<Instance*>(storage.member.instance)->*reinterpret_cast<R(Class::*&)(Args...)>(storage.member.data))(Death::forward<Args>(args)...);
		}));
	}
	template<class R, class ...Args> template<class Instance, class Class> Function<R(Args...)>::Function(Instance& instance, R(Class::*f)(Args...) &) noexcept {
		static_assert(sizeof(f) <= sizeof(_storage.member.data),
			"Size of member function pointer is incorrectly assumed to be smaller");
		if(!f) return;

		_storage.member.instance = &instance;
		reinterpret_cast<R(Class::*&)(Args...) &>(_storage.member.data) = f;
		_call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return (static_cast<Instance*>(storage.member.instance)->*reinterpret_cast<R(Class::*&)(Args...) &>(storage.member.data))(Death::forward<Args>(args)...);
		}));
	}
	template<class R, class ...Args> template<class Instance, class Class> Function<R(Args...)>::Function(Instance& instance, R(Class::*f)(Args...) const) noexcept {
		static_assert(sizeof(f) <= sizeof(_storage.member.data),
			"Size of member function pointer is incorrectly assumed to be smaller");
		if(!f) return;

		_storage.member.instance = &instance;
		reinterpret_cast<R(Class::*&)(Args...) const>(_storage.member.data) = f;
		_call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return (static_cast<Instance*>(storage.member.instance)->*reinterpret_cast<R(Class::*&)(Args...) const>(storage.member.data))(Death::forward<Args>(args)...);
		}));
	}
	template<class R, class ...Args> template<class Instance, class Class> Function<R(Args...)>::Function(Instance& instance, R(Class::*f)(Args...) const &) noexcept {
		static_assert(sizeof(f) <= sizeof(_storage.member.data),
			"Size of member function pointer is incorrectly assumed to be smaller");
		if(!f) return;

		_storage.member.instance = &instance;
		reinterpret_cast<R(Class::*&)(Args...) const &>(_storage.member.data) = f;
		_call = reinterpret_cast<void(*)()>(
#if !defined(DEATH_MSVC2015_COMPATIBILITY)
			+
#else
			static_cast<R(*)(Storage&, Args...)>
#endif
		([](Storage& storage, Args... args) -> R {
			return (static_cast<Instance*>(storage.member.instance)->*reinterpret_cast<R(Class::*&)(Args...) const &>(storage.member.data))(Death::forward<Args>(args)...);
		}));
	}

#if defined(__cpp_deduction_guides)
	template<class Instance, class Class, class R, class ...Args>
	Function(Instance&, R(Class::*)(Args...)) -> Function<R(Args...)>;
#endif

}}
