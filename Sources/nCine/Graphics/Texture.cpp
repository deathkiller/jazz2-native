#include "Texture.h"
#include "TextureLoaderRaw.h"
#include "RHI/Rhi.h"
#include "RenderStatistics.h"
#include "../ServiceLocator.h"
#include "../tracy.h"

#include <Containers/String.h>

namespace nCine
{
	Texture::Texture()
		: Object(ObjectType::Texture), rhiTexture_(std::make_unique<RHI::Texture>(TextureTarget::Texture2D)), width_(0), height_(0),
			mipMapLevels_(0), isCompressed_(false), format_(Format::Unknown), dataSize_(0), minFiltering_(SamplerFilter::Nearest),
			magFiltering_(SamplerFilter::Nearest), wrapMode_(SamplerWrapping::ClampToEdge)
	{
	}

	/** @note Specifies a pixel format and is intended to be used with `LoadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height)
		: Texture()
	{
		Init(name, format, mipMapCount, width, height);
	}

	/** @note Specifies a pixel format and is intended to be used with `LoadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t mipMapCount, Vector2i size)
		: Texture(name, format, mipMapCount, size.X, size.Y)
	{
	}

	/** @note Specifies a pixel format and is intended to be used with `LoadFromTexels()` */
	Texture::Texture(const char* name, Format format, std::int32_t width, std::int32_t height)
		: Texture(name, format, 1, width, height)
	{
	}

	/** @note Specifies a pixel format and is intended to be used with `LoadFromTexels()` */
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
		if (dataSize_ > 0 && rhiTexture_ != nullptr) {
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

		TextureLoaderRaw texLoader(width, height, mipMapCount, format);

#if defined(NCINE_PROFILING)
		if (dataSize_ > 0) {
			RenderStatistics::RemoveTexture(dataSize_);
		}
#endif
		rhiTexture_->Bind();
		rhiTexture_->SetObjectLabel(name);
		Initialize(texLoader);

#if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(dataSize_);
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
		rhiTexture_->Bind();
		rhiTexture_->SetObjectLabel(filename);
		Initialize(*texLoader);
		Load(*texLoader);

#if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(dataSize_);
#endif
		return true;
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr)
	{
		return LoadFromTexels(bufferPtr, 0, 0, 0, width_, height_);
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		return LoadFromTexels(bufferPtr, 0, x, y, width, height);
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, Recti region)
	{
		return LoadFromTexels(bufferPtr, 0, region.X, region.Y, region.W, region.H);
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr, std::int32_t level, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		const std::uint8_t* data = bufferPtr;

		// Tightly-packed single-/dual-/triple-channel rows may not meet the default 4-byte unpack alignment
		std::int32_t alignment;
		switch (format_) {
			case Format::R8:	alignment = 1; break;
			case Format::RG8:	alignment = 2; break;
			case Format::RGB8:	alignment = 1; break;
			default:			alignment = 4; break;
		}
		RHI::Texture::ClearErrors();
		if (alignment != 4) {
			RHI::Texture::SetUnpackAlignment(alignment);
		}
		rhiTexture_->TexSubImage2D(level, x, y, width, height, format_, false, data);
		if (alignment != 4) {
			RHI::Texture::SetUnpackAlignment(4);
		}

		return !RHI::Texture::CheckErrors();
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
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
		if (!RHI::Texture::SupportsTextureReadback()) {
			return false;
		}

		RHI::Texture::ClearErrors();
		rhiTexture_->GetTexImage(level, format_, false, bufferPtr);
		return !RHI::Texture::CheckErrors();
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

		rhiTexture_->SetMinFiltering(filter);
		minFiltering_ = filter;
	}

	void Texture::SetMagFiltering(SamplerFilter filter)
	{
		if (magFiltering_ == filter) {
			return;
		}

		rhiTexture_->SetMagFiltering(filter);
		magFiltering_ = filter;
	}

	void Texture::SetWrap(SamplerWrapping wrapMode)
	{
		if (wrapMode_ == wrapMode) {
			return;
		}

		rhiTexture_->SetWrap(wrapMode);
		wrapMode_ = wrapMode;
	}

