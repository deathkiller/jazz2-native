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
		: Object(ObjectType::Texture), _rhiTexture(std::make_unique<RHI::Texture>(TextureTarget::Texture2D)), _width(0), _height(0),
			_mipMapLevels(0), _isCompressed(false), _format(Format::Unknown), _dataSize(0), _minFiltering(SamplerFilter::Nearest),
			_magFiltering(SamplerFilter::Nearest), _wrapMode(SamplerWrapping::ClampToEdge)
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
		if (_dataSize > 0 && _rhiTexture != nullptr) {
			RenderStatistics::RemoveTexture(_dataSize);
		}
#endif
	}

	Texture::Texture(Texture&&) = default;

	Texture& Texture::operator=(Texture&&) = default;

	void Texture::Init(const char* name, Format format, std::int32_t mipMapCount, std::int32_t width, std::int32_t height)
	{
		ZoneScopedC(0x81A861);

		if (width == _width && height == _height && mipMapCount == _mipMapLevels && format == _format) {
			return;
		}

		TextureLoaderRaw texLoader(width, height, mipMapCount, format);

#if defined(NCINE_PROFILING)
		if (_dataSize > 0) {
			RenderStatistics::RemoveTexture(_dataSize);
		}
#endif
		_rhiTexture->Bind();
		_rhiTexture->SetObjectLabel(name);
		Initialize(texLoader);

#if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(_dataSize);
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
		if (_dataSize > 0) {
			RenderStatistics::RemoveTexture(_dataSize);
		}
#endif
		_rhiTexture->Bind();
		_rhiTexture->SetObjectLabel(filename);
		Initialize(*texLoader);
		Load(*texLoader);

#if defined(NCINE_PROFILING)
		RenderStatistics::AddTexture(_dataSize);
#endif
		return true;
	}

	/** @note Loads uncompressed pixel data from memory using the `Format` specified in the constructor */
	bool Texture::LoadFromTexels(const std::uint8_t* bufferPtr)
	{
		return LoadFromTexels(bufferPtr, 0, 0, 0, _width, _height);
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
		switch (_format) {
			case Format::R8:	alignment = 1; break;
			case Format::RG8:	alignment = 2; break;
			case Format::RGB8:	alignment = 1; break;
			default:			alignment = 4; break;
		}
		RHI::Texture::ClearErrors();
		if (alignment != 4) {
			RHI::Texture::SetUnpackAlignment(alignment);
		}
		_rhiTexture->TexSubImage2D(level, x, y, width, height, _format, false, data);
		if (alignment != 4) {
			RHI::Texture::SetUnpackAlignment(4);
		}

		return !RHI::Texture::CheckErrors();
	}

#if defined(RHI_CAP_STREAMING_TEXTURES)
	void* Texture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		return _rhiTexture->MapStreamingTexels(strideBytes);
	}
