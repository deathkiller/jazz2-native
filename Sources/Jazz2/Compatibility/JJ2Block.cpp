#include "JJ2Block.h"

#include <cstring>

#include <Base/Memory.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::Memory;
using namespace Death::IO::Compression;

namespace Jazz2::Compatibility
{
	JJ2Block::JJ2Block(std::unique_ptr<Stream>& s, std::int32_t length, std::int32_t uncompressedLength)
		: _length(0), _offset(0)
	{
		const std::int64_t position = s->GetPosition();
		const std::int64_t size = s->GetSize();
		if (position >= 0 && size >= position && std::int64_t(length) > size - position) {
			length = std::int32_t(size - position);
		}
		if (length < 0 || uncompressedLength < 0) {
			LOGW("Block has invalid length {} (unpacked {})", length, uncompressedLength);
			return;
		}

		if (uncompressedLength > 0) {
			// Deflate cannot expand its input more than 1032 times, so a larger unpacked size is corrupt
			if (length < 2 || uncompressedLength > MaxUnpackedLength || std::int64_t(uncompressedLength) > std::int64_t(length) * 1032) {
				LOGW("Block has invalid unpacked length {} for {} packed bytes", uncompressedLength, length);
				s->Seek(length, SeekOrigin::Current);
				return;
			}

			s->Seek(2, SeekOrigin::Current);
			_buffer = std::make_unique<uint8_t[]>(uncompressedLength);
			DeflateStream uc(*s, length - 2);
			uc.Read(_buffer.get(), uncompressedLength);
			_length = (uc.IsValid() ? uncompressedLength : 0);
		} else {
			_buffer = std::make_unique<uint8_t[]>(length);
			s->Read(_buffer.get(), length);
			_length = length;
		}
	}

	void JJ2Block::SeekTo(std::int32_t offset)
	{
		_offset = (offset >= 0 && offset <= _length ? offset : INT32_MAX);
	}

	void JJ2Block::DiscardBytes(std::int32_t length)
	{
		_offset = (length >= 0 && length <= _length - _offset ? _offset + length : INT32_MAX);
	}

	bool JJ2Block::ReadBool()
	{
		if (_offset >= _length) {
			_offset = INT32_MAX;
			return false;
		}

		return _buffer[_offset++] != 0x00;
	}

	std::uint8_t JJ2Block::ReadByte()
	{
		if (_offset >= _length) {
			_offset = INT32_MAX;
			return 0;
		}

		return _buffer[_offset++];
	}

	std::int16_t JJ2Block::ReadInt16()
	{
		if (_offset > _length - 2) {
			_offset = INT32_MAX;
			return false;
		}

		std::int16_t result;
		std::memcpy(&result, &_buffer[_offset], sizeof(result));
		_offset += sizeof(result);
		return AsLE(result);
	}

	std::uint16_t JJ2Block::ReadUInt16()
	{
		if (_offset > _length - 2) {
			_offset = INT32_MAX;
			return false;
		}

		std::uint16_t result;
		std::memcpy(&result, &_buffer[_offset], sizeof(result));
		_offset += sizeof(result);
		return AsLE(result);
	}

	std::int32_t JJ2Block::ReadInt32()
	{
		if (_offset > _length - 4) {
			_offset = INT32_MAX;
			return false;
		}

		std::int32_t result;
		std::memcpy(&result, &_buffer[_offset], sizeof(result));
		_offset += sizeof(result);
		return AsLE(result);
	}

	std::uint32_t JJ2Block::ReadUInt32()
	{
		if (_offset > _length - 4) {
			_offset = INT32_MAX;
			return false;
		}

		std::uint32_t result;
		std::memcpy(&result, &_buffer[_offset], sizeof(result));
		_offset += sizeof(result);
		return AsLE(result);
	}


	std::int32_t JJ2Block::ReadUint7bitEncoded()
	{
		std::uint32_t result = 0;

		while (true) {
			if (_offset >= _length) {
				_offset = INT32_MAX;
				break;
			}

			std::uint8_t current = _buffer[_offset++];
			result |= (current & 0x7F);
			if (current >= 0x80) {
				result <<= 7;
			} else {
				break;
			}
		}

		return std::int32_t(result);
	}

	float JJ2Block::ReadFloat()
	{
		if (_offset > _length - 4) {
			_offset = INT32_MAX;
			return false;
		}

		float result;
		std::memcpy(&result, &_buffer[_offset], sizeof(result));
		_offset += sizeof(result);
		return AsLE(result);
	}

	float JJ2Block::ReadFloatEncoded()
	{
		return ((float)ReadInt32() / 65536.0f);
	}

	void JJ2Block::ReadRawBytes(std::uint8_t* dst, std::int32_t length)
	{
		if (length <= 0) {
			return;
		}

		std::int32_t bytesLeft = GetRemainingLength();
		bool endOfStream = false;
		if (length > bytesLeft) {
			std::memset(dst + bytesLeft, 0, length - bytesLeft);
			length = bytesLeft;
			endOfStream = true;
		}

		if (length > 0) {
			std::memcpy(dst, _buffer.get() + _offset, length);
		}

		if (endOfStream) {
			_offset = INT32_MAX;
		} else {
			_offset += length;
		}
	}

	StringView JJ2Block::ReadString(std::int32_t length, bool trimToNull)
	{
		if (length < 0) {
			length = 0;
		}

		std::int32_t bytesLeft = GetRemainingLength();
		bool endOfStream = false;
		if (length > bytesLeft) {
			length = bytesLeft;
			endOfStream = true;
		}

		if (length == 0) {
			if (endOfStream) {
				_offset = INT32_MAX;
			}
			return {};
		}

		std::int32_t realLength = length;
		if (trimToNull) {
			for (std::int32_t i = 0; i < realLength; i++) {
				if (_buffer[_offset + i] == '\0') {
					realLength = i;
					break;
				}
			}
		} else {
			while (realLength > 0 && (_buffer[_offset + realLength - 1] == '\0' || _buffer[_offset + realLength - 1] == ' ')) {
				realLength--;
			}
		}

		StringView result((const char*)&_buffer[_offset], realLength);

		if (endOfStream) {
			_offset = INT32_MAX;
		} else {
			_offset += length;
		}

		return result;
	}
}