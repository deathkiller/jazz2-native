#pragma once

#include <Common.h>

#if defined(DEATH_TARGET_EMSCRIPTEN)

#include <memory>
#include <string>

namespace nCine
{
	/// The class dealing with opening and saving a local file on Emscripten
	class EmscriptenLocalFile
	{
	public:
		using LoadedCallbackType = void(const EmscriptenLocalFile& localFile, void* userData);

		EmscriptenLocalFile()
			: fileSize_(0), /*filename_(MaxFilenameLength),*/ loading_(false), loadedCallback_(nullptr) {}

		/// Opens a dialog in the browser to choose a file to load
		void Load();
		/// Opens a filtered dialog in the browser to choose a file to load
		void Load(const char* fileFilter);
		/// Saves a file from the browser to the local machine
		void Save(const char* filename);
		/// Reads a certain amount of bytes from the file to a buffer
		/*! \return Number of bytes read */
		unsigned long int Read(void* buffer, unsigned long int bytes) const;
		/// Writes a certain amount of bytes from a buffer to the file
		/*! \return Number of bytes written */
		unsigned long int Write(void* buffer, unsigned long int bytes);

		/// Sets the callback to be invoked when loading is complete
		void SetLoadedCallback(LoadedCallbackType* loadedCallback, void* userData = nullptr);

		/// Returns a read-only pointer to the internal file buffer
		inline const char* Data() const {
			return fileBuffer_.get();
		}
		/// Returns the file size in bytes
		inline long int GetSize() const {
			return fileSize_;
		}
		/// Returns the file name
		inline const char* GetFilename() const {
			return filename_.data();
		}
		/// Returns true if loading is in progress
		inline bool IsLoading() const {
			return loading_;
		}

	private:
		/// Maximum number of characters for a file name
		static constexpr unsigned int MaxFilenameLength = 256;

		/// The memory buffer that stores file contents
		std::unique_ptr<char[]> fileBuffer_;
		/// File size in bytes
		long int fileSize_;
		/// File name
		std::string filename_;
		/// The flag indicating if the JavaScript loading callback has not yet returned
		bool loading_;

		LoadedCallbackType* loadedCallback_;
		void* userData_;

		static void FileDataCallback(void* context, char* contentPointer, size_t contentSize, const char* fileName);
		static void LoadingCallback(void* context);
	};

}

#endif