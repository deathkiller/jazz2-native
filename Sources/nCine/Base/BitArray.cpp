#include "BitArray.h"

#include <algorithm>
#include <climits>
#include <cstring>

#if !defined(CHAR_BIT)
#	warning "CHAR_BIT not defined, assuming 8 bits"
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
		: _size(0), _data(nullptr)
	{
	}

	BitArray::BitArray(ValueInitT, std::size_t sizeInBits)
	{
		_size = sizeInBits;
		_data = _size ? new char[BITS_TO_CHARS(sizeInBits)]{} : nullptr;
	}

	BitArray::BitArray(NoInitT, std::size_t sizeInBits)
	{
		_size = sizeInBits;
		_data = _size ? new char[BITS_TO_CHARS(sizeInBits)] : nullptr;
	}

	BitArray::~BitArray()
	{
		if (_data != nullptr) {
			delete[] _data;
			_data = nullptr;
		}
	}

	BitArray::BitArray(BitArray&& other) noexcept
		: _data(other._data), _size(other._size)
	{
		other._data = nullptr;
		other._size = 0;
	}

	BitArray& BitArray::operator=(BitArray&& other) noexcept
	{
		using std::swap;
		swap(_data, other._data);
		swap(_size, other._size);
		return *this;
	}

	std::size_t BitArray::sizeInBytes() const
	{
		return BITS_TO_CHARS(_size);
	}

	void BitArray::resize(ValueInitT, std::size_t sizeInBits)
	{
		if (_size == sizeInBits) {
			return;
		}
		if (sizeInBits == 0) {
			delete[] _data;
			_data = nullptr;
			_size = 0;
			return;
		}

		char* prevData = _data;
		std::size_t prevSize = _size;
		std::size_t prevSizeInBytes = BITS_TO_CHARS(prevSize);

		_size = sizeInBits;
		std::size_t sizeInBytes = BITS_TO_CHARS(sizeInBits);
		_data = new char[sizeInBytes]{};

		if (prevData != nullptr) {
			std::memcpy(_data, prevData, std::min(prevSizeInBytes, sizeInBytes));
			delete[] prevData;
		}
	}

	void BitArray::resize(NoInitT, std::size_t sizeInBits)
	{
		if (_size == sizeInBits) {
			return;
		}
		if (sizeInBits == 0) {
			delete[] _data;
			_data = nullptr;
			_size = 0;
			return;
		}

		char* prevData = _data;
		std::size_t prevSize = _size;
		std::size_t prevSizeInBytes = BITS_TO_CHARS(prevSize);

		_size = sizeInBits;
		std::size_t sizeInBytes = BITS_TO_CHARS(sizeInBits);
		_data = new char[sizeInBytes];

		if (prevData != nullptr) {
			std::memcpy(_data, prevData, std::min(prevSizeInBytes, sizeInBytes));
			delete[] prevData;
		}
	}

	void BitArray::setAll()
	{
		std::size_t size = BITS_TO_CHARS(_size);
		std::fill_n(_data, size, UINT8_MAX);

		std::size_t bits = (_size % CHAR_BIT);
		if (bits != 0) {
			std::uint8_t mask = UCHAR_MAX << (CHAR_BIT - bits);
			_data[BIT_CHAR(_size - 1)] = mask;
		}
	}

	void BitArray::resetAll()
	{
		std::size_t size = BITS_TO_CHARS(_size);
		std::fill_n(_data, size, 0);
	}

	void BitArray::set(std::size_t bit)
	{
		if (bit >= _size) {
			return;
		}

		_data[BIT_CHAR(bit)] |= BIT_IN_CHAR(bit);
	}

	void BitArray::set(std::size_t bit, bool value)
	{
		if (bit >= _size) {
			return;
		}

		char& byte = _data[BIT_CHAR(bit)];
		byte ^= (-(std::uint8_t)value ^ byte) & BIT_IN_CHAR(bit);
	}

	void BitArray::reset(std::size_t bit)
	{
		if (bit >= _size) {
			return;
		}

		std::uint8_t mask = BIT_IN_CHAR(bit);
		_data[BIT_CHAR(bit)] &= ~mask;
	}

	BitArrayIndex BitArray::operator()(std::size_t bit)
	{
		return BitArrayIndex(this, bit);
	}

	bool BitArray::operator[](std::size_t bit) const
	{
		return (bit < _size ? ((_data[BIT_CHAR(bit)] & BIT_IN_CHAR(bit)) != 0) : false);
	}

	bool BitArray::operator==(const BitArray& other) const
	{
		if (_size != other._size) {
			return false;
		}

		std::size_t size = BITS_TO_CHARS(_size);
		return (std::memcmp(this->_data, other._data, size) == 0);
	}

	BitArray BitArray::operator~() const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result.notAll();

		return result;
	}

	BitArray BitArray::operator&(const BitArray& other) const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result &= other;

		return result;
	}

	BitArray BitArray::operator^(const BitArray& other) const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result ^= other;

		return result;
	}

	BitArray BitArray::operator|(const BitArray& other) const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result |= other;

		return result;
	}

	BitArray BitArray::operator<<(const std::size_t count) const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result <<= count;

		return result;
	}

	BitArray BitArray::operator>>(const std::size_t count) const
	{
		BitArray result(NoInit, _size);
		std::memcpy(result._data, _data, BITS_TO_CHARS(_size));
		result >>= count;

		return result;
	}

	BitArray& BitArray::operator++()
	{
		if (_size == 0) {
			return *this;
		}

		std::uint8_t maxValue, one;
		std::size_t i = (_size % CHAR_BIT);
		if (i != 0) {
			maxValue = UINT8_MAX << (CHAR_BIT - i);
			one = 1 << (CHAR_BIT - i);
		} else {
			maxValue = UINT8_MAX;
			one = 1;
		}

		for (i = BIT_CHAR(_size - 1); i >= 0; i--) {
			if (_data[i] != maxValue) {
				_data[i] = _data[i] + one;
				return *this;
			} else {
				_data[i] = 0;

				maxValue = UINT8_MAX;
				one = 1;
			}
		}

		return *this;
	}

	BitArray& BitArray::operator++(int)
	{
		++(*this);
		return *this;
	}

	BitArray& BitArray::operator--()
	{
		if (_size == 0) {
			return *this;
		}

		std::uint8_t maxValue, one;
		std::size_t i = (_size % CHAR_BIT);
		if (i != 0) {
			maxValue = UINT8_MAX << (CHAR_BIT - i);
			one = 1 << (CHAR_BIT - i);
		} else {
			maxValue = UINT8_MAX;
			one = 1;
		}

		for (i = BIT_CHAR(_size - 1); i >= 0; i--) {
			if (_data[i] >= one) {
				_data[i] = _data[i] - one;
				return *this;
			} else {
				_data[i] = maxValue;

				maxValue = UINT8_MAX;
				one = 1;
			}
		}

		return *this;
	}

	BitArray& BitArray::operator--(int)
	{
		--(*this);
		return *this;
	}

	BitArray& BitArray::operator&=(const BitArray& src)
	{
		if (_size != src._size) {
			return *this;
		}

		std::size_t size = BITS_TO_CHARS(_size);
		for (std::size_t i = 0; i < size; i++) {
			_data[i] = (_data[i] & src._data[i]);
		}

		return *this;
	}

	BitArray& BitArray::operator^=(const BitArray& src)
	{
		if (_size != src._size) {
			return *this;
		}

		std::size_t size = BITS_TO_CHARS(_size);
		for (std::size_t i = 0; i < size; i++) {
			_data[i] = (_data[i] ^ src._data[i]);
		}

		return *this;
	}

	BitArray& BitArray::operator|=(const BitArray& src)
	{
		if (_size != src._size) {
			return *this;
		}

		std::size_t size = BITS_TO_CHARS(_size);
		for (std::size_t i = 0; i < size; i++) {
			_data[i] = (_data[i] | src._data[i]);
		}

		return *this;
	}

	BitArray& BitArray::notAll()
	{
		if (_size == 0) {
			return *this;
		}

		std::size_t size = BITS_TO_CHARS(_size);
		for (std::size_t i = 0; i < size; i++) {
			_data[i] = ~_data[i];
		}

		std::size_t bits = (_size % CHAR_BIT);
		if (bits != 0) {
			std::uint8_t mask = UINT8_MAX << (CHAR_BIT - bits);
			_data[BIT_CHAR(_size - 1)] &= mask;
		}

		return *this;
	}

	BitArray& BitArray::operator<<=(const std::size_t shifts)
	{
		if (shifts >= _size) {
			this->resetAll();
			return *this;
		}

		std::size_t i;
		std::size_t chars = (shifts / CHAR_BIT);
		if (chars > 0) {
			std::size_t size = BITS_TO_CHARS(_size);

			for (i = 0; (i + chars) < size; i++) {
				_data[i] = _data[i + chars];
			}

			for (i = size; chars > 0; chars--) {
				_data[i - chars] = 0;
			}
		}

		for (i = 0; i < (shifts % CHAR_BIT); i++) {
			for (std::size_t j = 0; j < BIT_CHAR(_size - 1); j++) {
				_data[j] <<= 1;

				if (_data[j + 1] & MS_BIT) {
					_data[j] |= 0x01;
				}
			}

			_data[BIT_CHAR(_size - 1)] <<= 1;
		}

		return *this;
	}

	BitArray& BitArray::operator>>=(std::size_t shifts)
	{
		if (shifts >= _size) {
			this->resetAll();
			return *this;
		}

		std::size_t i;
		std::size_t chars = shifts / CHAR_BIT;
		if (chars > 0) {
			for (i = BIT_CHAR(_size - 1); (i - chars) >= 0; i--) {
				_data[i] = _data[i - chars];
			}

			for (; chars > 0; chars--) {
				_data[chars - 1] = 0;
			}
		}

		for (i = 0; i < (shifts % CHAR_BIT); i++) {
			for (std::size_t j = BIT_CHAR(_size - 1); j > 0; j--) {
				_data[j] >>= 1;

				if (_data[j - 1] & 0x01) {
					_data[j] |= MS_BIT;
				}
			}

			_data[0] >>= 1;
		}

		i = _size % CHAR_BIT;
		if (i != 0) {
			std::uint8_t mask = UINT8_MAX << (CHAR_BIT - i);
			_data[BIT_CHAR(_size - 1)] &= mask;
		}

		return *this;
	}

	BitArrayIndex::BitArrayIndex(BitArray* array, std::size_t bit)
	{
		_bitArray = array;
		_bit = bit;
	}

	void BitArrayIndex::operator=(bool value)
	{
		_bitArray->set(_bit, value);
	}
}
