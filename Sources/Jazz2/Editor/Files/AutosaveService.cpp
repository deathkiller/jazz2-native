#include "AutosaveService.h"

#if defined(WITH_EDITOR)

#include "AtomicFile.h"
#include "../Document/CommandArchive.h"
#include "../../PreferencesCache.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <cstdint>
#include <cstdio>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	AutosaveService::AutosaveService()
		: _timer(0.0f), _writtenRevision(0), _writtenOutsideChanges(0)
	{
	}

	AutosaveService::~AutosaveService()
	{
		FinishWriting();
	}

	void AutosaveService::FinishWriting()
	{
#if defined(NCINE_AUTOSAVE_THREADED)
		if (_job == nullptr) {
			return;
		}
		_job->Wait();
		_job = nullptr;
		std::unique_ptr<PendingCopy> pending = Death::move(_pending);
		if (pending != nullptr) {
			Complete(pending->Success, pending->Path, pending->Revision, pending->OutsideChanges);
		}
#endif
	}

	bool AutosaveService::Complete(bool success, StringView path, std::uint32_t revision, std::uint32_t outsideChanges)
	{
		if (!success) {
			LOGW("Recovery copy of the level cannot be written to \"{}\"", path);
			return false;
		}
		_writtenRevision = revision;
		_writtenOutsideChanges = outsideChanges;
		LOGD("Recovery copy of the level written to \"{}\"", path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Into the storage of the browser, it would be gone with the page otherwise
		fs::SyncToPersistent();
#endif
		return true;
	}

	void AutosaveService::SetSource(StringView filePath)
	{
		// A copy of the previous level still being written belongs to it, it's finished first
		FinishWriting();
		_filePath = filePath;
		_timer = 0.0f;
		_writtenRevision = 0;
		_writtenOutsideChanges = 0;
	}

	bool AutosaveService::IsWritten(const CommandHistory& history) const
	{
		// Changes made outside of the history don't change the revision, so they're counted too
		return (_writtenRevision != 0 && history.GetRevision() == _writtenRevision && history.GetOutsideChangeCount() == _writtenOutsideChanges);
	}

	void AutosaveService::Update(float timeMult, LevelDocument& document, CommandHistory& history)
	{
#if defined(NCINE_AUTOSAVE_THREADED)
		// The copy written meanwhile is taken as written once the thread is done with it
		if (_job != nullptr) {
			if (!_job->IsDone()) {
				return;
			}
			FinishWriting();
		}
#endif
		if (!history.IsDirty()) {
			// Undone back to how it was saved, the copy written meanwhile would only offer the undone changes again
			if (_writtenRevision != 0) {
				Discard();
			}
			_timer = 0.0f;
			return;
		}
		if (IsWritten(history)) {
			_timer = 0.0f;
			return;
		}
		_timer += timeMult;
		if (_timer >= Interval) {
			SaveNow(document, history);
		}
	}

	bool AutosaveService::SaveNow(LevelDocument& document, CommandHistory& history, bool wait)
	{
		_timer = 0.0f;
		// A copy still being written is finished first, it may be all that's needed then
		if (wait) {
			FinishWriting();
		}
#if defined(NCINE_AUTOSAVE_THREADED)
		else if (_job != nullptr) {
			// Tried again the next time, not stacked behind it
			return true;
		}
#endif
		if (!history.IsDirty() || IsWritten(history)) {
			return true;
		}

		if (BeforeSave) {
			BeforeSave();
		}

		String path = GetRecoveryPath(_filePath);
		fs::CreateDirectories(fs::GetDirectoryName(path));

		// Where the copy belongs and the history of the changes are kept in the copy itself, the document is left as
		// it was, so the changes can be undone after they're restored
		SmallVector<std::uint8_t, 0> info;
		info.append(reinterpret_cast<const std::uint8_t*>(_filePath.data()), reinterpret_cast<const std::uint8_t*>(_filePath.data()) + _filePath.size());
		// Only the commands that changed since the last copy are written again, see CommandArchive::Write()
		SmallVector<std::uint8_t, 0> archive;
		CommandArchive::Write(history, archive);
		std::uint32_t revision = history.GetRevision();
		std::uint32_t outsideChanges = history.GetOutsideChangeCount();

#if defined(NCINE_AUTOSAVE_THREADED)
		if (!wait) {
			// Compressing a large level takes a while, so a copy of it is written on a thread of its own and the editor
			// doesn't stop for it, the history was taken above as it's changed only here
			_pending = std::make_unique<PendingCopy>();
			_pending->Document = document;
			_pending->Document.ReplaceChunk(RecoveryChunk, Death::move(info));
			_pending->Document.ReplaceChunk(CommandArchive::Chunk, Death::move(archive));
			_pending->Path = Death::move(path);
			_pending->Revision = revision;
			_pending->OutsideChanges = outsideChanges;
			PendingCopy* pending = _pending.get();
			_job = std::make_unique<BackgroundJob>();
			_job->Start([pending](JobProgress&) {
				pending->Success = AtomicFile::Write(pending->Path, [pending](Stream& s) {
					return pending->Document.Save(s, false, 1);
				});
			}, "Recovery copy");
			return true;
		}
#endif

		document.ReplaceChunk(RecoveryChunk, Death::move(info));
		document.ReplaceChunk(CommandArchive::Chunk, Death::move(archive));
		// Written next to the copy first and checked, so a full disk never replaces a good copy by a damaged one,
		// saved quickly, it's only a copy
		bool success = AtomicFile::Write(path, [&document](Stream& s) {
			return document.Save(s, false, 1);
		});
		document.SetChunk(RecoveryChunk, {});
		document.SetChunk(CommandArchive::Chunk, {});
		return Complete(success, path, revision, outsideChanges);
	}

	void AutosaveService::Discard()
	{
		// Not to be written again by a thread after it's removed
		FinishWriting();
		String path = GetRecoveryPath(_filePath);
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
			fs::SyncToPersistent();
#endif
		}
		_writtenRevision = 0;
		_writtenOutsideChanges = 0;
		_timer = 0.0f;
	}

	String& AutosaveService::GetUntitledName()
	{
		// Each run has a copy of its own for a level that wasn't saved yet, so two of them don't write over each other
		static String name;
		if (name.empty()) {
			std::uint64_t seed = std::uint64_t(DateTime::UtcNow().ToUnixMilliseconds()) ^ (std::uint64_t(reinterpret_cast<std::uintptr_t>(&name)) << 16);
			char buffer[48];
			std::snprintf(buffer, sizeof(buffer), "untitled-%016llx.j2l", (unsigned long long)seed);
			name = buffer;
		}
		return name;
	}

	String AutosaveService::FindRecovery(StringView filePath)
	{
		if (filePath.empty()) {
			// The copy of a level that wasn't saved is of another run, the newest one is offered, and it's taken as the
			// copy of this run then, so discarding the changes removes it and new changes are written over it
			String directory = fs::CombinePath({ PreferencesCache::GetDirectory(), "Editor"_s, "Recovery"_s });
			String newest;
			std::int64_t newestTime = INT64_MIN;
			if (fs::DirectoryExists(directory)) {
				for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
					StringView name = fs::GetFileName(item);
					if (!name.hasPrefix("untitled"_s) || StringUtils::lowercase(fs::GetExtension(item)) != "j2l"_s) {
						continue;
					}
					std::int64_t time = fs::GetLastModificationTime(item).ToUnixMilliseconds();
					if (newest.empty() || time > newestTime) {
						newest = item;
						newestTime = time;
					}
				}
			}
			if (!newest.empty()) {
				GetUntitledName() = fs::GetFileName(newest);
			}
			return newest;
		}

		String path = GetRecoveryPath(filePath);
		if (!fs::FileExists(path)) {
			return {};
		}
		if (!filePath.empty() && fs::FileExists(filePath) &&
			fs::GetLastModificationTime(path) <= fs::GetLastModificationTime(filePath)) {
			// The level was saved after the copy was written, by another way
			return {};
		}
		return path;
	}

	String AutosaveService::GetRecoveryPath(StringView filePath)
	{
		String name;
		if (filePath.empty()) {
			name = GetUntitledName();
		} else {
			// One copy for each file, named by a hash of its path (FNV-1a), a path that differs only in the case is
			// the same file only where the file system ignores the case
			String normalized = fs::FromNativeSeparators(String(filePath));
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_APPLE)
			normalized = StringUtils::lowercase(normalized);
#endif
			std::uint64_t hash = 0xCBF29CE484222325ull;
			for (char c : normalized) {
				hash ^= std::uint8_t(c);
				hash *= 0x100000001B3ull;
			}
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%016llx.j2l", (unsigned long long)hash);
			name = buffer;
		}
		return fs::CombinePath({ PreferencesCache::GetDirectory(), "Editor"_s, "Recovery"_s, name });
	}
}

#endif
