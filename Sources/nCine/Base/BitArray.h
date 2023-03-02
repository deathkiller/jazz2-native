#pragma once

#include "../../Common.h"

namespace nCine
{
	class BitArray;

	class BitArrayIndex
	{
		public:
			BitArrayIndex(BitArray* array, const uint32_t index);

			void operator=(const bool value);

		private:
			BitArray* _bitArray;
			uint32_t _index;
	};

	class BitArray
	{
		public:
			BitArray();
			BitArray(uint32_t numBits);
			~BitArray();

			uint32_t Size() const { return _size; };

			void SetSize(uint32_t numBits);

			void SetAll();
			void ClearAll();
			void Set(const uint32_t bit);
			void Set(const uint32_t bit, bool value);
			void Reset(const uint32_t bit);

			BitArrayIndex operator()(const uint32_t bit);

			bool operator[](const uint32_t bit) const;
			bool operator==(const BitArray &other) const;

			BitArray operator&(const BitArray &other) const;
			BitArray operator^(const BitArray &other) const;
			BitArray operator|(const BitArray &other) const;
			BitArray operator~() const;

			BitArray operator<<(const uint32_t count) const;
			BitArray operator>>(const uint32_t count) const;

			BitArray& operator++();
			BitArray& operator++(int32_t);
			BitArray& operator--();
			BitArray& operator--(int32_t);

			BitArray& operator=(const BitArray &src);

			BitArray& operator&=(const BitArray &src);
			BitArray& operator^=(const BitArray &src);
			BitArray& operator|=(const BitArray &src);
			BitArray& Not();

			BitArray& operator<<=(const uint32_t shifts);
			BitArray& operator>>=(const uint32_t shifts);

		protected:
			uint32_t _size;
			unsigned char* _storage;
	};
}