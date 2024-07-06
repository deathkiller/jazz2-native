#pragma once

#include "../../Common.h"

#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	class JJ2Block
	{
	public:
		JJ2Block(std::unique_ptr<Stream>& s, std::int32_t length, std::int32_t uncompressedLength = 0);

		void SeekTo(std::int32_t offset);
		void DiscardBytes(std::int32_t length);

		bool ReadBool();
		std::uint8_t ReadByte();
		std::int16_t ReadInt16();
		std::uint16_t ReadUInt16();
		std::int32_t ReadInt32();
		std::uint32_t ReadUInt32();
		std::int32_t ReadUint7bitEncoded();
		float ReadFloat();
		float ReadFloatEncoded();
		void ReadRawBytes(std::uint8_t* dst, std::int32_t length);
		StringView ReadString(std::int32_t length, bool trimToNull);

		bool ReachedEndOfStream() {
			return (_offset == INT32_MAX);
		}

		std::int32_t GetLength() {
			return _length;
		}

	private:
		std::unique_ptr<std::uint8_t[]> _buffer;
		std::int32_t _length;
		std::int32_t _offset;
	};
}