#pragma once

#include "../CommonBase.h"

#if defined(DEATH_TARGET_ANDROID) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Stream.h"
#include "FileAccess.h"
#include "../Containers/String.h"
#include "../Containers/StringView.h"

#include <android_native_app_glue.h>	// for android_app
#include <android/asset_manager.h>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	using Containers::Literals::operator"" _s;

	/**
		@brief Read-only streaming directly from Android asset files

		@partialsupport Available only on @ref DEATH_TARGET_ANDROID "Android" platform.
	*/
	class AndroidAssetStream : public Stream
	{
	public:
		/** @{ @name Constants */

		/** @brief Path prefix of Android asset files */
		static constexpr Containers::StringView Prefix = "asset:"_s;

		/** @} */

		AndroidAssetStream(Containers::StringView path, FileAccess mode);
		AndroidAssetStream(Containers::String&& path, FileAccess mode);
		~AndroidAssetStream() override;

		AndroidAssetStream(const AndroidAssetStream&) = delete;
		AndroidAssetStream& operator=(const AndroidAssetStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;	
		std::int64_t GetSize() const override;

		/** @brief Returns file path */
		Containers::StringView GetPath() const;

#if defined(DEATH_USE_FILE_DESCRIPTORS)
		/** @brief Returns file descriptor */
		DEATH_ALWAYS_INLINE std::int32_t GetFileDescriptor() const {
			return _fileDescriptor;
		}
#endif

		/** @brief Sets the global pointer to the AAssetManager */
		static void InitializeAssetManager(struct android_app* state);

		/** @brief Returns the path of an Android asset without the prefix */
		static Containers::StringView TryGetAssetPath(const Containers::StringView path);

		/** @brief Returns path to the application's internal data directory */
		static const char* GetInternalDataPath();
		/** @brief Returns path to the application's external (removable/mountable) data directory */
		static const char* GetExternalDataPath();
		/** @brief Returns path to the directory containing the application's OBB files */
		static const char* GetObbPath();

		/** @brief Checks if an asset path exists as a file or as a directory and can be opened */
		static bool TryOpen(const Containers::StringView path);
		/** @brief Checks if an asset path exists and can be opened as a file */
		static bool TryOpenFile(const Containers::StringView path);
		/** @brief Checks if an asset path exists and can be opened as a directory */
		static bool TryOpenDirectory(const Containers::StringView path);
		/** @brief Returns the total size of the asset data */
		static std::int64_t GetFileSize(const Containers::StringView path);

#ifndef DOXYGEN_GENERATING_OUTPUT
		static AAssetDir* OpenDirectory(const Containers::StringView path);
		static void CloseDirectory(AAssetDir* assetDir);
		static void RewindDirectory(AAssetDir* assetDir);
		static const char* GetNextFileName(AAssetDir* assetDir);
#endif

	private:
		static ANativeActivity* _nativeActivity;

		Containers::String _path;
		std::int64_t _size;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		std::int32_t _fileDescriptor;
		unsigned long int _startOffset;
#else
		AAsset* _asset;
#endif

		void Open(FileAccess mode);
	};
}}

#endif
