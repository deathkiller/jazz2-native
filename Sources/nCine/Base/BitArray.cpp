#include "BitArray.h"

#include <algorithm>
#include <climits>
#include <cstring>

#if !defined(CHAR_BIT)
#	warning CHAR_BIT not defined, assuming 8 bits
#	define CHAR_BIT 8
#endif

#if CHAR_BIT == 8
// Array index for character containing bit
#	define BIT_CHAR(bit)         ((bit) >> 3)
// Position of bit within character
#	define BIT_IN_CHAR(bit)      (1 << ((bit) & 0x07))
// Number of characters required to contain number of bits
#	define BITS_TO_CHARS(bits)   ((((bits) - 1) >> 3) + 1)
// Most significant bit in a character
#	define MS_BIT                (1 << (CHAR_BIT - 1))
#else
// Array index for character containing bit
#	define BIT_CHAR(bit)         ((bit) / CHAR_BIT)
// Position of bit within character
#	define BIT_IN_CHAR(bit)      (1 << ((bit) % CHAR_BIT))
// Number of characters required to contain number of bits
#	define BITS_TO_CHARS(bits)   ((((bits) - 1) / CHAR_BIT) + 1)
// Most significant bit in a character
#	define MS_BIT                (1 << (CHAR_BIT - 1))
#endif

namespace nCine
{
	BitArray::BitArray()
		: _size(0), _storage(nullptr)
	{

	}

	BitArray::BitArray(uint32_t numBits)
		: BitArray()
	{
		SetSize(numBits);
	}

	BitArray::~BitArray()
	{
		if (_storage != nullptr) {
			delete[] _storage;
			_storage = nullptr;
		}
	}

	void BitArray::SetSize(uint32_t numBits)
	{
		if (_storage != nullptr) {
			delete[] _storage;
		}

		_size = numBits;
		uint32_t numBytes = BITS_TO_CHARS(numBits);
		_storage = new unsigned char[numBytes] { };
	}

	void BitArray::SetAll()
	{
		int32_t bits;
		unsigned char mask;

		uint32_t size = BITS_TO_CHARS(_size);
		std::fill_n(_storage, size, UCHAR_MAX);

		bits = _size % CHAR_BIT;
		if (bits != 0) {
			mask = UCHAR_MAX << (CHAR_BIT - bits);
			_storage[BIT_CHAR(_size - 1)] = mask;
		}
	}

	void BitArray::ClearAll()
	{
		uint32_t size = BITS_TO_CHARS(_size);
		std::fill_n(_storage, size, 0);
	}

	void BitArray::Set(const uint32_t bit)
	{
		if (bit >= _size) {
			return;
		}

		_storage[BIT_CHAR(bit)] |= BIT_IN_CHAR(bit);
	}

	void BitArray::Set(const uint32_t bit, bool value)
	{
		if (bit >= _size) {
			return;
		}

		unsigned char& byte = _storage[BIT_CHAR(bit)];
		byte ^= (-(unsigned char)value ^ byte) & BIT_IN_CHAR(bit);
	}

	void BitArray::Reset(const uint32_t bit)
	{
		if (bit >= _size) {
			return;
		}

		unsigned char mask = BIT_IN_CHAR(bit);
		_storage[BIT_CHAR(bit)] &= ~mask;
	}

	BitArrayIndex BitArray::operator()(const uint32_t bit)
	{
		return BitArrayIndex(this, bit);
	}

	bool BitArray::operator[](const uint32_t bit) const
	{
		return (bit < _size ? ((_storage[BIT_CHAR(bit)] & BIT_IN_CHAR(bit)) != 0) : false);
	}

	bool BitArray::operator==(const BitArray& other) const
	{
		if (_size != other._size) {
			return false;
		}

		uint32_t size = BITS_TO_CHARS(_size);
		return (std::memcmp(this->_storage, other._storage, size) == 0);
	}

	BitArray BitArray::operator~() const
	{
		BitArray result(this->_size);
		result = *this;
		result.Not();

		return result;
	}

	BitArray BitArray::operator&(const BitArray& other) const
	{
		BitArray result(this->_size);
		result = *this;
		result &= other;

		return result;
	}

	BitArray BitArray::operator^(const BitArray& other) const
	{
		BitArray result(this->_size);
		result = *this;
		result ^= other;

		return result;
	}

	BitArray BitArray::operator|(const BitArray& other) const
	{
		BitArray result(this->_size);
		result = *this;
		result |= other;

		return result;
	}

	BitArray BitArray::operator<<(const uint32_t count) const
	{
		BitArray result(this->_size);
		result = *this;
		result <<= count;

		return result;
	}

	BitArray BitArray::operator>>(const uint32_t count) const
	{
		BitArray result(this->_size);
		result = *this;
		result >>= count;

		return result;
	}

	BitArray& BitArray::operator++()
	{
		unsigned char maxValue;
		unsigned char one;

		if (_size == 0) {
			return *this;
		}

		int32_t i = (_size % CHAR_BIT);
		if (i != 0) {
			maxValue = UCHAR_MAX << (CHAR_BIT - i);
			one = 1 << (CHAR_BIT - i);
		} else {
			maxValue = UCHAR_MAX;
			one = 1;
		}

		for (i = BIT_CHAR(_size - 1); i >= 0; i--) {
			if (_storage[i] != maxValue) {
				_storage[i] = _storage[i] + one;
				return *this;
			} else {
				_storage[i] = 0;

				maxValue = UCHAR_MAX;
				one = 1;
			}
		}

		return *this;
	}

