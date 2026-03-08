#pragma once

// ============================================================================
// Sega Dreamcast backend — KallistiOS PVR API
// Provides the Rhi interface for the Dreamcast PowerVR2 (CLX2) GPU.
//
// The PVR hardware offers:
//   - Fixed-function transform & lighting (Z sort, alpha blending, fogging)
//   - Hardware texture mapping (twiddled RGB565 / ARGB4444 / ARGB1555)
//   - Three display lists: Opaque, Punch-Through, Translucent
//   - NO programmable vertex or fragment shaders
//   - NO off-screen framebuffer objects
//
// Feature set:
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_UNIFORM_BLOCKS,
//         RHI_CAP_BINARY_SHADERS, RHI_CAP_INSTANCING, RHI_CAP_VAO,
//         RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_MIPMAPS           // PVR hardware mip-map support (power-of-2 only)
#define RHI_CAP_BUFFER_MAPPING    // All vertex buffers reside in host RAM

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE      // Per-vertex colour multiply (ARGB in pvr_vertex_t)
#define RHI_FF_ALPHA_BLEND        // Hardware alpha blending via PVR_BLEND_*
#define RHI_FF_TEXTURING          // PVR texture mapping

// ---------------------------------------------------------------------------
// KallistiOS PVR headers — only available when targeting Dreamcast
// ---------------------------------------------------------------------------
#include <kos.h>
#include <dc/pvr.h>

#include "../../Primitives/Rect.h"
#include "../../Primitives/Vector2.h"
#include "../../Primitives/Color.h"
#include "../RenderTypes.h"

#include <memory>
#include <cstdint>
#include <cstring>

namespace Rhi
{
	// =========================================================================
	// Buffer — host-side vertex / index storage (no GPU object on DC)
	// Vertex data is DMA'd to the PVR display list at draw time.
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
	// Texture — wraps a VRAM allocation via pvr_ptr_t
	//
	// The PVR hardware only supports power-of-2 textures in twiddled format.
	// Callers must ensure dimensions satisfy these constraints.
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1; // PVR: 1 texture unit

		Texture() = default;

		~Texture()
		{
			FreeVram();
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

		/// Returns the VRAM texture pointer (may be nullptr if not yet uploaded).
		inline pvr_ptr_t GetVramPtr() const { return vram_; }

		/// Returns the native PVR texture format flag (PVR_TXRFMT_*).
		inline std::uint32_t GetNativeFormat() const { return nativeFormat_; }

		/// Upload texture mip data to VRAM.
		/// @param mipLevel  0 = base, 1+ = lower mips
		/// @param width     Width of this mip level (must be power-of-2)
		/// @param height    Height of this mip level (must be power-of-2)
		/// @param format    Rhi texture format
		/// @param data      Source pixel data in the native PVR format
		/// @param size      Byte size of @p data
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel == 0) {
				// Allocate VRAM on first upload or if size changes
				std::uint32_t nativeFmt = ToNativeFormat(format);
				std::size_t vramSize = static_cast<std::size_t>(width * height) * BytesPerPixel(nativeFmt);
				if (vram_ == nullptr || width_ != width || height_ != height || nativeFormat_ != nativeFmt) {
					FreeVram();
					vram_ = pvr_mem_malloc(vramSize);
				}
				width_        = width;
				height_       = height;
				format_       = format;
				nativeFormat_ = nativeFmt;
				mipCount_     = 1;
			} else {
				mipCount_ = mipLevel + 1;
			}

			if (vram_ != nullptr && data != nullptr) {
				// pvr_txr_load_ex handles twiddling and DMA to VRAM
				pvr_txr_load_ex(const_cast<void*>(data), vram_, static_cast<uint32>(size), PVR_TXRLOAD_INVERT_Y);
			}
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

		/// Translates Rhi TextureFormat to a PVR_TXRFMT_* constant.
		static std::uint32_t ToNativeFormat(TextureFormat format)
		{
			switch (format) {
				case TextureFormat::RGBA8:   return PVR_TXRFMT_ARGB4444; // lossy, closest to RGBA8
				case TextureFormat::RGB8:    return PVR_TXRFMT_RGB565;
				case TextureFormat::RG8:     return PVR_TXRFMT_RGB565;   // R in R, G in G, B=0
				case TextureFormat::R8:      return PVR_TXRFMT_RGB565;
				case TextureFormat::RGBA4:   return PVR_TXRFMT_ARGB4444;
				case TextureFormat::RGB5A1:  return PVR_TXRFMT_ARGB1555;
				case TextureFormat::R16F:    return PVR_TXRFMT_RGB565;
				case TextureFormat::RG16F:   return PVR_TXRFMT_RGB565;
				case TextureFormat::RGBA16F: return PVR_TXRFMT_ARGB4444;
				case TextureFormat::R32F:    return PVR_TXRFMT_RGB565;
				default:                     return PVR_TXRFMT_ARGB4444;
			}
		}

