#pragma once

#if defined(WITH_RHI_SW)

// ============================================================================
// Software rendering backend
// Provides the GAPI interface using pure CPU rasterization.
// This backend targets low-end devices, retro consoles without a fixed-
// function or programmable 3-D GPU, and testing/reference builds.
//
// Feature set deliberately minimal - no programmable shaders.
// The renderer implements the same visual output as the GL backend via
// a fixed pipeline: transform → clip → rasterize → shade (fixed) → blit.
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Absent flags: RHI_CAP_SHADERS, RHI_CAP_UNIFORM_BLOCKS, RHI_CAP_BINARY_SHADERS
// RHI_CAP_DEPTHSTENCIL, RHI_CAP_INSTANCING, RHI_CAP_VAO, RHI_CAP_TEXTURE_FLOAT

#define RHI_CAP_FRAMEBUFFERS		// CPU-side offscreen surfaces
#define RHI_CAP_MIPMAPS				// Software mipmap lookup
#define RHI_CAP_BUFFER_MAPPING		// All buffers are always host-mapped

// Fixed-function configuration constants (replaces what shaders would do)
#define RHI_FF_TINTED_SPRITE		// Sprite multiply-tint without shaders
#define RHI_FF_ALPHA_BLEND			// Source-over alpha compositing

#include "../RenderTypes.h"

#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Vector2.h"
#include "../../../Primitives/Color.h"
#include "../../../Primitives/Colorf.h"

