#include "WebFileBridge.h"

#if defined(WITH_EDITOR) && defined(DEATH_TARGET_EMSCRIPTEN)

#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <emscripten.h>

#include <memory>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		struct PendingPick
		{
			std::int32_t Id;
			Function<void(StringView)> OnPicked;
			String Path;
			bool Picked = false;
		};

		SmallVector<PendingPick, 2> _pending;
		std::int32_t _nextId = 1;
	}
}

// The functions of the runtime the scripts below use, they would be left out of the build otherwise, as nothing
// else refers to them
#if defined(EM_JS_DEPS)
EM_JS_DEPS(jazz2EditorWebFileBridge, "$stringToNewUTF8,$UTF8ToString,malloc,free");
#endif

// A hidden <input type="file">, the file read whole is handed to the game by the exported function below
EM_JS(void, __jazz2EditorPickFile, (const char* accept, int requestId), {
	var input = document.createElement('input');
	input.type = 'file';
	input.accept = UTF8ToString(accept);
	input.style.display = 'none';
	input.onchange = function(e) {
		var file = (e.target.files.length > 0 ? e.target.files[0] : null);
		document.body.removeChild(input);
		if (!file) {
			return;
		}
		var reader = new FileReader();
		reader.onload = function() {
			var bytes = new Uint8Array(reader.result);
			// A file larger than the memory of the game can take isn't picked at all
			var data = _malloc(bytes.length > 0 ? bytes.length : 1);
			if (!data) {
				console.error('File "' + file.name + '" is too large to be opened');
				return;
			}
			HEAPU8.set(bytes, data);
			var name = stringToNewUTF8(file.name);
			if (!name) {
				_free(data);
				return;
			}
			_jazz2EditorFilePicked(requestId, name, data, bytes.length);
			_free(name);
			_free(data);
		};
		reader.readAsArrayBuffer(file);
	};
	document.body.appendChild(input);
	input.click();
});

// A link to the bytes as a blob, clicked, so the browser saves them
EM_JS(void, __jazz2EditorDownload, (const char* name, const unsigned char* data, int size), {
	var bytes = HEAPU8.slice(data, data + size);
	var blob = new Blob([bytes], { type: 'application/octet-stream' });
	var url = URL.createObjectURL(blob);
	var link = document.createElement('a');
	link.href = url;
	link.download = UTF8ToString(name);
	link.style.display = 'none';
	document.body.appendChild(link);
	link.click();
	document.body.removeChild(link);
	setTimeout(function() { URL.revokeObjectURL(url); }, 2000);
});

extern "C" EMSCRIPTEN_KEEPALIVE void jazz2EditorFilePicked(int requestId, const char* name, const unsigned char* data, int size)
{
	using namespace Jazz2::Editor;
	if (name == nullptr || (data == nullptr && size > 0)) {
		return;
	}
	for (PendingPick& pick : _pending) {
		if (pick.Id != requestId) {
			continue;
		}
		// Into the memory of the game, under its own name, so it's saved by the same name
		String directory = fs::CombinePath(fs::GetTempDirectory(), "Upload"_s);
		fs::CreateDirectories(directory);
		String path = fs::CombinePath(directory, fs::GetFileName(StringView(name)));
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		auto s = fs::Open(path, FileAccess::Write);
		// Flushed and checked, a file cut short by a full memory would be opened as damaged
		if (s->IsValid() && (size <= 0 || s->Write(data, std::int64_t(size)) == std::int64_t(size)) && s->Flush()) {
			pick.Path = path;
			pick.Picked = true;
		}
		break;
	}
}

namespace Jazz2::Editor
{
	void WebFileBridge::PickFile(StringView extensions, Function<void(StringView)>&& onPicked)
	{
		// The browser says nothing when no file was chosen, so those asked for long ago are forgotten
		if (_pending.size() >= 4) {
			_pending.erase(_pending.begin());
		}
		PendingPick& pick = _pending.emplace_back();
		pick.Id = _nextId++;
		pick.OnPicked = Death::move(onPicked);
		// The extensions as the browser takes them, separated by commas
		String accept = extensions;
		for (char& c : accept) {
			if (c == ';') {
				c = ',';
			}
		}
		__jazz2EditorPickFile(accept.data(), pick.Id);
	}

	bool WebFileBridge::DownloadFile(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		std::int64_t size = (s->IsValid() ? s->GetSize() : -1);
		if (size < 0) {
			return false;
		}
		std::unique_ptr<unsigned char[]> data = std::make_unique<unsigned char[]>(std::size_t(size > 0 ? size : 1));
		s->Read(data.get(), size);
		String name = String(fs::GetFileName(path));
		__jazz2EditorDownload(name.data(), data.get(), std::int32_t(size));
		return true;
	}

	void WebFileBridge::Update()
	{
		for (std::size_t i = 0; i < _pending.size(); ) {
			if (!_pending[i].Picked) {
				i++;
				continue;
			}
			PendingPick pick = Death::move(_pending[i]);
			_pending.erase(_pending.begin() + i);
			if (pick.OnPicked) {
				pick.OnPicked(pick.Path);
			}
		}
	}
}

#endif
