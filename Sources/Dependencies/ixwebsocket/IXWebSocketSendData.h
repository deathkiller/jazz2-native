/*
 *  IXWebSocketSendData.h
 *
 *  WebSocket (Binary/Text) send data buffer
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <iterator>

namespace ix
{
	/**
		@brief Wrapper for WebSocket send data (text or binary).
	
		Allows sending data from `std::string`, `std::vector<char>`, `std::vector<uint8_t>`, or raw pointer without copying.
		Used by @ref ix::WebSocket and @ref ix::WebSocketTransport.
	*/
	class IXWebSocketSendData
	{
	public:
		/**
		 * @brief Const iterator for send data buffer.
		 * @tparam T Data type (char or uint8_t)
		 */
		template<typename T>
		struct IXWebSocketSendData_const_iterator
			//: public std::iterator<std::forward_iterator_tag, T>
		{
			typedef IXWebSocketSendData_const_iterator<T> const_iterator;

			using iterator_category = std::forward_iterator_tag;
			using difference_type = std::ptrdiff_t;
			using value_type = T;
			using pointer = value_type*;
			using reference = const value_type&;

			pointer _ptr;

		public:
			/** @brief Default constructor */
			IXWebSocketSendData_const_iterator() : _ptr(nullptr) {}
			/** @brief Construct from pointer */
			IXWebSocketSendData_const_iterator(pointer ptr) : _ptr(ptr) {}
			/** @brief Destructor */
			~IXWebSocketSendData_const_iterator() {}

			const_iterator  operator++(int) { return const_iterator(_ptr++); }
			const_iterator& operator++() { ++_ptr; return *this; }
			reference       operator* () const { return *_ptr; }
			pointer         operator->() const { return _ptr; }
			const_iterator  operator+ (const difference_type offset) const { return const_iterator(_ptr + offset); }
			const_iterator  operator- (const difference_type offset) const { return const_iterator(_ptr - offset); }
			difference_type operator- (const const_iterator& rhs) const { return _ptr - rhs._ptr; }
			bool            operator==(const const_iterator& rhs) const { return _ptr == rhs._ptr; }
			bool            operator!=(const const_iterator& rhs) const { return _ptr != rhs._ptr; }
			const_iterator& operator+=(const difference_type offset) { _ptr += offset; return *this; }
			const_iterator& operator-=(const difference_type offset) { _ptr -= offset; return *this; }
		};

		/** @brief Const iterator for char data */
		using const_iterator = IXWebSocketSendData_const_iterator<char>;

		/**
		 * @brief Construct from std::string (must remain valid)
		 * @param str String data
		 */
		IXWebSocketSendData(const std::string& str)
			: _data(str.data())
			, _size(str.size())
		{
		}

		/**
		 * @brief Construct from std::vector<char> (must remain valid)
		 * @param v Vector data
		 */
		IXWebSocketSendData(const std::vector<char>& v)
			: _data(v.data())
			, _size(v.size())
		{
		}

		/**
		 * @brief Construct from std::vector<uint8_t> (must remain valid)
		 * @param v Vector data
		 */
		IXWebSocketSendData(const std::vector<uint8_t>& v)
			: _data(reinterpret_cast<const char*>(v.data()))
			, _size(v.size())
		{
		}

		/**
		 * @brief Construct from raw pointer (must remain valid)
		 * @param data Pointer to data
		 * @param size Data size
		 */
		IXWebSocketSendData(const char* data, size_t size)
			: _data(data)
			, _size(data == nullptr ? 0 : size)
		{
		}

		/** @brief Check if buffer is empty */
		bool empty() const { return _data == nullptr || _size == 0; }
		/** @brief Get pointer to data as C string */
		const char* c_str() const { return _data; }
		/** @brief Get pointer to data */
		const char* data() const { return _data; }
		/** @brief Get data size */
		size_t size() const { return _size; }

		/** @brief Begin iterator */
		inline const_iterator begin() const { return const_iterator(const_cast<char*>(_data)); }
		/** @brief End iterator */
		inline const_iterator end() const { return const_iterator(const_cast<char*>(_data) + _size); }
		/** @brief Begin const iterator */
		inline const_iterator cbegin() const { return begin(); }
		/** @brief End const iterator */
		inline const_iterator cend() const { return end(); }

	private:
		const char* _data;
		const size_t _size;
	};

}