	BitArray& BitArray::operator++(int32_t)
	{
		++(*this);
		return *this;
	}

	BitArray& BitArray::operator--()
	{
		int32_t i;
		unsigned char maxValue;
		unsigned char one;

		if (_size == 0) {
			return *this;
		}

		i = (_size % CHAR_BIT);
		if (i != 0) {
			maxValue = UCHAR_MAX << (CHAR_BIT - i);
			one = 1 << (CHAR_BIT - i);
		} else {
			maxValue = UCHAR_MAX;
			one = 1;
		}

		for (i = BIT_CHAR(_size - 1); i >= 0; i--) {
			if (_storage[i] >= one) {
				_storage[i] = _storage[i] - one;
				return *this;
			} else {
				_storage[i] = maxValue;

				maxValue = UCHAR_MAX;
				one = 1;
			}
		}

		return *this;
	}


	BitArray& BitArray::operator--(int32_t)
	{
		--(*this);
		return *this;
	}

	BitArray& BitArray::operator=(const BitArray& src)
	{
		if (*this == src) {
			return *this;
		}

		if (_size != src._size) {
			return *this;
		}

		if ((_size == 0) || (src._size == 0)) {
			return *this;
		}

		uint32_t size = BITS_TO_CHARS(_size);
		std::copy(src._storage, &src._storage[size], this->_storage);
		return *this;
	}

	BitArray& BitArray::operator&=(const BitArray& src)
	{
		uint32_t size = BITS_TO_CHARS(_size);

		if (_size != src._size) {
			return *this;
		}

		for (uint32_t i = 0; i < size; i++) {
			_storage[i] = _storage[i] & src._storage[i];
		}

		return *this;
	}

	BitArray& BitArray::operator^=(const BitArray& src)
	{
		uint32_t size = BITS_TO_CHARS(_size);

		if (_size != src._size) {
			return *this;
		}

		for (uint32_t i = 0; i < size; i++) {
			_storage[i] = _storage[i] ^ src._storage[i];
		}

		return *this;
	}

	BitArray& BitArray::operator|=(const BitArray& src)
	{
		uint32_t size = BITS_TO_CHARS(_size);

		if (_size != src._size) {
			return *this;
		}

		for (uint32_t i = 0; i < size; i++) {
			_storage[i] = _storage[i] | src._storage[i];
		}

		return *this;
	}

	BitArray& BitArray::Not()
	{
		int32_t bits;
		unsigned char mask;
		uint32_t size = BITS_TO_CHARS(_size);

		if (_size == 0) {
			return *this;
		}

		for (uint32_t i = 0; i < size; i++) {
			_storage[i] = ~_storage[i];
		}

		bits = _size % CHAR_BIT;
		if (bits != 0) {
			mask = UCHAR_MAX << (CHAR_BIT - bits);
			_storage[BIT_CHAR(_size - 1)] &= mask;
		}

		return *this;
	}

	BitArray& BitArray::operator<<=(const uint32_t shifts)
	{
		uint32_t i;
		uint32_t chars = shifts / CHAR_BIT;

		if (shifts >= _size) {
			this->ClearAll();
			return *this;
		}

		if (chars > 0) {
			uint32_t size = BITS_TO_CHARS(_size);

			for (i = 0; (i + chars) < size; i++) {
				_storage[i] = _storage[i + chars];
			}

			for (i = size; chars > 0; chars--) {
				_storage[i - chars] = 0;
			}
		}

		for (i = 0; i < (shifts % CHAR_BIT); i++) {
			for (uint32_t j = 0; j < BIT_CHAR(_size - 1); j++) {
				_storage[j] <<= 1;

				if (_storage[j + 1] & MS_BIT) {
					_storage[j] |= 0x01;
				}
			}

			_storage[BIT_CHAR(_size - 1)] <<= 1;
		}

		return *this;
	}

	BitArray& BitArray::operator>>=(const uint32_t shifts)
	{
		int32_t i;
		char mask;
		int32_t chars = shifts / CHAR_BIT;

		if (shifts >= _size) {
			this->ClearAll();
			return *this;
		}

		if (chars > 0) {
			for (i = BIT_CHAR(_size - 1); (i - chars) >= 0; i--) {
				_storage[i] = _storage[i - chars];
			}

			for (; chars > 0; chars--) {
				_storage[chars - 1] = 0;
			}
		}

		for (i = 0; i < (int32_t)(shifts % CHAR_BIT); i++) {
			for (uint32_t j = BIT_CHAR(_size - 1); j > 0; j--) {
				_storage[j] >>= 1;

				if (_storage[j - 1] & 0x01) {
					_storage[j] |= MS_BIT;
				}
			}

			_storage[0] >>= 1;
		}

		i = _size % CHAR_BIT;
		if (i != 0) {
			mask = UCHAR_MAX << (CHAR_BIT - i);
			_storage[BIT_CHAR(_size - 1)] &= mask;
		}

		return *this;
	}

	BitArrayIndex::BitArrayIndex(BitArray* array, const uint32_t index)
	{
		_bitArray = array;
		_index = index;
	}

	void BitArrayIndex::operator=(const bool value)
	{
		if (_bitArray != nullptr) {
			_bitArray->Set(_index, value);
		}
	}
}
