#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "Texture.h"
#include "TextureLoaderRaw.h"
#include "RenderStatistics.h"
#include "../ServiceLocator.h"
#include "../tracy.h"

#include <Containers/String.h>

namespace nCine
{
	GLenum ncFormatToInternal(Texture::Format format)
	{
		switch (format) {
			case Texture::Format::R8:
				return GL_R8;
			case Texture::Format::RG8:
				return GL_RG8;
			case Texture::Format::RGB8:
				return GL_RGB8;
			case Texture::Format::RGBA8:
			default:
				return GL_RGBA8;
		}
	}

	GLenum ncFormatToNonInternal(Texture::Format format)
	{
		switch (format) {
			case Texture::Format::R8:
				return GL_RED;
			case Texture::Format::RG8:
				return GL_RG;
			case Texture::Format::RGB8:
				return GL_RGB;
			case Texture::Format::RGBA8:
			default:
				return GL_RGBA;
		}
	}

	Texture::Format internalFormatToNc(GLenum format)
	{
		switch (format) {
			case GL_R8:
				return Texture::Format::R8;
			case GL_RG8:
				return Texture::Format::RG8;
			case GL_RGB8:
				return Texture::Format::RGB8;
			case GL_RGBA8:
				return Texture::Format::RGBA8;
			default:
				return Texture::Format::Unknown;
		}
	}

	Texture::Texture()
		: Object(ObjectType::Texture), texture_(RHI::CreateTexture()), width_(0), height_(0),
			mipMapLevels_(0), isCompressed_(false), format_(Format::Unknown), dataSize_(0), minFiltering_(SamplerFilter::Nearest),
			magFiltering_(SamplerFilter::Nearest), wrapMode_(SamplerWrapping::ClampToEdge)
	{
	}

	/*! \note It specifies a pixel format and it is intended to be used with `loadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height)
		: Texture()
	{
		Init(name, format, mipMapCount, width, height);
	}

	/*! \note It specifies a pixel format and it is intended to be used with `loadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t mipMapCount, Vector2i size)
		: Texture(name, format, mipMapCount, size.X, size.Y)
	{
	}

	/*! \note It specifies a pixel format and it is intended to be used with `loadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t width, std::int32_t height)
		: Texture(name, format, 1, width, height)
	{
	}

	/*! \note It specifies a pixel format and it is intended to be used with `loadFromTexels()` */
	Texture::Texture(const char* name, Format format, Vector2i size)
		: Texture(name, format, 1, size.X, size.Y)
	{
	}

