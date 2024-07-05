#pragma once

#include <Common.h>

#if defined(DEATH_TARGET_EMSCRIPTEN)

#include <functional>
#include <memory>
#include <string>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// The class dealing with opening and saving a local file on Emscripten
	class EmscriptenLocalFile
	{
	public:
		using FileDataCallbackType = void(void* context, std::unique_ptr<char[]> data, std::size_t length, StringView name);
		using FileCountCallbackType = void(void* context, std::int32_t fileCount);

		EmscriptenLocalFile() = delete;
		~EmscriptenLocalFile() = delete;

		static void Load(StringView fileFilter, bool multiple, FileDataCallbackType fileDataCallback, FileCountCallbackType fileCountCallback, void* userData);
	};
}

#endif