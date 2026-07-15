#include "SwDevice.h"
#include "SwRaster.h"
#include "SwShaderProgram.h"
#include "SwRenderTarget.h"
#include "SwTexture.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace nCine::RhiSoftware
{
	namespace
	{
		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance), so the effect reads members
		// at these fixed byte offsets instead of chasing per-member reflection (the batched reflection only
		// exposes the "instances" array as one symbolic struct member)
		constexpr std::uint32_t kModelMatrixOffset = 0;
		constexpr std::uint32_t kColorOffset = 64;
		constexpr std::uint32_t kTexRectOffset = 80;
		constexpr std::uint32_t kSpriteSizeOffset = 96;
		// The palette shaders park a flat palette index in the std140 tail padding after spriteSize
		constexpr std::uint32_t kPaletteOffsetOffset = 104;

		const float IdentityMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		// Column-major 4x4 multiply: out = a * b (element (col c, row r) is stored at index c*4 + r)
		void Mat4Mul(const float* a, const float* b, float* out)
		{
			for (int c = 0; c < 4; c++) {
				for (int r = 0; r < 4; r++) {
					float sum = 0.0f;
					for (int k = 0; k < 4; k++) {
						sum += a[k * 4 + r] * b[c * 4 + k];
					}
					out[c * 4 + r] = sum;
				}
			}
		}

		// Column-major 4x4 times a column vector: out = m * v
		void Mat4Vec4(const float* m, const float* v, float* out)
		{
			for (int r = 0; r < 4; r++) {
				out[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2] + m[3 * 4 + r] * v[3];
			}
		}

		// Maps a pipeline-neutral blend factor onto the rasterizer-local mirror the engine specializes. The
		// two enums list the same factors, so this is a plain meaning-preserving translation; factors the CPU
		// blenders never see fall back to One.
		SwBlendFactor MapBlend(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return SwBlendFactor::Zero;
				case nCine::BlendingFactor::One:				return SwBlendFactor::One;
				case nCine::BlendingFactor::SrcColor:			return SwBlendFactor::SrcColor;
				case nCine::BlendingFactor::OneMinusSrcColor:	return SwBlendFactor::OneMinusSrcColor;
				case nCine::BlendingFactor::SrcAlpha:			return SwBlendFactor::SrcAlpha;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return SwBlendFactor::OneMinusSrcAlpha;
				case nCine::BlendingFactor::DstAlpha:			return SwBlendFactor::DstAlpha;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return SwBlendFactor::OneMinusDstAlpha;
				case nCine::BlendingFactor::DstColor:			return SwBlendFactor::DstColor;
				case nCine::BlendingFactor::OneMinusDstColor:	return SwBlendFactor::OneMinusDstColor;
				default:										return SwBlendFactor::One;
			}
		}

		// --- Scalar helpers reproducing the GLSL built-ins used by the procedural fragment shaders ---

		inline float Fractf(float x) { return x - std::floor(x); }
		inline float Mixf(float a, float b, float t) { return a + (b - a) * t; }
		inline float Clampf(float x, float lo, float hi) { return (x < lo ? lo : (x > hi ? hi : x)); }
		inline float Signf(float x) { return (x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f)); }

		void Hash2D(float px, float py, float out[2])
		{
			const float h = px * 12.9898f + py * 78.233f;
			const float h2 = px * 37.271f + py * 377.632f;
			out[0] = -1.0f + 2.0f * Fractf(std::sin(h) * 43758.5453f);
			out[1] = -1.0f + 2.0f * Fractf(std::sin(h2) * 43758.5453f);
		}

		// Returns voronoi(p).x (the squared distance to the nearest feature point), the only component the
		// star field reads
		float VoronoiDistance(float px, float py)
		{
			const float nx = std::floor(px), ny = std::floor(py);
			const float fx = px - nx, fy = py - ny;
			float md = 8.0f;
			for (int j = -1; j <= 1; ++j) {
				for (int i = -1; i <= 1; ++i) {
					const float gx = float(i), gy = float(j);
					float o[2];
					Hash2D(nx + gx, ny + gy, o);
					const float rx = gx + o[0] - fx;
					const float ry = gy + o[1] - fy;
					const float d = rx * rx + ry * ry;
					if (d < md) { md = d; }
				}
			}
			return md;
		}

		float AddStarField(float px, float py, float threshold)
		{
			const float value = VoronoiDistance(px, py);
			if (value < threshold) {
				const float power = 1.0f - (value / threshold);
				return std::min(power * power * power, 0.5f);
			}
			return 0.0f;
		}

		// A read-only view of a bound texture the procedural fragment callbacks sample directly
		struct SourceView
		{
			const std::uint8_t* pixels = nullptr;
			std::int32_t width = 0;
			std::int32_t height = 0;
			std::int32_t strideBytes = 0;
			PixelFormat format = PixelFormat::RGBA8;
		};

		SourceView MakeSourceView(const SwTexture* texture)
		{
			SourceView v;
			if (texture != nullptr) {
				v.pixels = texture->GetPixels();
				v.width = texture->GetWidth();
				v.height = texture->GetHeight();
				v.strideBytes = texture->GetStrideBytes();
				v.format = texture->GetFormat();
			}
			return v;
		}

		// Nearest-neighbour texture fetch returning normalized RGBA (0..1); the wrap flag selects Repeat vs.
		// ClampToEdge. Bilinear filtering is a later slice, so like the fast paths this samples nearest.
		void SampleNearest(const SourceView& t, float u, float v, bool repeat, float out[4])
		{
			if (t.pixels == nullptr || t.width <= 0 || t.height <= 0) {
				out[0] = out[1] = out[2] = out[3] = 0.0f;
				return;
			}
			std::int32_t tx = std::int32_t(std::floor(u * float(t.width)));
			std::int32_t ty = std::int32_t(std::floor(v * float(t.height)));
			if (repeat) {
				tx = ((tx % t.width) + t.width) % t.width;
				ty = ((ty % t.height) + t.height) % t.height;
			} else {
				tx = (tx < 0 ? 0 : (tx >= t.width ? t.width - 1 : tx));
				ty = (ty < 0 ? 0 : (ty >= t.height ? t.height - 1 : ty));
			}
			const std::int32_t bpp = (t.format == PixelFormat::RGBA8 ? 4 : (t.format == PixelFormat::RGB8 ? 3 : 4));
			const std::uint8_t* p = t.pixels + std::size_t(ty) * t.strideBytes + std::size_t(tx) * bpp;
			out[0] = float(p[0]) * (1.0f / 255.0f);
			out[1] = float(p[1]) * (1.0f / 255.0f);
			out[2] = float(p[2]) * (1.0f / 255.0f);
			out[3] = (bpp == 4 ? float(p[3]) * (1.0f / 255.0f) : 1.0f);
		}

		// --- PaletteRemap / BatchedPaletteRemap ---

		struct PaletteContext
		{
			SourceView palette;
			std::int32_t paletteRowOffset;
			std::int32_t paletteColumnOffset;
			bool isRG8;
		};

		// Ports the palette-remap fragment. The promoted index texture keeps the palette index in byte 0 (and,
		// for an RG8 source, its own alpha in byte 1) of the primary sample the engine already wrote into rgba,
		// so this looks the index up in the shared 256x256 palette - advanced by the per-instance row/column
		// offset - and modulates by the instance color. R8 sources take their alpha from the palette entry;
		// RG8 sources from the index texture's G channel.
		void PaletteFragment(const FragmentShaderInput& in)
		{
			const PaletteContext& c = *static_cast<const PaletteContext*>(in.userData);
			const std::int32_t idx = in.rgba[0];
			// The RG8 alpha source is the index texture's G channel; read it before rgba[] is overwritten below
			const std::int32_t srcA = in.rgba[1];

			const std::int32_t col = (idx + c.paletteColumnOffset) & 0xFF;
			const std::int32_t row = c.paletteRowOffset;

			float palR = 0.0f, palG = 0.0f, palB = 0.0f, palA = 0.0f;
			if (c.palette.pixels != nullptr && row >= 0 && row < c.palette.height && col >= 0 && col < c.palette.width) {
				const std::uint8_t* p = c.palette.pixels + std::size_t(row) * c.palette.strideBytes + std::size_t(col) * 4;
				palR = float(p[0]);
				palG = float(p[1]);
				palB = float(p[2]);
				palA = float(p[3]);
			}

			in.rgba[0] = std::uint8_t(Clampf(palR * in.color[0], 0.0f, 255.0f));
			in.rgba[1] = std::uint8_t(Clampf(palG * in.color[1], 0.0f, 255.0f));
			in.rgba[2] = std::uint8_t(Clampf(palB * in.color[2], 0.0f, 255.0f));
			const float outA = (c.isRG8 ? (palA / 255.0f) * float(srcA) * in.color[3] : palA * in.color[3]);
			in.rgba[3] = std::uint8_t(Clampf(outA, 0.0f, 255.0f));
		}

		// --- TexturedBackground / TexturedBackgroundCircle (+ DITHER) ---

		struct BackgroundContext
		{
			SourceView texture;
			float viewSize[2];
			float cameraPos[2];
			float horizonColor[4];
			float shift[2];
			bool dither;
			bool circle;
		};

		// Ports the canvas fragment of TexturedBackground(.shader)/TexturedBackgroundCircle(.shader): warps
		// UV into a scrolling texture lookup (Repeat wrap), optionally dithers a second tap, then blends the
		// texture with a horizon colour (+ an optional voronoi star field) by a vertical-distance opacity.
		void BackgroundFragment(const FragmentShaderInput& in)
		{
			const BackgroundContext& c = *static_cast<const BackgroundContext*>(in.userData);
			const float uvx = in.u, uvy = in.v;

			float distance, texturePosX, texturePosY, horizonOpacity;
			if (!c.circle) {
				distance = 1.3f - std::fabs(2.0f * uvy - 1.0f);
				const float horizonDepth = std::pow(distance, 1.4f);
				const float yShift = (uvy > 0.5f ? 1.0f : 0.0f);
				const float correction = (c.viewSize[0] * 9.0f) / (c.viewSize[1] * 16.0f);
				texturePosX = (c.shift[0] / 256.0f) + (uvx - 0.5f) * (0.5f + (1.5f * horizonDepth)) * correction;
				texturePosY = (c.shift[1] / 256.0f) + (uvy - yShift) * 1.4f * distance;
				horizonOpacity = Clampf(std::pow(distance, 1.5f) - 0.3f, 0.0f, 1.0f);
			} else {
				float tcx = 2.0f * uvx - 1.0f;
				const float tcy = 2.0f * uvy - 1.0f;
				tcx *= c.viewSize[0] / c.viewSize[1];
				distance = std::sqrt(tcx * tcx + tcy * tcy);
				const float INV_PI = 0.31830988618379067153776752675f;
				const float xShift = (tcx == 0.0f ? Signf(tcy) * 0.5f : std::atan2(tcy, tcx) * INV_PI);
				texturePosX = xShift * 1.0f + (c.shift[0] * 0.01f);
				texturePosY = (1.0f / distance) * 1.4f + (c.shift[1] * 0.002f);
				horizonOpacity = 1.0f - Clampf(std::pow(distance, 1.4f) - 0.3f, 0.0f, 1.0f);
			}

			float texColor[4];
			SampleNearest(c.texture, texturePosX, texturePosY, true, texColor);
			if (c.dither) {
				float noise[2];
				Hash2D(uvx * c.viewSize[0] + (c.cameraPos[0] + c.shift[0]) * 0.001f,
					uvy * c.viewSize[1] + (c.cameraPos[1] + c.shift[1]) * 0.001f, noise);
				const float dx = texturePosX + noise[0] * 8.0f / c.viewSize[0];
				const float dy = texturePosY + noise[1] * 8.0f / c.viewSize[1];
				float texColor2[4];
				SampleNearest(c.texture, dx, dy, true, texColor2);
				for (int i = 0; i < 4; i++) {
					texColor[i] = Mixf(texColor[i], texColor2[i], 0.333f);
				}
			}

			float horizon[4] = {c.horizonColor[0], c.horizonColor[1], c.horizonColor[2], 1.0f};
			if (c.horizonColor[3] > 0.0f) {
				const float aspect = c.viewSize[1] / c.viewSize[0];
				float spx = uvx + c.cameraPos[0] * 0.00012f;
				float spy = uvy * aspect + c.cameraPos[1] * 0.00012f;
				const float star1 = AddStarField(spx * 7.0f, spy * 7.0f, 0.00008f);
				spx = uvx + c.cameraPos[0] * 0.00018f + 0.5f;
				spy = uvy * aspect + c.cameraPos[1] * 0.00018f + 0.5f;
				const float star2 = AddStarField(spx * 7.0f, spy * 7.0f, 0.00008f);
				const float stars = star1 + star2;
				horizon[0] += stars;
				horizon[1] += stars;
				horizon[2] += stars;
			}

			in.rgba[0] = std::uint8_t(Clampf(Mixf(texColor[0], horizon[0], horizonOpacity) * 255.0f, 0.0f, 255.0f));
			in.rgba[1] = std::uint8_t(Clampf(Mixf(texColor[1], horizon[1], horizonOpacity) * 255.0f, 0.0f, 255.0f));
			in.rgba[2] = std::uint8_t(Clampf(Mixf(texColor[2], horizon[2], horizonOpacity) * 255.0f, 0.0f, 255.0f));
			in.rgba[3] = 255;
		}

		// --- Combine (viewport compositor) ---

		struct CombineContext
		{
			SourceView scene;
			SourceView lighting;
			SourceView blurHalf;
			SourceView blurQuarter;
			float ambientColor[4];
			float time;
			float viewSizeInv[2];
		};

		// Ports Combine(.shader): composites the scene render target with the lighting map (a noise-jittered
		// lookup), the blurred/grayscaled bloom and the ambient colour. All source RTs sample ClampToEdge.
		void CombineFragment(const FragmentShaderInput& in)
		{
			const CombineContext& c = *static_cast<const CombineContext*>(in.userData);
			const float uvx = in.u, uvy = in.v;

			float blur1[4], blur2[4], mainColor[4], light[4];
			SampleNearest(c.blurHalf, uvx, uvy, false, blur1);
			SampleNearest(c.blurQuarter, uvx, uvy, false, blur2);
			SampleNearest(c.scene, uvx, uvy, false, mainColor);

			// noiseTexCoords(): jitter the lighting lookup by a hashed offset, clamped to the [0,1] range
			const float seedFract = Fractf(c.time * 0.01f);
			float noise[2];
			Hash2D(uvx + seedFract, uvy + seedFract, noise);
			const float lx = Clampf(uvx + noise[0] * c.viewSizeInv[0] * 1.4f, 0.0f, 1.0f);
			const float ly = Clampf(uvy + noise[1] * c.viewSizeInv[1] * 1.4f, 0.0f, 1.0f);
			SampleNearest(c.lighting, lx, ly, false, light);

			float blur[4];
			for (int i = 0; i < 4; i++) {
				blur[i] = (blur1[i] + blur2[i]) * 0.5f;
			}
			const float gray = blur[0] * 0.299f + blur[1] * 0.587f + blur[2] * 0.114f;
			blur[0] = gray;
			blur[1] = gray;
			blur[2] = gray;

			const float lightG = light[1];
			const float lightR = light[0];
			const float glow = std::max(lightG - 0.7f, 0.0f);
			const float t1 = Clampf((1.0f - lightR) / std::sqrt(std::max(c.ambientColor[3], 0.35f)), 0.0f, 1.0f);
			const float t2 = 1.0f - lightR;
			for (int i = 0; i < 4; i++) {
				const float lit = mainColor[i] * (1.0f + lightG) + glow;
				const float mid = Mixf(lit, blur[i], t1);
				in.rgba[i] = std::uint8_t(Clampf(Mixf(mid, c.ambientColor[i], t2) * 255.0f, 0.0f, 255.0f));
			}
			in.rgba[3] = 255;
		}
	}

	SwDevice::BlendingState SwDevice::blending_;
	SwDevice::DepthTestState SwDevice::depthTest_;
	SwDevice::CullFaceState SwDevice::cullFace_;
	SwDevice::ScissorState SwDevice::scissor_;
	Recti SwDevice::viewport_(0, 0, 0, 0);
	Colorf SwDevice::clearColor_(0.0f, 0.0f, 0.0f, 0.0f);

	SwShaderProgram* SwDevice::currentProgram_ = nullptr;
	const SwTexture* SwDevice::boundTextures_[SwDevice::MaxTextureUnits] = {};
	SwDevice::UniformRange SwDevice::boundUniformRanges_[SwDevice::MaxUniformBindings] = {};
	SwRenderTarget* SwDevice::currentRenderTarget_ = nullptr;
	std::uint8_t* SwDevice::defaultFbPixels_ = nullptr;
	std::int32_t SwDevice::defaultFbWidth_ = 0;
	std::int32_t SwDevice::defaultFbHeight_ = 0;
	std::int32_t SwDevice::defaultFbStride_ = 0;
	std::vector<std::uint8_t> SwDevice::screenPixels_;

	void SwDevice::SetBlendingEnabled(bool enabled)
	{
		blending_.Enabled = enabled;
	}

	void SwDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}

	SwDevice::BlendingState SwDevice::GetBlendingState()
	{
		return blending_;
	}

	void SwDevice::SetBlendingState(const BlendingState& state)
	{
		blending_ = state;
	}

	void SwDevice::SetDepthTestEnabled(bool enabled)
	{
		depthTest_.TestEnabled = enabled;
	}

	void SwDevice::SetDepthMaskEnabled(bool enabled)
	{
		depthTest_.MaskEnabled = enabled;
	}

	SwDevice::DepthTestState SwDevice::GetDepthTestState()
	{
		return depthTest_;
	}

	void SwDevice::SetDepthTestState(const DepthTestState& state)
	{
		depthTest_ = state;
	}

	void SwDevice::SetCullFaceEnabled(bool enabled)
	{
		cullFace_.Enabled = enabled;
	}

	SwDevice::CullFaceState SwDevice::GetCullFaceState()
	{
		return cullFace_;
	}

	void SwDevice::SetCullFaceState(const CullFaceState& state)
	{
		cullFace_ = state;
	}

	SwDevice::ScissorState SwDevice::GetScissorState()
	{
		return scissor_;
	}

	void SwDevice::SetScissorState(const ScissorState& state)
	{
		scissor_ = state;
	}

	void SwDevice::SetScissor(const Recti& rect)
	{
		scissor_.Enabled = true;
		scissor_.Rect = rect;
	}

	void SwDevice::SetScissorTestEnabled(bool enabled)
	{
		scissor_.Enabled = enabled;
	}

	Recti SwDevice::GetViewport()
	{
		return viewport_;
	}

	void SwDevice::SetViewport(const Recti& rect)
	{
		viewport_ = rect;
	}

	void SwDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		viewport_ = Recti(x, y, width, height);
	}

	Colorf SwDevice::GetClearColor()
	{
		return clearColor_;
	}

	void SwDevice::SetClearColor(const Colorf& color)
	{
		clearColor_ = color;
	}

	void SwDevice::Clear(ClearFlags flags)
	{
		// The software backend is 2D, so only the color buffer is meaningful
		if ((flags & ClearFlags::Color) == ClearFlags::None) {
			return;
		}
		Framebuffer fb;
		if (!ResolveFramebuffer(fb)) {
			return;
		}
		// A render target's color texture is stored bottom-up, the screen back-buffer top-down; Clear fills
		// the whole store either way, so the flag only has to match SetColorBuffer's row convention
		SwRaster::SetColorBuffer(fb.pixels, fb.width, fb.height, currentRenderTarget_ != nullptr);
		SwRaster::Clear(clearColor_.R, clearColor_.G, clearColor_.B, clearColor_.A);
	}

	void SwDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		static_cast<void>(firstVertex);
		Dispatch(primitive, numVertices);
	}

	void SwDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(firstVertex);
		static_cast<void>(numInstances);
		// The sprite path batches into one non-instanced draw; hardware instancing is unused here
		Dispatch(primitive, numVertices);
	}

	void SwDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(baseVertex);
		Dispatch(primitive, std::int32_t(numIndices));
	}

	void SwDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		static_cast<void>(baseVertex);
		Dispatch(primitive, std::int32_t(numIndices));
	}

	FenceHandle SwDevice::InsertFence()
	{
		return nullptr;
	}

	void SwDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}

	bool SwDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void SwDevice::SetupInitialState()
	{
		blending_ = BlendingState{};
		depthTest_ = DepthTestState{};
		cullFace_ = CullFaceState{};
		scissor_ = ScissorState{};
	}

	void SwDevice::BindProgram(SwShaderProgram* program)
	{
		currentProgram_ = program;
	}

	SwShaderProgram* SwDevice::CurrentProgram()
	{
		return currentProgram_;
	}

	void SwDevice::BindTexture(std::uint32_t unit, const SwTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	const SwTexture* SwDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void SwDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void SwDevice::SetRenderTarget(SwRenderTarget* renderTarget)
	{
		currentRenderTarget_ = renderTarget;
	}

	void SwDevice::SetDefaultFramebuffer(const Framebuffer& framebuffer)
	{
		defaultFbPixels_ = framebuffer.pixels;
		defaultFbWidth_ = framebuffer.width;
		defaultFbHeight_ = framebuffer.height;
		defaultFbStride_ = framebuffer.strideBytes;
	}

	void SwDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width <= 0 || height <= 0) {
			return;
		}
		const std::size_t required = std::size_t(width) * std::size_t(height) * 4;
		if (screenPixels_.size() != required) {
			screenPixels_.assign(required, 0);
		}
		Framebuffer fb;
		fb.pixels = screenPixels_.data();
		fb.width = width;
		fb.height = height;
		fb.strideBytes = width * 4;
		SetDefaultFramebuffer(fb);
	}

	Framebuffer SwDevice::GetScreenFramebuffer()
	{
		Framebuffer fb;
		fb.pixels = defaultFbPixels_;
		fb.width = defaultFbWidth_;
		fb.height = defaultFbHeight_;
		fb.strideBytes = defaultFbStride_;
		return fb;
	}

	bool SwDevice::ResolveFramebuffer(Framebuffer& out)
	{
		if (currentRenderTarget_ != nullptr) {
			SwTexture* texture = currentRenderTarget_->GetColorTexture(0);
			if (texture != nullptr && texture->MutablePixels() != nullptr) {
				out.pixels = texture->MutablePixels();
				out.width = texture->GetWidth();
				out.height = texture->GetHeight();
				out.strideBytes = texture->GetStrideBytes();
				return true;
			}
		}
		if (defaultFbPixels_ != nullptr) {
			out.pixels = defaultFbPixels_;
			out.width = defaultFbWidth_;
			out.height = defaultFbHeight_;
			out.strideBytes = defaultFbStride_;
			return true;
		}
		return false;
	}

	void SwDevice::Dispatch(PrimitiveType primitive, std::int32_t numVertices)
	{
		static_cast<void>(primitive);
		if (currentProgram_ == nullptr || numVertices <= 0) {
			return;
		}

		const SwEffect effect = currentProgram_->GetEffect();
		if (effect == SwEffect::Unknown) {
			std::fprintf(stderr, "[SwDevice] Skipped draw: no C++ effect for the bound program\n");
			return;
		}

		Framebuffer fb;
		if (!ResolveFramebuffer(fb)) {
			std::fprintf(stderr, "[SwDevice] Skipped draw: no render target bound\n");
			return;
		}

		// A render target's color texture is stored bottom-up (OpenGL framebuffer convention); the screen
		// back-buffer is top-down. The engine derives every store's orientation - and the matching sample
		// alignment - from this one flag, so the effects never need a manual Y-flip.
		const bool isFboTarget = (currentRenderTarget_ != nullptr);

		const Recti viewport = (viewport_.W > 0 && viewport_.H > 0) ? viewport_ : Recti(0, 0, fb.width, fb.height);

		const std::uint8_t* projBytes = currentProgram_->ResolveUniform("uProjectionMatrix");
		const std::uint8_t* viewBytes = currentProgram_->ResolveUniform("uViewMatrix");
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		const SwUniformBlock* block = currentProgram_->FindBlock("InstanceBlock");
		if (block == nullptr) {
			block = currentProgram_->FindBlock("InstancesBlock");
		}
		if (block == nullptr) {
			std::fprintf(stderr, "[SwDevice] Skipped draw: program has no instance block\n");
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = boundUniformRanges_[binding].Data;
		if (blockData == nullptr) {
			std::fprintf(stderr, "[SwDevice] Skipped draw: no uniform block data bound\n");
			return;
		}

		// Sampler texture units and the batched instance stride come from the offline reflection
		const ShaderCompiler::ProgramVariant* reflection = currentProgram_->GetReflection();
		auto samplerUnit = [reflection](const char* name, std::int32_t def) -> std::int32_t {
			if (reflection != nullptr) {
				for (std::size_t i = 0; i < reflection->TextureCount; i++) {
					if (std::strcmp(reflection->Textures[i].Name, name) == 0) {
						return (reflection->Textures[i].Unit >= 0 ? reflection->Textures[i].Unit : def);
					}
				}
			}
			return def;
		};
		std::uint32_t instanceStride = 0;
		if (reflection != nullptr) {
			for (std::size_t i = 0; i < reflection->BlockCount; i++) {
				if (reflection->Blocks[i].InstanceStride > 0) {
					instanceStride = reflection->Blocks[i].InstanceStride;
					break;
				}
			}
		}

		// Reads a committed loose uniform (or fills a default when it was never set)
		auto readUniform = [](const char* name, float* dst, int count, float def) {
			const std::uint8_t* p = currentProgram_->ResolveUniform(name);
			if (p != nullptr) {
				const float* f = reinterpret_cast<const float*>(p);
				for (int i = 0; i < count; i++) { dst[i] = f[i]; }
			} else {
				for (int i = 0; i < count; i++) { dst[i] = def; }
			}
		};

		float pv[16];
		Mat4Mul(projMat, viewMat, pv);

		// Persistent rasterizer state for every quad this draw issues
		SwRaster::SetColorBuffer(fb.pixels, fb.width, fb.height, isFboTarget);
		SwRaster::SetViewport(viewport.X, viewport.Y, viewport.W, viewport.H);
		// nCine hands scissor rectangles in bottom-up (OpenGL) window coordinates; the engine flips them
		SwRaster::SetScissor(scissor_.Enabled, scissor_.Rect.X, scissor_.Rect.Y, scissor_.Rect.W, scissor_.Rect.H);

		SwBlendFactor bsrc = SwBlendFactor::One, bdst = SwBlendFactor::Zero;
		bool blendOn = blending_.Enabled;
		if (blendOn) {
			bsrc = MapBlend(static_cast<BlendingFactor>(static_cast<std::uint32_t>(blending_.SrcRgb)));
			bdst = MapBlend(static_cast<BlendingFactor>(static_cast<std::uint32_t>(blending_.DstRgb)));
		}

		// Fills a draw context from one instance's fixed-function state and hands it to the engine as a
		// procedural sprite quad (vertexData stays null, so FetchVertex synthesizes the four corners from ff)
		auto drawQuad = [&](const FFState& ff, FragmentShaderFn fragmentShader, void* userData) {
			DrawContext ctx;
			for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
				ctx.textures[u] = boundTextures_[u];
			}
			ctx.ff = ff;
			ctx.fragmentShader = fragmentShader;
			ctx.fragmentShaderUserData = userData;
			ctx.blendingEnabled = blendOn;
			ctx.blendSrc = bsrc;
			ctx.blendDst = bdst;
			ctx.scissorEnabled = scissor_.Enabled;
			ctx.scissorRect = scissor_.Rect;
			SwRaster::SetDrawContext(ctx);
			SwRaster::Draw(PrimitiveType::TriangleStrip, 0, 4);
		};

		const bool batched = (effect == SwEffect::DefaultBatchedSprites || effect == SwEffect::BatchedPaletteRemap);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
		}

		switch (effect) {
			case SwEffect::DefaultSprite:
			case SwEffect::DefaultBatchedSprites: {
				// The promotion in SwTexture guarantees an RGBA8 store, so the primary sample is always 4 bytes
				const std::int32_t uTextureUnit = samplerUnit("uTexture", 0);
				const SwTexture* texture = GetBoundTexture(std::uint32_t(uTextureUnit));
				if (texture == nullptr || texture->GetPixels() == nullptr) {
					std::fprintf(stderr, "[SwDevice] Skipped draw: no texture bound to the sampler unit\n");
					break;
				}
				for (std::int32_t k = 0; k < numInstances; k++) {
					const std::uint8_t* inst = blockData + std::size_t(k) * instanceStride;
					FFState ff;
					Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), ff.mvpMatrix);
					std::memcpy(ff.color, inst + kColorOffset, sizeof(ff.color));
					std::memcpy(ff.texRect, inst + kTexRectOffset, sizeof(ff.texRect));
					std::memcpy(ff.spriteSize, inst + kSpriteSizeOffset, sizeof(ff.spriteSize));
					ff.hasTexture = true;
					ff.textureUnit = uTextureUnit;
					drawQuad(ff, nullptr, nullptr);
				}
				break;
			}

			case SwEffect::PaletteRemap:
			case SwEffect::BatchedPaletteRemap: {
				const std::int32_t indexUnit = samplerUnit("uTexture", 0);
				const std::int32_t paletteUnit = samplerUnit("uTexturePalette", 1);
				const SwTexture* indexTex = GetBoundTexture(std::uint32_t(indexUnit));
				const SwTexture* paletteTex = GetBoundTexture(std::uint32_t(paletteUnit));
				if (indexTex == nullptr || indexTex->GetPixels() == nullptr || paletteTex == nullptr || paletteTex->GetPixels() == nullptr) {
					std::fprintf(stderr, "[SwDevice] Skipped draw: palette effect needs an index texture + a palette texture\n");
					break;
				}
				const std::int32_t paletteRows = paletteTex->GetHeight();
				const bool isRG8 = (indexTex->GetUploadFormat() == PixelFormat::RG8);
				for (std::int32_t k = 0; k < numInstances; k++) {
					const std::uint8_t* inst = blockData + std::size_t(k) * instanceStride;
					FFState ff;
					Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), ff.mvpMatrix);
					std::memcpy(ff.color, inst + kColorOffset, sizeof(ff.color));
					std::memcpy(ff.texRect, inst + kTexRectOffset, sizeof(ff.texRect));
					std::memcpy(ff.spriteSize, inst + kSpriteSizeOffset, sizeof(ff.spriteSize));
					ff.hasTexture = true;
					ff.textureUnit = indexUnit;

					// A whole-row palette offset selects the row via paletteRowOffset; the residual column
					// offset feeds the lookup (0 in practice, since the offsets are multiples of 256)
					std::int32_t palOffset = std::int32_t(std::lround(*reinterpret_cast<const float*>(inst + kPaletteOffsetOffset)));
					if (palOffset < 0) { palOffset = 0; }
					std::int32_t row = palOffset / 256;
					if (row >= paletteRows) { row = (paletteRows > 0 ? paletteRows - 1 : 0); }
					const std::int32_t columnOffset = palOffset - row * 256;

					// Declared here so it stays alive across the Draw below (SetDrawContext keeps only a pointer)
					PaletteContext pctx;
					pctx.palette = MakeSourceView(paletteTex);
					pctx.paletteRowOffset = row;
					pctx.paletteColumnOffset = columnOffset;
					pctx.isRG8 = isRG8;
					drawQuad(ff, &PaletteFragment, &pctx);
				}
				break;
			}

			case SwEffect::TexturedBackground:
			case SwEffect::TexturedBackgroundCircle: {
				const std::int32_t uTextureUnit = samplerUnit("uTexture", 0);
				const SwTexture* texture = GetBoundTexture(std::uint32_t(uTextureUnit));
				if (texture == nullptr || texture->GetPixels() == nullptr) {
					std::fprintf(stderr, "[SwDevice] Skipped draw: textured background has no source texture\n");
					break;
				}
				BackgroundContext bctx;
				bctx.texture = MakeSourceView(texture);
				readUniform("uViewSize", bctx.viewSize, 2, 1.0f);
				readUniform("uCameraPos", bctx.cameraPos, 2, 0.0f);
				readUniform("uHorizonColor", bctx.horizonColor, 4, 0.0f);
				readUniform("uShift", bctx.shift, 2, 0.0f);
				bctx.dither = currentProgram_->IsDitherVariant();
				bctx.circle = (effect == SwEffect::TexturedBackgroundCircle);

				// Not batched: the single instance block drives one full-screen quad
				FFState ff;
				Mat4Mul(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), ff.mvpMatrix);
				std::memcpy(ff.color, blockData + kColorOffset, sizeof(ff.color));
				std::memcpy(ff.texRect, blockData + kTexRectOffset, sizeof(ff.texRect));
				std::memcpy(ff.spriteSize, blockData + kSpriteSizeOffset, sizeof(ff.spriteSize));
				ff.hasTexture = true;
				ff.textureUnit = uTextureUnit;
				drawQuad(ff, &BackgroundFragment, &bctx);
				break;
			}

			case SwEffect::Combine: {
				const std::int32_t sceneUnit = samplerUnit("uTexture", 0);
				CombineContext cctx;
				cctx.scene = MakeSourceView(GetBoundTexture(std::uint32_t(sceneUnit)));
				cctx.lighting = MakeSourceView(GetBoundTexture(std::uint32_t(samplerUnit("uTextureLighting", 1))));
				cctx.blurHalf = MakeSourceView(GetBoundTexture(std::uint32_t(samplerUnit("uTextureBlurHalf", 2))));
				cctx.blurQuarter = MakeSourceView(GetBoundTexture(std::uint32_t(samplerUnit("uTextureBlurQuarter", 3))));
				if (cctx.scene.pixels == nullptr) {
					std::fprintf(stderr, "[SwDevice] Skipped draw: combine has no scene texture\n");
					break;
				}
				readUniform("uAmbientColor", cctx.ambientColor, 4, 0.0f);
				readUniform("uTime", &cctx.time, 1, 0.0f);
				const float* spriteSize = reinterpret_cast<const float*>(blockData + kSpriteSizeOffset);
				cctx.viewSizeInv[0] = (spriteSize[0] != 0.0f ? 1.0f / spriteSize[0] : 0.0f);
				cctx.viewSizeInv[1] = (spriteSize[1] != 0.0f ? 1.0f / spriteSize[1] : 0.0f);

				// Not batched: the single instance block drives one full-screen quad
				FFState ff;
				Mat4Mul(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), ff.mvpMatrix);
				std::memcpy(ff.color, blockData + kColorOffset, sizeof(ff.color));
				std::memcpy(ff.texRect, blockData + kTexRectOffset, sizeof(ff.texRect));
				std::memcpy(ff.spriteSize, blockData + kSpriteSizeOffset, sizeof(ff.spriteSize));
				ff.hasTexture = true;
				ff.textureUnit = sceneUnit;
				drawQuad(ff, &CombineFragment, &cctx);
				break;
			}

			default:
				break;
		}

		SwRaster::ClearDrawContext();
	}
}
