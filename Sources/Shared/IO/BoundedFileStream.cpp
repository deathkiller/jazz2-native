#include "BoundedFileStream.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	BoundedFileStream::BoundedFileStream(Containers::StringView path, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize)
		: BoundedFileStream(Containers::String{path}, offset, size, bufferSize)
	{
	}

	BoundedFileStream::BoundedFileStream(Containers::String&& path, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize)
		: _underlyingStream(std::make_unique<FileStream>(Death::move(path), FileAccess::Read, bufferSize)), _offset(offset), _size(size)
	{
		_underlyingStream->Seek(static_cast<std::int64_t>(offset), SeekOrigin::Begin);
	}

	BoundedFileStream::BoundedFileStream(std::shared_ptr<FileStreamPool> pool, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize)
		: _underlyingStream(pool->Acquire(bufferSize)), _pool(Death::move(pool)), _offset(offset), _size(size)
	{
		_underlyingStream->Seek(static_cast<std::int64_t>(offset), SeekOrigin::Begin);
	}

	BoundedFileStream::~BoundedFileStream()
	{
		Dispose();
	}

	void BoundedFileStream::Dispose()
	{
		if (_underlyingStream != nullptr) {
			if (_pool != nullptr) {
				// Back to the pool with the file still open, for the next resource read from the same archive
				_pool->Release(Death::move(_underlyingStream));
				_pool = nullptr;
			}
			// A stream that was not pooled (or that the full pool declined) closes the file here
			_underlyingStream = nullptr;
		}
	}

	std::int64_t BoundedFileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		if (_underlyingStream == nullptr) {
			return Stream::Invalid;
		}

		std::int64_t newPos;
		switch (origin) {
			case SeekOrigin::Begin: newPos = _offset + offset; break;
			case SeekOrigin::Current: newPos = _underlyingStream->GetPosition() + offset; break;
			case SeekOrigin::End: newPos = _offset + _size + offset; break;
			default: return Stream::OutOfRange;
		}

		if (newPos < static_cast<std::int64_t>(_offset) || newPos > static_cast<std::int64_t>(_offset + _size)) {
			newPos = Stream::OutOfRange;
		} else {
			newPos = _underlyingStream->Seek(newPos, SeekOrigin::Begin);
			if (newPos >= static_cast<std::int64_t>(_offset)) {
				newPos -= _offset;
			}
		}
		return newPos;
	}

	std::int64_t BoundedFileStream::GetPosition() const
	{
		if (_underlyingStream == nullptr) {
			return Stream::Invalid;
		}
		return _underlyingStream->GetPosition() - static_cast<std::int64_t>(_offset);
	}

	std::int64_t BoundedFileStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		if (_underlyingStream == nullptr) {
			return Stream::Invalid;
		}

		std::int64_t pos = _underlyingStream->GetPosition() - _offset;
		if (bytesToRead > static_cast<std::int64_t>(_size) - pos) {
			bytesToRead = static_cast<std::int64_t>(_size) - pos;
		}

		return _underlyingStream->Read(destination, bytesToRead);
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
		return (_underlyingStream != nullptr && _underlyingStream->IsValid());
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