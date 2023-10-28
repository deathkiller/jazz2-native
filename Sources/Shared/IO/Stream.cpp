#include "Stream.h"

namespace Death::IO
{
	Containers::StringView Stream::GetPath() const
	{
		return _path;
	}

	std::int32_t Stream::ReadVariableInt32()
	{
		std::uint32_t n = ReadVariableUint32();
		return (std::int32_t)(n >> 1) ^ -(std::int32_t)(n & 1);
	}

	std::int64_t Stream::ReadVariableInt64()
	{
		std::uint64_t n = ReadVariableUint64();
		return (std::int64_t)(n >> 1) ^ -(std::int64_t)(n & 1);
	}

	std::uint32_t Stream::ReadVariableUint32()
	{
		std::int32_t pos = GetPosition();
		std::uint32_t result = 0;
		std::uint32_t shift = 0;
		while (pos < _size) {
			std::uint8_t byte = ReadValue<std::uint8_t>();
			result |= (byte & 0x7f) << shift;
			shift += 7;
			if ((byte & 0x80) == 0) {
				break;
			}
			pos++;
		}
		return result;
	}

	std::uint64_t Stream::ReadVariableUint64()
	{
		std::int32_t pos = GetPosition();
		std::uint64_t result = 0;
		std::uint64_t shift = 0;
		while (pos < _size) {
			std::uint8_t byte = ReadValue<std::uint8_t>();
			result |= (byte & 0x7f) << shift;
			shift += 7;
			if ((byte & 0x80) == 0) {
				break;
			}
			pos++;
		}
		return result;
	}

	std::int32_t Stream::WriteVariableInt32(std::int32_t value)
	{
		std::uint32_t n = (std::uint32_t)(value << 1) ^ (std::uint32_t)(value >> 31);
		return WriteVariableUint32(n);
	}

	std::int32_t Stream::WriteVariableInt64(std::int64_t value)
	{
		std::uint64_t n = (std::uint64_t)(value << 1) ^ (std::uint64_t)(value >> 63);
		return WriteVariableUint64(n);
	}

	std::int32_t Stream::WriteVariableUint32(std::uint32_t value)
	{
		std::int32_t bytesWritten = 1;
		std::uint32_t valueLeft = value;
		while (valueLeft >= 0x80) {
			WriteValue((std::uint8_t)(valueLeft | 0x80));
			valueLeft = valueLeft >> 7;
			bytesWritten++;
		}
		WriteValue((std::uint8_t)valueLeft);
		return bytesWritten;
	}

	std::int32_t Stream::WriteVariableUint64(std::uint64_t value)
	{
		std::int32_t bytesWritten = 1;
		std::uint64_t valueLeft = value;
		while (valueLeft >= 0x80) {
			WriteValue((std::uint8_t)(valueLeft | 0x80));
			valueLeft = valueLeft >> 7;
			bytesWritten++;
		}
		WriteValue((std::uint8_t)valueLeft);
		return bytesWritten;
	}
}