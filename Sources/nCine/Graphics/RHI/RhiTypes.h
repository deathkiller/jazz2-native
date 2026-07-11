#pragma once

#include <cstdint>

namespace nCine
{
	/**
		@brief Primitive topology of a draw call

		Backend-neutral replacement for the `GL_TRIANGLES`-style primitive enums. The numeric values
		intentionally match the corresponding OpenGL constants, so the OpenGL backend can translate
		with a plain cast. Other backends map these values through their own tables.
	*/
	enum class PrimitiveType : std::uint32_t
	{
		Points = 0x0000,			/**< A list of individual points */
		Lines = 0x0001,				/**< A list of independent line segments */
		LineLoop = 0x0002,			/**< A connected line strip closed back to the first vertex */
		LineStrip = 0x0003,			/**< A connected strip of line segments */
		Triangles = 0x0004,			/**< A list of independent triangles */
		TriangleStrip = 0x0005,		/**< A connected strip of triangles */
		TriangleFan = 0x0006		/**< A fan of triangles sharing the first vertex */
	};

	/**
		@brief Source or destination factor of the blending equation

		Backend-neutral replacement for the `GL_SRC_ALPHA`-style blending factor enums. The numeric
		values intentionally match the corresponding OpenGL constants, so the OpenGL backend can
		translate with a plain cast.
	*/
	enum class BlendingFactor : std::uint32_t
	{
		Zero = 0x0000,						/**< Multiplies by zero */
		One = 0x0001,						/**< Multiplies by one */
		SrcColor = 0x0300,					/**< Multiplies by the source color */
		OneMinusSrcColor = 0x0301,			/**< Multiplies by one minus the source color */
		SrcAlpha = 0x0302,					/**< Multiplies by the source alpha */
		OneMinusSrcAlpha = 0x0303,			/**< Multiplies by one minus the source alpha */
		DstAlpha = 0x0304,					/**< Multiplies by the destination alpha */
		OneMinusDstAlpha = 0x0305,			/**< Multiplies by one minus the destination alpha */
		DstColor = 0x0306,					/**< Multiplies by the destination color */
		OneMinusDstColor = 0x0307,			/**< Multiplies by one minus the destination color */
		SrcAlphaSaturate = 0x0308,			/**< Multiplies by the saturated source alpha (source factor only) */
		ConstantColor = 0x8001,				/**< Multiplies by the constant blend color */
		OneMinusConstantColor = 0x8002,		/**< Multiplies by one minus the constant blend color */
		ConstantAlpha = 0x8003,				/**< Multiplies by the constant blend alpha */
		OneMinusConstantAlpha = 0x8004		/**< Multiplies by one minus the constant blend alpha */
	};

	/**
		@brief Expected update pattern of a vertex or index buffer

		Backend-neutral replacement for the `GL_STATIC_DRAW`-style buffer usage hints. The numeric
		values intentionally match the corresponding OpenGL constants, so the OpenGL backend can
		translate with a plain cast.
	*/
	enum class BufferUsage : std::uint32_t
	{
		StreamDraw = 0x88E0,		/**< Written once and drawn only a few times */
		StaticDraw = 0x88E4,		/**< Written once and drawn many times */
		DynamicDraw = 0x88E8		/**< Rewritten repeatedly and drawn many times */
	};

	/**
		@brief Binding target of a buffer object

		Backend-neutral replacement for the `GL_ARRAY_BUFFER`-style buffer binding targets. The numeric
		values intentionally match the corresponding OpenGL constants, so the OpenGL backend can
		translate with a plain cast.
	*/
	enum class BufferTarget : std::uint32_t
	{
		Vertex = 0x8892,		/**< Vertex attribute data */
		Index = 0x8893,			/**< Index (element array) data */
		Uniform = 0x8A11		/**< Uniform block data */
	};

