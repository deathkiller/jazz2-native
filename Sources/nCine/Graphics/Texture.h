#pragma once

#include "RHI/RhiTypes.h"
#include "RHI/RhiFwd.h"
#include "../Base/Object.h"
#include "../Primitives/Rect.h"
#include "../Primitives/Color.h"
#include "../Primitives/Colorf.h"

#include <memory>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class ITextureLoader;

	/**
		@brief Texture minification and magnification filtering modes
	*/
	enum class SamplerFilter
	{
		Unknown,

		Nearest,
		Linear,
		NearestMipmapNearest,
		LinearMipmapNearest,
		NearestMipmapLinear,
		LinearMipmapLinear
	};

	/**
		@brief Texture coordinate wrapping modes
	*/
	enum class SamplerWrapping
	{
		Unknown,

		ClampToEdge,
		MirroredRepeat,
		Repeat
	};

	/**
		@brief Source for a sampled texture channel (see @ref Texture::SetSwizzle())
	*/
	enum class SwizzleChannel
	{
		Red,
		Green,
		Blue,
		Alpha,
		Zero,
		One
	};

	/**
		@brief Image data uploaded to the GPU and sampled by shaders
		
		Wraps an OpenGL texture object. It can be created empty with a given format and size, loaded
		from an image file or filled from raw texels, and configured with filtering, wrapping and
		channel swizzling before being bound by a material.
	*/
	class Texture : public Object
	{
		friend class Material;
		friend class Viewport;

	public:
		/** @brief Pixel formats for an empty texture */
		using Format = PixelFormat;

		/** @brief Creates an OpenGL texture name */
		Texture();

		/** @brief Creates an empty texture with the specified format, MIP levels and size */
		Texture(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height);
		/** @brief Creates an empty texture with the specified format, MIP levels and size given as a vector */
		Texture(const char* name, Format format, std::int32_t mipMapCount, Vector2i size);
		/** @brief Creates an empty texture with the specified format and size */
		Texture(const char* name, Format format, std::int32_t width, std::int32_t height);
		/** @brief Creates an empty texture with the specified format and size given as a vector */
		Texture(const char* name, Format format, Vector2i size);

		/** @brief Creates a texture from an image file */
		explicit Texture(StringView filename);

		~Texture() override;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&&);
		Texture& operator=(Texture&&);

		/** @brief Initializes an empty texture with the specified format, MIP levels and size */
		void Init(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height);
		/** @brief Initializes an empty texture with the specified format, MIP levels and size given as a vector */
		void Init(const char* name, Format format, std::int32_t mipMapCount, Vector2i size);
		/** @brief Initializes an empty texture with the specified format and size */
		void Init(const char* name, Format format, std::int32_t width, std::int32_t height);
		/** @brief Initializes an empty texture with the specified format and size given as a vector */
		void Init(const char* name, Format format, Vector2i size);

		//bool loadFromMemory(const std::uint8_t* bufferPtr, unsigned long int bufferSize);
		/** @brief Loads the texture from an image file */
		bool LoadFromFile(StringView filename);

		/** @brief Loads all texels in raw format from a memory buffer into the first MIP level */
		bool LoadFromTexels(const std::uint8_t* bufferPtr);
		/** @brief Loads texels in raw format from a memory buffer into a sub-region of the first MIP level */
		bool LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/** @brief Loads texels in raw format from a memory buffer into a rectangular sub-region of the first MIP level */
		bool LoadFromTexels(const std::uint8_t* bufferPtr, Recti region);
		/** @brief Loads texels in raw format from a memory buffer into a sub-region of the specified MIP level */
		bool LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/** @brief Loads texels in raw format from a memory buffer into a rectangular sub-region of the specified MIP level */
		bool LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, Recti region);

		/** @brief Saves all texels of the first MIP level in raw format to a memory buffer */
		bool SaveToMemory(std::uint8_t* bufferPtr);
		/** @brief Saves all texels of the specified MIP level in raw format to a memory buffer */
		bool SaveToMemory(std::uint8_t* bufferPtr, std::int32_t level);

		/** @brief Returns the texture width */
		inline std::int32_t GetWidth() const {
			return width_;
		}
		/** @brief Returns the texture height */
		inline std::int32_t GetHeight() const {
			return height_;
		}
		/** @brief Returns the number of texture MIP map levels */
		inline std::int32_t GetMipMapLevels() const {
			return mipMapLevels_;
		}
		/** @brief Returns the texture size */
		inline Vector2i GetSize() const {
			return Vector2i(width_, height_);
		}
		/** @brief Returns the texture rectangle */
		inline Recti GetRect() const {
			return Recti(0, 0, width_, height_);
		}

		/** @brief Returns `true` if the texture holds compressed data */
		inline bool IsCompressed() const {
			return isCompressed_;
		}
		/** @brief Returns the number of color channels */
		std::uint32_t GetChannelCount() const;
		/** @brief Returns the amount of video memory needed to load the texture */
		inline std::uint32_t GetDataSize() const {
			return dataSize_;
		}

		/** @brief Returns the texture filtering for minification */
		inline SamplerFilter GetMinFiltering() const {
			return minFiltering_;
		}
		/** @brief Returns the texture filtering for magnification */
		inline SamplerFilter GetMagFiltering() const {
			return magFiltering_;
		}
		/** @brief Returns the texture wrapping for both `s` and `t` coordinates */
		inline SamplerWrapping GetWrap() const {
			return wrapMode_;
		}
		/** @brief Sets the texture filtering for minification */
		void SetMinFiltering(SamplerFilter filter);
		/** @brief Sets the texture filtering for magnification */
		void SetMagFiltering(SamplerFilter filter);
		/** @brief Sets the texture wrapping for both `s` and `t` coordinates */
		void SetWrap(SamplerWrapping wrapMode);

		/**
		 * @brief Remaps the channels returned when the texture is sampled
		 *
		 * The default mapping is `Red, Green, Blue, Alpha`. Swizzling lets a reduced-channel
		 * texture (e.g., an RG8 sprite holding a palette index plus alpha) be sampled as if it
		 * were RGBA8 in the shader.
		 */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);

		/** @brief Sets the OpenGL object label for the texture */
		void SetGLTextureLabel(const char* label);

		/** @brief Returns the opaque user data pointer used as ImGui's `ImTextureID` */
		void* GetGuiTexId() const;

		inline static ObjectType sType() {
			return ObjectType::Texture;
		}

	private:
		std::unique_ptr<Rhi::Texture> glTexture_;
		std::int32_t width_;
		std::int32_t height_;
		std::int32_t mipMapLevels_;
		bool isCompressed_;
		Format format_;
		std::uint32_t dataSize_;

		SamplerFilter minFiltering_;
		SamplerFilter magFiltering_;
		SamplerWrapping wrapMode_;

		/** @brief Initializes an empty texture by creating storage for it */
		void Initialize(const ITextureLoader& texLoader);
		/** @brief Loads the data into a previously initialized texture */
		void Load(const ITextureLoader& texLoader);
	};

}
