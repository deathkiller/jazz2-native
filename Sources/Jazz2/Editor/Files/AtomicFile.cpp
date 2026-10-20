#include "AtomicFile.h"

#if defined(WITH_EDITOR)

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#if defined(__linux__) || defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#	include <climits>
#	include <cstdlib>
#	include <fcntl.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Passes everything to the file and remembers whether any write came up short
		class CheckedStream : public Stream
		{
		public:
			explicit CheckedStream(Stream& target) : _target(target), _failed(false) {}

			void Dispose() override {}
			std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override { return _target.Seek(offset, origin); }
			std::int64_t GetPosition() const override { return _target.GetPosition(); }
			std::int64_t Read(void* destination, std::int64_t bytesToRead) override { return _target.Read(destination, bytesToRead); }
			std::int64_t Write(const void* source, std::int64_t bytesToWrite) override {
				std::int64_t written = _target.Write(source, bytesToWrite);
				if (bytesToWrite > 0 && written != bytesToWrite) {
					_failed = true;
				}
				return written;
			}
			bool Flush() override { return _target.Flush(); }
			bool IsValid() override { return !_failed && _target.IsValid(); }
			std::int64_t GetSize() const override { return _target.GetSize(); }
			std::int64_t SetSize(std::int64_t size) override { return _target.SetSize(size); }

			bool HasFailed() const { return _failed; }

		private:
			Stream& _target;
			bool _failed;
		};

		// A file reached through a symbolic link is replaced where the link leads, the link would be replaced by a file
		// of its own otherwise, and the file it led to would stay as it was
		String ResolveTarget(StringView path)
		{
#if defined(__linux__) || defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
			String nullTerminated = String::nullTerminatedView(path);
			struct stat info;
			if (::lstat(nullTerminated.data(), &info) == 0 && S_ISLNK(info.st_mode)) {
				char resolved[PATH_MAX];
				if (::realpath(nullTerminated.data(), resolved) != nullptr) {
					return String(resolved);
				}
			}
#endif
			return String(path);
		}

		// The rename is lasting only once the directory it's in is written too, on a file system that doesn't do so by itself
		void SyncDirectory(StringView path)
		{
#if defined(__linux__) || defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
			String directory = fs::GetDirectoryName(path);
			if (directory.empty()) {
				directory = "."_s;
			}
			std::int32_t fd = ::open(directory.data(), O_RDONLY);
			if (fd >= 0) {
				::fsync(fd);
				::close(fd);
			}
#else
			static_cast<void>(path);
#endif
		}

		bool ReplaceByTemporary(StringView tempPath, StringView path)
		{
			// The move replaces the target in one step, the old file stays where it was if it fails
			if (!fs::Move(tempPath, path)) {
				LOGE("File \"{}\" cannot be replaced, the new content was left in \"{}\"", path, tempPath);
				return false;
			}
			SyncDirectory(path);
			return true;
		}
	}

	bool AtomicFile::Write(StringView requestedPath, Function<bool(Stream&)>&& write, bool keepBackup)
	{
		if (requestedPath.empty()) {
			return false;
		}

		String path = ResolveTarget(requestedPath);
		String tempPath = path + ".tmp"_s;
		bool success;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			if (!s->IsValid()) {
				LOGE("File \"{}\" cannot be created", tempPath);
				return false;
			}
			CheckedStream checked(*s);
			success = write(checked);
			// The last buffered bytes are written by the flush, only then a full disk shows up
			success = (success && !checked.HasFailed() && s->Flush());
		}
		if (!success) {
			fs::RemoveFile(tempPath);
			LOGE("File \"{}\" cannot be written", path);
			return false;
		}

		if (keepBackup && fs::FileExists(path)) {
			String backupPath = path + ".bak"_s;
			// Not a reason to fail, the new content is complete
			if (!fs::Copy(path, backupPath, true)) {
				LOGW("Backup \"{}\" cannot be written", backupPath);
			}
		}

		return ReplaceByTemporary(tempPath, path);
	}

	bool AtomicFile::Write(StringView path, const void* data, std::int64_t size, bool keepBackup)
	{
		return Write(path, [data, size](Stream& s) {
			return (size <= 0 || s.Write(data, size) == size);
		}, keepBackup);
	}

	bool AtomicFile::Stage(StringView requestedPath, Function<bool(Stream&)>&& write)
	{
		if (requestedPath.empty()) {
			return false;
		}

		String path = ResolveTarget(requestedPath);
		String tempPath = path + ".tmp"_s;
		bool success;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			if (!s->IsValid()) {
				LOGE("File \"{}\" cannot be created", tempPath);
				return false;
			}
			CheckedStream checked(*s);
			success = write(checked);
			success = (success && !checked.HasFailed() && s->Flush());
		}
		if (!success) {
			fs::RemoveFile(tempPath);
			LOGE("File \"{}\" cannot be written", path);
			return false;
		}
		return true;
	}

	bool AtomicFile::Commit(StringView requestedPath, bool keepBackup)
	{
		String path = ResolveTarget(requestedPath);
		String tempPath = path + ".tmp"_s;
		if (keepBackup && fs::FileExists(path)) {
			String backupPath = path + ".bak"_s;
			if (!fs::Copy(path, backupPath, true)) {
				// Committed together with other files, it has to be possible to put it back
				LOGE("Backup \"{}\" cannot be written", backupPath);
				return false;
			}
		}
		return ReplaceByTemporary(tempPath, path);
	}

	void AtomicFile::Discard(StringView requestedPath)
	{
		String path = ResolveTarget(requestedPath);
		fs::RemoveFile(String(path + ".tmp"_s));
	}

	bool AtomicFile::RestoreBackup(StringView requestedPath, bool existedBefore)
	{
		String path = ResolveTarget(requestedPath);
		if (!existedBefore) {
			return fs::RemoveFile(path);
		}
		String backupPath = path + ".bak"_s;
		String tempPath = path + ".tmp"_s;
		// Copied next to the file and moved over it in one step, a copy straight over it would leave it cut short if it
		// failed in the middle; the backup stays in case the file can't be replaced now either
		if (!fs::Copy(backupPath, tempPath, true)) {
			fs::RemoveFile(tempPath);
			LOGE("Backup \"{}\" cannot be put back", backupPath);
			return false;
		}
		return ReplaceByTemporary(tempPath, path);
	}
}

#endif
