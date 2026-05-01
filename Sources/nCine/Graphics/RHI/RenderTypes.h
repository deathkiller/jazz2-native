#pragma once

#include <Common.h>

namespace nCine::RHI
{
	// Primitive draw types — replaces GLenum for draw calls
	enum class PrimitiveType : std::uint8_t
	{
		Unknown,
		Points,
		Lines,
		LineStrip,
		Triangles,
		TriangleStrip,
		TriangleFan
	};

	// Buffer binding targets
	enum class BufferType : std::uint8_t
	{
		Unknown,
		Vertex,
		Index,
		Uniform
	};

	// Buffer usage hints — replaces GLenum GL_STATIC_DRAW / GL_DYNAMIC_DRAW / GL_STREAM_DRAW
	enum class BufferUsage : std::uint8_t
	{
		Unknown,
		Static,
		Dynamic,
		Stream
	};

	// Buffer mapping access flags — replaces GL_MAP_* bitmask
	enum class MapFlags : std::uint32_t
	{
		None              = 0,
		Read              = 1 << 0,
		Write             = 1 << 1,
		InvalidateRange   = 1 << 2,
		InvalidateBuffer  = 1 << 3,
		FlushExplicit     = 1 << 4,
		Unsynchronized    = 1 << 5,
		Persistent        = 1 << 6,
		Coherent          = 1 << 7
	};

	inline constexpr MapFlags operator|(MapFlags a, MapFlags b)
	{
		return static_cast<MapFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}
	inline constexpr MapFlags operator&(MapFlags a, MapFlags b)
	{
		return static_cast<MapFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
	}
	inline constexpr bool HasFlag(MapFlags flags, MapFlags bit)
	{
		return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0;
	}

	// Blend source/destination factors — replaces GLenum for GL_SRC_ALPHA etc.
	enum class BlendFactor : std::uint8_t
	{
		Unknown,
		Zero,
		One,
		SrcColor,
		DstColor,
		OneMinusSrcColor,
		OneMinusDstColor,
		SrcAlpha,
		DstAlpha,
		OneMinusSrcAlpha,
		OneMinusDstAlpha,
		ConstantColor,
		OneMinusConstantColor,
		ConstantAlpha,
		OneMinusConstantAlpha,
		SrcAlphaSaturate
	};

	// Depth/stencil compare functions
	enum class CompareFunc : std::uint8_t
	{
		Unknown,
		Never,
		Less,
		Equal,
		LessOrEqual,
		Greater,
		NotEqual,
		GreaterOrEqual,
		Always
	};

	// Face culling mode
	enum class CullMode : std::uint8_t
	{
		Unknown,
		None,
		Back,
		Front,
		FrontAndBack
	};

	// Index element type
	enum class IndexType : std::uint8_t
	{
		Unknown,
		UInt16,
		UInt32
	};

	// Framebuffer clear targets
	enum class ClearFlags : std::uint32_t
	{
		None    = 0,
		Color   = 1 << 0,
		Depth   = 1 << 1,
		Stencil = 1 << 2,
		All     = Color | Depth | Stencil
	};

	inline constexpr ClearFlags operator|(ClearFlags a, ClearFlags b)
	{
		return static_cast<ClearFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}
	inline constexpr ClearFlags operator&(ClearFlags a, ClearFlags b)
	{
		return static_cast<ClearFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
	}

	// Texture/surface formats
	enum class TextureFormat : std::uint8_t
	{
		Unknown,

		// Uncompressed colour
		R8,
		RG8,
		RGB8,
		RGBA8,

		// Renderable depth/stencil (availability: RHI_CAP_FRAMEBUFFERS)
		Depth16,
		Depth24,
		Depth24Stencil8,

		// Compressed formats (availability: RHI_CAP_TEXTURE_COMPRESSION)
		RGB_DXT1,
		RGBA_DXT3,
		RGBA_DXT5,
		RGB_ETC1,
		RGB_ETC2,
		RGBA_ETC2,

