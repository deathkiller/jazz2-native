#pragma once

#ifdef __ANDROID__

#include "IFileStream.h"
#include <android_native_app_glue.h> // for android_app
#include <android/asset_manager.h>

namespace nCine
{

	/// The class dealing with Android asset files
	class AssetFile : public IFileStream
	{
	public:
		static constexpr char Prefix[] = "asset::";

		/// Constructs an asset file object
		/*! \param filename File name including path relative to the assets directory */
		explicit AssetFile(const char* filename);
		~AssetFile() override;

		/// Tries to open the asset file
		void Open(FileAccessMode mode) override;
		/// Closes the asset file
		void Close() override;
		long int Seek(long int offset, SeekOrigin origin) const override;
		long int GetPosition() const override;
		unsigned long int Read(void* buffer, unsigned long int bytes) const override;
		unsigned long int Write(void* buffer, unsigned long int bytes) override {
			return 0;
		}

		bool isOpened() const override;

		/// Sets the global pointer to the AAssetManager
		static void initAssetManager(struct android_app* state) {
			assetManager_ = state->activity->assetManager;
		}

		/// Returns the path of an Android asset without the prefix
		static const char* assetPath(const char* path);

		/// Checks if an asset path exists as a file or as a directory and can be opened
		static bool tryOpen(const char* path);
		/// Checks if an asset path exists and can be opened as a file
		static bool tryOpenFile(const char* path);
		/// Checks if an asset path exists and can be opened as a directory
		static bool tryOpenDirectory(const char* path);
		/// Returns the total size of the asset data
		static off_t length(const char* path);

		static AAssetDir* openDir(const char* dirName);
		static void closeDir(AAssetDir* assetDir);
		static void rewindDir(AAssetDir* assetDir);
		static const char* nextFileName(AAssetDir* assetDir);

	private:
		static AAssetManager* assetManager_;
		AAsset* asset_;
		unsigned long int startOffset_;

		/// Opens the file with `AAsset_openFileDescriptor()`
		void OpenFD(FileAccessMode mode);
		/// Opens the file with `AAssetManager_open()` only
		void OpenAsset(FileAccessMode mode);
	};

}

#endif
