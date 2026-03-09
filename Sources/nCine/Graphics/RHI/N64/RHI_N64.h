#pragma once

#if defined(WITH_RHI_N64)

// ============================================================================
// Nintendo 64 backend — libdragon rdpq API
// Provides the Rhi interface for the N64 Reality Display Processor (RDP).
//
// The RDP hardware offers:
//   - Fixed-function triangle rasteriser with perspective-correct texturing
//   - Hardware alpha blending and Z-buffering (16-bit)
//   - Texture formats: RGBA16 (5551), RGBA32, CI8 (palette), IA8, I8
//   - Mipmaps (2–8 levels, power-of-2 textures only)
//   - NO programmable vertex or fragment shaders
//   - NO off-screen framebuffer objects
//
// Target SDK: libdragon (https://github.com/DragonMinded/libdragon)
// Build: mips64-elf cross-compiler, N64_INST toolchain
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_UNIFORM_BLOCKS,
//         RHI_CAP_BINARY_SHADERS, RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING,
//         RHI_CAP_VAO, RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_MIPMAPS           // RDP hardware mip-map support (power-of-2 only)
#define RHI_CAP_BUFFER_MAPPING    // All vertex buffers reside in host RAM (RDRAM)

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE      // Per-primitive shade colour (combine mode)
#define RHI_FF_ALPHA_BLEND        // Blender unit alpha blending
#define RHI_FF_TEXTURING          // RDP texture mapping via tiled TMEM

// ---------------------------------------------------------------------------
// libdragon headers — only available when targeting Nintendo 64
// ---------------------------------------------------------------------------
#include <libdragon.h>

#include "../../Primitives/Rect.h"
#include "../../Primitives/Vector2.h"
#include "../../Primitives/Color.h"
#include "../RenderTypes.h"

#include <memory>
#include <cstdint>
#include <cstring>

namespace nCine::RHI
{
	// =========================================================================
	// Buffer — host-side vertex / index storage (RDRAM, no GPU object on N64)
	// Vertex data is processed by the RSP microcode and sent to the RDP.
	// =========================================================================
	class Buffer
	{
	public:
		explicit Buffer(BufferType type, BufferUsage usage = BufferUsage::Dynamic)
			: type_(type), usage_(usage), size_(0) {}

		~Buffer() = default;

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;
		Buffer(Buffer&&) = default;
		Buffer& operator=(Buffer&&) = default;

		inline BufferType  GetType()  const { return type_;       }
		inline BufferUsage GetUsage() const { return usage_;      }
		inline std::size_t GetSize()  const { return size_;       }
		inline const void* GetData()  const { return data_.get(); }
		inline       void* GetData()        { return data_.get(); }

		void SetData(std::size_t size, const void* data, BufferUsage usage = BufferUsage::Dynamic)
		{
			if (size > size_) {
				data_ = std::make_unique<std::uint8_t[]>(size);
			}
			size_  = size;
			usage_ = usage;
			if (data != nullptr) {
				std::memcpy(data_.get(), data, size);
			}
		}

		void UpdateSubData(std::size_t offset, std::size_t size, const void* data)
		{
			if (data != nullptr && data_.get() != nullptr && offset + size <= size_) {
				std::memcpy(data_.get() + offset, data, size);
			}
		}

		/// Always host-mapped — return pointer directly.
		void* MapRange(std::size_t offset, std::size_t /*length*/, MapFlags /*flags*/)
		{
			return (data_.get() != nullptr && offset < size_) ? data_.get() + offset : nullptr;
		}

		void FlushRange(std::size_t /*offset*/, std::size_t /*length*/) {}
		void Unmap() {}

	private:
		BufferType  type_;
		BufferUsage usage_;
		std::size_t size_;
		std::unique_ptr<std::uint8_t[]> data_;
	};

	// =========================================================================
	// Texture — wraps a libdragon sprite / surface in RDRAM + TMEM
	//
	// The RDP uses TMEM (4 KB texture memory tile cache) with hardware-managed
	// loading via LOAD_TILE.  Textures must be power-of-2 in each dimension.
	// The rdpq API handles TMEM management automatically per draw call.
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1; // RDP: 1 texture unit (2 interleaved tiles possible but uncommon)

