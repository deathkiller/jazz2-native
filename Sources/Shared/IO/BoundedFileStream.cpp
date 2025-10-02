#include "BoundedFileStream.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	BoundedFileStream::BoundedFileStream(Containers::StringView path, std::uint64_t offset, std::uint32_t size)
		: BoundedFileStream(Containers::String{path}, offset, size)
	{
	}

	BoundedFileStream::BoundedFileStream(Containers::String&& path, std::uint64_t offset, std::uint32_t size)
		: _underlyingStream(path, FileAccess::Read), _offset(offset), _size(size)
	{
		_underlyingStream.Seek(static_cast<std::int64_t>(offset), SeekOrigin::Begin);
	}

	void BoundedFileStream::Dispose()
	{
		_underlyingStream.Dispose();
	}

	std::int64_t BoundedFileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		std::int64_t newPos;
		switch (origin) {
			case SeekOrigin::Begin: newPos = _offset + offset; break;
			case SeekOrigin::Current: newPos = _underlyingStream.GetPosition() + offset; break;
			case SeekOrigin::End: newPos = _offset + _size + offset; break;
			default: return Stream::OutOfRange;
		}

		if (newPos < static_cast<std::int64_t>(_offset) || newPos > static_cast<std::int64_t>(_offset + _size)) {
			newPos = Stream::OutOfRange;
		} else {
			newPos = _underlyingStream.Seek(newPos, SeekOrigin::Begin);
			if (newPos >= static_cast<std::int64_t>(_offset)) {
				newPos -= _offset;
			}
		}
		return newPos;
	}

	std::int64_t BoundedFileStream::GetPosition() const
	{
		return _underlyingStream.GetPosition() - static_cast<std::int64_t>(_offset);
	}

	std::int64_t BoundedFileStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);

		std::int64_t pos = _underlyingStream.GetPosition() - _offset;
		if (bytesToRead > static_cast<std::int64_t>(_size) - pos) {
			bytesToRead = static_cast<std::int64_t>(_size) - pos;
		}

		return _underlyingStream.Read(destination, bytesToRead);
	}

	std::int64_t BoundedFileStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool BoundedFileStream::Flush()
	{
		// Not supported
		return true;
	}

	bool BoundedFileStream::IsValid()
	{
		return _underlyingStream.IsValid();
	}

	std::int64_t BoundedFileStream::GetSize() const
	{
		return _size;
	}

	std::int64_t BoundedFileStream::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

}}