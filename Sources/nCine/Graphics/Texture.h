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

	/// Texture class
	class Texture : public Object
	{
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
		Texture(const char* name, Format format, int mipMapCount, int width, int height);
		/// Creates an empty texture with the specified format, MIP levels, and size using a vector
		Texture(const char* name, Format format, int mipMapCount, Vector2i size);
		/// Creates an empty texture with the specified format and size
		Texture(const char* name, Format format, int width, int height);
		/// Creates an empty texture with the specified format and size using a vector
		Texture(const char* name, Format format, Vector2i size);

		/// Creates a texture from a named memory buffer
		Texture(const unsigned char* bufferPtr, unsigned long int bufferSize);
		/// Creates a texture from an image file
		explicit Texture(const StringView& filename);

		~Texture() override;

		/// Default move constructor
		Texture(Texture&&);
		/// Default move assignment operator
		Texture& operator=(Texture&&);

		/// Initializes an empty texture with the specified format, MIP levels, and size
		void init(const char* name, Format format, int mipMapCount, int width, int height);
		/// Initializes an empty texture with the specified format, MIP levels, and size using a vector
		void init(const char* name, Format format, int mipMapCount, Vector2i size);
		/// Initializes an empty texture with the specified format and size
		void init(const char* name, Format format, int width, int height);
		/// Initializes an empty texture with the specified format and size using a vector
		void init(const char* name, Format format, Vector2i size);

		bool loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize);
		bool loadFromFile(const StringView& filename);

		/// Loads all texture texels in raw format from a memory buffer in the first mip level
		bool loadFromTexels(const unsigned char* bufferPtr);
		/// Loads texels in raw format from a memory buffer to a texture sub-region in the first mip level
		bool loadFromTexels(const unsigned char* bufferPtr, unsigned int x, unsigned int y, unsigned int width, unsigned int height);
		/// Loads texels in raw format from a memory buffer to a texture sub-region with a rectangle in the first mip level
		bool loadFromTexels(const unsigned char* bufferPtr, Recti region);
		/// Loads texels in raw format from a memory buffer to a specific texture mip level and sub-region
		bool loadFromTexels(const unsigned char* bufferPtr, unsigned int level, unsigned int x, unsigned int y, unsigned int width, unsigned int height);
		/// Loads texels in raw format from a memory buffer to a specific texture mip level and sub-region with a rectangle
		bool loadFromTexels(const unsigned char* bufferPtr, unsigned int level, Recti region);

		/// Saves all texture texels in the first mip level in raw format to a memory buffer
		bool saveToMemory(unsigned char* bufferPtr);
		/// Saves all texture texels in the specified texture mip level in raw format to a memory buffer
		bool saveToMemory(unsigned char* bufferPtr, unsigned int level);

		/// Returns texture width
		inline int width() const {
			return width_;
		}
		/// Returns texture height
		inline int height() const {
			return height_;
		}
		/// Returns texture MIP map levels
		inline int mipMapLevels() const {
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

		/// Returns true if the texture holds compressed data
		inline bool isCompressed() const {
			return isCompressed_;
		}
		/// Returns the number of color channels
		unsigned int numChannels() const;
		/// Returns the amount of video memory needed to load the texture
		inline unsigned long dataSize() const {
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
		int width_;
		int height_;
		int mipMapLevels_;
		bool isCompressed_;
		Format format_;
		unsigned long dataSize_;

		SamplerFilter minFiltering_;
		SamplerFilter magFiltering_;
		SamplerWrapping wrapMode_;

		/// Deleted copy constructor
		Texture(const Texture&) = delete;
		/// Deleted assignment operator
		Texture& operator=(const Texture&) = delete;

		/// Initialize an empty texture by creating storage for it
		void initialize(const ITextureLoader& texLoader);
		/// Loads the data in a previously initialized texture
		void load(const ITextureLoader& texLoader);

		friend class Material;
		friend class Viewport;
	};

}
