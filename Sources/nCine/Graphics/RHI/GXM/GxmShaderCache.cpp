#include "GxmShaderCache.h"

#if defined(WITH_RHI_GXM)

#include "../../../Base/HashFunctions.h"

#include <cstdlib>
#include <cstring>

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace nCine::RHI::GXM
{
	namespace
	{
		// A single stage is a few kilobytes of GXP; this only has to reject a file that is not one of ours
		constexpr std::int64_t MaxPackSize = 16 * 1024 * 1024;
		constexpr std::uint32_t MaxEntrySize = 1 * 1024 * 1024;
		constexpr std::uint32_t MaxEntryCount = 4096;
		// Above this many entries a write drops the ones this run never looked up (see Flush()). The whole
		// shader set is around a hundred stages, so reaching it means old revisions have piled up
		constexpr std::uint32_t PruneThreshold = 512;
		// Signature + version + fingerprint + count, so a file shorter than this cannot carry a header
		constexpr std::int64_t HeaderSize = sizeof(GxmShaderCache::Signature) + 2 + 4 + 4;
	}

	String GxmShaderCache::_writablePath;
	std::uint32_t GxmShaderCache::_compilerFingerprint = 0;
	SmallVector<GxmShaderCache::Entry, 0> GxmShaderCache::_entries;
	bool GxmShaderCache::_dirty = false;
	bool GxmShaderCache::_writeFailed = false;
	bool GxmShaderCache::_initialized = false;

	std::uint64_t GxmShaderCache::KeyOf(const char* source, bool vertexStage)
	{
		// The stage flag is folded in because the same text compiled as a vertex and as a fragment stage is
		// two different binaries - nothing in this engine does that today, but a key that cannot tell them
		// apart would hand over the wrong one silently rather than missing
		std::uint64_t hash = xxHash3(source, std::strlen(source));
		return (vertexStage ? hash : (hash ^ 0x9E3779B97F4A7C15ull));
	}

	void GxmShaderCache::Initialize(StringView writablePath, std::uint32_t compilerFingerprint)
	{
		_entries.clear();
		_dirty = false;
		_writeFailed = false;
		_compilerFingerprint = compilerFingerprint;
		_writablePath = writablePath;
		_initialized = !writablePath.empty();

		if (!_initialized) {
			LOGD("GXP shader cache is disabled (no cache path)");
			return;
		}

		// The shipped pack first, the writable one over it: an entry the console recompiled (because the
		// shipped pack was built before the last shader change) wins over the shipped one under the same key,
		// and a key only the shipped pack has stays available
		LoadPack(fs::CombinePath(PrebakedPath, FileName), false);
		LoadPack(fs::CombinePath(_writablePath, FileName), true);

		if (!_entries.empty()) {
			LOGI("Loaded {} cached GXP shader binaries", _entries.size());
		}
	}

	bool GxmShaderCache::LoadPack(StringView path, bool writable)
	{
		std::unique_ptr<Stream> s = fs::Open(path, FileAccess::Read);
		if (s == nullptr || !s->IsValid()) {
			// Not having a pack is the normal first-run state, so only a pack that IS there and is unusable
			// is worth a line in the log
			return false;
		}

		const std::int64_t fileSize = s->GetSize();
		if (fileSize <= HeaderSize || fileSize > MaxPackSize) {
			LOGW("Ignoring GXP shader cache \"{}\": implausible size ({} bytes)", path, fileSize);
			return false;
		}

		std::uint8_t signature[sizeof(Signature)];
		if (s->Read(signature, sizeof(signature)) != std::int64_t(sizeof(signature)) ||
			std::memcmp(signature, Signature, sizeof(Signature)) != 0) {
			LOGW("Ignoring GXP shader cache \"{}\": not a shader cache", path);
			return false;
		}

		const std::uint16_t fileVersion = s->ReadValueAsLE<std::uint16_t>();
		if (fileVersion != FileVersion) {
			LOGI("Discarding GXP shader cache \"{}\": format {}, expected {}", path, fileVersion, FileVersion);
			return false;
		}

		const std::uint32_t fileFingerprint = s->ReadValueAsLE<std::uint32_t>();
		if (fileFingerprint != _compilerFingerprint) {
			LOGI("Discarding GXP shader cache \"{}\": built by a different Cg compiler", path);
			return false;
		}

		const std::uint32_t entryCount = s->ReadValueAsLE<std::uint32_t>();
		if (entryCount > MaxEntryCount) {
			LOGW("Ignoring GXP shader cache \"{}\": {} entries is not plausible", path, entryCount);
			return false;
		}

		DeflateStream ds(*s);
		std::uint32_t loaded = 0;
		for (std::uint32_t i = 0; i < entryCount; i++) {
			const std::uint64_t key = ds.ReadValueAsLE<std::uint64_t>();
			const std::uint32_t length = ds.ReadVariableUint32();
			if (length == 0 || length > MaxEntrySize) {
				// A truncated or corrupt pack keeps whatever was read before the damage rather than being
				// thrown away wholesale; the stages that did not survive simply miss and are recompiled
				LOGW("GXP shader cache \"{}\" is truncated after {} of {} entries", path, loaded, entryCount);
				_dirty = true;
				break;
			}

			Array<std::uint8_t> binary{NoInit, length};
			if (ds.Read(binary.data(), std::int64_t(length)) != std::int64_t(length)) {
				LOGW("GXP shader cache \"{}\" is truncated after {} of {} entries", path, loaded, entryCount);
				_dirty = true;
				break;
			}

			if (Entry* existing = Find(key)) {
				existing->Binary = Death::move(binary);
			} else {
				_entries.push_back(Entry{key, Death::move(binary), false});
			}
			loaded++;
		}

		if (!writable && loaded > 0) {
			// Nothing has written the shipped pack's entries into the writable one yet, and Flush() only runs
			// when something changed - so a first run served entirely by the shipped pack would leave no
			// writable pack behind. Marking it dirty makes that run write one, which is also what lets the
			// next run start without the content pack being consulted at all.
			_dirty = true;
		}
		return loaded > 0;
	}

	GxmShaderCache::Entry* GxmShaderCache::Find(std::uint64_t key)
	{
		for (Entry& entry : _entries) {
			if (entry.Key == key) {
				return &entry;
			}
		}
		return nullptr;
	}

	bool GxmShaderCache::IsAvailable()
	{
		return _initialized;
	}

	std::uint32_t GxmShaderCache::GetEntryCount()
	{
		return std::uint32_t(_entries.size());
	}

	SceGxmProgram* GxmShaderCache::Lookup(const char* source, bool vertexStage, std::uint32_t& sizeInBytes)
	{
		sizeInBytes = 0;
		if (!_initialized || source == nullptr) {
			return nullptr;
		}

		Entry* entry = Find(KeyOf(source, vertexStage));
		if (entry == nullptr) {
			return nullptr;
		}
		entry->Used = true;

		// The caller owns the block and frees it with std::free(), which is the same contract a freshly
		// compiled stage arrives under (see GxmShaderProgram::CompileCgStage())
		const std::uint32_t length = std::uint32_t(entry->Binary.size());
		void* owned = std::malloc(length);
		if (owned == nullptr) {
			LOGE("Out of memory reading a {} byte GXP binary from the cache", length);
			return nullptr;
		}
		std::memcpy(owned, entry->Binary.data(), length);
		sizeInBytes = length;
		return static_cast<SceGxmProgram*>(owned);
	}

	void GxmShaderCache::Store(const char* source, bool vertexStage, const void* binary, std::uint32_t sizeInBytes)
	{
		if (!_initialized || source == nullptr || binary == nullptr ||
			sizeInBytes == 0 || sizeInBytes > MaxEntrySize) {
			return;
		}

		const std::uint64_t key = KeyOf(source, vertexStage);
		Array<std::uint8_t> owned{NoInit, sizeInBytes};
		std::memcpy(owned.data(), binary, sizeInBytes);

		if (Entry* existing = Find(key)) {
			existing->Binary = Death::move(owned);
			existing->Used = true;
		} else {
			_entries.push_back(Entry{key, Death::move(owned), true});
		}
		_dirty = true;
	}

	void GxmShaderCache::Flush()
	{
		// _writeFailed is what keeps a cache that cannot be written (a full or read-only card) from retrying,
		// and re-reporting, on every frame - the flush runs from PresentFrame()
		if (!_initialized || !_dirty || _writeFailed) {
			return;
		}

		// Everything is written back, not only what this run looked up: which shaders a run compiles depends on
		// where it went (the water composite needs a level with water in it), so writing only the used ones
		// would have a session in a dry level throw away the water stages the previous one paid for. What that
		// leaves behind is orphans from an older revision of a ".shader" file - dead weight rather than a
		// correctness problem, since an entry is keyed by its source and a stale one can no longer be found -
		// and they are bounded: past PruneThreshold entries the unused ones are dropped, which is exactly the
		// orphans plus whatever this run did not need, and the latter costs one recompile to earn back.
		const bool prune = (_entries.size() > PruneThreshold);
		std::uint32_t entryCount = 0;
		for (const Entry& entry : _entries) {
			if (entry.Used || !prune) {
				entryCount++;
			}
		}
		if (entryCount == 0) {
			_dirty = false;
			return;
		}
		if (prune) {
			LOGD("Pruning {} unused entries from the GXP shader cache", std::uint32_t(_entries.size()) - entryCount);
		}

		if (!fs::DirectoryExists(_writablePath) && !fs::CreateDirectories(_writablePath)) {
			LOGW("Cannot create \"{}\" to write the GXP shader cache into", _writablePath);
			_writeFailed = true;
			return;
		}

		// Written next to the real file and moved over it, so an interrupted write (the console losing power
		// during a save is the ordinary case here) cannot leave a half-file where the pack should be. A
		// truncated pack would be rejected on load anyway, but this way the previous good one survives.
		String path = fs::CombinePath(_writablePath, FileName);
		String tempPath = path + ".tmp"_s;
		{
			std::unique_ptr<Stream> s = fs::Open(tempPath, FileAccess::Write);
			if (s == nullptr || !s->IsValid()) {
				LOGW("Cannot write the GXP shader cache to \"{}\"", tempPath);
				_writeFailed = true;
				return;
			}

			s->Write(Signature, sizeof(Signature));
			s->WriteValueAsLE<std::uint16_t>(FileVersion);
			s->WriteValueAsLE<std::uint32_t>(_compilerFingerprint);
			s->WriteValueAsLE<std::uint32_t>(entryCount);

			DeflateWriter dw(*s);
			for (const Entry& entry : _entries) {
				if (prune && !entry.Used) {
					continue;
				}
				dw.WriteValueAsLE<std::uint64_t>(entry.Key);
				dw.WriteVariableUint32(std::uint32_t(entry.Binary.size()));
				dw.Write(entry.Binary.data(), std::int64_t(entry.Binary.size()));
			}
		}

		fs::RemoveFile(path);
		if (!fs::Move(tempPath, path)) {
			LOGW("Cannot move the GXP shader cache into place at \"{}\"", path);
			fs::RemoveFile(tempPath);
			_writeFailed = true;
			return;
		}

		_dirty = false;
		LOGI("Wrote {} compiled GXP shader binaries to the cache", entryCount);
	}

	void GxmShaderCache::Shutdown()
	{
		Flush();
		_entries.clear();
		_writablePath = {};
		_initialized = false;
	}
}

#endif
