#pragma once

#include "../../Main.h"

#include <Containers/Tags.h>

using namespace Death::Containers;

namespace nCine
{
	class BitArray;

	/**
		@brief Proxy referring to a single bit of a @ref BitArray
		
		Returned by @ref BitArray::operator()() to allow a specific bit to be assigned a boolean value
		through @ref operator=().
	*/
	class BitArrayIndex
	{
	public:
		BitArrayIndex(BitArray* array, std::size_t bit);

		/** @brief Sets or clears the referenced bit */
		void operator=(bool value);

	private:
		BitArray* _bitArray;
		std::size_t _bit;
	};

	/**
		@brief Dynamically allocated array of bits
		
		Stores an arbitrary number of bits in a heap buffer and exposes per-bit access together with
		bitwise, shift and increment/decrement operators. The array is move-only.
	*/
	class BitArray
	{
	public:
		BitArray();
		/** @brief Creates an array of the given size in bits with all bits cleared */
		BitArray(ValueInitT, std::size_t sizeInBits);
		/** @brief Creates an array of the given size in bits without initializing its contents */
		BitArray(NoInitT, std::size_t sizeInBits);
		~BitArray();

		BitArray(const BitArray&) = delete;
		BitArray& operator=(const BitArray&) = delete;
		BitArray(BitArray&& other) noexcept;
		BitArray& operator=(BitArray&& other) noexcept;

		/** @brief Returns a pointer to the underlying byte buffer */
		char* data() { return _data; }

		/** @brief Returns `true` if the array contains no bits */
		bool empty() const { return !_size; }
		/** @brief Returns the size of the array in bits */
		std::size_t size() const { return _size; };
		/** @brief Returns the size of the underlying buffer in bytes */
		std::size_t sizeInBytes() const;

		/** @brief Resizes the array to the given size in bits, clearing its contents */
		void resize(ValueInitT, std::size_t sizeInBits);
		/** @brief Resizes the array to the given size in bits without initializing its contents */
		void resize(NoInitT, std::size_t sizeInBits);

		/** @brief Sets every bit in the array */
		void setAll();
		/** @brief Clears every bit in the array */
		void resetAll();
		/** @brief Sets the bit at the specified position */
		void set(std::size_t bit);
		/** @brief Sets or clears the bit at the specified position */
		void set(std::size_t bit, bool value);
		/** @brief Clears the bit at the specified position */
		void reset(std::size_t bit);

		/** @brief Returns a writable proxy to the bit at the specified position */
		BitArrayIndex operator()(const std::size_t bit);

		/** @brief Returns the value of the bit at the specified position */
		bool operator[](std::size_t bit) const;
		/** @brief Returns `true` if both arrays have the same size and contents */
		bool operator==(const BitArray& other) const;

		/** @brief Returns the bitwise AND of two arrays */
		BitArray operator&(const BitArray& other) const;
		/** @brief Returns the bitwise XOR of two arrays */
		BitArray operator^(const BitArray& other) const;
		/** @brief Returns the bitwise OR of two arrays */
		BitArray operator|(const BitArray& other) const;
		/** @brief Returns the bitwise complement of the array */
		BitArray operator~() const;

		/** @brief Returns the array shifted left by the specified number of bits */
		BitArray operator<<(std::size_t count) const;
		/** @brief Returns the array shifted right by the specified number of bits */
		BitArray operator>>(std::size_t count) const;

		BitArray& operator++();
		BitArray& operator++(int);
		BitArray& operator--();
		BitArray& operator--(int);

		BitArray& operator&=(const BitArray& src);
		BitArray& operator^=(const BitArray& src);
		BitArray& operator|=(const BitArray& src);
		/** @brief Replaces the array with its bitwise complement */
		BitArray& notAll();

		BitArray& operator<<=(std::size_t shifts);
		BitArray& operator>>=(std::size_t shifts);

	private:
		char* _data;
		std::size_t _size;
	};
}