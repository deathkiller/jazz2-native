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
		using FileDataCallbackType = void(void* context, std::unique_ptr<char[]> data, size_t length, const StringView& name);
		using FileCountCallbackType = void(void* context, int fileCount);

		static void Load(const StringView& fileFilter, bool multiple, FileDataCallbackType fileDataCallback, FileCountCallbackType fileCountCallback, void* userData);

	private:
		/// Deleted copy constructor
		EmscriptenLocalFile(const EmscriptenLocalFile&) = delete;
		/// Deleted assignment operator
		EmscriptenLocalFile& operator=(const EmscriptenLocalFile&) = delete;
	};
}

#endif