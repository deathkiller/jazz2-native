#pragma once

#include <cstdint>
#include <cstddef>

namespace Rhi
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

} // namespace Rhi