		/// Returns the bytes per pixel for a given PVR_TXRFMT_* constant.
		static std::size_t BytesPerPixel(std::uint32_t nativeFormat)
		{
			// All PVR 2D texture formats are 16-bit (2 bytes) except palettized
			(void)nativeFormat;
			return 2;
		}

		/// Returns the PVR filter constant matching a SamplerFilter.
		static int ToNativeFilter(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::Linear:
				case SamplerFilter::LinearMipmapLinear:
				case SamplerFilter::LinearMipmapNearest:
					return PVR_FILTER_BILINEAR;
				default:
					return PVR_FILTER_NEAREST;
			}
		}

		/// Returns the PVR clamp constant matching a SamplerWrapping.
		static int ToNativeClamp(SamplerWrapping wrapping)
		{
			return (wrapping == SamplerWrapping::ClampToEdge) ? PVR_UVCLAMP_UV : PVR_UVCLAMP_NONE;
		}

	private:
		void FreeVram()
		{
			if (vram_ != nullptr) {
				pvr_mem_free(vram_);
				vram_ = nullptr;
			}
		}

		pvr_ptr_t      vram_        = nullptr;
		std::uint32_t  nativeFormat_= PVR_TXRFMT_ARGB4444;
		std::int32_t   width_       = 0;
		std::int32_t   height_      = 0;
		std::int32_t   mipCount_    = 0;
		TextureFormat  format_      = TextureFormat::Unknown;
		SamplerFilter  minFilter_   = SamplerFilter::Nearest;
		SamplerFilter  magFilter_   = SamplerFilter::Nearest;
		SamplerWrapping wrapS_      = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_      = SamplerWrapping::ClampToEdge;
	};

	// =========================================================================
	// Fixed-function draw state
	// Mirrors the SW backend's FFState; used by the RenderCommand pipeline
	// to pass per-draw parameters without programmable shaders.
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
		float depth         = 0.5f;       // Z in [0..1]; PVR uses 1/Z internally
		bool  hasTexture    = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// =========================================================================
	// ShaderUniforms stub
	// =========================================================================
	class UniformCache; // forward decl for UniformHashMapType

	class ShaderUniforms
	{
	public:
		using UniformHashMapType = int;

		ShaderUniforms() = default;

		inline void SetProgram(void* /*program*/, const char* /*includeOnly*/, const char* /*exclude*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline void SetDirty(bool /*isDirty*/) {}
		inline bool HasUniform(const char* /*name*/) const { return false; }
		inline void CommitUniforms() {}

		inline std::int32_t GetUniformCount() const { return 0; }
		inline UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniforms() const { return 0; }

		FFState& GetFFState()             { return ffState_; }
		const FFState& GetFFState() const { return ffState_; }

	private:
		FFState ffState_;
	};

	// =========================================================================
	// =========================================================================
	// ShaderUniformBlocks stub
	// =========================================================================
	class UniformBlockCache; // forward decl for UniformHashMapType

	class ShaderUniformBlocks
	{
	public:
		using UniformHashMapType = int;

		ShaderUniformBlocks() = default;

		inline void SetProgram(void* /*program*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline bool HasUniformBlock(const char* /*name*/) const { return false; }
		inline void Bind() {}
		inline void CommitUniformBlocks() {}

		inline UniformBlockCache* GetUniformBlock(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniformBlocks() const { return 0; }
	};

	// =========================================================================
	// =========================================================================
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		inline bool SetFloatVector(const float* /*values*/) { return true; }
		inline bool SetFloatValue(float /*v0*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/, float /*v3*/) { return true; }
		inline bool SetIntVector(const std::int32_t* /*values*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/, std::int32_t /*v3*/) { return true; }
		inline std::int32_t GetIntValue(std::int32_t /*componentIdx*/) const { return 0; }
		// Dirty flag — no-op for the DC backend
		inline void SetDirty(bool /*isDirty*/) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }

		inline std::uint32_t GetSize()        const { return 0; }
		inline std::uint32_t GetAlignAmount() const { return 0; }
		inline const std::uint8_t* GetDataPointer() const { return nullptr; }
		inline bool CopyData(const std::uint8_t* /*src*/)                                                       { return false; }
		inline bool CopyData(std::uint32_t /*destIndex*/, const std::uint8_t* /*src*/, std::uint32_t /*bytes*/) { return false; }
		inline void SetUsedSize(std::uint32_t /*size*/) {}
	};

	// =========================================================================
	// ShaderProgram stub — no GPU shaders on Dreamcast
	// =========================================================================
	class ShaderProgram
	{
	public:
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0;    }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0;    }
		std::int32_t GetBatchSize()         const { return 0;    }
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		/// Vertex-format binding — no-op for the fixed-function DC pipeline
		void DefineVertexFormat(const Buffer* /*vbo*/, const Buffer* /*ibo*/, std::uint32_t /*vboOffset*/) {}

		FFState ffState; // direct access for fixed-function draws
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

	/// DC has no native VAO concept; reuse the format description directly.
	using VertexArrayObject = VertexFormat;

	// =========================================================================
	// Blending helpers
	// =========================================================================

	/// Translates a Rhi BlendFactor to a PVR_BLEND_* constant.
	inline int ToNativeBlendFactor(BlendFactor factor)
	{
		switch (factor) {
			case BlendFactor::Zero:         return PVR_BLEND_ZERO;
			case BlendFactor::One:          return PVR_BLEND_ONE;
			case BlendFactor::SrcColor:     return PVR_BLEND_SRCCOLOR;
			case BlendFactor::InvSrcColor:  return PVR_BLEND_INVSRCCOLOR;
			case BlendFactor::SrcAlpha:     return PVR_BLEND_SRCALPHA;
			case BlendFactor::InvSrcAlpha:  return PVR_BLEND_INVSRCALPHA;
			case BlendFactor::DstAlpha:     return PVR_BLEND_DESTALPHA;
			case BlendFactor::InvDstAlpha:  return PVR_BLEND_INVDESTALPHA;
			case BlendFactor::DstColor:     return PVR_BLEND_DESTCOLOR;
			case BlendFactor::InvDstColor:  return PVR_BLEND_INVDESTCOLOR;
			default:                        return PVR_BLEND_ONE;
		}
	}

	/// Returns the PVR display list that corresponds to the active blend mode.
	/// Opaque and additive geometry goes into OP_POLY; alpha-blended into TR_POLY.
	inline pvr_list_t BlendFactorsToPvrList(BlendFactor src, BlendFactor dst)
	{
		if (src == BlendFactor::One && dst == BlendFactor::Zero) {
			return PVR_LIST_OP_POLY;
		}
		return PVR_LIST_TR_POLY;
	}

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

	inline bool BindBuffer(Buffer& /*buf*/)
	{
		return true;
	}

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

	/// No explicit texture bind required — texture is referenced by pvr_poly_hdr_t.
	inline void BindTexture(const Texture& /*tex*/, std::uint32_t /*unit*/) {}
	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	// =========================================================================
	// Build a PVR polygon header from the current DrawContext.
	//
	// Call this each time the material changes (texture, blend mode, etc.)
	// before submitting vertices to the PVR list.
	// =========================================================================
	inline void BuildPvrHeader(pvr_poly_hdr_t& outHdr, const DrawContext& ctx)
	{
		pvr_poly_cxt_t cxt;
		Texture* tex = ctx.textures[0];
		pvr_list_t pvrList = ctx.blendingEnabled
		    ? BlendFactorsToPvrList(ctx.blendSrc, ctx.blendDst)
		    : PVR_LIST_OP_POLY;

		if (tex != nullptr && tex->GetVramPtr() != nullptr) {
			int filter = Texture::ToNativeFilter(ctx.blendSrc == BlendFactor::One
			    ? tex->GetMinFilter() : tex->GetMagFilter());
			int uvclamp = Texture::ToNativeClamp(tex->GetWrapS());
			pvr_poly_cxt_txr(&cxt, pvrList, tex->GetNativeFormat(),
			    tex->GetWidth(), tex->GetHeight(), tex->GetVramPtr(), filter);
			cxt.gen.uv_clamp = uvclamp;
		} else {
			pvr_poly_cxt_col(&cxt, pvrList);
		}

		if (ctx.blendingEnabled) {
			cxt.blend.src = ToNativeBlendFactor(ctx.blendSrc);
			cxt.blend.dst = ToNativeBlendFactor(ctx.blendDst);
		}

		pvr_poly_compile(&outHdr, &cxt);
	}

	// =========================================================================
	// Draw call declarations — implemented in RHI_DC.cpp
	// =========================================================================

	/// Submit vertices from the host buffer to the active PVR display list.
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

} // namespace Rhi
