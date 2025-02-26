#include "EmscriptenFileStream.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)

#include <utility>
#include <emscripten/bind.h>

#include "../Containers/StringStl.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	EmscriptenFileStream::EmscriptenFileStream(emscripten::val handle, std::int32_t bufferSize)
		: _handle(handle), _size(-1), _filePos(0), _readPos(0), _readLength(0), _bufferSize(bufferSize)
	{
	}

	void EmscriptenFileStream::Dispose()
	{
	}

	std::int64_t EmscriptenFileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		if (origin == SeekOrigin::Current) {
			offset -= (_readLength - _readPos);
		}

		std::int32_t oldPos = _filePos + (_readPos - _readLength);
		std::int64_t pos = SeekInternal(offset, origin);
		if (pos < 0) {
			return pos;
		}

		if (_readLength > 0) {
			if (oldPos == pos) {
				// Seek after the buffered part, so the position is still correct
				if (SeekInternal(_readLength - _readPos, SeekOrigin::Current) < 0) {
					// This shouldn't fail, but if it does, invalidate the buffer
					_readPos = 0;
					_readLength = 0;
				}
			} else if (oldPos - _readPos <= pos && pos < oldPos + _readLength - _readPos) {
				// Some part of the buffer is still valid
				std::int32_t diff = static_cast<std::int32_t>(pos - oldPos);
				_readPos += diff;
				// Seek after the buffered part, so the position is still correct
				if (SeekInternal(_readLength - _readPos, SeekOrigin::Current) < 0) {
					// This shouldn't fail, but if it does, invalidate the buffer
					_readPos = 0;
					_readLength = 0;
				}
			} else {
				_readPos = 0;
				_readLength = 0;
			}
		}

		return pos;
	}

	std::int64_t EmscriptenFileStream::GetPosition() const
	{
		return (_filePos - _readLength) + _readPos;
	}

	std::int64_t EmscriptenFileStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);

		bool isBlocked = false;
		std::int64_t n = (_readLength - _readPos);
		if (n == 0) {
			if (bytesToRead >= _bufferSize) {
				_readPos = 0;
				_readLength = 0;

				do {
					std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? (std::int32_t)bytesToRead : INT32_MAX);
					std::int32_t bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
					if DEATH_UNLIKELY(bytesRead < 0) {
						return bytesRead;
					} else if DEATH_UNLIKELY(bytesRead == 0) {
						break;
					}
					n += bytesRead;
					bytesToRead -= bytesRead;
				} while (bytesToRead > 0);

				return n;
			}

			InitializeBuffer();
			n = ReadInternal(&_buffer[0], _bufferSize);
			if (n == 0) {
				return 0;
			}
			isBlocked = (n < _bufferSize);
			_readPos = 0;
			_readLength = (std::int32_t)n;
		}

		if (bytesToRead < n) {
			n = bytesToRead;
		}

		std::memcpy(typedBuffer, &_buffer[_readPos], n);
		_readPos += (std::int32_t)n;

		bytesToRead -= n;
		if (bytesToRead > 0 && !isBlocked) {
			DEATH_DEBUG_ASSERT(_readPos == _readLength);
			_readPos = 0;
			_readLength = 0;

			do {
				std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? (std::int32_t)bytesToRead : INT32_MAX);
				std::int32_t bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
				if DEATH_UNLIKELY(bytesRead < 0) {
					return bytesRead;
				} else if DEATH_UNLIKELY(bytesRead == 0) {
					break;
				}
				n += bytesRead;
				bytesToRead -= bytesRead;
			} while (bytesToRead > 0);
		}

		return n;
	}

	std::int64_t EmscriptenFileStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		return Stream::Invalid;
	}

	bool EmscriptenFileStream::Flush()
	{
		return true;
	}

	bool EmscriptenFileStream::IsValid()
	{
		return true;
	}

	std::int64_t EmscriptenFileStream::GetSize() const
	{
		if (_size < 0) {
			_size = _handle["size"].as<std::int32_t>();
		}
		return _size;
	}

	Containers::StringView EmscriptenFileStream::GetName() const
	{
		if (_name.empty()) {
			_name = _handle["name"].as<std::string>();
		}
		return _name;
	}

	void EmscriptenFileStream::InitializeBuffer()
	{
		if DEATH_UNLIKELY(_buffer == nullptr) {
			_buffer = std::make_unique<std::uint8_t[]>(_bufferSize);
		}
	}

	std::int64_t EmscriptenFileStream::SeekInternal(std::int64_t offset, SeekOrigin origin)
	{
		std::int32_t size = GetSize();
		std::int32_t newPos;
		switch (origin) {
			case SeekOrigin::Begin: newPos = static_cast<std::int32_t>(offset); break;
			case SeekOrigin::Current: newPos = _filePos + static_cast<std::int32_t>(offset); break;
			case SeekOrigin::End: newPos = size + static_cast<std::int32_t>(offset); break;
			default: return Stream::OutOfRange;
		}

		if (newPos < 0 || newPos > size) {
			newPos = Stream::OutOfRange;
		} else {
			_filePos = newPos;
		}
		return newPos;
	}

	std::int32_t EmscriptenFileStream::ReadInternal(std::uint8_t* destination, std::int32_t bytesToRead)
	{
		auto blob = _handle.call<emscripten::val>("slice", _filePos, _filePos + bytesToRead);
		auto result = blob.call<emscripten::val>("arrayBuffer").await();
		auto sourceTypedArray = emscripten::val::global("Uint8Array").new_(result);

		auto length = sourceTypedArray["length"].as<std::size_t>();
		emscripten::val memoryView{emscripten::typed_memory_view(length, destination)};
		memoryView.call<void>("set", sourceTypedArray);

		_filePos += static_cast<std::int32_t>(length);
		return static_cast<std::int32_t>(length);
	}

	EmscriptenFilePicker::EmscriptenFilePicker()
		: _supportsCancel(false)
	{
	}

	bool EmscriptenFilePicker::IsCancelSupported() const
	{
		return _supportsCancel;
	}

	void EmscriptenFilePicker::FetchFilesAsync(Containers::StringView fileFilter, bool multiple, Containers::Function<void(Containers::ArrayView<EmscriptenFileStream>)>&& callback)
	{
		if (_activeCallback) {
			// Another callback is already active, cancel this one
			callback({});
			return;
		}

		_activeCallback = Death::move(callback);

		// Create file input element which will display a native file dialog
		auto document = emscripten::val::global("document");
		auto input = document.call<emscripten::val>("createElement", std::string("input"));
		input.set("type", "file");
		input.set("style", "display:none");
		if (!fileFilter.empty()) {
			input.set("accept", emscripten::val(fileFilter.data()));
		}
		if (multiple) {
			input.set("multiple", "multiple");
		}

		// http://perfectionkills.com/detecting-event-support-without-browser-sniffing
		_supportsCancel = false;
		if (emscripten::val("oncancel").in(input)) {
			_supportsCancel = true;
		} else {
			input.set("oncancel", emscripten::val("return;"));
			if (input["oncancel"].typeOf().as<std::string>() == "function") {
				_supportsCancel = true;
			}
		}

		input.set("onchange", emscripten::val::module_property("jsReadFiles"));
		input.set("oncancel", emscripten::val::module_property("jsCancelReadFiles"));
		input.set("data-callbackContext", emscripten::val(std::size_t(this)));

		auto body = document["body"];
		body.call<void>("appendChild", input);
		input.call<void>("click");
		body.call<void>("removeChild", input);
	}

	void EmscriptenFilePicker::FetchDirectoryAsync(Containers::Function<void(Containers::ArrayView<EmscriptenFileStream>)>&& callback)
	{
		if (_activeCallback) {
			// Another callback is already active, cancel this one
			callback({});
			return;
		}

		auto window = emscripten::val::global("window");
		if (emscripten::val("showDirectoryPicker").in(window)) {
			_supportsCancel = true;

			auto dirHandle = window.call<emscripten::val>("showDirectoryPicker").await();
			auto values = dirHandle.call<emscripten::val>("values"); // Async iterator
			auto prevSize = _files.size();
			while (true) {
				auto entry = values.call<emscripten::val>("next").await();
				if (entry["done"].as<bool>()) {
					break; // End of async iteration
				}

				auto entryValue = entry["value"];
				if (entryValue["kind"].as<std::string>() == "file") {
					_files.emplace_back(entryValue.call<emscripten::val>("getFile").await());
				}
			}

			// Exclude previously added files
			callback(arrayView(_files).exceptPrefix(prevSize));
			return;
		}

		_activeCallback = Death::move(callback);

		// Create file input element which will display a native file dialog
		auto document = emscripten::val::global("document");
		auto input = document.call<emscripten::val>("createElement", emscripten::val("input"));
		input.set("type", "file");
		input.set("style", "display:none");
		input.set("multiple", "multiple");
		input.set("webkitdirectory", "webkitdirectory");

		// http://perfectionkills.com/detecting-event-support-without-browser-sniffing
		_supportsCancel = false;
		if (emscripten::val("oncancel").in(input)) {
			_supportsCancel = true;
		} else {
			input.set("oncancel", emscripten::val("return;"));
			if (input["oncancel"].typeOf().as<std::string>() == "function") {
				_supportsCancel = true;
			}
		}

		input.set("onchange", emscripten::val::module_property("jsReadFiles"));
		input.set("oncancel", emscripten::val::module_property("jsCancelReadFiles"));
		input.set("data-callbackContext", emscripten::val(std::size_t(this)));

		auto body = document["body"];
		body.call<void>("appendChild", input);
		input.call<void>("click");
		body.call<void>("removeChild", input);
	}

	void EmscriptenFilePicker::SaveFileAsync(Containers::ArrayView<char> bytesToSave, Containers::StringView filenameHint)
	{
		auto Blob = emscripten::val::global("Blob");
		auto contentArray = emscripten::val::array();
		emscripten::val content{emscripten::typed_memory_view(bytesToSave.size(), bytesToSave.data())};
		contentArray.call<void>("push", content);
		auto type = emscripten::val::object();
		type.set("type", "application/octet-stream");
		auto fileBlob = Blob.new_(contentArray, type);

		// Create Blob download link
		auto document = emscripten::val::global("document");
		auto link = document.call<emscripten::val>("createElement", emscripten::val("a"));
		link.set("download", emscripten::val(filenameHint.data()));
		auto window = emscripten::val::global("window");
		auto URL = window["URL"];
		link.set("href", URL.call<emscripten::val>("createObjectURL", fileBlob));
		link.set("style", "display:none");

		auto body = document["body"];
		body.call<void>("appendChild", link);
		link.call<void>("click");
		body.call<void>("removeChild", link);
	}

	void EmscriptenFilePicker::jsReadFiles(emscripten::val event)
	{
		auto target = event["target"];
		auto files = target["files"];
		auto fileCount = files["length"].as<std::int32_t>();

		EmscriptenFilePicker* context = reinterpret_cast<EmscriptenFilePicker*>(target["data-callbackContext"].as<std::size_t>());
		auto prevSize = context->_files.size();
		context->_files.reserve(prevSize + fileCount);

		for (std::int32_t i = 0; i < fileCount; i++) {
			emscripten::val file = files[i];
			context->_files.emplace_back(file);
		}

		if (context->_activeCallback) {
			context->_activeCallback(arrayView(context->_files).exceptPrefix(prevSize));
			context->_activeCallback = {};
		}
	}

	void EmscriptenFilePicker::jsCancelReadFiles(emscripten::val event)
	{
		auto target = event["target"];
		EmscriptenFilePicker* context = reinterpret_cast<EmscriptenFilePicker*>(target["data-callbackContext"].as<std::size_t>());

		if (context->_activeCallback) {
			context->_activeCallback({});
			context->_activeCallback = {};
		}
	}

	EMSCRIPTEN_BINDINGS(localfileaccess) {
		emscripten::function("jsReadFiles", &EmscriptenFilePicker::jsReadFiles);
		emscripten::function("jsCancelReadFiles", &EmscriptenFilePicker::jsCancelReadFiles);
	}

}}

#endif