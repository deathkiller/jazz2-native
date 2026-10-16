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
			var data = _malloc(bytes.length > 0 ? bytes.length : 1);
			HEAPU8.set(bytes, data);
			var name = stringToNewUTF8(file.name);
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
		if (s->IsValid()) {
			s->Write(data, std::int64_t(size));
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
		pick.OnPicked = std::move(onPicked);
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
			PendingPick pick = std::move(_pending[i]);
			_pending.erase(_pending.begin() + i);
			if (pick.OnPicked) {
				pick.OnPicked(pick.Path);
			}
		}
	}
}

#endif
