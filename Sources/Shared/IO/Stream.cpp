#include "Stream.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	Stream::Stream()
	{
	}

	std::int64_t Stream::CopyTo(Stream& targetStream)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		constexpr std::int32_t BufferSize = 8192;
#else
		constexpr std::int32_t BufferSize = 16384;
#endif

		char buffer[BufferSize];
		std::int64_t bytesWrittenTotal = 0;

		while (true) {
			std::int32_t bytesRead = Read(buffer, BufferSize);
			if (bytesRead <= 0) {
				break;
			}

			std::int32_t bytesWritten = targetStream.Write(buffer, bytesRead);
			bytesWrittenTotal += bytesWritten;
			if (bytesWritten < bytesRead) {
				break;
			}
		}

		return bytesWrittenTotal;
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
		std::uint32_t result = 0;
		std::uint32_t shift = 0;
		while (true) {
			std::uint8_t byte;
			if (Read(&byte, 1) == 0) {
				break;
			}

			result |= (std::uint32_t)(byte & 0x7f) << shift;
			shift += 7;
			if ((byte & 0x80) == 0) {
				break;
			}
		}
		return result;
	}

	std::uint64_t Stream::ReadVariableUint64()
	{
		std::uint64_t result = 0;
		std::uint64_t shift = 0;
		while (true) {
			std::uint8_t byte;
			if (Read(&byte, 1) == 0) {
				break;
			}

			result |= (std::uint64_t)(byte & 0x7f) << shift;
			shift += 7;
			if ((byte & 0x80) == 0) {
				break;
			}
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

}}