		// ATC (Adreno)
		RGB_ATC,
		RGBA_ATC_Explicit,
		RGBA_ATC_Interpolated,

		// PVRTC (PowerVR)
		RGB_PVRTC_2BPP,
		RGBA_PVRTC_2BPP,
		RGB_PVRTC_4BPP,
		RGBA_PVRTC_4BPP,

		// ASTC (all 128 bits per block, varying block footprint)
		RGBA_ASTC_4x4,
		RGBA_ASTC_5x4,
		RGBA_ASTC_5x5,
		RGBA_ASTC_6x5,
		RGBA_ASTC_6x6,
		RGBA_ASTC_8x5,
		RGBA_ASTC_8x6,
		RGBA_ASTC_8x8,
		RGBA_ASTC_10x5,
		RGBA_ASTC_10x6,
		RGBA_ASTC_10x8,
		RGBA_ASTC_10x10,
		RGBA_ASTC_12x10,
		RGBA_ASTC_12x12,

		// Float formats (availability: RHI_CAP_TEXTURE_FLOAT)
		R_Float16,
		RGBA_Float16
	};

	// Sampler filtering
	enum class SamplerFilter : std::uint8_t
	{
		Unknown,
		Nearest,
		Linear,
		NearestMipmapNearest,
		LinearMipmapNearest,
		NearestMipmapLinear,
		LinearMipmapLinear
	};

	// Sampler address wrap mode
	enum class SamplerWrapping : std::uint8_t
	{
		Unknown,
		ClampToEdge,
		MirroredRepeat,
		Repeat
	};

	// Framebuffer attachment target
	enum class FramebufferAttachment : std::uint8_t
	{
		Unknown,
		Color0,
		Color1,
		Color2,
		Color3,
		Depth,
		DepthStencil
	};

	// Renderbuffer format
	enum class RenderbufferFormat : std::uint8_t
	{
		Unknown,
		Color,         // implementation-chosen colour format
		Depth16,
		Depth24,
		Depth24Stencil8
	};

	// Vertex attribute component type
	enum class VertexCompType : std::uint8_t
	{
		Unknown,
		Float,
		Int8,
		UInt8,
		Int16,
		UInt16,
		Int32,
		UInt32
	};

	/// Returns number of colour channels for uncompressed formats, 0 for compressed/unknown
	inline constexpr std::uint32_t NumChannels(TextureFormat format)
	{
		switch (format) {
			case TextureFormat::R8:
			case TextureFormat::R_Float16:
				return 1;
			case TextureFormat::RG8:
				return 2;
			case TextureFormat::RGB8:
			case TextureFormat::RGB_DXT1:
			case TextureFormat::RGB_ETC1:
			case TextureFormat::RGB_ETC2:
			case TextureFormat::RGB_ATC:
			case TextureFormat::RGB_PVRTC_2BPP:
			case TextureFormat::RGB_PVRTC_4BPP:
				return 3;
			case TextureFormat::RGBA8:
			case TextureFormat::RGBA_Float16:
			case TextureFormat::RGBA_DXT3:
			case TextureFormat::RGBA_DXT5:
			case TextureFormat::RGBA_ETC2:
			case TextureFormat::RGBA_ATC_Explicit:
			case TextureFormat::RGBA_ATC_Interpolated:
			case TextureFormat::RGBA_PVRTC_2BPP:
			case TextureFormat::RGBA_PVRTC_4BPP:
			case TextureFormat::RGBA_ASTC_4x4:
			case TextureFormat::RGBA_ASTC_5x4:
			case TextureFormat::RGBA_ASTC_5x5:
			case TextureFormat::RGBA_ASTC_6x5:
			case TextureFormat::RGBA_ASTC_6x6:
			case TextureFormat::RGBA_ASTC_8x5:
			case TextureFormat::RGBA_ASTC_8x6:
			case TextureFormat::RGBA_ASTC_8x8:
			case TextureFormat::RGBA_ASTC_10x5:
			case TextureFormat::RGBA_ASTC_10x6:
			case TextureFormat::RGBA_ASTC_10x8:
			case TextureFormat::RGBA_ASTC_10x10:
			case TextureFormat::RGBA_ASTC_12x10:
			case TextureFormat::RGBA_ASTC_12x12:
				return 4;
			default:
				return 0;
		}
	}

