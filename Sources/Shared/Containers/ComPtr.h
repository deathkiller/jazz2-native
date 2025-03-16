#pragma once

#include "../CommonWindows.h"
#include "../Asserts.h"

#include <type_traits>

// Forward declaration from <unknwn.h>
struct IUnknown;

// Define a helper for the macro below: we just need a function taking a pointer and not returning
// anything to avoid warnings about unused return value of the cast in the macro itself.
namespace Death { namespace Implementation {
	inline void PPV_ARGS_CHECK(void*) {}
}}

#define DEATH_IID_PPV_ARGS(pType)																				\
	__uuidof(**(pType)),																						\
	(Death::Implementation::PPV_ARGS_CHECK(static_cast<::IUnknown*>(*pType)), reinterpret_cast<void**>(pType))

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Wraps the interface pointer of the specified type and maintains a reference count
	*/
	template<class T>
	class ComPtr
	{
		template<typename U>
		friend class ComPtr;

	public:
		/** @brief Default constructor */
		constexpr ComPtr(std::nullptr_t = nullptr) noexcept : _ptr(nullptr) {}

		/** @brief Construct from the pointer */
		explicit ComPtr(T* ptr) noexcept : _ptr(ptr) {
			if (_ptr != nullptr) {
				_ptr->AddRef();
			}
		}
		
#if defined(DEATH_TARGET_MSVC) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Construct from the pointer of a related type */
		template<class U, std::enable_if_t<!std::is_same<::IUnknown*, U*>::value && !std::is_base_of<T, U>::value, int> = 0>
		explicit ComPtr(U* ptr) noexcept : _ptr(nullptr) {
			HRESULT result = ptr->QueryInterface(DEATH_IID_PPV_ARGS(&_ptr));
			DEATH_ASSERT(result >= 0, ("ComPtr::QueryInterface() failed with error 0x%08x", result), );
		}
#endif

		/** @brief Copy constructor */
		ComPtr(const ComPtr& other) noexcept : ComPtr(other.get()) {}

		/** @overload */
		template<typename U, std::enable_if_t<std::is_convertible<U*, T*>::value, int> = 0>
		ComPtr(const ComPtr<U>& other) noexcept : ComPtr(static_cast<T*>(other.get())) {}

		/** @brief Move constructor */
		ComPtr(ComPtr&& other) noexcept : _ptr(other.detach()) {}

		/** @overload */
		template<typename U, std::enable_if_t<std::is_convertible<U*, T*>::value, int> = 0>
		ComPtr(ComPtr<U>&& other) noexcept : _ptr(other.detach()) {}

		/** @brief Destructor */
		~ComPtr() noexcept {
			if (_ptr != nullptr) {
				_ptr->Release();
			}
		}

		/** @brief Relinquishes ownership and returns the internal interface pointer */
		/*[[nodiscard]]*/ T* detach() noexcept {
			auto prev = _ptr;
			_ptr = nullptr;
			return prev;
		}

		/** @brief Returns the pointer */
		DEATH_CONSTEXPR14 T* get() const noexcept {
			return _ptr;
		}

		/** @brief Releases the pointer and sets it specified value (or nullptr) */
		void reset(T* ptr = nullptr) noexcept {
			if (_ptr == ptr) {
				return;
			}
			auto prev = _ptr;
			_ptr = ptr;
			if (_ptr != nullptr) {
				_ptr->AddRef();
			}
			if (prev != nullptr) {
				prev->Release();
			}
		}

		/** @brief `nullptr` assignment */
		ComPtr& operator=(std::nullptr_t) noexcept {
			reset();
			return *this;
		}

		/** @brief Pointer assignment */
		ComPtr& operator=(T* ptr) noexcept {
			reset(ptr);
			return *this;
		}

		/** @brief Copy assignment */
		ComPtr& operator=(const ComPtr& other) noexcept {
			reset(other.get());
			return *this;
		}

		/** @overload */
		template<typename U, std::enable_if_t<std::is_convertible<U*, T*>::value, int> = 0>
		ComPtr& operator=(const ComPtr<U>& other) noexcept {
			reset(other.get());
			return *this;
		}

		/** @brief Move assignment */
		ComPtr& operator=(ComPtr&& other) noexcept {
			if (_ptr != other._ptr) {
				if (_ptr != nullptr) {
					_ptr->Release();
				}
				_ptr = other.detach();
			}

			return *this;
		}

		/** @overload */
		template<typename U, std::enable_if_t<std::is_convertible<U*, T*>::value, int> = 0>
		ComPtr& operator=(ComPtr<U>&& other) noexcept {
			if (_ptr != other._ptr) {
				if (_ptr != nullptr) {
					_ptr->Release();
				}
				_ptr = other.detach();
			}

			return *this;
		}

		/** @brief Returns the pointer */
		DEATH_CONSTEXPR14 operator T*() const noexcept {
			return _ptr;
		}

		/** @brief Dereferences the pointer */
		DEATH_CONSTEXPR14 T& operator*() const noexcept {
			return *_ptr;
		}

		/** @brief Allows direct calls against the pointer */
		DEATH_CONSTEXPR14 T* operator->() const noexcept {
			return _ptr;
		}

		/** @brief Returns the address of the internal pointer if the pointer is not initialized yet to be populated by external call */
		DEATH_CONSTEXPR14 T** operator&() {
			DEATH_DEBUG_ASSERT(_ptr == nullptr, "Cannot get direct access to initialized pointer", nullptr);
			return &_ptr;
		}

		/** @brief Whether the pointer is assigned */
		explicit operator bool() const noexcept {
			return (_ptr != nullptr);
		}

		/** @brief Tries to cast the instance to another type */
		template<class U, std::enable_if_t<std::is_convertible<T, U>::value, int> = 0>
		HRESULT as(U** result) const noexcept {
			*result = _ptr;
			(*result)->AddRef();
			return S_OK;
		}

#if defined(DEATH_TARGET_MSVC) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @overload */
		template<class U, std::enable_if_t<!std::is_convertible<T, U>::value, int> = 0>
		HRESULT as(U** result) const noexcept {
			return _ptr->QueryInterface(DEATH_IID_PPV_ARGS(result));
		}
#endif

		/** @overload */
		HRESULT as(REFIID riid, void** result) const noexcept {
			return _ptr->QueryInterface(riid, result);
		}

	private:
		T* _ptr;
	};

	// Comparison with another ComPtr<T> instances
	template<typename TLeft, typename TRight>
	inline bool operator==(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left.get() == right.get());
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<typename TLeft>
	inline bool operator==(const ComPtr<TLeft>& left, std::nullptr_t) noexcept
	{
		return (left.get() == nullptr);
	}

	template<typename TLeft, typename TRight>
	inline bool operator!=(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		return (!(left == right));
	}

	template<typename TLeft, typename TRight>
	inline bool operator<(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left.get() < right.get());
	}

	template<typename TLeft, typename TRight>
	inline bool operator>=(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		return (!(left < right));
	}

	template<typename TLeft, typename TRight>
	inline bool operator>(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		return (right < left);
	}

	template<typename TLeft, typename TRight>
	inline bool operator<=(const ComPtr<TLeft>& left, const ComPtr<TRight>& right) noexcept
	{
		return (!(right < left));
	}

	template<typename TRight>
	inline bool operator==(std::nullptr_t, const ComPtr<TRight>& right) noexcept
	{
		return (right.get() == nullptr);
	}

	template<typename TLeft>
	inline bool operator!=(const ComPtr<TLeft>& left, std::nullptr_t) noexcept
	{
		return (left.get() != nullptr);
	}

	template<typename TRight>
	inline bool operator!=(std::nullptr_t, const ComPtr<TRight>& right) noexcept
	{
		return (right.get() != nullptr);
	}

	// Comparison with raw pointers
	template<typename TLeft, typename TRight>
	inline bool operator==(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left.get() == right);
	}

	template <typename TLeft, typename TRight>
	inline bool operator<(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left.get() < right);
	}

	template <typename TLeft, typename ErrLeft, typename TRight>
	inline bool operator!=(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		return (!(left == right));
	}

	template <typename TLeft, typename ErrLeft, typename TRight>
	inline bool operator>=(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		return (!(left < right));
	}

	template <typename TLeft, typename ErrLeft, typename TRight>
	inline bool operator>(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		return (right < left);
	}

	template <typename TLeft, typename ErrLeft, typename TRight>
	inline bool operator<=(const ComPtr<TLeft>& left, TRight* right) noexcept
	{
		return (!(right < left));
	}

	template <typename TLeft, typename TRight>
	inline bool operator==(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left == right.get());
	}

	template <typename TLeft, typename TRight>
	inline bool operator<(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		static_assert(std::is_convertible<TLeft*, TRight*>::value || std::is_convertible<TRight*, TLeft*>::value,
			"Comparison operator requires left and right pointers to be compatible");
		return (left < right.get());
	}

	template <typename TLeft, typename TRight>
	inline bool operator!=(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		return (!(left == right));
	}

	template <typename TLeft, typename TRight>
	inline bool operator>=(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		return (!(left < right));
	}

	template <typename TLeft, typename TRight>
	inline bool operator>(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		return (right < left);
	}

	template <typename TLeft, typename TRight>
	inline bool operator<=(TLeft* left, const ComPtr<TRight>& right) noexcept
	{
		return (!(right < left));
	}
#endif

}}