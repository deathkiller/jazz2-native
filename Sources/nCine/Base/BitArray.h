#pragma once

#include "../../Common.h"

#include <Containers/Tags.h>

using namespace Death::Containers;

namespace nCine
{
	class BitArray;

	class BitArrayIndex
	{
	public:
		BitArrayIndex(BitArray* array, std::size_t bit);

		void operator=(bool value);

	private:
		BitArray* _bitArray;
		std::size_t _bit;
	};

	class BitArray
	{
	public:
		BitArray();
		BitArray(ValueInitT, std::size_t sizeInBits);
		BitArray(NoInitT, std::size_t sizeInBits);
		~BitArray();

		BitArray(const BitArray&) = delete;
		BitArray& operator=(const BitArray&) = delete;
		BitArray(BitArray&& other) noexcept;
		BitArray& operator=(BitArray&& other) noexcept;

		char* data() { return _data; }

		bool empty() const { return !_size; }
		std::size_t size() const { return _size; };
		std::size_t sizeInBytes() const;

		void resize(ValueInitT, std::size_t sizeInBits);
		void resize(NoInitT, std::size_t sizeInBits);

		void setAll();
		void resetAll();
		void set(std::size_t bit);
		void set(std::size_t bit, bool value);
		void reset(std::size_t bit);

		BitArrayIndex operator()(const std::size_t bit);

		bool operator[](std::size_t bit) const;
		bool operator==(const BitArray& other) const;

		BitArray operator&(const BitArray& other) const;
		BitArray operator^(const BitArray& other) const;
		BitArray operator|(const BitArray& other) const;
		BitArray operator~() const;

		BitArray operator<<(std::size_t count) const;
		BitArray operator>>(std::size_t count) const;

		BitArray& operator++();
		BitArray& operator++(int);
		BitArray& operator--();
		BitArray& operator--(int);

		BitArray& operator&=(const BitArray& src);
		BitArray& operator^=(const BitArray& src);
		BitArray& operator|=(const BitArray& src);
		BitArray& notAll();

		BitArray& operator<<=(std::size_t shifts);
		BitArray& operator>>=(std::size_t shifts);

	protected:
		char* _data;
		std::size_t _size;
	};
}