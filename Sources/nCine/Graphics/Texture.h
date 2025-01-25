#pragma once

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
	class GLTexture;

	/// Texture filtering modes
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

	/// Texture wrap modes
	enum class SamplerWrapping
	{
		Unknown,

		ClampToEdge,
		MirroredRepeat,
		Repeat
	};

	/// Texture
	class Texture : public Object
	{
		friend class Material;
		friend class Viewport;

	public:
		/// Texture formats
		enum class Format
		{
			Unknown,

			R8,
			RG8,
			RGB8,
			RGBA8
		};

		/// Creates an OpenGL texture name
		Texture();

		/// Creates an empty texture with the specified format, MIP levels, and size
		Texture(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height);
		/// Creates an empty texture with the specified format, MIP levels, and size using a vector
		Texture(const char* name, Format format, std::int32_t mipMapCount, Vector2i size);
		/// Creates an empty texture with the specified format and size
		Texture(const char* name, Format format, std::int32_t width, std::int32_t height);
		/// Creates an empty texture with the specified format and size using a vector
		Texture(const char* name, Format format, Vector2i size);

		/// Creates a texture from an image file
		explicit Texture(StringView filename);

		~Texture() override;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&&);
		Texture& operator=(Texture&&);

		/// Initializes an empty texture with the specified format, MIP levels, and size
		void init(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height);
		/// Initializes an empty texture with the specified format, MIP levels, and size using a vector
		void init(const char* name, Format format, std::int32_t mipMapCount, Vector2i size);
		/// Initializes an empty texture with the specified format and size
		void init(const char* name, Format format, std::int32_t width, std::int32_t height);
		/// Initializes an empty texture with the specified format and size using a vector
		void init(const char* name, Format format, Vector2i size);

		//bool loadFromMemory(const std::uint8_t* bufferPtr, unsigned long int bufferSize);
		bool loadFromFile(StringView filename);

		/// Loads all texture texels in raw format from a memory buffer in the first mip level
		bool loadFromTexels(const std::uint8_t* bufferPtr);
		/// Loads texels in raw format from a memory buffer to a texture sub-region in the first mip level
		bool loadFromTexels(const std::uint8_t* bufferPtr, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/// Loads texels in raw format from a memory buffer to a texture sub-region with a rectangle in the first mip level
		bool loadFromTexels(const std::uint8_t* bufferPtr, Recti region);
		/// Loads texels in raw format from a memory buffer to a specific texture mip level and sub-region
		bool loadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/// Loads texels in raw format from a memory buffer to a specific texture mip level and sub-region with a rectangle
		bool loadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, Recti region);

		/// Saves all texture texels in the first mip level in raw format to a memory buffer
		bool saveToMemory(std::uint8_t* bufferPtr);
		/// Saves all texture texels in the specified texture mip level in raw format to a memory buffer
		bool saveToMemory(std::uint8_t* bufferPtr, std::int32_t level);

		/// Returns texture width
		inline std::int32_t width() const {
			return width_;
		}
		/// Returns texture height
		inline std::int32_t height() const {
			return height_;
		}
		/// Returns texture MIP map levels
		inline std::int32_t mipMapLevels() const {
			return mipMapLevels_;
		}
		/// Returns texture size
		inline Vector2i size() const {
			return Vector2i(width_, height_);
		}
		/// Returns texture rectangle
		inline Recti rect() const {
			return Recti(0, 0, width_, height_);
		}

		/// Returns `true` if the texture holds compressed data
		inline bool isCompressed() const {
			return isCompressed_;
		}
		/// Returns the number of color channels
		std::uint32_t numChannels() const;
		/// Returns the amount of video memory needed to load the texture
		inline std::uint32_t dataSize() const {
			return dataSize_;
		}

		/// Returns the texture filtering for minification
		inline SamplerFilter minFiltering() const {
			return minFiltering_;
		}
		/// Returns the texture filtering for magnification
		inline SamplerFilter magFiltering() const {
			return magFiltering_;
		}
		/// Returns texture wrap for both `s` and `t` coordinates
		inline SamplerWrapping wrap() const {
			return wrapMode_;
		}
		/// Sets the texture filtering for minification
		void setMinFiltering(SamplerFilter filter);
		/// Sets the texture filtering for magnification
		void setMagFiltering(SamplerFilter filter);
		/// Sets texture wrap for both `s` and `t` coordinates
		void setWrap(SamplerWrapping wrapMode);

		/// Sets the OpenGL object label for the texture
		void setGLTextureLabel(const char* label);

		/// Returns the user data opaque pointer for ImGui's `ImTextureID`
		void* guiTexId() const;

		inline static ObjectType sType() {
			return ObjectType::Texture;
		}

	private:
		std::unique_ptr<GLTexture> glTexture_;
		std::int32_t width_;
		std::int32_t height_;
		std::int32_t mipMapLevels_;
		bool isCompressed_;
		Format format_;
		std::uint32_t dataSize_;

		SamplerFilter minFiltering_;
		SamplerFilter magFiltering_;
		SamplerWrapping wrapMode_;

		/// Initialize an empty texture by creating storage for it
		void initialize(const ITextureLoader& texLoader);
		/// Loads the data in a previously initialized texture
		void load(const ITextureLoader& texLoader);
	};

}
