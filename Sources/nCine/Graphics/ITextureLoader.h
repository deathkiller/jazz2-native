#pragma once

#include "TextureFormat.h"
#include "../Primitives/Vector2.h"

#include <memory>

#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace nCine
{
	/**
		@brief Texture loader interface class
		
		Common base for all texture file loaders. Decodes a texture file into raw pixel data together with
		its dimensions, pixel format and MIP map chain, ready to be uploaded to the GPU by @ref Texture.
		Concrete subclasses handle individual file formats and are instantiated through the @ref createFromFile()
		factory according to the file extension.
	*/
	class ITextureLoader
	{
	public:
		virtual ~ITextureLoader() { }

		/**
		 * @brief Returns `true` if the texture has been correctly loaded
		 */
		inline bool hasLoaded() const {
			return hasLoaded_;
		}

		/**
		 * @brief Returns texture width in pixels
		 */
		inline std::int32_t width() const {
			return width_;
		}
		/**
		 * @brief Returns texture height in pixels
		 */
		inline std::int32_t height() const {
			return height_;
		}
		/**
		 * @brief Returns texture size as a `Vector2i`
		 */
		inline Vector2i size() const {
			return Vector2i(width_, height_);
		}
		/**
		 * @brief Returns the number of MIP maps stored in the texture file
		 */
		inline std::int32_t mipMapCount() const {
			return mipMapCount_;
		}
		/**
		 * @brief Returns texture data size in bytes
		 */
		inline std::uint32_t dataSize() const {
			return dataSize_;
		}
		/**
		 * @brief Returns the texture data size in bytes for the specified MIP map level
		 *
		 * @param mipMapLevel  Zero-based MIP map level
		 */
		std::int32_t dataSize(std::uint32_t mipMapLevel) const;
		/**
		 * @brief Returns the texture format object
		 */
		inline const TextureFormat& texFormat() const {
			return texFormat_;
		}
		/**
		 * @brief Returns the pointer to pixel data
		 */
		inline const std::uint8_t* pixels() const {
			return pixels_.get();
		}
		/**
		 * @brief Returns the pointer to pixel data for the specified MIP map level
		 *
		 * @param mipMapLevel  Zero-based MIP map level
		 */
		const std::uint8_t* pixels(std::uint32_t mipMapLevel) const;

		//static std::unique_ptr<ITextureLoader> createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize);
		/**
		 * @brief Returns the proper texture loader according to the file extension
		 *
		 * @param filename  Path of the texture file to load
		 */
		static std::unique_ptr<ITextureLoader> createFromFile(const Death::Containers::StringView filename);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Whether the loading process has been successful */
		bool hasLoaded_;
		/** @brief Texture file handle */
		std::unique_ptr<Death::IO::Stream> fileHandle_;

		std::int32_t width_;
		std::int32_t height_;
		std::int32_t headerSize_;
		std::uint32_t dataSize_;
		std::int32_t mipMapCount_;
		std::unique_ptr<std::uint32_t[]> mipDataOffsets_;
		std::unique_ptr<std::uint32_t[]> mipDataSizes_;
		TextureFormat texFormat_;
		std::unique_ptr<std::uint8_t[]> pixels_;
#endif

		/** @brief An empty constructor only used by `TextureLoaderRaw` */
		ITextureLoader();
		explicit ITextureLoader(std::unique_ptr<Death::IO::Stream> fileHandle);

		static std::unique_ptr<ITextureLoader> createLoader(std::unique_ptr<Death::IO::Stream> fileHandle, const Death::Containers::StringView path);
		/**
		 * @brief Loads pixel data from a texture file holding either compressed or uncompressed data
		 *
		 * @param internalFormat  OpenGL internal format describing the decoded pixels
		 */
		void loadPixels(GLenum internalFormat);
		/**
		 * @brief Loads pixel data from a texture file holding either compressed or uncompressed data, overriding pixel type
		 *
		 * @param internalFormat  OpenGL internal format describing the decoded pixels
		 * @param type            OpenGL pixel type that overrides the format default
		 */
		void loadPixels(GLenum internalFormat, GLenum type);
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	/** @brief A loader created when the texture file extension is not recognized */
	class InvalidTextureLoader : public ITextureLoader
	{
	public:
		explicit InvalidTextureLoader(std::unique_ptr<Death::IO::Stream> fileHandle)
			: ITextureLoader(std::move(fileHandle)) { }
	};
#endif
}
