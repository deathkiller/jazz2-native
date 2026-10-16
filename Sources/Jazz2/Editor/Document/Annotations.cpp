#include "Annotations.h"

#if defined(WITH_EDITOR)

#include "LevelDocument.h"

#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		// varuint count, then for each one: i32 X, i32 Y, u8 slot, varuint length and the text
		void WriteVarUint(SmallVectorImpl<std::uint8_t>& data, std::uint32_t value)
		{
			do {
				std::uint8_t byte = std::uint8_t(value & 0x7F);
				value >>= 7;
				data.push_back(value != 0 ? std::uint8_t(byte | 0x80) : byte);
			} while (value != 0);
		}

		bool ReadVarUint(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& value)
		{
			value = 0;
			for (std::int32_t shift = 0; shift < 35 && p < end; shift += 7) {
				std::uint8_t byte = *p++;
				value |= std::uint32_t(byte & 0x7F) << shift;
				if ((byte & 0x80) == 0) {
					return true;
				}
			}
			return false;
		}

		void WriteInt32(SmallVectorImpl<std::uint8_t>& data, std::int32_t value)
		{
			std::uint32_t v = std::uint32_t(value);
			for (std::int32_t i = 0; i < 4; i++) {
				data.push_back(std::uint8_t(v >> (i * 8)));
			}
		}

		bool ReadInt32(const std::uint8_t*& p, const std::uint8_t* end, std::int32_t& value)
		{
			if (end - p < 4) {
				return false;
			}
			value = std::int32_t(std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24));
			p += 4;
			return true;
		}
	}

	void Annotation::Read(const LevelDocument& document, SmallVectorImpl<Annotation>& annotations)
	{
		annotations.clear();
		const Formats::ChunkRecord* chunk = document.FindChunk(Chunk);
		if (chunk == nullptr) {
			return;
		}
		const std::uint8_t* p = chunk->Data.data();
		const std::uint8_t* end = p + chunk->Data.size();
		std::uint32_t count;
		if (!ReadVarUint(p, end, count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			Annotation annotation;
			std::uint32_t length;
			if (!ReadInt32(p, end, annotation.X) || !ReadInt32(p, end, annotation.Y) || p >= end) {
				return;
			}
			annotation.Slot = *p++;
			if (!ReadVarUint(p, end, length) || std::uint32_t(end - p) < length) {
				return;
			}
			annotation.Text = String(reinterpret_cast<const char*>(p), length);
			p += length;
			annotations.push_back(std::move(annotation));
		}
	}

	void Annotation::Write(LevelDocument& document, const SmallVectorImpl<Annotation>& annotations)
	{
		SmallVector<std::uint8_t, 0> data;
		if (!annotations.empty()) {
			WriteVarUint(data, std::uint32_t(annotations.size()));
			for (const Annotation& annotation : annotations) {
				WriteInt32(data, annotation.X);
				WriteInt32(data, annotation.Y);
				data.push_back(annotation.Slot);
				WriteVarUint(data, std::uint32_t(annotation.Text.size()));
				for (char c : annotation.Text) {
					data.push_back(std::uint8_t(c));
				}
			}
		}
		// No annotations, no chunk
		document.SetChunk(Chunk, data);
	}
}

#endif