	/// Returns true if the format holds compressed data
	inline constexpr bool IsCompressed(TextureFormat format)
	{
		switch (format) {
			case TextureFormat::RGB_DXT1:
			case TextureFormat::RGBA_DXT3:
			case TextureFormat::RGBA_DXT5:
			case TextureFormat::RGB_ETC1:
			case TextureFormat::RGB_ETC2:
			case TextureFormat::RGBA_ETC2:
			case TextureFormat::RGB_ATC:
			case TextureFormat::RGBA_ATC_Explicit:
			case TextureFormat::RGBA_ATC_Interpolated:
			case TextureFormat::RGB_PVRTC_2BPP:
			case TextureFormat::RGBA_PVRTC_2BPP:
			case TextureFormat::RGB_PVRTC_4BPP:
			case TextureFormat::RGBA_PVRTC_4BPP:
			case TextureFormat::RGBA_ASTC_4x4:
			case TextureFormat::RGBA_ASTC_5x4:
			case TextureFormat::RGBA_ASTC_5x5:
			case TextureFormat::RGBA_ASTC_6x5:
			case TextureFormat::RGBA_ASTC_6x6:
			case TextureFormat::RGBA_ASTC_8x5:
			case TextureFormat::RGBA_ASTC_8x6:
			case TextureFormat::RGBA_ASTC_8x8:
			case TextureFormat::RGBA_ASTC_10x5:
			case TextureFormat::RGBA_ASTC_10x6:
			case TextureFormat::RGBA_ASTC_10x8:
			case TextureFormat::RGBA_ASTC_10x10:
			case TextureFormat::RGBA_ASTC_12x10:
			case TextureFormat::RGBA_ASTC_12x12:
				return true;
			default:
				return false;
		}
	}

	/// Returns bits per pixel for uncompressed formats
	inline constexpr std::uint32_t BitsPerPixel(TextureFormat format)
	{
		switch (format) {
			case TextureFormat::R8:         return 8;
			case TextureFormat::RG8:        return 16;
			case TextureFormat::RGB8:       return 24;
			case TextureFormat::RGBA8:      return 32;
			case TextureFormat::Depth16:    return 16;
			case TextureFormat::Depth24:    return 24;
			case TextureFormat::Depth24Stencil8: return 32;
			case TextureFormat::R_Float16:  return 16;
			case TextureFormat::RGBA_Float16: return 64;
			case TextureFormat::RGB_DXT1:   return 4;
			case TextureFormat::RGBA_DXT3:  return 8;
			case TextureFormat::RGBA_DXT5:  return 8;
			case TextureFormat::RGB_ETC1:   return 4;
			case TextureFormat::RGB_ETC2:   return 4;
			case TextureFormat::RGBA_ETC2:  return 8;
			case TextureFormat::RGB_ATC:    return 4;
			case TextureFormat::RGBA_ATC_Explicit:       return 8;
			case TextureFormat::RGBA_ATC_Interpolated:   return 8;
			case TextureFormat::RGB_PVRTC_2BPP:   return 2;
			case TextureFormat::RGBA_PVRTC_2BPP:  return 2;
			case TextureFormat::RGB_PVRTC_4BPP:   return 4;
			case TextureFormat::RGBA_PVRTC_4BPP:  return 4;
			case TextureFormat::RGBA_ASTC_4x4:    return 8;
			default:                        return 0;
		}
	}

