#pragma once

/** @file
	@brief Class @ref Death::IO::FileStreamPool
*/

#include "FileStream.h"
#include "../Containers/SmallVector.h"
#include "../Containers/String.h"
#include "../Threading/Spinlock.h"

#include <memory>
#include <mutex>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Keeps the open stream of one file between uses, so reading a resource does not reopen it

		A `.pak` archive hands out a @ref BoundedFileStream per resource, and each one used to open the archive
		by path again. On a desktop that is a cheap system call. On a console reading from optical media or
		a memory card it is a directory lookup and a seek - about 30 ms on a PlayStation 2 or Dreamcast disc -
		and the PCSX2 log showed the archive reopened roughly every 50 ms while the intro played.

		The pool lends a @ref FileStream out with @ref Acquire() and takes it back with @ref Release(). A stream
		that comes back keeps its descriptor AND its read buffer, so the open-read-close sequence loading is
		made of pays for the open once, and a resource that sits next to the previous one in the archive is
		served out of the buffer the previous read already filled - @ref FileStream::Seek() keeps the buffer
		when the target lies inside it - with no disc access at all.

		Only @ref MaxIdleStreams (one) is kept when nothing is using it, because the game reads sequentially:
		an idle stream pins a file descriptor, which the console filesystems have few of (KallistiOS has a
		fixed `fd_table`, the PS2 `cdfs` driver a handful of handles), plus its buffer, 8 KB by default, and
		a second one would only save a reopen on the rare occasion that two readers return at the same time.
		Streams alive at once (a music stream and a level load on another thread) each get their own, so a
		reader never waits on another's position. When a stream comes back and the pool is full, the newcomer
		replaces the parked one, because its buffer holds what was read last and the next resource is most
		likely right after it. The requested buffer size never parks a stream: a reused stream adopts the size
		its new user asks for (@ref FileStream::SetBufferSize()). Safe to call from any thread.
	*/
	class FileStreamPool
	{
	public:
		/** @brief How many idle streams are kept open; a stream returned beyond this replaces the oldest */
		static constexpr std::int32_t MaxIdleStreams = 1;

		explicit FileStreamPool(Containers::StringView path)
			: _path(path) {}

		FileStreamPool(const FileStreamPool&) = delete;
		FileStreamPool& operator=(const FileStreamPool&) = delete;

		/** @brief Returns the path of the file the streams open */
		Containers::StringView GetPath() const {
			return _path;
		}

		/**
			@brief Returns an open read-only stream of the file with the given buffer size, reusing an idle one
			if there is any and opening a new one otherwise

			The position of a reused stream is wherever its last user left it, so seek before reading.
		*/
		std::unique_ptr<FileStream> Acquire(std::int32_t bufferSize) {
			std::unique_ptr<FileStream> stream;
			{
				std::lock_guard<Threading::Spinlock> lock(_lock);
				if (!_idle.empty()) {
					stream = Death::move(_idle.back());
					_idle.pop_back();
				}
			}
			if (stream != nullptr) {
				if (stream->GetBufferSize() != bufferSize) {
					stream->SetBufferSize(bufferSize);
				}
				return stream;
			}
			return std::make_unique<FileStream>(_path, FileAccess::Read, bufferSize);
		}

		/** @brief Takes a stream back for the next @ref Acquire(); an invalid one is closed, and a full pool drops its oldest parked stream for it */
		void Release(std::unique_ptr<FileStream>&& stream) {
			if (stream == nullptr || !stream->IsValid()) {
				return;
			}
			std::unique_ptr<FileStream> evicted;
			{
				std::lock_guard<Threading::Spinlock> lock(_lock);
				if (_idle.size() >= std::size_t(MaxIdleStreams)) {
					// The oldest parked stream goes, the rest shift down (the pool is one or two entries)
					evicted = Death::move(_idle.front());
					for (std::size_t i = 1; i < _idle.size(); i++) {
						_idle[i - 1] = Death::move(_idle[i]);
					}
					_idle.pop_back();
				}
				_idle.push_back(Death::move(stream));
			}
			// `evicted` closes its file here, outside the lock
		}

	private:
		Containers::SmallVector<std::unique_ptr<FileStream>, MaxIdleStreams> _idle;
		Containers::String _path;
		Threading::Spinlock _lock;
	};

}}
