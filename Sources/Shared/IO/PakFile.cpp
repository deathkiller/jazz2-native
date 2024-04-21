#include "PakFile.h"
#include "DeflateStream.h"
#include "FileSystem.h"
#include "../Containers/GrowableArray.h"
#include "../Containers/StringConcatenable.h"

#include <algorithm>

using namespace Death::Containers;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	class BoundedStream : public Stream
	{
	public:
		BoundedStream(const Containers::String& path, std::uint64_t offset, std::uint32_t size);

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;

		bool IsValid() const override;

	private:
		BoundedStream(const BoundedStream&) = delete;
		BoundedStream& operator=(const BoundedStream&) = delete;

		FileStream _underlyingStream;
		std::uint64_t _offset;
		std::uint32_t _pos;
	};

	BoundedStream::BoundedStream(const Containers::String& path, std::uint64_t offset, std::uint32_t size)
		: _underlyingStream(path, FileAccessMode::Read), _offset(offset), _pos(0)
	{
		_size = size;
		_underlyingStream.Seek(offset, SeekOrigin::Begin);
	}

	void BoundedStream::Close()
	{
		_underlyingStream.Close();
	}

	std::int32_t BoundedStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		std::int32_t seekValue;
		switch (origin) {
			case SeekOrigin::Begin: seekValue = _offset + offset; break;
			case SeekOrigin::Current: seekValue = _pos + offset; break;
			case SeekOrigin::End: seekValue = _offset + _size + offset; break;
			default: seekValue = -1; break;
		}

		if (seekValue < _offset || seekValue > _offset + _size) {
			seekValue = -1;
		} else {
			_underlyingStream.Seek(seekValue, SeekOrigin::Begin);
			seekValue -= _offset;
			_pos = seekValue;
		}
		return seekValue;
	}

	std::int32_t BoundedStream::GetPosition() const
	{
		return _pos;
	}

	std::int32_t BoundedStream::Read(void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		if (bytes > _size - _pos) {
			bytes = _size - _pos;
		}

		std::int32_t bytesRead = _underlyingStream.Read(buffer, bytes);
		_pos += bytesRead;
		return bytesRead;
	}

	std::int32_t BoundedStream::Write(const void* buffer, std::int32_t bytes)
	{
		return 0;
	}

	bool BoundedStream::IsValid() const
	{
		return _underlyingStream.IsValid();
	}

#if defined(WITH_ZLIB)

	class ZlibCompressedBoundedStream : public Stream
	{
	public:
		ZlibCompressedBoundedStream(const Containers::String& path, std::uint64_t offset, std::uint32_t size);

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;

		bool IsValid() const override;

	private:
		ZlibCompressedBoundedStream(const ZlibCompressedBoundedStream&) = delete;
		ZlibCompressedBoundedStream& operator=(const ZlibCompressedBoundedStream&) = delete;

		BoundedStream _underlyingStream;
		DeflateStream _deflateStream;
	};

	ZlibCompressedBoundedStream::ZlibCompressedBoundedStream(const Containers::String& path, std::uint64_t offset, std::uint32_t size)
		: _underlyingStream(path, offset, size)
	{
		_underlyingStream.Seek(offset, SeekOrigin::Begin);
		_deflateStream = DeflateStream(_underlyingStream, size);
	}

	void ZlibCompressedBoundedStream::Close()
	{
		_deflateStream.Close();
		_underlyingStream.Close();
	}

	std::int32_t ZlibCompressedBoundedStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		return _deflateStream.Seek(offset, origin);
	}

	std::int32_t ZlibCompressedBoundedStream::GetPosition() const
	{
		return _deflateStream.GetPosition();
	}

	std::int32_t ZlibCompressedBoundedStream::Read(void* buffer, std::int32_t bytes)
	{
		return _deflateStream.Read(buffer, bytes);
	}

	std::int32_t ZlibCompressedBoundedStream::Write(const void* buffer, std::int32_t bytes)
	{
		return 0;
	}

	bool ZlibCompressedBoundedStream::IsValid() const
	{
		return _underlyingStream.IsValid() /*&& _deflateStream.IsValid()*/;
	}

