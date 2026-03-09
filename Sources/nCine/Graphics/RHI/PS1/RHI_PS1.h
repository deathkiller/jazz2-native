#pragma once

#if defined(WITH_RHI_PS1)

// ============================================================================
// PlayStation 1 backend - PSn00bSDK GPU API
// Provides the Rhi interface for the original PlayStation (PsyQ / PSn00bSDK).
//
// The PS1 GPU hardware offers:
//   - Fixed-function Gouraud shading, perspective-correct texture mapping
//   - Semi-transparency (4 modes: add, sub, avg, add-subtract)
//   - Ordering table (OT) for depth priority instead of a hardware Z-buffer
//   - Texture formats: 4bpp CLUT, 8bpp CLUT, 15-bit direct
//   - Texture pages: 256×256 regions within a 1024×512 VRAM
//   - Integer vertex coordinates (11-bit signed sub-pixel precision in GPU)
//   - NO programmable shaders
//   - NO hardware Z-buffer (use OT depth)
//   - NO mipmaps
//
// Target SDK: PSn00bSDK (https://github.com/Lameguy64/PSn00bSDK)
// Build: mipsel-none-elf cross-compiler, ps1-cmake toolchain
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_MIPMAPS,
//         RHI_CAP_UNIFORM_BLOCKS, RHI_CAP_BINARY_SHADERS,
//         RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING, RHI_CAP_VAO,
//         RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_BUFFER_MAPPING		// All vertex data resides in main RAM

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE		// Per-vertex RGB modulation (POLY_FT4 RGB0-3)
#define RHI_FF_ALPHA_BLEND			// Semi-transparency via setABRMode
#define RHI_FF_TEXTURING			// Texture page + CLUT mapping

// ---------------------------------------------------------------------------
// PSn00bSDK headers - only available when targeting PlayStation 1
// ---------------------------------------------------------------------------
#include <psxgpu.h>
#include <psxgte.h>

#include "../../Primitives/Rect.h"
#include "../../Primitives/Color.h"
#include "../RenderTypes.h"

#include <memory>
#include <cstdint>
#include <cstring>

namespace nCine::RHI
{
	// =========================================================================
	// Ordering-table configuration
	// PS1 has no hardware Z-buffer; instead draw calls are sorted into a
	// linked list (OT) keyed by depth. A larger OT produces finer depth
	// granularity at the cost of more memory + CPU time to clear it.
	// =========================================================================
	static constexpr std::int32_t kOtLen      = 512;	// ordering table entries
	static constexpr std::int32_t kPrimBufSz  = 32768;	// primitive work buffer per frame (bytes)

	// =========================================================================
	// Buffer — host-side vertex / index storage
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
	// VRAM region - describes an area in the 1024×512 PlayStation VRAM
	// PS1 textures are uploaded into VRAM via DMA (LoadImage / StoreImage).
	// =========================================================================
	struct VramRegion
	{
		std::int16_t x = 0;     // VRAM X (must be multiple of 64 for tex pages)
		std::int16_t y = 0;     // VRAM Y (0 or 256)
		std::int16_t w = 0;     // Width in 16-bit (VRAM) pixels
		std::int16_t h = 0;     // Height
		bool         valid = false;
	};

	// =========================================================================
	// Texture - wraps a VRAM allocation + texture-page + CLUT descriptor
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1;

		Texture() = default;
		~Texture() = default;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()    const { return width_;    }
		inline std::int32_t  GetHeight()   const { return height_;   }
		inline std::int32_t  GetMipCount() const { return 1;         }
		inline TextureFormat GetFormat()   const { return format_;   }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS()  const { return wrapS_;    }
		inline SamplerWrapping GetWrapT()  const { return wrapT_;    }

		/// Packed TPage word returned by getTPage(); set in POLY_FT4.tpage.
		inline std::uint16_t GetTPage()  const { return tpage_;  }
		/// Packed CLUT word returned by getClut(); 0 for 15-bit direct.
		inline std::uint16_t GetClut()   const { return clut_;   }
		/// Pixel format: 0=4bpp, 1=8bpp, 2=15bpp
		inline std::int32_t  GetMode()   const { return mode_;   }
		inline const VramRegion& GetRegion() const { return region_; }

