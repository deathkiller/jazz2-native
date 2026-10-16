#include "AutosaveService.h"

#if defined(WITH_EDITOR)

#include "../../PreferencesCache.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <cstdio>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	AutosaveService::AutosaveService()
		: _timer(0.0f), _writtenRevision(0)
	{
	}

	void AutosaveService::SetSource(StringView filePath)
	{
		_filePath = filePath;
		_timer = 0.0f;
		_writtenRevision = 0;
	}

	void AutosaveService::Update(float timeMult, LevelDocument& document, CommandHistory& history)
	{
		if (!history.IsDirty() || history.GetRevision() == _writtenRevision) {
			_timer = 0.0f;
			return;
		}
		_timer += timeMult;
		if (_timer >= Interval) {
			SaveNow(document, history);
		}
	}

	bool AutosaveService::SaveNow(LevelDocument& document, CommandHistory& history)
	{
		_timer = 0.0f;
		if (!history.IsDirty() || history.GetRevision() == _writtenRevision) {
			return true;
		}

		if (BeforeSave) {
			BeforeSave();
		}

		String path = GetRecoveryPath(_filePath);
		fs::CreateDirectories(fs::GetDirectoryName(path));
		String tempPath = path + ".tmp"_s;

		// Where the copy belongs is kept in the copy itself, the document is left as it was
		SmallVector<std::uint8_t, 0> info;
		info.append(reinterpret_cast<const std::uint8_t*>(_filePath.data()), reinterpret_cast<const std::uint8_t*>(_filePath.data()) + _filePath.size());
		document.SetChunk(RecoveryChunk, info);
		bool success;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			// Saved quickly, it's only a copy
			success = (s->IsValid() && document.Save(*s, false, 1));
		}
		document.SetChunk(RecoveryChunk, {});

		if (!success) {
			fs::RemoveFile(tempPath);
			LOGW("Recovery copy of the level cannot be written to \"{}\"", path);
			return false;
		}
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		if (!fs::Move(tempPath, path)) {
			LOGW("Recovery copy of the level cannot be written to \"{}\"", path);
			return false;
		}

		_writtenRevision = history.GetRevision();
		LOGD("Recovery copy of the level written to \"{}\"", path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Into the storage of the browser, it would be gone with the page otherwise
		fs::SyncToPersistent();
#endif
		return true;
	}

	void AutosaveService::Discard()
	{
		String path = GetRecoveryPath(_filePath);
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		_writtenRevision = 0;
		_timer = 0.0f;
	}

	String AutosaveService::FindRecovery(StringView filePath)
	{
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
			name = "untitled.j2l"_s;
		} else {
			// One copy for each file, named by a hash of its path (FNV-1a)
			String normalized = StringUtils::lowercase(fs::FromNativeSeparators(String(filePath)));
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
