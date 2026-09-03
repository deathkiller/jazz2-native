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
		@brief Image data uploaded to the GPU and sampled by shaders

		Wraps a backend texture object. It can be created empty with a given format and size, loaded
		from an image file or filled from raw texels, and configured with filtering, wrapping and
		channel swizzling before being bound by a material.

		@ref SamplerFilter, @ref SamplerWrapping and @ref SwizzleChannel are declared in @ref RhiTypes.h.
	*/
	class Texture : public Object
	{
		friend class Material;
		friend class Viewport;

	public:
		/** @brief Pixel formats for an empty texture */
		using Format = PixelFormat;

		/**
			@brief Pixel format the engine's opaque color render targets are created with

			`RHI_USE_FB16` (the `NCINE_RHI_USE_FB16` option) is a performance switch, so on the OpenGL family
			backend it covers every color surface it can rather than only the presented one: the scene, blur
			and rescale targets become RGB565, which halves both their memory and the bandwidth each
			post-processing pass reads and writes them with.

			Targets that carry alpha keep 8 bits per channel regardless - the only 16-bit RGBA layout is
			RGBA4, and 16 levels per channel band visibly in the UI overlay and in the light falloff. The
			software rasterizer also keeps 4-byte RGBA render targets: its inner loops work on RGBA8 either
			way, so a packed target would only add a pack/unpack per span, which is why its 16-bit mode stays
			confined to the screen buffer - there it halves the per-frame upload and pays off.
		*/
#if defined(RHI_USE_FB16) && defined(WITH_RHI_GL)
		static constexpr Format ColorTargetFormat = Format::RGB565;
#else
		static constexpr Format ColorTargetFormat = Format::RGB8;
#endif

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

#if defined(RHI_CAP_STREAMING_TEXTURES) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Returns a writable pointer to the texture's own storage, for content rebuilt every frame

			Only backends that advertise @cpp RHI_CAP_STREAMING_TEXTURES @ce have this, and even there it is
			only answered for the formats the hardware stores verbatim; it returns `nullptr` otherwise, and the
			caller has to go through @ref LoadFromTexels() instead. @p strideBytes receives the distance between
			rows, which is not @cpp width * bytesPerPixel @ce when the storage is padded.

			What is written is not mirrored into the host copy the texture keeps, so a texture written this way
			must be written whole, every time, and never read back.
		*/
		void* MapStreamingTexels(std::int32_t& strideBytes);
#endif

		/** @brief Saves all texels of the first MIP level in raw format to a memory buffer */
		bool SaveToMemory(std::uint8_t* bufferPtr);
		/** @brief Saves all texels of the specified MIP level in raw format to a memory buffer */
		bool SaveToMemory(std::uint8_t* bufferPtr, std::int32_t level);

		/** @brief Returns the texture width */
		inline std::int32_t GetWidth() const {
			return _width;
		}
		/** @brief Returns the texture height */
		inline std::int32_t GetHeight() const {
			return _height;
		}
		/** @brief Returns the number of texture MIP map levels */
		inline std::int32_t GetMipMapLevels() const {
			return _mipMapLevels;
		}
		/** @brief Returns the texture size */
		inline Vector2i GetSize() const {
			return Vector2i(_width, _height);
		}
		/** @brief Returns the texture rectangle */
		inline Recti GetRect() const {
			return Recti(0, 0, _width, _height);
		}

		/** @brief Returns `true` if the texture holds compressed data */
		inline bool IsCompressed() const {
			return _isCompressed;
		}
		/** @brief Returns the number of color channels */
		std::uint32_t GetChannelCount() const;
		/** @brief Returns the amount of video memory needed to load the texture */
		inline std::uint32_t GetDataSize() const {
			return _dataSize;
		}

		/** @brief Returns the texture filtering for minification */
		inline SamplerFilter GetMinFiltering() const {
			return _minFiltering;
		}
		/** @brief Returns the texture filtering for magnification */
		inline SamplerFilter GetMagFiltering() const {
			return _magFiltering;
		}
		/** @brief Returns the texture wrapping for both `s` and `t` coordinates */
		inline SamplerWrapping GetWrap() const {
			return _wrapMode;
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
		 *
		 * Two profiles cannot remap channels at all: OpenGL|ES 2.0 has no `GL_TEXTURE_SWIZZLE_*`,
		 * and WebGL 2.0 leaves them out as well even though the ES 3.0 it is based on has them.
		 * Both ignore the call and reach the same sampling by resolving @ref PixelFormat::RG8 to
		 * `LUMINANCE_ALPHA`, which samples as `(L,L,L,A)` on its own, so shaders need no
		 * per-profile handling. Two things follow for callers: a *new* non-identity swizzle would
		 * silently do nothing there, and `RG8` cannot back a render target on those profiles,
		 * because the substitute is not color-renderable.
		 */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);

		/**
			@brief Declares that the texture will not be uploaded to or read back again

			Some backends have to keep the decoded texels in main memory after the upload, because what the
			hardware samples is a padded and swizzled copy built from them - so a texture costs twice its
			size until one of the two can go. Calling this once a texture is complete lets such a backend
			give the texels up; elsewhere it does nothing. Content that is still being assembled, streamed
			or read back must not call it - a later @ref LoadFromTexels() would be dropped (with a warning).
		*/
		void ReleaseHostCopy();

		/** @brief Sets the backend object label for the texture, for debugging */
		void SetTextureLabel(const char* label);

		/** @brief Returns the opaque user data pointer used as ImGui's `ImTextureID` */
		void* GetGuiTexId() const;

		inline static ObjectType sType() {
			return ObjectType::Texture;
		}

	private:
		std::unique_ptr<RHI::Texture> _rhiTexture;
		std::int32_t _width;
		std::int32_t _height;
		std::int32_t _mipMapLevels;
		bool _isCompressed;
		Format _format;
		std::uint32_t _dataSize;

		SamplerFilter _minFiltering;
		SamplerFilter _magFiltering;
		SamplerWrapping _wrapMode;

		/** @brief Initializes an empty texture by creating storage for it */
		void Initialize(const ITextureLoader& texLoader);
		/** @brief Loads the data into a previously initialized texture */
		void Load(const ITextureLoader& texLoader);
	};

}