		/// Upload pixel data to VRAM using DMA.
		/// Caller must have called InitVram() to set up region_ first.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel != 0 || data == nullptr) {
				return;
			}

			width_  = width;
			height_ = height;
			format_ = format;
			mode_   = ToNativeMode(format);

			// Copy to a staging buffer for VRAM upload
			staging_ = std::make_unique<std::uint8_t[]>(size);
			std::memcpy(staging_.get(), data, size);

			if (!region_.valid) {
				return; // VramRegion must be set before uploading
			}

			// Upload to VRAM via DMA
			RECT rect;
			setRECT(&rect, region_.x, region_.y, static_cast<short>(width), static_cast<short>(height));
			LoadImage(&rect, reinterpret_cast<const std::uint32_t*>(staging_.get()));
			DrawSync(0);

			// Compute texture page word — tx in 64-pixel steps, ty in 256-row steps
			tpage_ = static_cast<std::uint16_t>(getTPage(mode_, 0,
			    region_.x / 64, region_.y / 256));
		}

		/// Set VRAM region (must be called before UploadMip).
		void InitVram(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h)
		{
			region_ = { x, y, w, h, true };
		}

		/// Set CLUT (colour look-up table) address for 4/8-bpp textures.
		void SetClut(std::int16_t cx, std::int16_t cy)
		{
			clut_ = static_cast<std::uint16_t>(getClut(cx, cy));
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

		/// PS1 pixel mode: 0 = 4bpp CLUT, 1 = 8bpp CLUT, 2 = 15-bit direct, 3 = 24-bit
		static std::int32_t ToNativeMode(TextureFormat fmt)
		{
			switch (fmt) {
				case TextureFormat::RGBA8:   return 2; // store as 15-bit direct (loses alpha precision)
				case TextureFormat::RGB8:    return 2;
				case TextureFormat::RGBA4:   return 0; // 4bpp CLUT
				case TextureFormat::RGB5A1:  return 2;
				case TextureFormat::R8:      return 1; // 8bpp CLUT
				default:                     return 2;
			}
		}

	private:
		std::unique_ptr<std::uint8_t[]> staging_;
		VramRegion      region_;
		std::int32_t    width_         = 0;
		std::int32_t    height_        = 0;
		TextureFormat   format_        = TextureFormat::Unknown;
		std::int32_t    mode_          = 2;     // default: 15-bit direct
		std::uint16_t   tpage_         = 0;
		std::uint16_t   clut_          = 0;
		SamplerFilter   minFilter_     = SamplerFilter::Nearest;
		SamplerFilter   magFilter_     = SamplerFilter::Nearest;
		SamplerWrapping wrapS_         = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_         = SamplerWrapping::ClampToEdge;
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
		float depth         = 0.5f;  // Maps to OT index (0 = back, kOtLen-1 = front)
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

		inline void SetProgram(void*, const char*, const char*) {}
		inline void SetUniformsDataPointer(std::uint8_t*) {}
		inline void SetDirty(bool) {}
		inline bool HasUniform(const char*) const { return false; }
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

		inline void SetProgram(void*) {}
		inline void SetUniformsDataPointer(std::uint8_t*) {}
		inline bool HasUniformBlock(const char*) const { return false; }
		inline void Bind() {}
	};

	// =========================================================================
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		inline void SetFloatVector(const float*) {}
		inline void SetFloatValue(float) {}
		inline void SetIntValue(std::int32_t) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char*) { return nullptr; }
	};

	// =========================================================================
	// ShaderProgram stub
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
		UniformCache* GetUniform(const char*) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		FFState ffState;
	};

	// =========================================================================
	// VertexFormat
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
				    a.type    != b.type    || a.normalized != b.normalized ||
				    a.stride  != b.stride  || a.offset     != b.offset || a.vbo != b.vbo) {
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

		/// PS1 depth slot in the ordering table (0 = furthest back)
		std::int32_t otDepth = kOtLen / 2;
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

	void BindTexture(Texture& tex, std::uint32_t unit);
	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	template<typename StringViewType>
	inline void SetTextureLabel(Texture& /*tex*/, StringViewType /*label*/) {}

	// =========================================================================
	// Draw call declarations
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

}

#endif