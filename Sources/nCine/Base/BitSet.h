#pragma once

#include "Algorithms.h"

#include <cstdint>

namespace nCine
{
	/**
		@brief Fixed-size sequence of bits backed by an unsigned integer
		
		Wraps an unsigned integral type `T` and exposes per-bit query and manipulation methods as well
		as the usual bitwise and shift operators. The number of bits equals `sizeof(T) * 8`.
	*/
	template<class T>
	class BitSet
	{
	public:
		BitSet();
		explicit BitSet(T value);

		bool operator==(const BitSet& other) const;
		bool operator!=(const BitSet& other) const;

		/** @brief Returns `true` if the bit at the specified position is set */
		bool test(std::uint32_t pos) const;

		/** @brief Returns `true` if all bits are set */
		bool all() const;
		/** @brief Returns `true` if at least one bit is set */
		bool any() const;
		/** @brief Returns `true` if no bit is set */
		bool none() const;

		/** @brief Returns the number of bits that are set */
		std::uint32_t count() const;

		/** @brief Returns the total number of bits in the bitset */
		std::uint32_t size() const;

		BitSet& operator&=(const BitSet& other);
		BitSet& operator|=(const BitSet& other);
		BitSet& operator^=(const BitSet& other);

		BitSet operator~() const;

		BitSet& operator<<=(std::uint32_t pos);
		BitSet& operator>>=(std::uint32_t pos);

		BitSet operator<<(std::uint32_t pos) const;
		BitSet operator>>(std::uint32_t pos) const;

		/** @brief Sets all bits in the bitset */
		void set();
		/** @brief Sets the bit at the specified position */
		void set(std::uint32_t pos);
		/** @brief Sets or clears the bit at the specified position */
		void set(std::uint32_t pos, bool value);

		/** @brief Clears all bits in the bitset */
		void reset();
		/** @brief Clears the bit at the specified position */
		void reset(std::uint32_t pos);

		/** @brief Flips the bit at the specified position */
		void flip(std::uint32_t pos);

		/** @brief Returns the bitwise AND of two bitsets */
		friend BitSet operator&(const BitSet& lhs, const BitSet& rhs) {
			return BitSet(lhs.bits_ & rhs.bits_);
		}
		/** @brief Returns the bitwise OR of two bitsets */
		friend BitSet operator|(const BitSet& lhs, const BitSet& rhs) {
			return BitSet(lhs.bits_ | rhs.bits_);
		}
		/** @brief Returns the bitwise XOR of two bitsets */
		friend BitSet operator^(const BitSet& lhs, const BitSet& rhs) {
			return BitSet(lhs.bits_ ^ rhs.bits_);
		}

	private:
		T bits_;
	};

	template<class T>
	BitSet<T>::BitSet()
		: bits_(T(0))
	{
		static_assert(isIntegral<T>::value, "Integral type is required");
		static_assert(T(0) < T(-1), "Unsigned type is required");
	}

	template<class T>
	BitSet<T>::BitSet(T value)
		: bits_(value)
	{
		static_assert(isIntegral<T>::value, "Integral type is required");
		static_assert(T(0) < T(-1), "Unsigned type is required");
	}

	template<class T>
	inline bool BitSet<T>::operator==(const BitSet& other) const
	{
		return other.bits_ == bits_;
	}

	template<class T>
	inline bool BitSet<T>::operator!=(const BitSet& other) const
	{
		return other.bits_ != bits_;
	}

	template<class T>
	inline bool BitSet<T>::test(std::uint32_t pos) const
	{
		return ((bits_ >> pos) & T(1)) != T(0);
	}

	template<class T>
	inline bool BitSet<T>::all() const
	{
		return ~bits_ == T(0);
	}

	template<class T>
	inline bool BitSet<T>::any() const
	{
		return bits_ != T(0);
	}

	template<class T>
	inline bool BitSet<T>::none() const
	{
		return bits_ == T(0);
	}

	template<>
	inline std::uint32_t BitSet<uint8_t>::count() const
	{
		static const uint8_t splitLookup[] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
		return splitLookup[bits_ & 0xF] + splitLookup[bits_ >> 4];
	}

	template<>
	inline std::uint32_t BitSet<uint16_t>::count() const
	{
		return BitSet<uint8_t>(bits_ & 0xFF).count() + BitSet<uint8_t>(bits_ >> 8).count();
	}

	template<>
	inline std::uint32_t BitSet<uint32_t>::count() const
	{
		uint32_t bits = bits_;
		bits = bits - ((bits >> 1) & 0x55555555);
		bits = (bits & 0x33333333) + ((bits >> 2) & 0x33333333);
		return (((bits + (bits >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
	}

	template<>
	inline std::uint32_t BitSet<uint64_t>::count() const
	{
		uint64_t bits = bits_;
		bits = bits - ((bits >> 1) & 0x5555555555555555);
		bits = (bits & 0x3333333333333333) + ((bits >> 2) & 0x3333333333333333);
		return (((bits + (bits >> 4)) & 0x0F0F0F0F0F0F0F0F) * 0x0101010101010101) >> 56;
	}

	template<class T>
	inline std::uint32_t BitSet<T>::size() const
	{
		return sizeof(T) * 8;
	}

	template<class T>
	inline BitSet<T>& BitSet<T>::operator&=(const BitSet& other)
	{
		bits_ &= other.bits_;
		return *this;
	}

	template<class T>
	inline BitSet<T>& BitSet<T>::operator|=(const BitSet& other)
	{
		bits_ |= other.bits_;
		return *this;
	}

	template<class T>
	inline BitSet<T>& BitSet<T>::operator^=(const BitSet& other)
	{
		bits_ ^= other.bits_;
		return *this;
	}

	template<class T>
	inline BitSet<T> BitSet<T>::operator~() const
	{
		return BitSet(~bits_);
	}

	template<class T>
	inline BitSet<T>& BitSet<T>::operator<<=(std::uint32_t pos)
	{
		bits_ <<= pos;
		return *this;
	}

	template<class T>
	inline BitSet<T>& BitSet<T>::operator>>=(std::uint32_t pos)
	{
		bits_ >>= pos;
		return *this;
	}

	template<class T>
	inline BitSet<T> BitSet<T>::operator<<(std::uint32_t pos) const
	{
		return BitSet(bits_ << pos);
	}

	template<class T>
	inline BitSet<T> BitSet<T>::operator>>(std::uint32_t pos) const
	{
		return BitSet(bits_ >> pos);
	}

	template<class T>
	inline void BitSet<T>::set()
	{
		bits_ = ~T(0);
	}

	template<class T>
	inline void BitSet<T>::set(std::uint32_t pos)
	{
		bits_ |= T(1) << pos;
	}

	template<class T>
	inline void BitSet<T>::set(std::uint32_t pos, bool value)
	{
		value ? set(pos) : reset(pos);
	}

	template<class T>
	inline void BitSet<T>::reset()
	{
		bits_ = T(0);
	}

	template<class T>
	inline void BitSet<T>::reset(std::uint32_t pos)
	{
		bits_ &= ~(T(1) << pos);
	}

	template<class T>
	inline void BitSet<T>::flip(std::uint32_t pos)
	{
		bits_ ^= (T(1) << pos);
	}
}