	Texture::Texture(StringView filename)
		: Texture()
	{
		const bool hasLoaded = LoadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Texture \"{}\" cannot be loaded", filename);
		}
	}

	Texture::~Texture()
	{
#if defined(NCINE_PROFILING)
		// Don't remove data from statistics if this is a moved out object
		if (dataSize_ > 0 && texture_ != nullptr) {
			RenderStatistics::RemoveTexture(dataSize_);
		}
#endif
	}

	Texture::Texture(Texture&&) = default;

	Texture& Texture::operator=(Texture&&) = default;

	void Texture::Init(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height)
	{
		ZoneScopedC(0x81A861);

		if (width == width_ && height == height_ && mipMapCount == mipMapLevels_ && format == format_) {
			return;
		}

#if defined(WITH_RHI_GL)
		TextureLoaderRaw texLoader(width, height, mipMapCount, ncFormatToInternal(format));

#	if defined(NCINE_PROFILING)
		if (dataSize_ > 0) {
			RenderStatistics::RemoveTexture(dataSize_);
		}
#	endif
		texture_->Bind();
		texture_->SetObjectLabel(name);
		Initialize(texLoader);

#	if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(dataSize_);
#	endif
#else
		width_ = width;
		height_ = height;
		mipMapLevels_ = mipMapCount;
		format_ = format;
		dataSize_ = static_cast<std::uint32_t>(width * height) * GetChannelCount();
		texture_->SetFilter(static_cast<RHI::SamplerFilter>(minFiltering_), static_cast<RHI::SamplerFilter>(magFiltering_));
		texture_->SetWrapping(static_cast<RHI::SamplerWrapping>(wrapMode_), static_cast<RHI::SamplerWrapping>(wrapMode_));
		// Allocate mip-0 storage so that render-target usage and pixel sampling work on SW.
		// (RHI::Texture::width_/height_ are only set via UploadMip, not by the above field assignments.)
		texture_->UploadMip(0, width, height, RHI::TextureFormat::RGBA8, nullptr, static_cast<std::size_t>(width) * height * 4);
#endif
	}

	void Texture::Init(const char* name, Format format, std::int32_t mipMapCount, Vector2i size)
	{
		DEATH_ASSERT(mipMapCount > 0);
		Init(name, format, mipMapCount, size.X, size.Y);
	}

	void Texture::Init(const char* name, Format format, std::int32_t width, std::int32_t height)
	{
		Init(name, format, 1, width, height);
	}

	void Texture::Init(const char* name, Format format, Vector2i size)
	{
		Init(name, format, 1, size.X, size.Y);
	}

	bool Texture::LoadFromFile(StringView filename)
	{
		ZoneScopedC(0x81A861);

		std::unique_ptr<ITextureLoader> texLoader = ITextureLoader::createFromFile(filename);
		if (!texLoader->hasLoaded()) {
			return false;
		}

#if defined(NCINE_PROFILING)
		if (dataSize_ > 0) {
			RenderStatistics::RemoveTexture(dataSize_);
		}
#endif
#if defined(WITH_RHI_GL)
		texture_->Bind();
		texture_->SetObjectLabel(filename);
#endif
		Initialize(*texLoader);
		Load(*texLoader);

#if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(dataSize_);
#endif
		return true;
	}

	/*! \note It loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr)
	{
		return LoadFromTexels(bufferPtr, 0, 0, 0, width_, height_);
	}

	/*! \note It loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		return LoadFromTexels(bufferPtr, 0, x, y, width, height);
	}

	/*! \note It loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, Recti region)
	{
		return LoadFromTexels(bufferPtr, 0, region.X, region.Y, region.W, region.H);
	}

	/*! \note It loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
#if defined(WITH_RHI_GL)
		const std::uint8_t* data = bufferPtr;

		const GLenum format = ncFormatToNonInternal(format_);
		glGetError();
		texture_->TexSubImage2D(level, x, y, width, height, format, GL_UNSIGNED_BYTE, data);
		const GLenum error = glGetError();

		return (error == GL_NO_ERROR);
#else
		if (x == 0 && y == 0) {
			const std::int32_t mipW = std::max(1, width_ >> level);
			const std::int32_t mipH = std::max(1, height_ >> level);
			texture_->UploadMip(level, mipW, mipH, RHI::TextureFormat::RGBA8, bufferPtr, static_cast<std::size_t>(mipW) * mipH * GetChannelCount());
			return true;
		}
		return false;
#endif
	}

	/*! It loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, Recti region)
	{
		return LoadFromTexels(bufferPtr, level, region.X, region.Y, region.W, region.H);
	}

	bool Texture::SaveToMemory(std::uint8_t* bufferPtr)
	{
		return SaveToMemory(bufferPtr, 0);
	}

	bool Texture::SaveToMemory(std::uint8_t* bufferPtr, std::int32_t level)
	{
#if defined(WITH_RHI_GL) && !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
		const GLenum format = ncFormatToNonInternal(format_);
		glGetError();
		texture_->GetTexImage(level, format, GL_UNSIGNED_BYTE, bufferPtr);
		const GLenum error = glGetError();

		return (error == GL_NO_ERROR);
#elif !defined(WITH_RHI_GL)
		const std::uint8_t* pixels = texture_->GetPixels(level);
		if (pixels != nullptr && bufferPtr != nullptr) {
			const std::int32_t mipW = std::max(1, width_ >> level);
			const std::int32_t mipH = std::max(1, height_ >> level);
			std::memcpy(bufferPtr, pixels, static_cast<std::size_t>(mipW) * mipH * GetChannelCount());
			return true;
		}
		return false;
#else
		return false;
#endif
	}

	std::uint32_t Texture::GetChannelCount() const
	{
		switch (format_) {
			case Texture::Format::R8:
				return 1;
			case Texture::Format::RG8:
				return 2;
			case Texture::Format::RGB8:
				return 3;
			case Texture::Format::RGBA8:
				return 4;
			case Texture::Format::Unknown:
			default:
				return 0;
		}
	}

	void Texture::SetMinFiltering(SamplerFilter filter)
	{
		if (minFiltering_ == filter) {
			return;
		}

		GLenum glFilter = GL_NEAREST;
		// clang-format off
		switch (filter) {
			case SamplerFilter::Nearest:				glFilter = GL_NEAREST; break;
			case SamplerFilter::Linear:					glFilter = GL_LINEAR; break;
			case SamplerFilter::NearestMipmapNearest:	glFilter = GL_NEAREST_MIPMAP_NEAREST; break;
			case SamplerFilter::LinearMipmapNearest:	glFilter = GL_LINEAR_MIPMAP_NEAREST; break;
			case SamplerFilter::NearestMipmapLinear:	glFilter = GL_NEAREST_MIPMAP_LINEAR; break;
			case SamplerFilter::LinearMipmapLinear:		glFilter = GL_LINEAR_MIPMAP_LINEAR; break;
			default:									glFilter = GL_NEAREST; break;
		}
		// clang-format on

#if defined(WITH_RHI_GL)
		texture_->Bind();
		texture_->TexParameteri(GL_TEXTURE_MIN_FILTER, glFilter);
		minFiltering_ = filter;
#else
		minFiltering_ = filter;
		texture_->SetFilter(static_cast<RHI::SamplerFilter>(minFiltering_), static_cast<RHI::SamplerFilter>(magFiltering_));
#endif
	}

	void Texture::SetMagFiltering(SamplerFilter filter)
	{
		if (magFiltering_ == filter) {
			return;
		}

		GLenum glFilter = GL_NEAREST;
		// clang-format off
		switch (filter) {
			case SamplerFilter::Nearest:			glFilter = GL_NEAREST; break;
			case SamplerFilter::Linear:				glFilter = GL_LINEAR; break;
			default:								glFilter = GL_NEAREST; break;
		}
		// clang-format on

#if defined(WITH_RHI_GL)
		texture_->Bind();
		texture_->TexParameteri(GL_TEXTURE_MAG_FILTER, glFilter);
		magFiltering_ = filter;
#else
		magFiltering_ = filter;
		texture_->SetFilter(static_cast<RHI::SamplerFilter>(minFiltering_), static_cast<RHI::SamplerFilter>(magFiltering_));
#endif
	}

	void Texture::SetWrap(SamplerWrapping wrapMode)
	{
		if (wrapMode_ == wrapMode) {
			return;
		}

		GLenum glWrap;
		// clang-format off
		switch (wrapMode) {
			default:
			case SamplerWrapping::ClampToEdge:		glWrap = GL_CLAMP_TO_EDGE; break;
			case SamplerWrapping::MirroredRepeat:	glWrap = GL_MIRRORED_REPEAT; break;
			case SamplerWrapping::Repeat:			glWrap = GL_REPEAT; break;
		}
		// clang-format on

#if defined(WITH_RHI_GL)
		texture_->Bind();
		texture_->TexParameteri(GL_TEXTURE_WRAP_S, glWrap);
		texture_->TexParameteri(GL_TEXTURE_WRAP_T, glWrap);
		wrapMode_ = wrapMode;
#else
		wrapMode_ = wrapMode;
		texture_->SetWrapping(static_cast<RHI::SamplerWrapping>(wrapMode_), static_cast<RHI::SamplerWrapping>(wrapMode_));
#endif
	}

	void Texture::SetTextureLabel(const char* label)
	{
#if defined(WITH_RHI_GL)
		texture_->SetObjectLabel(label);
#endif
	}

	/*! The pointer is an opaque handle to be used only by ImGui.
	 *  It is considered immutable from an user point of view and thus retrievable by a constant method. */
	void* Texture::GetGuiTexId() const
	{
		return const_cast<void*>(reinterpret_cast<const void*>(texture_.get()));
	}

	void Texture::Initialize(const ITextureLoader& texLoader)
	{
#if defined(WITH_RHI_GL)
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		const std::int32_t maxTextureSize = gfxCaps.GetValue(IGfxCapabilities::IntValues::MAX_TEXTURE_SIZE);
		FATAL_ASSERT_MSG(texLoader.width() <= maxTextureSize, "Texture width {} is bigger than device maximum {}", texLoader.width(), maxTextureSize);
		FATAL_ASSERT_MSG(texLoader.height() <= maxTextureSize, "Texture height {} is bigger than device maximum {}", texLoader.height(), maxTextureSize);

		const TextureFormat& texFormat = texLoader.texFormat();
		GLenum internalFormat = texFormat.internalFormat();
		GLenum format = texFormat.format();
		std::uint32_t dataSize = texLoader.dataSize();

#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
		const bool withTexStorage = true;
#else
		const bool withTexStorage = gfxCaps.HasExtension(IGfxCapabilities::Extensions::ARB_TEXTURE_STORAGE);
#endif

		// Specify texture storage because it's either the very first time or there have been a change in size or format
		if (dataSize_ == 0 || (width_ != texLoader.width() || height_ != texLoader.height() || ncFormatToInternal(format_) != internalFormat)) {
			if (withTexStorage) {
				if (dataSize_ > 0) {
					// The texture needs to be recreated as its storage is immutable
					texture_ = RHI::CreateTexture();
					dataSize_ = 0;
				}

				if (dataSize_ == 0) {
					texture_->TexStorage2D(texLoader.mipMapCount(), internalFormat, texLoader.width(), texLoader.height());
				}
			} else if (!texFormat.isCompressed()) {
				std::int32_t levelWidth = texLoader.width();
				std::int32_t levelHeight = texLoader.height();

				for (std::int32_t i = 0; i < texLoader.mipMapCount(); i++) {
					texture_->TexImage2D(i, internalFormat, levelWidth, levelHeight, format, texFormat.type(), nullptr);
					levelWidth /= 2;
					levelHeight /= 2;
				}
			}
		}

		width_ = texLoader.width();
		height_ = texLoader.height();
		mipMapLevels_ = texLoader.mipMapCount();
		isCompressed_ = texFormat.isCompressed();
		format_ = internalFormatToNc(internalFormat);
		dataSize_ = dataSize;

		texture_->TexParameteri(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		texture_->TexParameteri(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		wrapMode_ = SamplerWrapping::ClampToEdge;

		if (mipMapLevels_ > 1) {
			texture_->TexParameteri(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			texture_->TexParameteri(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::LinearMipmapLinear;
			// To prevent artifacts if the MIP map chain is not complete
			texture_->TexParameteri(GL_TEXTURE_MAX_LEVEL, mipMapLevels_);
		} else {
			texture_->TexParameteri(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			texture_->TexParameteri(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::Linear;
		}
#else
		// SW backend: store dimensions and format metadata; pixel data is uploaded in Load()
		const TextureFormat& texFormat = texLoader.texFormat();
		const GLenum internalFormat = texFormat.internalFormat();
		width_ = texLoader.width();
		height_ = texLoader.height();
		mipMapLevels_ = texLoader.mipMapCount();
		isCompressed_ = texFormat.isCompressed();
		format_ = internalFormatToNc(internalFormat);
		dataSize_ = texLoader.dataSize();
		wrapMode_ = SamplerWrapping::ClampToEdge;
		if (mipMapLevels_ > 1) {
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::LinearMipmapLinear;
		} else {
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::Linear;
		}
		texture_->SetFilter(static_cast<RHI::SamplerFilter>(minFiltering_), static_cast<RHI::SamplerFilter>(magFiltering_));
		texture_->SetWrapping(static_cast<RHI::SamplerWrapping>(wrapMode_), static_cast<RHI::SamplerWrapping>(wrapMode_));
#endif
	}

	void Texture::Load(const ITextureLoader& texLoader)
	{
#if defined(WITH_RHI_GL)
#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
		const bool withTexStorage = true;
#else
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		const bool withTexStorage = gfxCaps.HasExtension(IGfxCapabilities::Extensions::ARB_TEXTURE_STORAGE);
#endif

		const TextureFormat& texFormat = texLoader.texFormat();
		std::int32_t levelWidth = width_;
		std::int32_t levelHeight = height_;

		GLenum format = texFormat.format();

		for (std::int32_t mipIdx = 0; mipIdx < texLoader.mipMapCount(); mipIdx++) {
			const std::uint8_t* data = texLoader.pixels(mipIdx);

			if (texFormat.isCompressed()) {
				if (withTexStorage) {
					texture_->CompressedTexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, texFormat.internalFormat(), texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				} else {
					texture_->CompressedTexImage2D(mipIdx, texFormat.internalFormat(), levelWidth, levelHeight, texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				}
			} else {
				// Storage has already been created at this point
				texture_->TexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, format, texFormat.type(), data);
			}

			levelWidth /= 2;
			levelHeight /= 2;
		}
#else
		// SW backend: upload raw pixel data to CPU-side surfaces
		const TextureFormat& texFormat = texLoader.texFormat();
		std::int32_t levelWidth = width_;
		std::int32_t levelHeight = height_;

		if (!texFormat.isCompressed()) {
			for (std::int32_t mipIdx = 0; mipIdx < texLoader.mipMapCount(); mipIdx++) {
				const std::uint8_t* data = texLoader.pixels(mipIdx);
				if (data != nullptr) {
					texture_->UploadMip(mipIdx, levelWidth, levelHeight, RHI::TextureFormat::RGBA8, data, texLoader.dataSize(mipIdx));
				}
				levelWidth = std::max(1, levelWidth / 2);
				levelHeight = std::max(1, levelHeight / 2);
			}
		}
#endif
	}
}