#endif

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
		_rhiTexture->GetTexImage(level, _format, false, bufferPtr);
		return !RHI::Texture::CheckErrors();
	}

	std::uint32_t Texture::GetChannelCount() const
	{
		switch (_format) {
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
		if (_minFiltering == filter) {
			return;
		}

		_rhiTexture->SetMinFiltering(filter);
		_minFiltering = filter;
	}

	void Texture::SetMagFiltering(SamplerFilter filter)
	{
		if (_magFiltering == filter) {
			return;
		}

		_rhiTexture->SetMagFiltering(filter);
		_magFiltering = filter;
	}

	void Texture::SetWrap(SamplerWrapping wrapMode)
	{
		if (_wrapMode == wrapMode) {
			return;
		}

		_rhiTexture->SetWrap(wrapMode);
		_wrapMode = wrapMode;
	}

	void Texture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_rhiTexture->SetSwizzle(r, g, b, a);
	}

	void Texture::SetTextureLabel(const char* label)
	{
		_rhiTexture->SetObjectLabel(label);
	}

	/**
	 * @note The pointer is an opaque handle to be used only by ImGui. It is considered immutable
	 * from the user's point of view and thus retrievable by a constant method.
	 */
	void* Texture::GetGuiTexId() const
	{
		return const_cast<void*>(reinterpret_cast<const void*>(_rhiTexture.get()));
	}

	void Texture::Initialize(const ITextureLoader& texLoader)
	{
		const RHI::IRhiCapabilities& caps = theServiceLocator().GetRhiCapabilities();
		const std::int32_t maxTextureSize = caps.GetValue(RHI::IRhiCapabilities::IntValues::MaxTextureSize);
#if defined(WITH_RHI_GU)
		// The PSP's GE tops out at 512 texels per axis, which is what the backend reports so the tileset
		// atlases get chunked to fit it - but a few prebaked assets are taller anyway (the small font atlas
		// is 128x529), and RHI::GU::GuTexture handles those by splitting the image into 512x512 pages that
		// the draw path selects between. So an oversized source image degrades (a primitive that samples
		// across a page boundary loses its overhang) instead of aborting the game before its first frame.
		if (texLoader.width() > maxTextureSize || texLoader.height() > maxTextureSize) {
			LOGW("Texture {}x{} is bigger than the device maximum {} and will be split into pages",
				texLoader.width(), texLoader.height(), maxTextureSize);
		}
#else
		FATAL_ASSERT_MSG(texLoader.width() <= maxTextureSize, "Texture width {} is bigger than device maximum {}", texLoader.width(), maxTextureSize);
		FATAL_ASSERT_MSG(texLoader.height() <= maxTextureSize, "Texture height {} is bigger than device maximum {}", texLoader.height(), maxTextureSize);
#endif

		const TextureFormat& texFormat = texLoader.texFormat();
		const PixelFormat pixelFormat = texFormat.pixelFormat();
		const bool bgr = texFormat.isBgr();
		std::uint32_t dataSize = texLoader.dataSize();

		const bool withTexStorage = RHI::Texture::SupportsImmutableStorage();

		// Specify texture storage because it's either the very first time or there have been a change in size or format
		if (_dataSize == 0 || (_width != texLoader.width() || _height != texLoader.height() || _format != pixelFormat)) {
			if (withTexStorage) {
				if (_dataSize > 0) {
					// The texture needs to be recreated as its storage is immutable
					_rhiTexture = std::make_unique<RHI::Texture>(TextureTarget::Texture2D);
					_dataSize = 0;
				}

				if (_dataSize == 0) {
					_rhiTexture->TexStorage2D(texLoader.mipMapCount(), pixelFormat, texLoader.width(), texLoader.height());
				}
			} else if (!texFormat.isCompressed()) {
				std::int32_t levelWidth = texLoader.width();
				std::int32_t levelHeight = texLoader.height();

				for (std::int32_t i = 0; i < texLoader.mipMapCount(); i++) {
					_rhiTexture->TexImage2D(i, pixelFormat, bgr, levelWidth, levelHeight, nullptr);
					levelWidth /= 2;
					levelHeight /= 2;
				}
			}
		}

		_width = texLoader.width();
		_height = texLoader.height();
		_mipMapLevels = texLoader.mipMapCount();
		_isCompressed = texFormat.isCompressed();
		_format = pixelFormat;
		_dataSize = dataSize;

		_rhiTexture->SetWrap(SamplerWrapping::ClampToEdge);
		_wrapMode = SamplerWrapping::ClampToEdge;

		if (_mipMapLevels > 1) {
			_rhiTexture->SetMagFiltering(SamplerFilter::Linear);
			_rhiTexture->SetMinFiltering(SamplerFilter::LinearMipmapLinear);
			_magFiltering = SamplerFilter::Linear;
			_minFiltering = SamplerFilter::LinearMipmapLinear;
			// To prevent artifacts if the MIP map chain is not complete
			_rhiTexture->SetMaxLevel(_mipMapLevels);
		} else {
			_rhiTexture->SetMagFiltering(SamplerFilter::Linear);
			_rhiTexture->SetMinFiltering(SamplerFilter::Linear);
			_magFiltering = SamplerFilter::Linear;
			_minFiltering = SamplerFilter::Linear;
		}
	}

	void Texture::Load(const ITextureLoader& texLoader)
	{
		const bool withTexStorage = RHI::Texture::SupportsImmutableStorage();

		const TextureFormat& texFormat = texLoader.texFormat();
		const PixelFormat pixelFormat = texFormat.pixelFormat();
		const bool bgr = texFormat.isBgr();
		std::int32_t levelWidth = _width;
		std::int32_t levelHeight = _height;

		for (std::int32_t mipIdx = 0; mipIdx < texLoader.mipMapCount(); mipIdx++) {
			const std::uint8_t* data = texLoader.pixels(mipIdx);

			if (texFormat.isCompressed()) {
				if (withTexStorage) {
					_rhiTexture->CompressedTexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, pixelFormat, texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				} else {
					_rhiTexture->CompressedTexImage2D(mipIdx, pixelFormat, levelWidth, levelHeight, texLoader.dataSize(mipIdx), texLoader.pixels(mipIdx));
				}
			} else {
				// Storage has already been created at this point
				_rhiTexture->TexSubImage2D(mipIdx, 0, 0, levelWidth, levelHeight, pixelFormat, bgr, data);
			}

			levelWidth /= 2;
			levelHeight /= 2;
		}
	}
}
