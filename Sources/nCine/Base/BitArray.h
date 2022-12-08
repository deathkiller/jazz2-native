#pragma once

namespace nCine
{
	class BitArray;

	class BitArrayIndex
	{
		public:
			BitArrayIndex(BitArray *array, const unsigned int index);

			void operator=(const bool value);

		private:
			BitArray* _bitArray;
			unsigned int _index;
	};

	class BitArray
	{
		public:
			BitArray();
			BitArray(unsigned int numBits);
			~BitArray();

			unsigned int Size() const { return _size; };

			void SetSize(unsigned int numBits);

			void SetAll(void);
			void ClearAll(void);
			void Set(const unsigned int bit);
			void Set(const unsigned int bit, bool value);
			void Reset(const unsigned int bit);

			BitArrayIndex operator()(const unsigned int bit);

			bool operator[](const unsigned int bit) const;
			bool operator==(const BitArray &other) const;

			BitArray operator&(const BitArray &other) const;
			BitArray operator^(const BitArray &other) const;
			BitArray operator|(const BitArray &other) const;
			BitArray operator~(void) const;

			BitArray operator<<(const unsigned int count) const;
			BitArray operator>>(const unsigned int count) const;

			BitArray& operator++(void);
			BitArray& operator++(int);
			BitArray& operator--(void);
			BitArray& operator--(int);

			BitArray& operator=(const BitArray &src);

			BitArray& operator&=(const BitArray &src);
			BitArray& operator^=(const BitArray &src);
			BitArray& operator|=(const BitArray &src);
			BitArray& Not(void);

			BitArray& operator<<=(unsigned const int shifts);
			BitArray& operator>>=(unsigned const int shifts);

		protected:
			unsigned int _size;
			unsigned char* _storage;
	};
}