		Texture() = default;

		~Texture()
		{
			FreeData();
		}

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()    const { return width_;    }
		inline std::int32_t  GetHeight()   const { return height_;   }
		inline std::int32_t  GetMipCount() const { return mipCount_; }
		inline TextureFormat GetFormat()   const { return format_;   }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS()  const { return wrapS_;    }
		inline SamplerWrapping GetWrapT()  const { return wrapT_;    }

		/// Returns the raw RDRAM pixel data pointer (for rdpq_tex_upload).
		inline const void* GetPixelData() const { return pixelData_.get(); }
		inline tex_format_t GetNativeFormat() const { return nativeFormat_; }

		/// Allocate / re-upload texture data.
		/// @p data must be in the native format for this texture.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel == 0) {
				tex_format_t nativeFmt = ToNativeFormat(format);
				std::size_t allocSize = static_cast<std::size_t>(width * height) * BytesPerPixel(nativeFmt);
				if (width_ != width || height_ != height || nativeFormat_ != nativeFmt) {
					FreeData();
					pixelData_ = std::make_unique<std::uint8_t[]>(allocSize);
				}
				width_        = width;
				height_       = height;
				format_       = format;
				nativeFormat_ = nativeFmt;
				mipCount_     = 1;
				dataSize_     = allocSize;
			} else {
				mipCount_ = mipLevel + 1;
			}

			if (pixelData_ != nullptr && data != nullptr) {
				std::memcpy(pixelData_.get(), data, size < dataSize_ ? size : dataSize_);
				// Flush data cache so the RDP DMA sees the update
				data_cache_hit_writeback(pixelData_.get(), dataSize_);
			}
			dirty_ = true;
		}

		void SetFilter(SamplerFilter minFilter, SamplerFilter magFilter)
		{
			minFilter_ = minFilter;
			magFilter_ = magFilter;
		}

		void SetWrapping(SamplerWrapping wrapS, SamplerWrapping wrapT)
		{
			wrapS_ = wrapS;
			wrapT_ = wrapT;
		}

		bool IsDirty() const { return dirty_; }
		void ClearDirty() { dirty_ = false; }

		/// Translates Rhi TextureFormat to a libdragon tex_format_t.
		static tex_format_t ToNativeFormat(TextureFormat format)
		{
			switch (format) {
				case TextureFormat::RGBA8:   return FMT_RGBA32;
				case TextureFormat::RGB8:    return FMT_RGBA32;   // expand to RGBA32
				case TextureFormat::RGBA4:   return FMT_RGBA16;
				case TextureFormat::RGB5A1:  return FMT_RGBA16;
				case TextureFormat::RG8:     return FMT_IA16;
				case TextureFormat::R8:      return FMT_I8;
				case TextureFormat::R16F:    return FMT_I8;
				case TextureFormat::RG16F:   return FMT_IA16;
				case TextureFormat::RGBA16F: return FMT_RGBA32;
				case TextureFormat::R32F:    return FMT_I8;
				default:                     return FMT_RGBA16;
			}
		}

		static std::size_t BytesPerPixel(tex_format_t fmt)
		{
			switch (fmt) {
				case FMT_RGBA32: return 4;
				case FMT_RGBA16:
				case FMT_IA16:   return 2;
				case FMT_I8:
				case FMT_IA8:    return 1;
				default:         return 2;
			}
		}

		/// rdpq_tex_s32 sub-pixel coordinates for sampler filter
		static rdpq_filter_t ToNativeFilter(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::Linear:
				case SamplerFilter::LinearMipmapLinear:
				case SamplerFilter::LinearMipmapNearest:
					return FILTER_BILINEAR;
				default:
					return FILTER_POINT;
			}
		}

		/// Mirror / clamp wrapping
		static rdpq_mipmap_t ToNativeMipmap(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::LinearMipmapLinear:   return MIPMAP_INTERPOLATE;
				case SamplerFilter::LinearMipmapNearest:
				case SamplerFilter::NearestMipmapNearest:
				case SamplerFilter::NearestMipmapLinear:  return MIPMAP_NEAREST;
				default:                                   return MIPMAP_NONE;
			}
		}

	private:
		void FreeData()
		{
			pixelData_.reset();
			dataSize_ = 0;
		}

		std::unique_ptr<std::uint8_t[]> pixelData_;
		std::size_t     dataSize_      = 0;
		tex_format_t    nativeFormat_  = FMT_RGBA16;
		std::int32_t    width_         = 0;
		std::int32_t    height_        = 0;
		std::int32_t    mipCount_      = 0;
		TextureFormat   format_        = TextureFormat::Unknown;
		SamplerFilter   minFilter_     = SamplerFilter::Nearest;
		SamplerFilter   magFilter_     = SamplerFilter::Nearest;
		SamplerWrapping wrapS_         = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_         = SamplerWrapping::ClampToEdge;
		bool            dirty_         = false;
	};

	// =========================================================================
	// Fixed-function draw state
	// =========================================================================
	struct FFState
	{
		float mvpMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		float color[4]      = { 1, 1, 1, 1 };
		float texRect[4]    = { 0, 0, 1, 1 };
		float spriteSize[2] = { 1, 1 };
		float depth         = 0.5f;
		bool  hasTexture    = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// ShaderUniforms stub
	// =========================================================================
	class ShaderUniforms
	{
	public:
		ShaderUniforms() = default;

		inline void SetProgram(void* /*program*/, const char* /*includeOnly*/, const char* /*exclude*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline void SetDirty(bool /*isDirty*/) {}
		inline bool HasUniform(const char* /*name*/) const { return false; }
		inline void CommitUniforms() {}

		FFState& GetFFState()             { return ffState_; }
		const FFState& GetFFState() const { return ffState_; }

	private:
		FFState ffState_;
	};

	// =========================================================================
	// ShaderUniformBlocks stub
	// =========================================================================
	class ShaderUniformBlocks
	{
	public:
		ShaderUniformBlocks() = default;

		inline void SetProgram(void* /*program*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline bool HasUniformBlock(const char* /*name*/) const { return false; }
		inline void Bind() {}
	};

	// =========================================================================
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		inline void SetFloatVector(const float* /*values*/) {}
		inline void SetFloatValue(float /*v0*/) {}
		inline void SetIntValue(std::int32_t /*v0*/) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
	};

	// =========================================================================
	// ShaderProgram stub — no GPU shaders on N64 (RSP microcodes not exposed here)
	// =========================================================================
	class ShaderProgram
	{
	public:
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0; }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0; }
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		FFState ffState;
	};

	// =========================================================================
	// VertexFormat — describes the vertex attribute layout in a Buffer
	// =========================================================================
	struct VertexFormatAttribute
	{
		bool          enabled    = false;
		std::uint32_t index      = 0;
		std::int32_t  size       = 4;
		VertexCompType type      = VertexCompType::Float;
		bool          normalized = false;
		std::int32_t  stride     = 0;
		std::size_t   offset     = 0;
		const Buffer* vbo        = nullptr;
	};

	class VertexFormat
	{
	public:
		static constexpr std::uint32_t MaxAttributes = 16;

		VertexFormat() : attrCount_(0) {}

		void SetAttribute(std::uint32_t index, std::int32_t size, VertexCompType type,
		                  bool normalized, std::int32_t stride, std::size_t offset)
		{
			if (index < MaxAttributes) {
				attrs_[index] = { true, index, size, type, normalized, stride, offset, nullptr };
				if (index + 1 > attrCount_) {
					attrCount_ = index + 1;
				}
			}
		}

		void SetVbo(std::uint32_t index, const Buffer* vbo)
		{
			if (index < MaxAttributes) {
				attrs_[index].vbo = vbo;
			}
		}

		std::uint32_t GetAttributeCount() const { return attrCount_; }
		const VertexFormatAttribute& GetAttribute(std::uint32_t i) const { return attrs_[i]; }

		bool operator==(const VertexFormat& other) const
		{
			if (attrCount_ != other.attrCount_) { return false; }
			for (std::uint32_t i = 0; i < attrCount_; ++i) {
				const auto& a = attrs_[i];
				const auto& b = other.attrs_[i];
				if (a.enabled != b.enabled || a.index != b.index || a.size != b.size ||
				    a.type != b.type || a.normalized != b.normalized ||
				    a.stride != b.stride || a.offset != b.offset || a.vbo != b.vbo) {
					return false;
				}
			}
			return true;
		}
		bool operator!=(const VertexFormat& other) const { return !operator==(other); }

	private:
		VertexFormatAttribute attrs_[MaxAttributes];
		std::uint32_t attrCount_;
	};

	using VertexArrayObject = VertexFormat;

	// =========================================================================
	// Per-draw context
	// =========================================================================
	struct DrawContext
	{
		const VertexFormat* vertexFormat  = nullptr;
		std::size_t         vboByteOffset = 0;
		Texture*            textures[Texture::MaxTextureUnitsConst] = {};
		FFState             ff;

		bool        blendingEnabled = false;
		BlendFactor blendSrc        = BlendFactor::SrcAlpha;
		BlendFactor blendDst        = BlendFactor::InvSrcAlpha;
		bool        scissorEnabled  = false;
		nCine::Recti scissorRect;
	};

	// =========================================================================
	// Constants
	// =========================================================================
	static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;

	// =========================================================================
	// Buffer factory / operation wrappers
	// =========================================================================

	inline std::unique_ptr<Buffer> CreateBuffer(BufferType type)
	{
		return std::make_unique<Buffer>(type);
	}

	inline void BufferData(Buffer& buf, std::size_t size, const void* data, BufferUsage usage)
	{
		buf.SetData(size, data, usage);
	}

	inline void BufferSubData(Buffer& buf, std::size_t offset, std::size_t size, const void* data)
	{
		buf.UpdateSubData(offset, size, data);
	}

	inline void* MapBufferRange(Buffer& buf, std::size_t offset, std::size_t size, MapFlags flags)
	{
		return buf.MapRange(offset, size, flags);
	}

	inline void FlushMappedBufferRange(Buffer& buf, std::size_t offset, std::size_t size)
	{
		buf.FlushRange(offset, size);
	}

	inline void UnmapBuffer(Buffer& buf)
	{
		buf.Unmap();
	}

	inline bool BindBuffer(Buffer& /*buf*/) { return true; }
	inline void BindBufferBase(Buffer& /*buf*/, std::uint32_t /*index*/) {}
	inline void BindBufferRange(Buffer& /*buf*/, std::uint32_t /*index*/, std::size_t /*offset*/, std::size_t /*size*/) {}

	template<typename StringViewType>
	inline void SetBufferLabel(Buffer& /*buf*/, StringViewType /*label*/) {}

	// =========================================================================
	// Texture operation wrappers
	// =========================================================================

	inline std::unique_ptr<Texture> CreateTexture()
	{
		return std::make_unique<Texture>();
	}

	/// Bind a texture for the next rdpq draw call.
	void BindTexture(Texture& tex, std::uint32_t unit);
	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	template<typename StringViewType>
	inline void SetTextureLabel(Texture& /*tex*/, StringViewType /*label*/) {}

	// =========================================================================
	// Draw call declarations — implemented in RHI_N64.cpp
	// =========================================================================
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count);
	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount);
	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex = 0);
	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex = 0);

	// =========================================================================
	// Render-state helpers
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst);
	void SetDepthTest(bool enabled);
	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetScissorTest(bool enabled, const nCine::Recti& rect);
	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetClearColor(float r, float g, float b, float a);
	void Clear(ClearFlags flags);

	struct ScissorState { bool enabled; std::int32_t x, y, w, h; };
	ScissorState GetScissorState();
	void         SetScissorState(const ScissorState& state);

} // namespace nCine::RHI

#endif