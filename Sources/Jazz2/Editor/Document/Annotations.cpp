#include "Annotations.h"

#if defined(WITH_EDITOR)

#include "LevelDocument.h"
#include "../../Formats/VarUint.h"

#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		using Formats::ReadVarUint;
		using Formats::WriteVarUint;

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

		// Fields of a record, as both versions have them, returns false if they don't fit
		bool ReadFields(const std::uint8_t*& p, const std::uint8_t* end, Annotation& annotation)
		{
			std::uint32_t length;
			if (!ReadInt32(p, end, annotation.X) || !ReadInt32(p, end, annotation.Y) || p >= end) {
				return false;
			}
			annotation.Slot = *p++;
			if (!ReadVarUint(p, end, length) || std::uint32_t(end - p) < length) {
				return false;
			}
			annotation.Text = String(reinterpret_cast<const char*>(p), length);
			p += length;
			return true;
		}

		void WriteString(SmallVectorImpl<std::uint8_t>& data, StringView text)
		{
			WriteVarUint(data, std::uint32_t(text.size()));
			data.append(reinterpret_cast<const std::uint8_t*>(text.data()), reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
		}

		bool ReadString(const std::uint8_t*& p, const std::uint8_t* end, String& text)
		{
			std::uint32_t length;
			if (!ReadVarUint(p, end, length) || std::uint32_t(end - p) < length) {
				return false;
			}
			text = String(reinterpret_cast<const char*>(p), length);
			p += length;
			return true;
		}

		void WriteInt64(SmallVectorImpl<std::uint8_t>& data, std::int64_t value)
		{
			std::uint64_t v = std::uint64_t(value);
			for (std::int32_t i = 0; i < 8; i++) {
				data.push_back(std::uint8_t(v >> (i * 8)));
			}
		}

		bool ReadInt64(const std::uint8_t*& p, const std::uint8_t* end, std::int64_t& value)
		{
			if (end - p < 8) {
				return false;
			}
			std::uint64_t v = 0;
			for (std::int32_t i = 0; i < 8; i++) {
				v |= std::uint64_t(p[i]) << (i * 8);
			}
			value = std::int64_t(v);
			p += 8;
			return true;
		}

		// What version 2 adds to a record, a record of version 1 ends before it, a damaged one is kept as far as it's read
		void ReadExtraFields(const std::uint8_t*& p, const std::uint8_t* end, Annotation& annotation)
		{
			std::int32_t color;
			if (p >= end || !ReadInt32(p, end, color)) {
				return;
			}
			annotation.Color = std::uint32_t(color);
			std::uint32_t replyCount;
			if (!ReadString(p, end, annotation.Author) || !ReadInt64(p, end, annotation.Time) || p >= end) {
				return;
			}
			annotation.Resolved = ((*p++ & 0x01) != 0);
			if (!ReadVarUint(p, end, replyCount)) {
				return;
			}
			for (std::uint32_t i = 0; i < replyCount && p < end; i++) {
				Annotation::Reply reply;
				if (!ReadString(p, end, reply.Author) || !ReadInt64(p, end, reply.Time) || !ReadString(p, end, reply.Text)) {
					return;
				}
				annotation.Replies.push_back(Death::move(reply));
			}
		}

		// varuint count, then for each one: i32 X, i32 Y, u8 slot, varuint length and the text
		void ReadLegacy(const Formats::ChunkRecord& chunk, SmallVectorImpl<Annotation>& annotations)
		{
			const std::uint8_t* p = chunk.Data.data();
			const std::uint8_t* end = p + chunk.Data.size();
			std::uint32_t count;
			if (!ReadVarUint(p, end, count)) {
				return;
			}
			for (std::uint32_t i = 0; i < count; i++) {
				Annotation annotation;
				if (!ReadFields(p, end, annotation)) {
					return;
				}
				annotations.push_back(Death::move(annotation));
			}
		}

		// u8 version, varuint count, then each record as a varuint size and its fields
		void ReadVersioned(const Formats::ChunkRecord& chunk, SmallVectorImpl<Annotation>& annotations)
		{
			const std::uint8_t* p = chunk.Data.data();
			const std::uint8_t* end = p + chunk.Data.size();
			std::uint32_t count;
			// A newer version only adds fields to the records, so any version but 0 is read
			if (p >= end || *p++ == 0 || !ReadVarUint(p, end, count)) {
				return;
			}
			for (std::uint32_t i = 0; i < count; i++) {
				std::uint32_t size;
				if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
					return;
				}
				const std::uint8_t* recordEnd = p + size;
				Annotation annotation;
				if (ReadFields(p, recordEnd, annotation)) {
					ReadExtraFields(p, recordEnd, annotation);
					annotations.push_back(Death::move(annotation));
				}
				// What a newer version added is skipped
				p = recordEnd;
			}
		}

		constexpr std::uint8_t Version = 2;
	}

	void Annotation::Read(const LevelDocument& document, SmallVectorImpl<Annotation>& annotations)
	{
		annotations.clear();
		// This version never writes the legacy chunk and drops it once it's read, so a level that has both was saved
		// by 3.9.0 last, which wrote the legacy one anew and only kept the other one as it was
		if (const Formats::ChunkRecord* legacy = document.FindChunk(LegacyChunk)) {
			ReadLegacy(*legacy, annotations);
		} else if (const Formats::ChunkRecord* chunk = document.FindChunk(Chunk)) {
			ReadVersioned(*chunk, annotations);
		}
	}

	void Annotation::Serialize(const SmallVectorImpl<Annotation>& annotations, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (annotations.empty()) {
			// No annotations, no chunk
			return;
		}
		data.push_back(Version);
		WriteVarUint(data, std::uint32_t(annotations.size()));
		SmallVector<std::uint8_t, 64> record;
		for (const Annotation& annotation : annotations) {
			record.clear();
			WriteInt32(record, annotation.X);
			WriteInt32(record, annotation.Y);
			record.push_back(annotation.Slot);
			WriteString(record, annotation.Text);
			// Version 2
			WriteInt32(record, std::int32_t(annotation.Color));
			WriteString(record, annotation.Author);
			WriteInt64(record, annotation.Time);
			record.push_back(annotation.Resolved ? 0x01 : 0x00);
			WriteVarUint(record, std::uint32_t(annotation.Replies.size()));
			for (const Annotation::Reply& reply : annotation.Replies) {
				WriteString(record, reply.Author);
				WriteInt64(record, reply.Time);
				WriteString(record, reply.Text);
			}
			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}
	}

	void Annotation::Write(LevelDocument& document, const SmallVectorImpl<Annotation>& annotations)
	{
		SmallVector<std::uint8_t, 0> data;
		Serialize(annotations, data);
		document.SetChunk(Chunk, data);
		document.SetChunk(LegacyChunk, {});
	}

	void Annotation::UpgradeLegacyChunk(LevelDocument& document)
	{
		if (document.FindChunk(LegacyChunk) == nullptr) {
			return;
		}
		SmallVector<Annotation, 0> annotations;
		Read(document, annotations);

		// What the legacy chunk has no room for (the color, the author, the replies) is taken from the older versioned one
		// for the annotations 3.9.0 left as they were
		if (const Formats::ChunkRecord* chunk = document.FindChunk(Chunk)) {
			SmallVector<Annotation, 0> previous;
			ReadVersioned(*chunk, previous);
			SmallVector<bool, 0> used;
			used.resize(previous.size());
			for (bool& value : used) {
				value = false;
			}
			for (Annotation& annotation : annotations) {
				for (std::size_t i = 0; i < previous.size(); i++) {
					const Annotation& match = previous[i];
					if (!used[i] && match.X == annotation.X && match.Y == annotation.Y && match.Slot == annotation.Slot && match.Text == annotation.Text) {
						used[i] = true;
						annotation.Color = match.Color;
						annotation.Author = match.Author;
						annotation.Time = match.Time;
						annotation.Resolved = match.Resolved;
						annotation.Replies = Death::move(previous[i].Replies);
						break;
					}
				}
			}
		}

		Write(document, annotations);
	}
}

#endif