	/**
		@brief Buffer mapping and immutable storage flags

		Backend-neutral replacement for the `GL_MAP_WRITE_BIT`-style mapping flags, combinable as bit
		flags. The numeric values intentionally match the corresponding OpenGL constants, so the OpenGL
		backend can translate with a plain cast.
	*/
	enum class MapFlags : std::uint32_t
	{
		None = 0,					/**< No mapping (a host-side buffer is used instead) */
		Write = 0x0002,				/**< The mapping is written to */
		InvalidateRange = 0x0004,	/**< The previous contents of the mapped range can be discarded */
		InvalidateBuffer = 0x0008,	/**< The previous contents of the whole buffer can be discarded */
		FlushExplicit = 0x0010,		/**< Modified ranges are flushed explicitly */
		Unsynchronized = 0x0020,	/**< The device does not synchronize pending operations on the buffer */
		Persistent = 0x0040,		/**< The mapping stays valid while the device reads from the buffer */
		Coherent = 0x0080			/**< Writes become visible to the device without an explicit flush */
	};

	constexpr MapFlags operator|(MapFlags a, MapFlags b) {
		return MapFlags(std::uint32_t(a) | std::uint32_t(b));
	}
	constexpr MapFlags operator&(MapFlags a, MapFlags b) {
		return MapFlags(std::uint32_t(a) & std::uint32_t(b));
	}

	/**
		@brief Data format of the indices in an index buffer

		The numeric values intentionally match the corresponding OpenGL constants, so the OpenGL
		backend can translate with a plain cast.
	*/
	enum class IndexFormat : std::uint32_t
	{
		UInt16 = 0x1403,		/**< 16-bit unsigned indices */
		UInt32 = 0x1405			/**< 32-bit unsigned indices */
	};

	/**
		@brief Face culling mode

		The numeric values intentionally match the corresponding OpenGL constants, so the OpenGL
		backend can translate with a plain cast.
	*/
	enum class CullFaceMode : std::uint32_t
	{
		Front = 0x0404,			/**< Front faces are culled */
		Back = 0x0405,			/**< Back faces are culled */
		FrontAndBack = 0x0408	/**< All faces are culled */
	};

	/**
		@brief Opaque handle to a GPU fence created by the device
	*/
	using FenceHandle = void*;

	/**
		@brief Depth and stencil format of a render target

		The values are backend-neutral, each backend maps them to its own depth/stencil storage formats.
	*/
	enum class DepthStencilFormat
	{
		None,				/**< No depth or stencil buffer */
		Depth16,			/**< 16-bit depth buffer */
		Depth24,			/**< 24-bit depth buffer */
		Depth24_Stencil8	/**< 24-bit depth buffer with an 8-bit stencil buffer */
	};

	/**
		@brief Buffers of a render target that can be cleared

		Combinable as bit flags. Unlike the other enums in this header the values are backend-neutral,
		each backend maps them to its own clear mask.
	*/
	enum class ClearFlags : std::uint32_t
	{
		None = 0,			/**< No buffer */
		Color = 0x01,		/**< The color buffer */
		Depth = 0x02,		/**< The depth buffer */
		Stencil = 0x04		/**< The stencil buffer */
	};

	constexpr ClearFlags operator|(ClearFlags a, ClearFlags b) {
		return ClearFlags(std::uint32_t(a) | std::uint32_t(b));
	}
	constexpr ClearFlags operator&(ClearFlags a, ClearFlags b) {
		return ClearFlags(std::uint32_t(a) & std::uint32_t(b));
	}

	/**
		@brief Pixel format of uncompressed 8-bit-per-channel texture data

		Backend-neutral description of the texel layouts supported for empty and decoded textures,
		shared by @ref Texture (as `Texture::Format`) and the texture loaders.
	*/
	enum class PixelFormat
	{
		Unknown,		/**< Unknown or unsupported format (e.g., compressed or non-8-bit data) */

		R8,				/**< One channel, 8 bits per pixel */
		RG8,			/**< Two channels, 16 bits per pixel */
		RGB8,			/**< Three channels, 24 bits per pixel */
		RGBA8			/**< Four channels, 32 bits per pixel */
	};
}
