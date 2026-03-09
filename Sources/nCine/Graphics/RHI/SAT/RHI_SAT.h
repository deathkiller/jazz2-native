#pragma once

#if defined(WITH_RHI_SAT)

// ============================================================================
// Sega Saturn backend - libyaul VDP1 API
// Provides the Rhi interface for the Sega Saturn VDP1 sprite processor.
//
// The Saturn VDP1 hardware offers:
//   - Fixed-function sprite / polygon rendering via command tables
//   - Flat-shaded, Gouraud-shaded, and textured quads / polygons
//   - Hardware semi-transparency (4 modes, same as PS1)
//   - Texture formats: 4bpp CLUT, 8bpp CLUT, 15-bit RGB direct
//   - Screen coordinates are signed 16-bit integers relative to screen center
//   - NO programmable shaders
//   - NO hardware Z-buffer (command list order determines depth)
//   - NO mipmaps
//
// Background scrolling planes are handled by VDP2 and are outside this backend.
//
// Target SDK: libyaul (https://github.com/yaul-org/libyaul)
// Build: sh2-elf / m68k-elf cross-compiler, yaul toolchain
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent: RHI_CAP_SHADERS, RHI_CAP_FRAMEBUFFERS, RHI_CAP_MIPMAPS,
//         RHI_CAP_UNIFORM_BLOCKS, RHI_CAP_BINARY_SHADERS,
//         RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING, RHI_CAP_VAO,
//         RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_BUFFER_MAPPING		// All vertex data resides in host RAM (SH2 LWRAM/HWRAM)

// Fixed-function feature flags
#define RHI_FF_TINTED_SPRITE		// Gouraud shading / flat colour modulation
#define RHI_FF_ALPHA_BLEND			// Semi-transparency modes
#define RHI_FF_TEXTURING			// VDP1 VRAM sprite texturing

// ---------------------------------------------------------------------------
// libyaul headers - only available when targeting Sega Saturn
// ---------------------------------------------------------------------------
#include <yaul.h>

#include "../../Primitives/Rect.h"
#include "../../Primitives/Color.h"
#include "../RenderTypes.h"

#include <memory>
#include <cstdint>
#include <cstring>

namespace nCine::RHI
{
	// =========================================================================
	// VDP1 command table size
	// Each VDP1 command is 32 bytes; the list is stored in VDP1 VRAM.
	// kCmdListLen must not exceed the available VDP1 VRAM / 32.
	// =========================================================================
	static constexpr std::int32_t kCmdListLen = 1024; // max draw calls per frame

	// =========================================================================
	// Saturn screen parameters (NTSC 320×224 lo-res)
	// Screen-space origin is at the center of the display.
	// =========================================================================
	static constexpr std::int32_t kScreenW     = 320;
	static constexpr std::int32_t kScreenH     = 224;
	static constexpr std::int32_t kScreenCX    = kScreenW / 2;
	static constexpr std::int32_t kScreenCY    = kScreenH / 2;

	// =========================================================================
	// Buffer - host-side vertex / index storage
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
	// Texture - wraps a VDP1 VRAM allocation for textured sprites
	//
	// VDP1 textures must be stored in VDP1 VRAM (0x25C00000..0x25C7FFFF).
	// The address is expressed as a character base address (offset / 8).
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

		/// VDP1 character base address (byte offset into VDP1 VRAM / 8).
		inline std::uint32_t GetCharBase()    const { return charBase_;    }
		/// VDP1 color mode: 0=4bpp, 1=4bpp+highlight, 2=8bpp, 3=15bpp+shadow, 4=15bpp direct
		inline std::int32_t  GetColorMode()   const { return colorMode_;  }
		/// CLUT address (word offset in CRAM for palettised modes).
		inline std::uint16_t GetClutOffset()  const { return clutOffset_; }
		/// Pixel data pointer for uploading to VDP1 VRAM.
		inline const void*   GetPixelData()   const { return pixelData_.get(); }
		inline std::size_t   GetPixelDataSize() const { return dataSize_; }

		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel != 0 || data == nullptr) {
				return;
			}

			width_     = width;
			height_    = height;
			format_    = format;
			colorMode_ = ToColorMode(format);

			pixelData_ = std::make_unique<std::uint8_t[]>(size);
			std::memcpy(pixelData_.get(), data, size);
			dataSize_  = size;

			// Write pixel data to VDP1 VRAM if charBase_ has been assigned
			if (charBase_ != 0) {
				std::uint8_t* vramDst = reinterpret_cast<std::uint8_t*>(VDP1_VRAM(charBase_ * 8));
				std::memcpy(vramDst, pixelData_.get(), dataSize_);
			}
		}

		/// Set VDP1 VRAM address (must be called before UploadMip or after).
		void SetVramAddress(std::uint32_t charBase)
		{
			charBase_ = charBase;
			if (pixelData_ != nullptr && charBase_ != 0) {
				std::uint8_t* vramDst = reinterpret_cast<std::uint8_t*>(VDP1_VRAM(charBase_ * 8));
				std::memcpy(vramDst, pixelData_.get(), dataSize_);
			}
		}

		void SetClutOffset(std::uint16_t clutOffset)
		{
			clutOffset_ = clutOffset;
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

		/// VDP1 color mode from Rhi TextureFormat.
		static std::int32_t ToColorMode(TextureFormat fmt)
		{
			switch (fmt) {
				case TextureFormat::RGBA4:   return 0;  // 4bpp CLUT
				case TextureFormat::R8:
				case TextureFormat::RG8:     return 2;  // 8bpp CLUT
				case TextureFormat::RGB8:
				case TextureFormat::RGBA8:   return 4;  // 15bpp direct
				case TextureFormat::RGB5A1:  return 3;  // 15bpp with shadow
				default:                     return 4;
			}
		}

	private:
		std::unique_ptr<std::uint8_t[]> pixelData_;
		std::size_t     dataSize_     = 0;
		std::int32_t    width_        = 0;
		std::int32_t    height_       = 0;
		TextureFormat   format_       = TextureFormat::Unknown;
		std::int32_t    colorMode_    = 4;
		std::uint32_t   charBase_     = 0;     // VDP1 VRAM character base (byte_offset / 8)
		std::uint16_t   clutOffset_   = 0;
		SamplerFilter   minFilter_    = SamplerFilter::Nearest;
		SamplerFilter   magFilter_    = SamplerFilter::Nearest;
		SamplerWrapping wrapS_        = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_        = SamplerWrapping::ClampToEdge;
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