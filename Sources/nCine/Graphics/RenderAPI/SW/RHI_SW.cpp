#include "RHI_SW.h"

#include <cstring>
#include <cassert>
#include <algorithm>
#include <cmath>

namespace Rhi
{
	// =========================================================================
	// Global SW render state
	// =========================================================================
	namespace
	{
		struct SWState
		{
			// Colour buffer (RGBA8, row-major)
			std::uint8_t* colorBuffer   = nullptr;
			std::int32_t  bufferWidth   = 0;
			std::int32_t  bufferHeight  = 0;

			// Depth buffer (float, same dimensions)
			float*        depthBuffer   = nullptr;

			bool   depthTestEnabled  = false;
			bool   blendingEnabled   = false;
			BlendFactor blendSrc     = BlendFactor::SrcAlpha;
			BlendFactor blendDst     = BlendFactor::OneMinusSrcAlpha;
			bool   scissorEnabled    = false;
			std::int32_t  scissorX   = 0;
			std::int32_t  scissorY   = 0;
			std::int32_t  scissorW   = 0;
			std::int32_t  scissorH   = 0;

			float clearR = 0, clearG = 0, clearB = 0, clearA = 1;

			// Viewport
			std::int32_t viewportX = 0;
			std::int32_t viewportY = 0;
			std::int32_t viewportW = 0;
			std::int32_t viewportH = 0;

			// Active draw context for the current draw call
			const DrawContext* drawCtx = nullptr;
		};

		SWState g_state;
	}

