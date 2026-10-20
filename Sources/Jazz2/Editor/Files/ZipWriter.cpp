#include "ZipWriter.h"

#if defined(WITH_EDITOR)

#include "../../../nCine/Base/Algorithms.h"

#include <Containers/DateTime.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <cstring>

using namespace Death::Containers;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Editor
{
	namespace
	{
		void Put16(SmallVector<std::uint8_t, 64>& data, std::uint32_t value)
		{
			data.push_back(std::uint8_t(value));
			data.push_back(std::uint8_t(value >> 8));
		}

		void Put32(SmallVector<std::uint8_t, 64>& data, std::uint32_t value)
		{
			Put16(data, value & 0xFFFF);
			Put16(data, value >> 16);
		}
	}

	ZipWriter::ZipWriter(Stream& target)
		: _target(target), _offset(0), _failed(false)
	{
		// All files get the time the archive is made, in the DOS format of local time ZIP uses
		DateTime::Tm local = DateTime::UtcNow().Partitioned(DateTime::Local);
		if (local.IsValid() && local.Year >= 1980) {
			_time = std::uint16_t((local.Hour << 11) | (local.Minute << 5) | (local.Second / 2));
			_date = std::uint16_t(((local.Year - 1980) << 9) | ((local.Month + 1) << 5) | local.Day);
		} else {
			_time = 0;
			_date = (1 << 5) | 1;
		}
	}

	std::uint32_t ZipWriter::Crc32(ArrayView<const std::uint8_t> data)
	{
		// The checksum of zlib, which ZIP uses too
		return nCine::crc32(data);
	}

	bool ZipWriter::WriteBytes(const void* data, std::size_t size)
	{
		if (_failed) {
			return false;
		}
		if (size > 0 && _target.Write(data, std::int64_t(size)) != std::int64_t(size)) {
			_failed = true;
			return false;
		}
		_offset += size;
		return true;
	}

	bool ZipWriter::AddFile(StringView name, ArrayView<const std::uint8_t> data)
	{
		// Without ZIP64, every size and offset has to fit 32 bits
		if (_failed || name.empty() || name.size() > UINT16_MAX || data.size() >= UINT32_MAX || _offset >= UINT32_MAX || _entries.size() >= UINT16_MAX) {
			_failed = true;
			return false;
		}

		Entry& entry = _entries.emplace_back();
		entry.Name = name;
		entry.Crc = Crc32(data);
		entry.Size = std::uint32_t(data.size());
		entry.Offset = std::uint32_t(_offset);

		// Deflated, unless it doesn't get smaller, as a file compressed already doesn't
		MemoryStream compressed(std::int64_t(data.size() / 2 + 64));
		if (!data.empty()) {
			DeflateWriter writer(compressed, 9);
			bool written = (writer.Write(data.data(), std::int64_t(data.size())) == std::int64_t(data.size()));
			// The state of the compressed stream is known only once it's completed, a broken entry would make a broken archive
			writer.Dispose();
			if (!written || !writer.IsValid()) {
				LOGE("File \"{}\" cannot be compressed into the archive", name);
				_entries.pop_back();
				_failed = true;
				return false;
			}
		}
		bool deflated = (!data.empty() && compressed.GetSize() < std::int64_t(data.size()));
		entry.Method = (deflated ? 8 : 0);
		entry.CompressedSize = (deflated ? std::uint32_t(compressed.GetSize()) : entry.Size);

		SmallVector<std::uint8_t, 64> header;
		Put32(header, 0x04034B50);	// Local file header
		Put16(header, 20);			// Version needed, 2.0 for Deflate
		Put16(header, 0x0800);		// The name is UTF-8
		Put16(header, entry.Method);
		Put16(header, _time);
		Put16(header, _date);
		Put32(header, entry.Crc);
		Put32(header, entry.CompressedSize);
		Put32(header, entry.Size);
		Put16(header, std::uint32_t(name.size()));
		Put16(header, 0);			// Extra field
		WriteBytes(header.data(), header.size());
		WriteBytes(name.data(), name.size());
		if (deflated) {
			WriteBytes(compressed.GetBuffer(), std::size_t(compressed.GetSize()));
		} else {
			WriteBytes(data.data(), data.size());
		}
		return !_failed;
	}

	bool ZipWriter::Finish()
	{
		if (_failed || _offset >= UINT32_MAX) {
			return false;
		}

		std::uint64_t directoryOffset = _offset;
		for (const Entry& entry : _entries) {
			SmallVector<std::uint8_t, 64> header;
			Put32(header, 0x02014B50);	// Central directory file header
			Put16(header, 20);			// Made by, 2.0
			Put16(header, 20);			// Version needed
			Put16(header, 0x0800);		// The name is UTF-8
			Put16(header, entry.Method);
			Put16(header, _time);
			Put16(header, _date);
			Put32(header, entry.Crc);
			Put32(header, entry.CompressedSize);
			Put32(header, entry.Size);
			Put16(header, std::uint32_t(entry.Name.size()));
			Put16(header, 0);			// Extra field
			Put16(header, 0);			// Comment
			Put16(header, 0);			// Disk number
			Put16(header, 0);			// Internal attributes
			Put32(header, 0);			// External attributes
			Put32(header, entry.Offset);
			WriteBytes(header.data(), header.size());
			WriteBytes(entry.Name.data(), entry.Name.size());
		}
		std::uint64_t directorySize = _offset - directoryOffset;
		if (directoryOffset >= UINT32_MAX || directorySize >= UINT32_MAX) {
			return false;
		}

		SmallVector<std::uint8_t, 64> end;
		Put32(end, 0x06054B50);		// End of central directory
		Put16(end, 0);				// Disk number
		Put16(end, 0);				// Disk with the directory
		Put16(end, std::uint32_t(_entries.size()));
		Put16(end, std::uint32_t(_entries.size()));
		Put32(end, std::uint32_t(directorySize));
		Put32(end, std::uint32_t(directoryOffset));
		Put16(end, 0);				// Comment
		WriteBytes(end.data(), end.size());
		return !_failed;
	}
}

#endif
