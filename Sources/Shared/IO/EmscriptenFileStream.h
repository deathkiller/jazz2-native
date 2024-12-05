#pragma once

#include "../Common.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)

#include <emscripten/val.h>

#include "Stream.h"
#include "../Containers/Function.h"
#include "../Containers/StringView.h"
#include "../Containers/SmallVector.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	class EmscriptenFileStream : public Stream
	{
	public:
		EmscriptenFileStream(emscripten::val handle, std::int32_t bufferSize = 4096);

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;

		std::int64_t GetSize() const override;
		Containers::StringView GetName() const;

	private:
		emscripten::val _handle;
		mutable std::string _name;
		mutable std::int32_t _size;
		std::int32_t _filePos;
		std::int32_t _readPos;
		std::int32_t _readLength;
		std::int32_t _bufferLength;
		std::unique_ptr<std::uint8_t[]> _buffer;

		void InitializeBuffer();

		std::int64_t SeekInternal(std::int64_t offset, SeekOrigin origin);
		std::int32_t ReadInternal(std::uint8_t* destination, std::int32_t bytesToRead);
	};

	class EmscriptenFilePicker
	{
	public:
		EmscriptenFilePicker();

		bool IsCancelSupported() const;

		void FetchFilesAsync(Containers::StringView fileFilter, bool multiple, Containers::Function<void(Containers::ArrayView<EmscriptenFileStream>)>&& callback);
		void FetchDirectoryAsync(Containers::Function<void(Containers::ArrayView<EmscriptenFileStream>)>&& callback);

		static void SaveFileAsync(Containers::ArrayView<char> bytesToSave, Containers::StringView filenameHint = {});

		/* Internal JavaScript callbacks */
		static void jsReadFiles(emscripten::val event);
		static void jsCancelReadFiles(emscripten::val event);

	private:
		Containers::SmallVector<EmscriptenFileStream> _files;
		Containers::Function<void(Containers::ArrayView<EmscriptenFileStream>)> _activeCallback;
		bool _supportsCancel;
	};

}}

#endif