	// =========================================================================
	// Texture implementation
	// =========================================================================
	void Texture::UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height, TextureFormat format,
	                        const void* data, std::size_t size)
	{
		if (mipLevel < 0 || mipLevel >= MaxMips) return;

		mips_[mipLevel].width  = width;
		mips_[mipLevel].height = height;

		std::size_t byteSize = size;
		// Only RGBA8 is directly stored; other formats converted at upload
		if (format == TextureFormat::RGBA8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		} else if (format == TextureFormat::RGB8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		}

		mips_[mipLevel].data = std::make_unique<std::uint8_t[]>(byteSize);

		if (data != nullptr) {
			if (format == TextureFormat::RGBA8) {
				std::memcpy(mips_[mipLevel].data.get(), data, byteSize);
			} else if (format == TextureFormat::RGB8) {
				// Convert RGB8 → RGBA8
				const std::uint8_t* src  = static_cast<const std::uint8_t*>(data);
				std::uint8_t*       dst  = mips_[mipLevel].data.get();
				std::int32_t pixels = width * height;
				for (std::int32_t i = 0; i < pixels; ++i) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
					src += 3;
					dst += 4;
				}
			} else {
				// Generic fallback: copy raw bytes
				std::memcpy(mips_[mipLevel].data.get(), data, size);
			}
		}

		if (mipLevel == 0) {
			width_    = width;
			height_   = height;
			format_   = format;
		}
		if (mipLevel + 1 > mipCount_) {
			mipCount_ = mipLevel + 1;
		}
	}

	const std::uint8_t* Texture::GetPixels(std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) return nullptr;
		return mips_[mipLevel].data.get();
	}

	nCine::Colorf Texture::Sample(float u, float v, std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) {
			return nCine::Colorf(1, 1, 1, 1);
		}

		const MipLevel& mip = mips_[mipLevel];
		if (mip.data == nullptr || mip.width == 0 || mip.height == 0) {
			return nCine::Colorf(1, 1, 1, 1);
		}

		// Apply wrapping
		auto wrapCoord = [](float t, SamplerWrapping mode) -> float {
			switch (mode) {
				case SamplerWrapping::Repeat:
					t -= std::floor(t);
					return t;
				case SamplerWrapping::MirroredRepeat: {
					float f = std::floor(t);
					t -= f;
					if (static_cast<std::int32_t>(f) & 1) t = 1.0f - t;
					return t;
				}
				case SamplerWrapping::ClampToEdge:
				default:
					return std::fmax(0.0f, std::fmin(1.0f, t));
			}
		};

		u = wrapCoord(u, wrapS_);
		v = wrapCoord(v, wrapT_);

		const std::int32_t px = static_cast<std::int32_t>(u * (mip.width  - 1) + 0.5f);
		const std::int32_t py = static_cast<std::int32_t>(v * (mip.height - 1) + 0.5f);
		const std::int32_t clampedX = std::max(0, std::min(mip.width  - 1, px));
		const std::int32_t clampedY = std::max(0, std::min(mip.height - 1, py));

		const std::uint8_t* pixel = mip.data.get() + (clampedY * mip.width + clampedX) * 4;
		return nCine::Colorf(pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f);
	}

	// =========================================================================
	// Render-state setters
	// =========================================================================
	void SetBlending(bool enabled, BlendFactor src, BlendFactor dst)
	{
		g_state.blendingEnabled = enabled;
		g_state.blendSrc = src;
		g_state.blendDst = dst;
	}

	void SetDepthTest(bool enabled)
	{
		g_state.depthTestEnabled = enabled;
	}

	void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.scissorEnabled = enabled;
		g_state.scissorX = x;
		g_state.scissorY = y;
		g_state.scissorW = width;
		g_state.scissorH = height;
	}

	void SetScissorTest(bool enabled, const nCine::Recti& rect)
	{
		SetScissorTest(enabled, rect.X, rect.Y, rect.W, rect.H);
	}

	void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.viewportX = x;
		g_state.viewportY = y;
		g_state.viewportW = width;
		g_state.viewportH = height;
	}

	void SetClearColor(float r, float g, float b, float a)
	{
		g_state.clearR = r;
		g_state.clearG = g;
		g_state.clearB = b;
		g_state.clearA = a;
	}

	void Clear(ClearFlags flags)
	{
		const bool doColor = (static_cast<std::uint32_t>(flags & ClearFlags::Color) != 0);
		const bool doDepth = (static_cast<std::uint32_t>(flags & ClearFlags::Depth) != 0);

		if (doColor && g_state.colorBuffer != nullptr) {
			const std::uint8_t r = static_cast<std::uint8_t>(g_state.clearR * 255.0f);
			const std::uint8_t gv = static_cast<std::uint8_t>(g_state.clearG * 255.0f);
			const std::uint8_t b = static_cast<std::uint8_t>(g_state.clearB * 255.0f);
			const std::uint8_t a = static_cast<std::uint8_t>(g_state.clearA * 255.0f);
			std::int32_t totalPixels = g_state.bufferWidth * g_state.bufferHeight;
			std::uint8_t* dst = g_state.colorBuffer;
			for (std::int32_t i = 0; i < totalPixels; ++i) {
				dst[0] = r; dst[1] = gv; dst[2] = b; dst[3] = a;
				dst += 4;
			}
		}

		if (doDepth && g_state.depthBuffer != nullptr) {
			std::int32_t totalPixels = g_state.bufferWidth * g_state.bufferHeight;
			for (std::int32_t i = 0; i < totalPixels; ++i) {
				g_state.depthBuffer[i] = 1.0f;
			}
		}
	}

	ScissorState GetScissorState()
	{
		return { g_state.scissorEnabled, g_state.scissorX, g_state.scissorY, g_state.scissorW, g_state.scissorH };
	}

	void SetScissorState(const ScissorState& state)
	{
		g_state.scissorEnabled = state.enabled;
		g_state.scissorX = state.x;
		g_state.scissorY = state.y;
		g_state.scissorW = state.w;
		g_state.scissorH = state.h;
	}

	// =========================================================================
	// Blending helper — applies source-over (or other configured blend) for
	// a single pixel.
	// =========================================================================
	namespace
	{
		inline float BlendComponent(BlendFactor f, float srcR, float srcG, float srcB, float srcA,
		                            float dstR, float dstG, float dstB, float dstA, float component,
		                            bool isSrc)
		{
			(void)dstR; (void)dstG; (void)dstB;
			switch (f) {
				case BlendFactor::Zero:             return 0.0f;
				case BlendFactor::One:              return component;
				case BlendFactor::SrcAlpha:         return component * srcA;
				case BlendFactor::OneMinusSrcAlpha: return component * (1.0f - srcA);
				case BlendFactor::DstAlpha:         return component * dstA;
				case BlendFactor::OneMinusDstAlpha: return component * (1.0f - dstA);
				case BlendFactor::SrcColor:         return component * (isSrc ? 1.0f : srcR); // rough
				case BlendFactor::DstColor:         return component * (isSrc ? dstR : 1.0f); // rough
				default:                            return component;
			}
		}

		inline void BlendPixel(std::uint8_t* dst,
		                       float srcR, float srcG, float srcB, float srcA,
		                       BlendFactor sFactor, BlendFactor dFactor)
		{
			const float dstR = dst[0] / 255.0f;
			const float dstG = dst[1] / 255.0f;
			const float dstB = dst[2] / 255.0f;
			const float dstA = dst[3] / 255.0f;

			auto bf = [&](BlendFactor f, float c, bool src) {
				return BlendComponent(f, srcR, srcG, srcB, srcA, dstR, dstG, dstB, dstA, c, src);
			};

			const float outR = bf(sFactor, srcR, true)  + bf(dFactor, dstR, false);
			const float outG = bf(sFactor, srcG, true)  + bf(dFactor, dstG, false);
			const float outB = bf(sFactor, srcB, true)  + bf(dFactor, dstB, false);
			const float outA = bf(sFactor, srcA, true)  + bf(dFactor, dstA, false);

			auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
			dst[0] = static_cast<std::uint8_t>(clamp01(outR) * 255.0f);
			dst[1] = static_cast<std::uint8_t>(clamp01(outG) * 255.0f);
			dst[2] = static_cast<std::uint8_t>(clamp01(outB) * 255.0f);
			dst[3] = static_cast<std::uint8_t>(clamp01(outA) * 255.0f);
		}

		// Write a fragment at screen position (px, py) with RGBA colour.
		inline void WriteFragment(std::int32_t px, std::int32_t py, float r, float g, float b, float a)
		{
			if (g_state.colorBuffer == nullptr) return;

			// Scissor test
			if (g_state.scissorEnabled) {
				if (px < g_state.scissorX || px >= g_state.scissorX + g_state.scissorW ||
				    py < g_state.scissorY || py >= g_state.scissorY + g_state.scissorH) {
					return;
				}
			}

			// Bounds check
			if (px < 0 || py < 0 || px >= g_state.bufferWidth || py >= g_state.bufferHeight) return;

			std::uint8_t* dst = g_state.colorBuffer + (py * g_state.bufferWidth + px) * 4;

			if (g_state.blendingEnabled) {
				BlendPixel(dst, r, g, b, a, g_state.blendSrc, g_state.blendDst);
			} else {
				auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
				dst[0] = static_cast<std::uint8_t>(clamp01(r) * 255.0f);
				dst[1] = static_cast<std::uint8_t>(clamp01(g) * 255.0f);
				dst[2] = static_cast<std::uint8_t>(clamp01(b) * 255.0f);
				dst[3] = static_cast<std::uint8_t>(clamp01(a) * 255.0f);
			}
		}

		// Fetch vertex position (xy) from the active vertex buffer.
		// Assumes pos is at offset=0, float[2].
		struct Vertex2D { float x, y, u, v, r, g, b, a; };

		Vertex2D FetchVertex(const DrawContext& ctx, std::int32_t index)
		{
			Vertex2D out = { 0, 0, 0, 0, 1, 1, 1, 1 };
			if (ctx.vertexFormat == nullptr) return out;

			const std::uint32_t attrCount = ctx.vertexFormat->GetAttributeCount();
			for (std::uint32_t i = 0; i < attrCount; ++i) {
				const VertexFormatAttribute& attr = ctx.vertexFormat->GetAttribute(i);
				if (!attr.enabled || attr.vbo == nullptr) continue;

				const std::uint8_t* base = static_cast<const std::uint8_t*>(attr.vbo->GetData());
				if (base == nullptr) continue;

				std::int32_t stride = attr.stride;
				if (stride == 0) stride = attr.size * 4; // assume float

				const std::uint8_t* ptr = base + ctx.vboByteOffset + static_cast<std::size_t>(index) * stride + attr.offset;

				// Attribute 0 = position (xy)
				if (i == 0 && attr.size >= 2) {
					const float* f = reinterpret_cast<const float*>(ptr);
					out.x = f[0];
					out.y = f[1];
				}
				// Attribute 1 = texcoords (uv)
				else if (i == 1 && attr.size >= 2) {
					const float* f = reinterpret_cast<const float*>(ptr);
					out.u = f[0];
					out.v = f[1];
				}
			}

			// Apply fixed-function tint from the ff state
			out.r = ctx.ff.color[0];
			out.g = ctx.ff.color[1];
			out.b = ctx.ff.color[2];
			out.a = ctx.ff.color[3];

			return out;
		}

		// Rasterise a single 2-D triangle (already in screen space).
		void RasteriseTriangle(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2)
		{
			// Bounding box in screen space
			const std::int32_t minX = std::max(0, static_cast<std::int32_t>(std::min({v0.x, v1.x, v2.x})));
			const std::int32_t maxX = std::min(g_state.bufferWidth  - 1, static_cast<std::int32_t>(std::max({v0.x, v1.x, v2.x})));
			const std::int32_t minY = std::max(0, static_cast<std::int32_t>(std::min({v0.y, v1.y, v2.y})));
			const std::int32_t maxY = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(std::max({v0.y, v1.y, v2.y})));

			// Edge function
			auto edgeFn = [](const Vertex2D& a, const Vertex2D& b, float px, float py) -> float {
				return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
			};

			const float area = edgeFn(v0, v1, v2.x, v2.y);
			if (std::fabs(area) < 1e-6f) return; // degenerate

			Texture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < MaxTextureUnits)
			             ? ctx.textures[ctx.ff.textureUnit]
			             : nullptr;

			for (std::int32_t py = minY; py <= maxY; ++py) {
				for (std::int32_t px = minX; px <= maxX; ++px) {
					const float fpx = px + 0.5f;
					const float fpy = py + 0.5f;

					const float w0 = edgeFn(v1, v2, fpx, fpy);
					const float w1 = edgeFn(v2, v0, fpx, fpy);
					const float w2 = edgeFn(v0, v1, fpx, fpy);

					// Same sign as area = inside triangle
					if ((w0 >= 0) == (area >= 0) && (w1 >= 0) == (area >= 0) && (w2 >= 0) == (area >= 0)) {
						const float bary0 = w0 / area;
						const float bary1 = w1 / area;
						const float bary2 = w2 / area;

						const float u = bary0 * v0.u + bary1 * v1.u + bary2 * v2.u;
						const float v  = bary0 * v0.v + bary1 * v1.v + bary2 * v2.v;

						float r = ctx.ff.color[0];
						float g = ctx.ff.color[1];
						float b = ctx.ff.color[2];
						float a = ctx.ff.color[3];

						if (tex != nullptr) {
							const nCine::Colorf texel = tex->Sample(u, v, 0);
							r *= texel.R();
							g *= texel.G();
							b *= texel.B();
							a *= texel.A();
						}

						WriteFragment(px, py, r, g, b, a);
					}
				}
			}
		}

		void DrawPrimitive(const DrawContext& ctx, PrimitiveType type,
		                   const std::vector<std::int32_t>& indices)
		{
			switch (type) {
				case PrimitiveType::Triangles: {
					for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
						RasteriseTriangle(ctx,
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]),
						    FetchVertex(ctx, indices[i + 2]));
					}
					break;
				}
				case PrimitiveType::TriangleStrip: {
					for (std::size_t i = 0; i + 2 < indices.size(); ++i) {
						if (i & 1) {
							RasteriseTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 2]),
							    FetchVertex(ctx, indices[i + 1]));
						} else {
							RasteriseTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 1]),
							    FetchVertex(ctx, indices[i + 2]));
						}
					}
					break;
				}
				case PrimitiveType::TriangleFan: {
					for (std::size_t i = 1; i + 1 < indices.size(); ++i) {
						RasteriseTriangle(ctx,
						    FetchVertex(ctx, indices[0]),
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]));
					}
					break;
				}
				default:
					// Points and lines are not rendered in the minimal SW backend
					break;
			}
		}
	} // anonymous namespace

	// =========================================================================
	// Public draw entry-points
	// =========================================================================
	void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		if (g_state.drawCtx == nullptr) return;
		std::vector<std::int32_t> indices(static_cast<std::size_t>(count));
		for (std::int32_t i = 0; i < count; ++i) indices[i] = firstVertex + i;
		DrawPrimitive(*g_state.drawCtx, type, indices);
	}

	void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		for (std::int32_t inst = 0; inst < instanceCount; ++inst) {
			Draw(type, firstVertex, count);
		}
	}

	void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex)
	{
		if (g_state.drawCtx == nullptr) return;
		const std::uint16_t* iboPtr = static_cast<const std::uint16_t*>(indexOffset);
		std::vector<std::int32_t> indices(static_cast<std::size_t>(count));
		for (std::int32_t i = 0; i < count; ++i) {
			indices[i] = static_cast<std::int32_t>(iboPtr[i]) + baseVertex;
		}
		DrawPrimitive(*g_state.drawCtx, type, indices);
	}

	void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset,
	                          std::int32_t instanceCount, std::int32_t baseVertex)
	{
		for (std::int32_t inst = 0; inst < instanceCount; ++inst) {
			DrawIndexed(type, count, indexOffset, baseVertex);
		}
	}

	// =========================================================================
	// Framebuffer management
	// =========================================================================

	void ResizeColorBuffer(std::int32_t width, std::int32_t height)
	{
		if (g_state.bufferWidth == width && g_state.bufferHeight == height) {
			return;
		}

		g_state.bufferWidth  = width;
		g_state.bufferHeight = height;

		const std::size_t colorBytes = static_cast<std::size_t>(width) * height * 4;
		const std::size_t depthBytes = static_cast<std::size_t>(width) * height;

		delete[] g_state.colorBuffer;
		delete[] g_state.depthBuffer;

		g_state.colorBuffer = new std::uint8_t[colorBytes]();
		g_state.depthBuffer = new float[depthBytes];

		// Clear depth to far plane
		const float farValue = 1.0f;
		for (std::size_t i = 0; i < depthBytes; ++i) {
			g_state.depthBuffer[i] = farValue;
		}

		// Mirror viewport to full buffer
		g_state.viewportX = 0;
		g_state.viewportY = 0;
		g_state.viewportW = width;
		g_state.viewportH = height;
	}

	const std::uint8_t* GetColorBuffer()
	{
		return g_state.colorBuffer;
	}

	std::int32_t GetColorBufferWidth()
	{
		return g_state.bufferWidth;
	}

	std::int32_t GetColorBufferHeight()
	{
		return g_state.bufferHeight;
	}

} // namespace Rhi
