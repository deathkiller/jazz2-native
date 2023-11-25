#pragma once

#include "../../Common.h"

namespace nCine
{
	class BitArray;

	class BitArrayIndex
	{
		public:
			BitArrayIndex(BitArray* array, std::uint32_t index);

			void operator=(bool value);

		private:
			BitArray* _bitArray;
			std::uint32_t _index;
	};

	class BitArray
	{
		public:
			BitArray();
			BitArray(std::uint32_t numBits);
			~BitArray();

			std::uint8_t* RawData() { return _storage; }

			std::int32_t Size() const { return _size; };
			std::int32_t SizeInBytes() const;

			void SetSize(std::uint32_t numBits);

			void SetAll();
			void ClearAll();
			void Set(std::uint32_t bit);
			void Set(std::uint32_t bit, bool value);
			void Reset(std::uint32_t bit);

			BitArrayIndex operator()(const uint32_t bit);

			bool operator[](std::uint32_t bit) const;
			bool operator==(const BitArray &other) const;

			BitArray operator&(const BitArray &other) const;
			BitArray operator^(const BitArray &other) const;
			BitArray operator|(const BitArray &other) const;
			BitArray operator~() const;

			BitArray operator<<(std::uint32_t count) const;
			BitArray operator>>(std::uint32_t count) const;

			BitArray& operator++();
			BitArray& operator++(std::int32_t);
			BitArray& operator--();
			BitArray& operator--(std::int32_t);

			BitArray& operator=(const BitArray &src);

			BitArray& operator&=(const BitArray &src);
			BitArray& operator^=(const BitArray &src);
			BitArray& operator|=(const BitArray &src);
			BitArray& Not();

			BitArray& operator<<=(std::uint32_t shifts);
			BitArray& operator>>=(std::uint32_t shifts);

		protected:
			std::int32_t _size;
			std::uint8_t* _storage;
	};
}