#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
	using Death::IO::Stream;
#endif

	/**
		@brief Writes a ZIP archive

		Files are added one after another, each compressed by Deflate unless it doesn't get smaller then, and
		the archive is completed by @ref Finish(). The names are UTF-8 with `/` between directories. Archives larger
		than 4 GB, which would need ZIP64, aren't supported.
	*/
	class ZipWriter
	{
	public:
		/** @brief Starts an archive in a stream */
		explicit ZipWriter(Stream& target);

		ZipWriter(const ZipWriter&) = delete;
		ZipWriter& operator=(const ZipWriter&) = delete;

		/** @brief Adds a file, returns `false` if it couldn't be written */
		bool AddFile(StringView name, ArrayView<const std::uint8_t> data);
		/** @brief Writes the directory of the archive, returns `false` if anything couldn't be written */
		bool Finish();

		/** @brief Returns the number of files added */
		std::size_t GetCount() const {
			return _entries.size();
		}

		/** @brief Returns the CRC-32 of data, as ZIP and PNG use it */
		static std::uint32_t Crc32(ArrayView<const std::uint8_t> data);

	private:
		struct Entry
		{
			String Name;
			std::uint32_t Crc;
			std::uint32_t CompressedSize;
			std::uint32_t Size;
			std::uint32_t Offset;
			std::uint16_t Method;
		};

		Stream& _target;
		SmallVector<Entry, 0> _entries;
		std::uint64_t _offset;
		std::uint16_t _time;
		std::uint16_t _date;
		bool _failed;

		bool WriteBytes(const void* data, std::size_t size);
	};
}

#endif