#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Buffer - host memory buffer standing in for a GPU buffer object
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

		inline BufferType  GetType()  const { return type_;          }
		inline BufferUsage GetUsage() const { return usage_;         }
		inline std::size_t GetSize()  const { return size_;          }
		inline const void* GetData()  const { return data_.get();    }
		inline       void* GetData()        { return data_.get();    }

		void SetData(std::size_t size, const void* data, BufferUsage usage = BufferUsage::Dynamic)
		{
			if (size > size_) {
				data_ = std::make_unique<std::uint8_t[]>(size);
			}
			size_ = size;
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

		/// Returns a pointer into the host buffer at the given offset.
		void* MapRange(std::size_t offset, std::size_t /*length*/, MapFlags /*flags*/)
		{
			return (data_.get() != nullptr && offset < size_) ? data_.get() + offset : nullptr;
		}

		void FlushRange(std::size_t /*offset*/, std::size_t /*length*/) { /* no-op: CPU coherent */ }
		void Unmap() { /* no-op */ }

	private:
		BufferType  type_;
		BufferUsage usage_;
		std::size_t size_;
		std::unique_ptr<std::uint8_t[]> data_;
	};

	// =========================================================================
	// Texture — RGBA8 surface in host memory
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 4;

		Texture() = default;
		~Texture() = default;
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()   const { return width_;   }
		inline std::int32_t  GetHeight()  const { return height_;  }
		inline std::int32_t  GetMipCount() const { return mipCount_; }
		inline TextureFormat GetFormat()  const { return format_;  }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS() const { return wrapS_; }
		inline SamplerWrapping GetWrapT() const { return wrapT_; }

		/// Allocate storage for a mip level.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height, TextureFormat format, const void* data, std::size_t size);

		/// Sample the texture using nearest-neighbour (no bilinear yet).
		nCine::Colorf Sample(float u, float v, std::int32_t mipLevel = 0) const;

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

		/// Returns raw RGBA8 pixel data for a given mip level (or nullptr).
		const std::uint8_t* GetPixels(std::int32_t mipLevel = 0) const;
		/// Returns mutable RGBA8 pixel data for a given mip level (or nullptr).
		std::uint8_t* GetMutablePixels(std::int32_t mipLevel = 0);
		/// Ensures mip 0 is allocated as a render target (RGBA8) at the texture's current width/height.
		void EnsureRenderTarget();

	private:
		struct MipLevel
		{
			std::int32_t width  = 0;
			std::int32_t height = 0;
			std::unique_ptr<std::uint8_t[]> data;
		};

		static constexpr std::int32_t MaxMips = 4;
		MipLevel mips_[MaxMips];
		std::int32_t  mipCount_  = 0;
		std::int32_t  width_     = 0;
		std::int32_t  height_    = 0;
		TextureFormat format_    = TextureFormat::Unknown;
		SamplerFilter minFilter_ = SamplerFilter::Nearest;
		SamplerFilter magFilter_ = SamplerFilter::Nearest;
		SamplerWrapping wrapS_   = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_   = SamplerWrapping::ClampToEdge;
	};

	// =========================================================================
	// Fixed-function shader state
	// Without programmable shaders we describe per-draw parameters as a plain
	// struct that the software rasterizer reads directly.
	// =========================================================================
	struct FFState
	{
		// Current transform (model-view-projection as 4×4, column-major)
		float mvpMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		float color[4] = { 1, 1, 1, 1 };		// Tint / solid colour
		float texRect[4] = { 0, 0, 1, 1 };		// UV sub-rect (x, y, w, h)
		float spriteSize[2] = { 1, 1 };			// Sprite pixel size
		float depth = 0.0f;
		bool  hasTexture = false;
		std::int32_t textureUnit = 0;
	};

	// =========================================================================
	// ShaderUniforms stub — fixed-function: just stores named float values
	// =========================================================================
	class UniformCache; // forward decl for UniformHashMapType

	class ShaderUniforms
	{
	public:
		// Dummy map type — the SW backend has no real per-uniform storage
		using UniformHashMapType = int;

		ShaderUniforms() = default;

		// Stub API to match GLShaderUniforms usage pattern
		inline void SetProgram(void* /*program*/, const char* /*includeOnly*/, const char* /*exclude*/) {}
		inline void SetUniformsDataPointer(std::uint8_t* /*ptr*/) {}
		inline void SetDirty(bool /*isDirty*/) {}
		inline bool HasUniform(const char* /*name*/) const { return false; }
		inline void CommitUniforms() {}

		inline std::int32_t GetUniformCount() const { return 0; }
		inline UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		inline UniformHashMapType GetAllUniforms() const { return 0; }

		FFState& GetFFState() { return ffState_; }
		const FFState& GetFFState() const { return ffState_; }

	private:
		FFState ffState_;
	};

	// =========================================================================
	// ShaderUniformBlocks stub
	// =========================================================================
	class UniformBlockCache; // forward decl for UniformHashMapType

	class ShaderUniformBlocks
	{
	public:
		// Dummy map type — the SW backend has no real per-block storage
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
	// UniformCache / UniformBlockCache stubs
	// =========================================================================
	class UniformCache
	{
	public:
		// Float setters
		inline bool SetFloatVector(const float* /*values*/) { return true; }
		inline bool SetFloatValue(float /*v0*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/) { return true; }
		inline bool SetFloatValue(float /*v0*/, float /*v1*/, float /*v2*/, float /*v3*/) { return true; }
		// Integer setters
		inline bool SetIntVector(const std::int32_t* /*values*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/) { return true; }
		inline bool SetIntValue(std::int32_t /*v0*/, std::int32_t /*v1*/, std::int32_t /*v2*/, std::int32_t /*v3*/) { return true; }
		// Getter
		inline std::int32_t GetIntValue(std::int32_t /*componentIdx*/) const { return 0; }
		// Dirty flag — no-op for the SW backend
		inline void SetDirty(bool /*isDirty*/) {}
	};

	class UniformBlockCache
	{
	public:
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }

		// Size / alignment — always 0 for the SW backend (no UBO)
		inline std::uint32_t GetSize()        const { return 0; }
		inline std::uint32_t GetAlignAmount() const { return 0; }

		// Data pointer — not backed by real memory in SW
		inline const std::uint8_t* GetDataPointer() const { return nullptr; }

		// Copy operations — no-ops
		inline bool CopyData(const std::uint8_t* /*src*/)                                           { return false; }
		inline bool CopyData(std::uint32_t /*destIndex*/, const std::uint8_t* /*src*/, std::uint32_t /*bytes*/) { return false; }
		inline void SetUsedSize(std::uint32_t /*size*/) {}
	};

	// =========================================================================
	// ShaderProgram stub — no GPU shaders; renders via fixed-function pipeline
	// =========================================================================
	class ShaderProgram
	{
	public:
		// Satisfy the interface expected by Material / RenderResources
		enum class Status { NotLinked, LinkedWithIntrospection, Linked };
		enum class Introspection { Enabled, NoUniformsInBlocks, Disabled };
		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram() = default;
		Status GetStatus() const { return Status::LinkedWithIntrospection; }
		std::int32_t GetUniformsSize()      const { return 0; }
		std::int32_t GetUniformBlocksSize() const { return 0; }
		std::int32_t GetAttributeCount()    const { return 0; }
		std::int32_t GetBatchSize()         const { return 0; }
		UniformCache* GetUniform(const char* /*name*/) { return nullptr; }
		bool IsLinked() const { return true; }
		void Use() {}

		/// Vertex-format binding — no-op for the fixed-function SW pipeline
		void DefineVertexFormat(const Buffer* /*vbo*/, const Buffer* /*ibo*/, std::uint32_t /*vboOffset*/) {}

		FFState ffState; // direct parameter access for fixed-function draws
	};

	// =========================================================================
	// VertexFormat — describes how vertices are arranged in a Buffer
	// =========================================================================
	struct VertexFormatAttribute
	{
		bool          enabled    = false;
		std::uint32_t index      = 0;
		std::int32_t  size       = 4;    // number of components
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
			if (attrCount_ != other.attrCount_) return false;
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

	/// Dummy VAO type — the SW backend doesn't need an OS object, just the format description
	using VertexArrayObject = VertexFormat;

	// =========================================================================
	// Framebuffer / Renderbuffer — CPU-side RGBA8 surfaces
	// =========================================================================
	class Renderbuffer
	{
	public:
		Renderbuffer() = default;
		void Storage(RenderbufferFormat /*format*/, std::int32_t width, std::int32_t height)
		{
			width_  = width;
			height_ = height;
		}
		std::int32_t GetWidth()  const { return width_;  }
		std::int32_t GetHeight() const { return height_; }
	private:
		std::int32_t width_  = 0;
		std::int32_t height_ = 0;
	};

	class Framebuffer
	{
	public:
		Framebuffer() = default;
		void AttachTexture(FramebufferAttachment /*slot*/, Texture* texture) { colorTarget_ = texture; }
		void AttachRenderbuffer(FramebufferAttachment /*slot*/, Renderbuffer* /*rb*/) {}
		bool IsComplete() const { return colorTarget_ != nullptr; }
		Texture* GetColorTarget() { return colorTarget_; }
	private:
		Texture* colorTarget_ = nullptr;
	};

	// =========================================================================
	// Constant
	// =========================================================================
	static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnitsConst;

	// =========================================================================
	// Buffer factory helpers
	// =========================================================================
	inline std::unique_ptr<Buffer> CreateBuffer(BufferType type)
	{
		return std::make_unique<Buffer>(type);
	}

	// =========================================================================
	// Buffer operation wrappers (mirrors RHI_GL.h interface)
	// =========================================================================

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
		return true; // No binding concept in SW renderer
	}

	inline void BindBufferBase(Buffer& /*buf*/, std::uint32_t /*index*/) {}

	inline void BindBufferRange(Buffer& /*buf*/, std::uint32_t /*index*/, std::size_t /*offset*/, std::size_t /*size*/) {}

	template<typename StringViewType>
	inline void SetBufferLabel(Buffer& /*buf*/, StringViewType /*label*/) {}

	// =========================================================================
	// Texture operation wrappers (mirrors RHI_GL.h interface)
	// =========================================================================

	inline std::unique_ptr<Texture> CreateTexture()
	{
		return std::make_unique<Texture>();
	}

	inline void BindTexture(const Texture& /*tex*/, std::uint32_t /*unit*/)
	{
		// SW renderer doesn't need explicit binding — textures are accessed via DrawContext
	}

	inline void UnbindTexture(std::uint32_t /*unit*/) {}

	// =========================================================================
	// Draw calls
	// Full implementation lives in RHI_SW.cpp; declarations are here.
	// =========================================================================

	/// Per-draw context set before issuing draw calls.
	struct DrawContext
	{
		const VertexFormat* vertexFormat  = nullptr;
		std::size_t         vboByteOffset = 0;  // byte offset within the VBO
		Texture*            textures[MaxTextureUnits] = {};
		FFState             ff;

		bool   blendingEnabled  = false;
		BlendFactor blendSrc    = BlendFactor::SrcAlpha;
		BlendFactor blendDst    = BlendFactor::OneMinusSrcAlpha;
		bool   scissorEnabled   = false;
		nCine::Recti scissorRect;
	};

	/// Sets the active DrawContext, making it available to the next nCine::RHI::Draw*() call.
	void SetDrawContext(const DrawContext& ctx);
	/// Clears the active DrawContext (disables rasterization until a new one is set).
	void ClearDrawContext();

	/// Rasterizes triangles / quads from a vertex buffer using the active DrawContext.
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count);
	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount);
	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex = 0);
	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex = 0);

	// =========================================================================
	// Render-state helpers (software equivalents)
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst);
	void SetDepthTest(bool enabled);
	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetScissorTest(bool enabled, const nCine::Recti& rect);
	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
	void SetClearColor(float r, float g, float b, float a);
	void Clear(ClearFlags flags);

	// SW-specific: state accessors used by RenderCommand
	struct ScissorState { bool enabled; std::int32_t x, y, w, h; };
	ScissorState GetScissorState();
	void         SetScissorState(const ScissorState& state);

	struct ViewportState { std::int32_t x, y, w, h; };
	ViewportState GetViewportState();
	void          SetViewportState(const ViewportState& s);

	struct ClearColorState { float r, g, b, a; };
	ClearColorState GetClearColorState();
	void            SetClearColorState(const ClearColorState& s);

	/// Depth mask is a no-op in software rendering (no GPU depth buffer).
	inline void SetDepthMask(bool /*enabled*/) {}

	// -------------------------------------------------------------------------
	// Framebuffer helpers — mirrors the RHI_GL.h interface, SW implementations
	// -------------------------------------------------------------------------

	/// Redirects SW rasterizer output to the framebuffer's attached texture.
	void FramebufferBind(Framebuffer& fbo);
	/// Restores the SW rasterizer output to the main window colour buffer.
	void FramebufferUnbind();

	inline void FramebufferSetDrawBuffers(Framebuffer& /*fbo*/, std::uint32_t /*n*/) {}  // no-op

	inline void FramebufferAttachTexture(Framebuffer& fbo, Texture& texture, std::uint32_t colorIndex)
	{
		fbo.AttachTexture(FramebufferAttachment(colorIndex), &texture);
	}

	inline void FramebufferDetachTexture(Framebuffer& fbo, std::uint32_t colorIndex)
	{
		fbo.AttachTexture(FramebufferAttachment(colorIndex), nullptr);
	}

	inline bool FramebufferIsComplete(Framebuffer& fbo)
	{
		return fbo.IsComplete();
	}

	template<typename StringViewType>
	inline void FramebufferSetLabel(Framebuffer& /*fbo*/, StringViewType /*label*/) {}  // no-op

	// =========================================================================
	// Framebuffer management — used by desktop presenters (GLFW/SDL2 blit)
	// =========================================================================

	/// Allocate (or reallocate) the internal RGBA8 color buffer.
	/// Must be called before the first frame and on every window resize.
	void ResizeColorBuffer(std::int32_t width, std::int32_t height);

	/// Returns a pointer to the current RGBA8 color buffer (row-major, top-down).
	/// Valid until the next ResizeColorBuffer() call.
	const std::uint8_t* GetColorBuffer();

	/// Returns the current color buffer width in pixels.
	std::int32_t GetColorBufferWidth();

	/// Returns the current color buffer height in pixels.
	std::int32_t GetColorBufferHeight();

}

#endif