	void Texture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		rhiTexture_->SetSwizzle(r, g, b, a);
	}

	void Texture::SetTextureLabel(const char* label)
	{
		rhiTexture_->SetObjectLabel(label);
	}

	/**
	 * @note The pointer is an opaque handle to be used only by ImGui. It is considered immutable
	 * from the user's point of view and thus retrievable by a constant method.
	 */
	void* Texture::GetGuiTexId() const
	{
		return const_cast<void*>(reinterpret_cast<const void*>(rhiTexture_.get()));
	}

	void Texture::Initialize(const ITextureLoader& texLoader)
	{
		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		const std::int32_t maxTextureSize = gfxCaps.GetValue(IGfxCapabilities::IntValues::MAX_TEXTURE_SIZE);
		FATAL_ASSERT_MSG(texLoader.width() <= maxTextureSize, "Texture width {} is bigger than device maximum {}", texLoader.width(), maxTextureSize);
		FATAL_ASSERT_MSG(texLoader.height() <= maxTextureSize, "Texture height {} is bigger than device maximum {}", texLoader.height(), maxTextureSize);

		const TextureFormat& texFormat = texLoader.texFormat();
		const PixelFormat pixelFormat = texFormat.pixelFormat();
		const bool bgr = texFormat.isBgr();
		std::uint32_t dataSize = texLoader.dataSize();

		const bool withTexStorage = RHI::Texture::SupportsImmutableStorage();

		// Specify texture storage because it's either the very first time or there have been a change in size or format
		if (dataSize_ == 0 || (width_ != texLoader.width() || height_ != texLoader.height() || format_ != pixelFormat)) {
			if (withTexStorage) {
				if (dataSize_ > 0) {
					// The texture needs to be recreated as its storage is immutable
					rhiTexture_ = std::make_unique<RHI::Texture>(TextureTarget::Texture2D);
					dataSize_ = 0;
				}

				if (dataSize_ == 0) {
					rhiTexture_->TexStorage2D(texLoader.mipMapCount(), pixelFormat, texLoader.width(), texLoader.height());
				}
			} else if (!texFormat.isCompressed()) {
				std::int32_t levelWidth = texLoader.width();
				std::int32_t levelHeight = texLoader.height();

				for (std::int32_t i = 0; i < texLoader.mipMapCount(); i++) {
					rhiTexture_->TexImage2D(i, pixelFormat, bgr, levelWidth, levelHeight, nullptr);
					levelWidth /= 2;
					levelHeight /= 2;
				}
			}
		}

		width_ = texLoader.width();
		height_ = texLoader.height();
		mipMapLevels_ = texLoader.mipMapCount();
		isCompressed_ = texFormat.isCompressed();
		format_ = pixelFormat;
		dataSize_ = dataSize;

		rhiTexture_->SetWrap(SamplerWrapping::ClampToEdge);
		wrapMode_ = SamplerWrapping::ClampToEdge;

		if (mipMapLevels_ > 1) {
			rhiTexture_->SetMagFiltering(SamplerFilter::Linear);
			rhiTexture_->SetMinFiltering(SamplerFilter::LinearMipmapLinear);
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::LinearMipmapLinear;
			// To prevent artifacts if the MIP map chain is not complete
			rhiTexture_->SetMaxLevel(mipMapLevels_);
		} else {
			rhiTexture_->SetMagFiltering(SamplerFilter::Linear);
			rhiTexture_->SetMinFiltering(SamplerFilter::Linear);
			magFiltering_ = SamplerFilter::Linear;
			minFiltering_ = SamplerFilter::Linear;
		}
	}

	void Texture::Load(const ITextureLoader& texLoader)
	{
		const bool withTexStorage = RHI::Texture::SupportsImmutableStorage();

		const TextureFormat& texFormat = texLoader.texFormat();
		const PixelFormat pixelFormat = texFormat.pixelFormat();
		const bool bgr = texFormat.isBgr();
		std::int32_t levelWidth = width_;
		std::int32_t levelHeight = height_;

		for (std::int32_t mipIdx = 0; mipIdx < texLoader.mipMapCount(); mipIdx++) {
			const std::uint8_t* data = texLoader.pixels(mipIdx);

			if (texFormat.isCompressed()) {
				if (withTexStorage) {
					rhiTexture_->CompressedTexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, pixelFormat, texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				} else {
					rhiTexture_->CompressedTexImage2D(mipIdx, pixelFormat, levelWidth, levelHeight, texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				}
			} else {
				// Storage has already been created at this point
				rhiTexture_->TexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, pixelFormat, bgr, data);
			}

			levelWidth /= 2;
			levelHeight /= 2;
		}
	}
}