#endif

	PakFile::PakFile(const Containers::StringView path)
	{
		std::unique_ptr<Stream> s = std::make_unique<FileStream>(path, FileAccessMode::Read);
		DEATH_ASSERT(s->GetSize() > 24, , "Invalid .pak file");

		// Header size is 18 bytes
		bool isSeekable = s->Seek(-18, SeekOrigin::End) >= 0;
		DEATH_ASSERT(isSeekable, , ".pak file must be opened from seekable stream");

		std::uint64_t signature = s->ReadValue<std::uint64_t>();
		DEATH_ASSERT(signature == Signature, , "Invalid .pak file");

		std::uint16_t fileVersion = s->ReadValue<std::uint16_t>();
		std::uint64_t rootIndexOffset = s->ReadValue<std::uint64_t>();

		s->Seek(rootIndexOffset, SeekOrigin::Begin);

		std::int32_t mountPointLength = s->ReadVariableInt32();
		String relativeMountPoint(NoInit, mountPointLength);
		s->Read(relativeMountPoint.data(), mountPointLength);

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

	Containers::StringView PakFile::GetMountPoint() const
	{
		return _mountPoint;
	}

	Containers::StringView PakFile::GetPath() const
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

			std::int32_t nameLength = s->ReadVariableInt32();
			item.Name = String(NoInit, nameLength);
			s->Read(item.Name.data(), nameLength);

			if ((item.Flags & ItemFlags::Directory) != ItemFlags::Directory) {
				item.Size = s->ReadVariableUint32();
			}

			item.Offset = s->ReadVariableUint64();
		}

		for (std::uint32_t i = 0; i < itemCount; i++) {
			Item& item = (*items)[i];
			if ((item.Flags & ItemFlags::Directory) == ItemFlags::Directory) {
				s->Seek(item.Offset, SeekOrigin::Begin);
				ReadIndex(s, &item);
			}
		}
	}

	std::unique_ptr<Stream> PakFile::OpenFile(const Containers::StringView path)
	{
		if (path.empty() || path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
			return nullptr;
		}

		Item* foundItem = FindItem(path);
		if (foundItem == nullptr || (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory) {
			return nullptr;
		}

#if defined(WITH_ZLIB)
		if ((foundItem->Flags & ItemFlags::ZlibCompressed) == ItemFlags::ZlibCompressed) {
			return std::make_unique<ZlibCompressedBoundedStream>(_path, foundItem->Offset, foundItem->Size);
		}
#endif

		return std::make_unique<BoundedStream>(_path, foundItem->Offset, foundItem->Size);
	}

	PakFile::Item* PakFile::FindItem(Containers::StringView path)
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
			Item* foundItem = std::lower_bound(items->begin(), items->end(), name, [](const PakFile::Item& a, const StringView& b) {
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

	PakWriter::PakWriter(const Containers::StringView path)
		: _finalized(false)
	{
		_outputStream = std::make_unique<FileStream>(path, FileAccessMode::Write);
	}

	PakWriter::~PakWriter()
	{
		Finalize();
	}

	bool PakWriter::IsValid() const
	{
		return _outputStream->IsValid();
	}

	bool PakWriter::AddFile(Stream& stream, Containers::StringView path, bool compress)
	{
		DEATH_ASSERT(_outputStream->IsValid(), false, "Invalid output stream specified");
		DEATH_ASSERT(!path.empty() && path[path.size() - 1] != '/' && path[path.size() - 1] != '\\', false, "Invalid file path to add");

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
		std::int32_t offset = _outputStream->GetPosition();
		std::int32_t size;

#if defined(WITH_ZLIB)
		if (compress) {
			DeflateWriter dw(*_outputStream);
			size = stream.CopyTo(dw);
			flags |= PakFile::ItemFlags::ZlibCompressed;
		} else
#endif
		{
			size = stream.CopyTo(*_outputStream);
		}

		DEATH_ASSERT(size > 0, false, "Failed to copy stream to .pak file");

		PakFile::Item* newItem = &arrayAppend(*items, PakFile::Item());
		newItem->Name = path;
		newItem->Flags = flags;
		newItem->Offset = offset;
		newItem->Size = size;

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
			for (PakFile::Item& item : item.ChildItems) {
				if ((item.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory) {
					arrayAppend(queuedDirectories, &item);
				}
			}
		}

		for (std::int32_t i = queuedDirectories.size() - 1; i >= 0; i--) {
			PakFile::Item& item = *queuedDirectories[i];
			item.Offset = _outputStream->GetPosition();

			// Names need to be sorted, because binary search is used to find files
			std::sort(item.ChildItems.begin(), item.ChildItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
				return a.Name < b.Name;
			});

			_outputStream->WriteVariableUint32(item.ChildItems.size());
			for (PakFile::Item& childItem : item.ChildItems) {
				WriteItemDescription(childItem);
			}
		}

		std::int32_t rootIndexOffset = _outputStream->GetPosition();

		_outputStream->WriteVariableInt32(MountPoint.size());
		_outputStream->Write(MountPoint.data(), MountPoint.size());

		// Names need to be sorted, because binary search is used to find files
		std::sort(_rootItems.begin(), _rootItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
			return a.Name < b.Name;
		});

		_outputStream->WriteVariableUint32(_rootItems.size());
		for (PakFile::Item& item : _rootItems) {
			WriteItemDescription(item);
		}

		_outputStream->WriteValue<std::uint64_t>(PakFile::Signature);
		_outputStream->WriteValue<std::uint16_t>(1);
		_outputStream->WriteValue<std::uint64_t>(rootIndexOffset);

		// Close the stream
		_outputStream = nullptr;
	}

	PakFile::Item* PakWriter::FindOrCreateParentItem(Containers::StringView& path)
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

		_outputStream->WriteVariableInt32(item.Name.size());
		_outputStream->Write(item.Name.data(), item.Name.size());

		if ((item.Flags & PakFile::ItemFlags::Directory) != PakFile::ItemFlags::Directory) {
			_outputStream->WriteVariableUint32(item.Size);
		}

		_outputStream->WriteVariableUint64(item.Offset);
	}

}}