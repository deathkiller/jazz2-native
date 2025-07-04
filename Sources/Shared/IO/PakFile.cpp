#include "PakFile.h"
#include "BoundedFileStream.h"
#include "FileSystem.h"
#include "Compression/DeflateStream.h"
#include "Compression/Lz4Stream.h"
#include "Compression/ZstdStream.h"
#include "../Containers/GrowableArray.h"
#include "../Containers/StringConcatenable.h"

#include <algorithm>

using namespace Death::Containers;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(WITH_ZLIB) || defined(WITH_MINIZ) || defined(WITH_LZ4) || defined(WITH_ZSTD)

	using namespace Death::IO::Compression;

	template<class T>
	class CompressedBoundedStream : public Stream
	{
	public:
		CompressedBoundedStream(StringView path, std::uint64_t offset, std::uint32_t uncompressedSize, std::uint32_t compressedSize);

		CompressedBoundedStream(const CompressedBoundedStream&) = delete;
		CompressedBoundedStream& operator=(const CompressedBoundedStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

	private:
		BoundedFileStream _underlyingStream;
		T _compressedStream;
		std::int64_t _uncompressedSize;
	};

	template<class T>
	CompressedBoundedStream<T>::CompressedBoundedStream(StringView path, std::uint64_t offset, std::uint32_t uncompressedSize, std::uint32_t compressedSize)
		: _underlyingStream(path, offset, compressedSize), _uncompressedSize(uncompressedSize)
	{
		_compressedStream.Open(_underlyingStream, static_cast<std::int32_t>(compressedSize));
	}

	template<class T>
	void CompressedBoundedStream<T>::Dispose()
	{
		_compressedStream.Dispose();
		_underlyingStream.Dispose();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return _compressedStream.Seek(offset, origin);
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::GetPosition() const
	{
		return _compressedStream.GetPosition();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Read(void* destination, std::int64_t bytesToRead)
	{
		return _compressedStream.Read(destination, bytesToRead);
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	template<class T>
	bool CompressedBoundedStream<T>::Flush()
	{
		// Not supported
		return true;
	}

	template<class T>
	bool CompressedBoundedStream<T>::IsValid()
	{
		return _underlyingStream.IsValid() && _compressedStream.IsValid();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::GetSize() const
	{
		return _uncompressedSize;
	}

#endif

	PakFile::PakFile(StringView path)
	{
		std::unique_ptr<Stream> s = std::make_unique<FileStream>(path, FileAccess::Read);
		DEATH_ASSERT(s->GetSize() > 24, "Invalid .pak file", );

		// Header size is 18 bytes
		bool isSeekable = s->Seek(-18, SeekOrigin::End) >= 0;
		DEATH_ASSERT(isSeekable, ".pak file must be opened from seekable stream", );

		std::uint64_t signature = s->ReadValue<std::uint64_t>();
		DEATH_ASSERT(signature == Signature, "Invalid .pak file", );

		std::uint16_t fileVersion = s->ReadValue<std::uint16_t>();
		std::uint64_t rootIndexOffset = s->ReadValue<std::uint64_t>();

		DEATH_ASSERT(rootIndexOffset < INT64_MAX, "Malformed .pak file", );
		s->Seek(static_cast<std::int64_t>(rootIndexOffset), SeekOrigin::Begin);

		std::uint32_t mountPointLength = s->ReadVariableUint32();
		DEATH_ASSERT(mountPointLength < INT32_MAX, "Malformed .pak file", );
		String relativeMountPoint(NoInit, mountPointLength);
		s->Read(relativeMountPoint.data(), static_cast<std::int32_t>(mountPointLength));

		//auto parentPath = FileSystem::GetDirectoryName(path);
		//_mountPoint = FileSystem::CombinePath(parentPath, relativeMountPoint)
		//	+ (!relativeMountPoint.empty() && relativeMountPoint[relativeMountPoint.size() - 1] != '/' && relativeMountPoint[relativeMountPoint.size() - 1] != '\\'
		//		? FileSystem::PathSeparator : "");
		_mountPoint = relativeMountPoint;
		if (!relativeMountPoint.empty() && relativeMountPoint[relativeMountPoint.size() - 1] != '/' && relativeMountPoint[relativeMountPoint.size() - 1] != '\\') {
			_mountPoint += FileSystem::PathSeparator;
		}

		ReadIndex(s, nullptr);
		_path = path;
	}

	StringView PakFile::GetMountPoint() const
	{
		return _mountPoint;
	}

	StringView PakFile::GetPath() const
	{
		return _path;
	}

	bool PakFile::IsValid() const
	{
		return !_path.empty();
	}

	void PakFile::ReadIndex(std::unique_ptr<Stream>& s, Item* parentItem)
	{
		std::uint32_t itemCount = s->ReadVariableUint32();

		Array<Item>* items;
		if (parentItem != nullptr) {
			parentItem->ChildItems = Array<Item>(itemCount);
			items = &parentItem->ChildItems;
		} else {
			_rootItems = Array<Item>(itemCount);
			items = &_rootItems;
		}

		for (std::uint32_t i = 0; i < itemCount; i++) {
			Item& item = (*items)[i];

			item.Flags = (ItemFlags)s->ReadVariableUint32();

			std::uint32_t nameLength = s->ReadVariableUint32();
			DEATH_ASSERT(nameLength == 0 || nameLength < INT32_MAX, "Malformed .pak file", );
			item.Name = String(NoInit, nameLength);
			s->Read(item.Name.data(), static_cast<std::int32_t>(nameLength));

			item.Offset = s->ReadVariableUint64();

			if ((item.Flags & ItemFlags::Directory) != ItemFlags::Directory) {
				item.UncompressedSize = s->ReadVariableUint32();

				if (HasCompressedSize(item.Flags)) {
					item.Size = s->ReadVariableUint32();
				}
			}
		}

		for (std::uint32_t i = 0; i < itemCount; i++) {
			Item& item = (*items)[i];
			if ((item.Flags & ItemFlags::Directory) == ItemFlags::Directory) {
				s->Seek(static_cast<std::int64_t>(item.Offset), SeekOrigin::Begin);
				ReadIndex(s, &item);
			}
		}
	}

	bool PakFile::FileExists(StringView path)
	{
		Item* foundItem = FindItem(path);
		return (foundItem != nullptr && (foundItem->Flags & ItemFlags::Directory) != ItemFlags::Directory);
	}

	bool PakFile::DirectoryExists(StringView path)
	{
		Item* foundItem = FindItem(path);
		return (foundItem != nullptr && (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory);
	}

	std::unique_ptr<Stream> PakFile::OpenFile(StringView path)
	{
		if (path.empty() || path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
			return nullptr;
		}

		Item* foundItem = FindItem(path);
		if (foundItem == nullptr || (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory) {
			return nullptr;
		}

		if ((foundItem->Flags & ItemFlags::ZlibCompressed) == ItemFlags::ZlibCompressed) {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			return std::make_unique<CompressedBoundedStream<DeflateStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("File \"{}\" was compressed using an unsupported compression method (Deflate)", path);
#	endif
			return nullptr;
#endif
		}

		if ((foundItem->Flags & ItemFlags::Lz4Compressed) == ItemFlags::Lz4Compressed) {
#if defined(WITH_LZ4)
			return std::make_unique<CompressedBoundedStream<Lz4Stream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("File \"{}\" was compressed using an unsupported compression method (LZ4)", path);
#	endif
			return nullptr;
#endif
		}

		if ((foundItem->Flags & ItemFlags::ZstdCompressed) == ItemFlags::ZstdCompressed) {
#if defined(WITH_ZSTD)
			return std::make_unique<CompressedBoundedStream<ZstdStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("File \"{}\" was compressed using an unsupported compression method (Zstd)", path);
#	endif
			return nullptr;
#endif
		}

		return std::make_unique<BoundedFileStream>(_path, foundItem->Offset, foundItem->UncompressedSize);
	}

	PakFile::Item* PakFile::FindItem(StringView path)
	{
		path = path.trimmedPrefix("/\\");

		Array<Item>* items = &_rootItems;
		while (true) {
			auto separator = path.findAnyOr("/\\", path.end());
			auto name = path.prefix(separator.begin());

			if (name.empty()) {
				// Skip all consecutive slashes
				path = path.suffix(separator.end());
				continue;
			}

			// Items are always sorted by PakWriter
			Item* foundItem = std::lower_bound(items->begin(), items->end(), name, [](const PakFile::Item& a, StringView b) {
				return a.Name < b;
			});

			if (foundItem == items->end() || foundItem->Name != name) {
				return nullptr;
			}

			if (separator == path.end()) {
				// If there is no separator left
				return foundItem;
			}

			path = path.suffix(separator.end());
			items = &foundItem->ChildItems;
		}
	}

	bool PakFile::HasCompressedSize(ItemFlags itemFlags)
	{
		return ((itemFlags & (PakFile::ItemFlags::ZlibCompressed | PakFile::ItemFlags::Lz4Compressed |
			PakFile::ItemFlags::Lzma2Compressed | PakFile::ItemFlags::ZstdCompressed)) != PakFile::ItemFlags::None);
	}

	class PakFile::Directory::Impl
	{
		friend class Directory;

	public:
		Impl(PakFile& pakFile, const StringView path, FileSystem::EnumerationOptions options)
			: _fileNamePart(nullptr)
		{
			Open(pakFile, path, options);
		}

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;

		bool Open(PakFile& pakFile, const StringView path, FileSystem::EnumerationOptions options)
		{
			_options = options;
			if (path.empty()) {
				_childItems = pakFile._rootItems;
			} else {
				Item* parentItem = pakFile.FindItem(path);
				if (parentItem == nullptr) {
					_path[0] = '\0';
					return false;
				}

				_childItems = parentItem->ChildItems;
			}

			_index = 0;

			if (!_childItems.empty()) {
				std::size_t pathLength = path.size();
				if (pathLength > 0) {
					std::memcpy(_path, path.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
#if defined(DEATH_TARGET_WINDOWS)
						_path[pathLength - 1] = '\\';
#else
						_path[pathLength - 1] = '/';
#endif
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
#if defined(DEATH_TARGET_WINDOWS)
						_path[pathLength] = '\\';
#else
						_path[pathLength] = '/';
#endif
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
				} else {
					_path[0] = '\0';
					_fileNamePart = _path;
				}

				Increment();
				return true;
			} else {
				_path[0] = '\0';
				return false;
			}
		}

		void Increment()
		{
			while (true) {
				if (_index >= _childItems.size()) {
					_path[0] = '\0';
					return;
				}

				Item& item = _childItems[_index];
				if (((_options & FileSystem::EnumerationOptions::SkipDirectories) == FileSystem::EnumerationOptions::SkipDirectories && (item.Flags & ItemFlags::Directory) == ItemFlags::Directory) ||
					((_options & FileSystem::EnumerationOptions::SkipFiles) == FileSystem::EnumerationOptions::SkipFiles && (item.Flags & ItemFlags::Directory) != ItemFlags::Directory)) {
					// Skip this file
					_index++;
					continue;
				}
			
				break;
			}

			auto& fileName = _childItems[_index].Name;
#if defined(DEATH_TARGET_WINDOWS)
			strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), fileName.data(), fileName.size());
#else
			strncpy(_fileNamePart, fileName.data(), std::min(sizeof(_path) - (_fileNamePart - _path), fileName.size()) - 1);
			_path[sizeof(_path) - 1] = '\0';
#endif
			_index++;
		}

	private:

		FileSystem::EnumerationOptions _options;
		char _path[FileSystem::MaxPathLength];
		char* _fileNamePart;
		ArrayView<Item> _childItems;
		std::size_t _index;
	};

	PakFile::Directory::Directory() noexcept
	{
	}

	PakFile::Directory::Directory(PakFile& pakFile, StringView path, FileSystem::EnumerationOptions options)
		: _impl(std::make_shared<Impl>(pakFile, path, options))
	{
	}

	PakFile::Directory::~Directory()
	{
	}

	PakFile::Directory::Directory(const Directory& other)
		: _impl(other._impl)
	{
	}

	PakFile::Directory::Directory(Directory&& other) noexcept
		: _impl(Death::move(other._impl))
	{
	}

	PakFile::Directory& PakFile::Directory::operator=(const Directory& other)
	{
		_impl = other._impl;
		return *this;
	}

	PakFile::Directory& PakFile::Directory::operator=(Directory&& other) noexcept
	{
		_impl = Death::move(other._impl);
		return *this;
	}

	StringView PakFile::Directory::operator*() const& noexcept
	{
		return _impl->_path;
	}

	PakFile::Directory& PakFile::Directory::operator++()
	{
		_impl->Increment();
		return *this;
	}

	bool PakFile::Directory::operator==(const Directory& other) const
	{
		bool isEnd1 = (_impl == nullptr || _impl->_path[0] == '\0');
		bool isEnd2 = (other._impl == nullptr || other._impl->_path[0] == '\0');
		if (isEnd1 || isEnd2) {
			return (isEnd1 && isEnd2);
		}
		return (_impl == other._impl);
	}

	bool PakFile::Directory::operator!=(const Directory& other) const
	{
		return !(*this == other);
	}

	PakFile::Directory::Proxy::Proxy(StringView path)
		: _path(path)
	{
	}

	StringView PakFile::Directory::Proxy::operator*() const& noexcept
	{
		return _path;
	}

	PakWriter::PakWriter(StringView path)
		: _finalized(false)
	{
		_outputStream = std::make_unique<FileStream>(path, FileAccess::Write);
	}

	PakWriter::~PakWriter()
	{
		Finalize();
	}

	bool PakWriter::IsValid() const
	{
		return _outputStream->IsValid();
	}

	bool PakWriter::AddFile(Stream& stream, StringView path, PakPreferredCompression preferredCompression)
	{
		DEATH_ASSERT(_outputStream->IsValid(), "Invalid output stream specified", false);
		DEATH_ASSERT(!path.empty() && path[path.size() - 1] != '/' && path[path.size() - 1] != '\\', ("\"{}\" is not valid file path", String::nullTerminatedView(path).data()), false);

		PakFile::Item* parentItem = FindOrCreateParentItem(path);
		Array<PakFile::Item>* items;
		if (parentItem != nullptr) {
			items = &parentItem->ChildItems;
		} else {
			items = &_rootItems;
		}

		for (PakFile::Item& item : *items) {
			if (item.Name == path) {
				// File already exists in the .pak file
				return false;
			}
		}

		PakFile::ItemFlags flags = PakFile::ItemFlags::None;
		std::int64_t offset = _outputStream->GetPosition();
		std::int64_t uncompressedSize, size;

#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		if (preferredCompression == PakPreferredCompression::Deflate) {
			DeflateWriter dw(*_outputStream);
			uncompressedSize = stream.CopyTo(dw);
			dw.Dispose();
			size = _outputStream->GetPosition() - offset;
			DEATH_DEBUG_ASSERT(size > 0);
			flags |= PakFile::ItemFlags::ZlibCompressed;
		} else
#endif
#if defined(WITH_LZ4)
		if (preferredCompression == PakPreferredCompression::Lz4) {
			Lz4Writer dw(*_outputStream);
			uncompressedSize = stream.CopyTo(dw);
			dw.Dispose();
			size = _outputStream->GetPosition() - offset;
			DEATH_DEBUG_ASSERT(size > 0);
			flags |= PakFile::ItemFlags::Lz4Compressed;
		} else
#endif
#if defined(WITH_ZSTD)
		if (preferredCompression == PakPreferredCompression::Zstd) {
			ZstdWriter dw(*_outputStream);
			uncompressedSize = stream.CopyTo(dw);
			dw.Dispose();
			size = _outputStream->GetPosition() - offset;
			DEATH_DEBUG_ASSERT(size > 0);
			flags |= PakFile::ItemFlags::ZstdCompressed;
		} else
#endif
		{
			uncompressedSize = stream.CopyTo(*_outputStream);
			size = 0;
		}

		DEATH_ASSERT(uncompressedSize > 0, "Failed to copy stream to .pak file", false);
		// NOTE: Files inside .pak are limited to 4 GB only for now
		DEATH_ASSERT(uncompressedSize < UINT32_MAX && size < UINT32_MAX, "File size in .pak file exceeded the allowed range", false);

		PakFile::Item* newItem = &arrayAppend(*items, PakFile::Item());
		newItem->Name = path;
		newItem->Flags = flags;
		newItem->Offset = offset;
		newItem->UncompressedSize = static_cast<std::uint32_t>(uncompressedSize);
		newItem->Size = static_cast<std::uint32_t>(size);

		return true;
	}

	void PakWriter::Finalize()
	{
		if (_finalized) {
			return;
		}

		_finalized = true;

		Array<PakFile::Item*> queuedDirectories;

		for (PakFile::Item& item : _rootItems) {
			if ((item.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory) {
				arrayAppend(queuedDirectories, &item);
			}
		}

		if (queuedDirectories.empty()) {
			// No files added - close the stream and try to delete the file
			auto path = _outputStream->GetPath();
			_outputStream = nullptr;
			FileSystem::RemoveFile(path);
			return;
		}

		for (std::int32_t i = 0; i < queuedDirectories.size(); i++) {
			PakFile::Item& item = *queuedDirectories[i];
			for (PakFile::Item& child : item.ChildItems) {
				if ((child.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory) {
					arrayAppend(queuedDirectories, &child);
				}
			}
		}

		std::size_t i = queuedDirectories.size() - 1;
		while (true) {
			PakFile::Item& item = *queuedDirectories[i];
			item.Offset = _outputStream->GetPosition();

			// Names need to be sorted, because binary search is used to find files
			std::sort(item.ChildItems.begin(), item.ChildItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
				return a.Name < b.Name;
			});

			_outputStream->WriteVariableUint32(static_cast<std::uint32_t>(item.ChildItems.size()));
			for (PakFile::Item& childItem : item.ChildItems) {
				WriteItemDescription(childItem);
			}

			if DEATH_UNLIKELY(i == 0) {
				break;
			}
			i--;
		}

		std::int64_t rootIndexOffset = _outputStream->GetPosition();

		_outputStream->WriteVariableUint32(static_cast<std::uint32_t>(MountPoint.size()));
		_outputStream->Write(MountPoint.data(), static_cast<std::int32_t>(MountPoint.size()));

		// Names need to be sorted, because binary search is used to find files
		std::sort(_rootItems.begin(), _rootItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
			return a.Name < b.Name;
		});

		_outputStream->WriteVariableUint32(static_cast<std::uint32_t>(_rootItems.size()));
		for (PakFile::Item& item : _rootItems) {
			WriteItemDescription(item);
		}

		_outputStream->WriteValue<std::uint64_t>(PakFile::Signature);
		_outputStream->WriteValue<std::uint16_t>(PakFile::Version);
		_outputStream->WriteValue<std::uint64_t>(rootIndexOffset);

		// Close the stream
		_outputStream = nullptr;
	}

	PakFile::Item* PakWriter::FindOrCreateParentItem(StringView& path)
	{
		path = path.trimmedPrefix("/\\");

		Array<PakFile::Item>* items = &_rootItems;
		PakFile::Item* parentItem = nullptr;
		while (true) {
			auto separator = path.findAny("/\\");
			if (separator == nullptr) {
				return parentItem;
			}

			auto name = path.prefix(separator.begin());

			PakFile::Item* foundItem = nullptr;
			for (PakFile::Item& item : *items) {
				if (item.Name == name) {
					foundItem = &item;
					break;
				}
			}

			if (foundItem == nullptr) {
				foundItem = &arrayAppend(*items, PakFile::Item());
				foundItem->Name = name;
				foundItem->Flags = PakFile::ItemFlags::Directory;
			}

			path = path.suffix(separator.end());
			parentItem = foundItem;
			items = &foundItem->ChildItems;
		}
	}

	void PakWriter::WriteItemDescription(PakFile::Item& item)
	{
		_outputStream->WriteVariableUint32((std::uint32_t)item.Flags);

		_outputStream->WriteVariableUint32(static_cast<std::uint32_t>(item.Name.size()));
		_outputStream->Write(item.Name.data(), static_cast<std::int32_t>(item.Name.size()));

		_outputStream->WriteVariableUint64(item.Offset);

		if ((item.Flags & PakFile::ItemFlags::Directory) != PakFile::ItemFlags::Directory) {
			_outputStream->WriteVariableUint32(item.UncompressedSize);

			if (PakFile::HasCompressedSize(item.Flags)) {
				_outputStream->WriteVariableUint32(item.Size);
			}
		}
	}

}}