	/// Calculates the pixel data size for each MIP map level, returns total size
	inline std::uint32_t CalculateMipSizes(TextureFormat format, std::int32_t width, std::int32_t height, std::int32_t mipMapCount, std::uint32_t* mipDataOffsets, std::uint32_t* mipDataSizes)
	{
		std::uint32_t blockWidth = 1, blockHeight = 1;
		std::uint32_t blockBytes = 0;	// If non-zero, use fixed block size instead of bpp
		std::uint32_t bpp = BitsPerPixel(format);
		std::uint32_t minDataSize = 1;

		if (IsCompressed(format)) {
			switch (format) {
				case TextureFormat::RGB_DXT1:
				case TextureFormat::RGB_ETC1:
				case TextureFormat::RGB_ETC2:
				case TextureFormat::RGB_ATC:
					blockWidth = 4; blockHeight = 4; minDataSize = 8;
					break;
				case TextureFormat::RGBA_DXT3:
				case TextureFormat::RGBA_DXT5:
				case TextureFormat::RGBA_ETC2:
				case TextureFormat::RGBA_ATC_Explicit:
				case TextureFormat::RGBA_ATC_Interpolated:
					blockWidth = 4; blockHeight = 4; minDataSize = 16;
					break;
				case TextureFormat::RGB_PVRTC_2BPP:
				case TextureFormat::RGBA_PVRTC_2BPP:
					blockWidth = 8; blockHeight = 4; minDataSize = 2 * 2 * ((8 * 4 * 2) / 8);
					break;
				case TextureFormat::RGB_PVRTC_4BPP:
				case TextureFormat::RGBA_PVRTC_4BPP:
					blockWidth = 4; blockHeight = 4; minDataSize = 2 * 2 * ((4 * 4 * 4) / 8);
					break;
				case TextureFormat::RGBA_ASTC_4x4:   blockWidth = 4;  blockHeight = 4;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_5x4:   blockWidth = 5;  blockHeight = 4;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_5x5:   blockWidth = 5;  blockHeight = 5;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_6x5:   blockWidth = 6;  blockHeight = 5;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_6x6:   blockWidth = 6;  blockHeight = 6;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_8x5:   blockWidth = 8;  blockHeight = 5;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_8x6:   blockWidth = 8;  blockHeight = 6;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_8x8:   blockWidth = 8;  blockHeight = 8;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_10x5:  blockWidth = 10; blockHeight = 5;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_10x6:  blockWidth = 10; blockHeight = 6;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_10x8:  blockWidth = 10; blockHeight = 8;  blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_10x10: blockWidth = 10; blockHeight = 10; blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_12x10: blockWidth = 12; blockHeight = 10; blockBytes = 16; minDataSize = 16; break;
				case TextureFormat::RGBA_ASTC_12x12: blockWidth = 12; blockHeight = 12; blockBytes = 16; minDataSize = 16; break;
				default: break;
			}
		}

		std::int32_t levelWidth = width;
		std::int32_t levelHeight = height;
		std::uint32_t dataSizesSum = 0;

		for (std::int32_t i = 0; i < mipMapCount; i++) {
			std::uint32_t blocksX = (levelWidth + blockWidth - 1) / blockWidth;
			std::uint32_t blocksY = (levelHeight + blockHeight - 1) / blockHeight;
			mipDataOffsets[i] = dataSizesSum;
			mipDataSizes[i] = (blockBytes > 0)
				? blocksX * blocksY * blockBytes
				: blocksX * blocksY * ((blockWidth * blockHeight * bpp) / 8);

			if (mipDataSizes[i] < minDataSize) {
				mipDataSizes[i] = minDataSize;
			}

			levelWidth /= 2;
			levelHeight /= 2;
			dataSizesSum += mipDataSizes[i];
		}

		return dataSizesSum;
	}

} // namespace nCine::RHI
