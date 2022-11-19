#pragma once

#include <Common.h>

#if defined(DEATH_TARGET_ANDROID)

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
		explicit AssetFile(const String& filename);
		~AssetFile() override;

		/// Tries to open the asset file
		void Open(FileAccessMode mode) override;
		/// Closes the asset file
		void Close() override;
		int32_t Seek(int32_t offset, SeekOrigin origin) const override;
		int32_t GetPosition() const override;
		uint32_t Read(void* buffer, uint32_t bytes) const override;
		uint32_t Write(const void* buffer, uint32_t bytes) override {
			return 0;
		}

		bool IsOpened() const override;

		/// Sets the global pointer to the AAssetManager
		static void InitializeAssetManager(struct android_app* state) {
			assetManager_ = state->activity->assetManager;
		}

		/// Returns the path of an Android asset without the prefix
		static const char* TryGetAssetPath(const char* path);

		/// Checks if an asset path exists as a file or as a directory and can be opened
		static bool TryOpen(const char* path);
		/// Checks if an asset path exists and can be opened as a file
		static bool TryOpenFile(const char* path);
		/// Checks if an asset path exists and can be opened as a directory
		static bool TryOpenDirectory(const char* path);
		/// Returns the total size of the asset data
		static off_t GetLength(const char* path);

		static AAssetDir* OpenDir(const char* dirName);
		static void CloseDir(AAssetDir* assetDir);
		static void RewindDir(AAssetDir* assetDir);
		static const char* GetNextFileName(AAssetDir* assetDir);

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
