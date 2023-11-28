#pragma once

#include "../CommonBase.h"

#if defined(DEATH_TARGET_ANDROID)

#include "Stream.h"
#include "../Containers/StringView.h"

#include <android_native_app_glue.h>	// For android_app
#include <android/asset_manager.h>

namespace Death::IO
{
	using Containers::Literals::operator"" _s;

	/** @brief The class dealing with Android asset files */
	class AndroidAssetStream : public Stream
	{
	public:
		static constexpr Containers::StringView Prefix = "asset::"_s;

		explicit AndroidAssetStream(const Containers::String& path, FileAccessMode mode);
		~AndroidAssetStream() override;

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;

		std::int32_t Write(const void* buffer, std::int32_t bytes) override {
			return 0;
		}

		bool IsValid() const override;

		void SetCloseOnDestruction(bool shouldCloseOnDestruction) override {
			_shouldCloseOnDestruction = shouldCloseOnDestruction;
		}

		/** @brief Returns file descriptor */
		DEATH_ALWAYS_INLINE std::int32_t GetFileDescriptor() const {
			return _fileDescriptor;
		}

		/** @brief Sets the global pointer to the AAssetManager */
		static void InitializeAssetManager(struct android_app* state);

		/** @brief Returns the path of an Android asset without the prefix */
		static const char* TryGetAssetPath(const char* path);

		/** @brief Returns the internal data path for the application */
		static const char* GetInternalDataPath() {
			return _internalDataPath;
		}

		/** @brief Checks if an asset path exists as a file or as a directory and can be opened */
		static bool TryOpen(const char* path);
		/** @brief Checks if an asset path exists and can be opened as a file */
		static bool TryOpenFile(const char* path);
		/** @brief Checks if an asset path exists and can be opened as a directory */
		static bool TryOpenDirectory(const char* path);
		/** @brief Returns the total size of the asset data */
		static off_t GetLength(const char* path);

		static AAssetDir* OpenDirectory(const char* dirName);
		static void CloseDirectory(AAssetDir* assetDir);
		static void RewindDirectory(AAssetDir* assetDir);
		static const char* GetNextFileName(AAssetDir* assetDir);

	private:
		static AAssetManager* _assetManager;
		static const char* _internalDataPath;

		AAsset* _asset;
		std::int32_t _fileDescriptor;
		unsigned long int _startOffset;
		bool _shouldCloseOnDestruction;

		void OpenDescriptor(FileAccessMode mode);
		void OpenAsset(FileAccessMode mode);
	};
}

#endif
