#include "SwDevice.h"
#include "SwBuffer.h"
#include "SwRaster.h"
#include "SwScanlineOps.h"
#include "SwShaderProgram.h"
#include "SwRenderTarget.h"
#include "SwTexture.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"
#include "../../../../Shaders/Generated/SwGeneratedShaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Containers/StringStl.h>

namespace nCine::RHI::Software
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
		// The no-texture sprite family (Sprite_NoTexture / Batched_Sprites_NoTexture) drops texRect from the
		// instance block, so spriteSize sits directly after color (offset 80, not 96) and there is no palette tail
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

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
	std::vector<SwDevice::PendingSoftwareLight> SwDevice::pendingSoftwareLights_;

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
		Dispatch(primitive, firstVertex, numVertices);
	}

	void SwDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		// The sprite path batches into one non-instanced draw; hardware instancing is unused here
		Dispatch(primitive, firstVertex, numVertices);
	}

	void SwDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	void SwDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
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

	void SwDevice::UnbindTexture(const SwTexture* texture)
	{
		// Called from ~SwTexture so a destroyed texture never lingers as a dangling pointer in boundTextures_ (a
		// later deferred draw reads it in Dispatch and would dereference freed memory - the same class of crash the
		// D3D11 backend hit during splitscreen level changes). Clears every unit it may be bound to.
		for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
			if (boundTextures_[unit] == texture) {
				boundTextures_[unit] = nullptr;
			}
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

	void SwDevice::UnbindRenderTarget(const SwRenderTarget* renderTarget)
	{
		// Called from ~SwRenderTarget so a destroyed target can't dangle as currentRenderTarget_ (ResolveFramebuffer
		// and Dispatch dereference it). Reverts to the default framebuffer (nullptr); the pipeline binds a fresh
		// target before the next off-screen draw.
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
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

	void SwDevice::FlushSoftwareRenderer()
	{
		SwRaster::Flush();
	}

	void SwDevice::EndFrame()
	{
		if (!pendingSoftwareLights_.empty()) {
			// A queued lightmap was not consumed by a Combine draw this frame (the draw was culled or skipped);
			// drop the leftovers so they cannot desync the FIFO pairing or dangle into the next frame
			static bool warnedLeftoverLights = false;
			if (!warnedLeftoverLights) {
				warnedLeftoverLights = true;
				LOGW("{} software-lighting entr{} left unconsumed at frame end - dropping", pendingSoftwareLights_.size(), pendingSoftwareLights_.size() == 1 ? "y" : "ies");
			}
			pendingSoftwareLights_.clear();
		}
	}

	void SwDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		PendingSoftwareLight entry;
		entry.Lightmap = lightmap;
		entry.LmW = lmW;
		entry.LmH = lmH;
		entry.Scale = (scale > 0 ? scale : 1);
		entry.VpX = vpX;
		entry.VpY = vpY;
		entry.VpW = vpW;
		entry.VpH = vpH;
		entry.AmbR = ambR;
		entry.AmbG = ambG;
		entry.AmbB = ambB;
		entry.WaterActive = waterActive;
		entry.WaterLevelPx = waterLevelPx;
		entry.WaterTime = waterTime;
		entry.WaterCamY = waterCamY;
		// Pushed in Visit/OnDraw order; the matching Combine draws are dispatched in that same order, so the queue
		// is consumed front-to-back (see ApplyPendingSoftwareLighting)
		pendingSoftwareLights_.push_back(entry);
	}

	// Shifts one row of RGBA8 pixels horizontally in place with edge clamping: out(x) = in(clamp(x + shift,
	// 0, count - 1)) - the per-row equivalent of the water shader's clamped uv.x displacement. A memmove plus
	// an edge-pixel fill, so it stays cheap even at full viewport width.
	static void ShiftRowHorizontal(std::uint8_t* row, std::int32_t count, std::int32_t shift)
	{
		if (shift == 0 || count <= 0) {
			return;
		}
		std::uint8_t edge[4];
		if (shift >= count || shift <= -count) {
			// Degenerate shift: the whole row clamps to one edge pixel
			std::memcpy(edge, row + (shift > 0 ? (std::size_t)(count - 1) * 4 : 0), 4);
			for (std::int32_t x = 0; x < count; x++) {
				std::memcpy(row + (std::size_t)x * 4, edge, 4);
			}
			return;
		}
		if (shift > 0) {
			std::memmove(row, row + (std::size_t)shift * 4, (std::size_t)(count - shift) * 4);
			std::memcpy(edge, row + (std::size_t)(count - shift - 1) * 4, 4);
			for (std::int32_t x = count - shift; x < count; x++) {
				std::memcpy(row + (std::size_t)x * 4, edge, 4);
			}
		} else {
			const std::int32_t s = -shift;
			std::memmove(row + (std::size_t)s * 4, row, (std::size_t)(count - s) * 4);
			std::memcpy(edge, row + (std::size_t)s * 4, 4);
			for (std::int32_t x = 0; x < s; x++) {
				std::memcpy(row + (std::size_t)x * 4, edge, 4);
			}
		}
	}

	void SwDevice::ApplyPendingSoftwareLighting()
	{
		if (pendingSoftwareLights_.empty()) {
			// No lighting queued for this Combine draw: the scene stays as rasterized (fully lit)
			return;
		}
		const PendingSoftwareLight light = pendingSoftwareLights_.front();
		pendingSoftwareLights_.erase(pendingSoftwareLights_.begin());

		const bool hasLighting = (light.Lightmap != nullptr && light.LmW > 0 && light.LmH > 0);
		const bool hasWater = light.WaterActive;
		if (!hasLighting && !hasWater) {
			return;
		}

		// The scene viewport rasterized straight into the screen back-buffer and deferred its tiles to the tile
		// renderer; drain them so the buffer holds the finished scene before it is read back and modified here. The
		// HUD is dispatched after this Combine draw, so it is not affected (it re-defers and is flushed at present).
		FlushSoftwareRenderer();
		const Framebuffer fb = GetScreenFramebuffer();
		if (fb.pixels == nullptr) {
			return;
		}

		// Clamp the viewport rectangle to the actual screen buffer (the compositor submits the unclamped rect)
		const std::int32_t vpX = std::max(0, light.VpX);
		const std::int32_t vpY = std::max(0, light.VpY);
		const std::int32_t vpW = std::min(light.VpW, fb.width - vpX);
		const std::int32_t vpH = std::min(light.VpH, fb.height - vpY);
		if (vpW <= 0 || vpH <= 0) {
			return;
		}

		const std::int32_t scale = light.Scale;
		const std::int32_t lmW = light.LmW;
		const std::int32_t lmH = light.LmH;
		const float ambR = light.AmbR;
		const float ambG = light.AmbG;
		const float ambB = light.AmbB;

		// Water precompute. This is the lightweight CPU replacement of the CombineWithWaterLow shader: a per-row
		// horizontal sine displacement, the constant water tint mix(main, (0.4, 0.6, 0.8), 0.4), a surface glow
		// band (0.2 * topGradient^2 plus 0.2 on the waterline row, approximated as a mix toward white instead of
		// the shader's saturating add) and the shader's extra darkness above deep water (uWaterLevel < 0.4). The
		// High-quality extras (noise displacement, chromatic aberration, light rays, wavy surface shape) are
		// dropped - both quality settings share this effect on the software backend.
		//
		// Rows inside the viewport run bottom-up visually (row = camY - worldY + vpH/2, the same convention the
		// lightmap uses; the present blit flips the buffer), so the underwater part (worldY > waterY) is the row
		// range [0, waterRowLimit) and the waterline sits at row waterRowLimit, counted from the buffer's top.
		const float invVpH = 1.0f / (float)vpH;
		float waterRowLimit = 0.0f;
		std::int32_t belowEndExcl = 0;
		float waveAmplitudePx = 0.0f, wavePhaseBase = 0.0f, wavePhasePerRow = 0.0f;
		std::uint8_t aboveWaterBlend[4] = {};
		if (hasWater) {
			waterRowLimit = (float)vpH - light.WaterLevelPx;
			belowEndExcl = std::min(vpH, (std::int32_t)std::ceil(waterRowLimit));
			// Shader: uv.x += 0.008 * sin(uTime * 16 + uvWorld.y * 20), uvWorld.y = (camY + vpH - row) / vpH
			waveAmplitudePx = 0.008f * (float)vpW;
			wavePhaseBase = light.WaterTime * 16.0f + (light.WaterCamY + (float)vpH) * invVpH * 20.0f;
			wavePhasePerRow = -20.0f * invVpH;
			// Shader: when the waterline is in the top 40% of the view, the above-water part is darkened toward
			// the ambient colour by (0.4 - uWaterLevel) - deep water dims the world above it
			const float waterLevelNorm = light.WaterLevelPx * invVpH;
			if (waterLevelNorm < 0.4f) {
				const std::int32_t a = (std::int32_t)((0.4f - waterLevelNorm) * 255.0f + 0.5f);
				aboveWaterBlend[0] = (std::uint8_t)std::clamp((std::int32_t)(ambR * 255.0f + 0.5f), 0, 255);
				aboveWaterBlend[1] = (std::uint8_t)std::clamp((std::int32_t)(ambG * 255.0f + 0.5f), 0, 255);
				aboveWaterBlend[2] = (std::uint8_t)std::clamp((std::int32_t)(ambB * 255.0f + 0.5f), 0, 255);
				aboveWaterBlend[3] = (std::uint8_t)std::clamp(a, 0, 254);
			}
		}
		constexpr float WaterColorR = 0.4f, WaterColorG = 0.6f, WaterColorB = 0.8f;

		// In-place combine over the viewport region, matching the core of Combine(WithWater).shader with the
		// (heavy, blur-dependent) grayscale night-vision term dropped:
		//   main = water tint/waves (underwater rows only)
		//   lit  = main * (1 + g) + max(g - 0.7, 0)
		//   out  = mix(lit, ambientRGB, clamp(1 - r, 0, 1))  (+ above-deep-water darkness)
		// The water colour ops run before the lighting mix, in the same order as the shader. The half-resolution
		// lightmap is sampled point-wise (both channels clamped to [0,1] to match the shader path's RG8 lighting
		// buffer, so a negative Brightness stays a no-op instead of becoming a "black light"), and a fully lit
		// texel leaves the scene pixel as-is. The row cores are the CPU-dispatched SIMD kernels (the lighting one
		// bit-identical to the scalar loop it replaced, see SwScanlineOps.h).
		for (std::int32_t y = 0; y < vpH; y++) {
			std::uint8_t* px = fb.pixels + (std::size_t)(vpY + y) * fb.strideBytes + (std::size_t)vpX * 4;

			const bool isUnderwaterRow = (hasWater && y < belowEndExcl);
			if (isUnderwaterRow) {
				// Horizontal wave displacement, constant per row
				const float phase = wavePhaseBase + wavePhasePerRow * (float)y;
				const std::int32_t shift = (std::int32_t)std::lround(waveAmplitudePx * std::sin(phase));
				ShiftRowHorizontal(px, vpW, shift);

				// Water tint + surface glow folded into one constant blend: out = main * 0.6 * (1 - glow) +
				// (waterColor * 0.4 * (1 - glow) + glow). Solving out = mix(main, src, a) for the blend kernel
				// gives a = 0.4 + 0.6 * glow and src = (waterColor * 0.4 * (1 - glow) + glow) / a, with src
				// always inside [0, 1] because it is a convex combination of waterColor and white.
				const float topDist = (waterRowLimit - (float)y) * invVpH;
				const float topGradient = std::max(1.0f - topDist, 0.0f);
				float glow = 0.2f * topGradient * topGradient;
				if (waterRowLimit - (float)y < 1.0f) {
					glow += 0.2f;	// The waterline row itself gets an extra highlight
				}
				const float a = 0.4f + 0.6f * glow;
				const float tintScale = 0.4f * (1.0f - glow) / a;
				const float glowTerm = glow / a;
				std::uint8_t tintBlend[4];
				tintBlend[0] = (std::uint8_t)std::clamp((std::int32_t)((WaterColorR * tintScale + glowTerm) * 255.0f + 0.5f), 0, 255);
				tintBlend[1] = (std::uint8_t)std::clamp((std::int32_t)((WaterColorG * tintScale + glowTerm) * 255.0f + 0.5f), 0, 255);
				tintBlend[2] = (std::uint8_t)std::clamp((std::int32_t)((WaterColorB * tintScale + glowTerm) * 255.0f + 0.5f), 0, 255);
				tintBlend[3] = (std::uint8_t)std::clamp((std::int32_t)(a * 255.0f + 0.5f), 1, 254);
				BlendScanlineConstSrcAlpha(px, vpW, tintBlend);
			}

			if (hasLighting) {
				const std::int32_t lmY = std::min(y / scale, lmH - 1);
				const float* texelBase = light.Lightmap + (std::size_t)lmY * lmW * 2;
				CombineLightingScanline(px, vpW, texelBase, lmW, scale, ambR, ambG, ambB);
			}

			if (hasWater && !isUnderwaterRow && aboveWaterBlend[3] != 0) {
				BlendScanlineConstSrcAlpha(px, vpW, aboveWaterBlend);
			}
		}
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

	namespace
	{
		/**
			General vertex-fed dispatch of the MeshSprite family (called from SwDevice::Dispatch when the
			bound program declares real vertex attributes). Reads the interleaved vertices of
			[firstVertex, firstVertex + numVertices) from the bound vertex buffer, transforms them on the
			CPU exactly like the DefaultMeshSprite vertex shader (aPosition scaled by the instance's
			spriteSize through modelMatrix and the projection; aTexCoords mapped through texRect) and
			issues one general clip-space [x, y, u, v] draw per instance run (batched draws carry a
			per-vertex aMeshIndex; a run is a maximal span of one index, so each run keeps its own
			instance color / texRect / constant varyings).
		*/
		void DispatchMeshVerticesImpl(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const SwGeneratedShaderInfo& generatedShader, SwShaderProgram* program, const float* pv,
			const std::uint8_t* blockData, std::uint32_t blockDataSize, std::uint32_t instanceStride,
			std::int32_t uTextureUnit, const SwTexture* const* boundTextures,
			bool blendOn, SwBlendFactor bsrc, SwBlendFactor bdst, bool scissorEnabled, const Recti& scissorRect)
		{
			SwVertexFormat::Attribute* posAttr = program->GetAttribute("aPosition");
			SwVertexFormat::Attribute* texAttr = program->GetAttribute("aTexCoords");
			SwVertexFormat::Attribute* idxAttr = program->GetAttribute("aMeshIndex");
			if (texAttr != nullptr && (!texAttr->IsEnabled() || texAttr->GetVbo() != posAttr->GetVbo())) {
				texAttr = nullptr;
			}
			if (idxAttr != nullptr && (!idxAttr->IsEnabled() || idxAttr->GetVbo() != posAttr->GetVbo())) {
				idxAttr = nullptr;
			}

			const SwBuffer* vbo = posAttr->GetVbo();
			const std::uint8_t* vboData = vbo->HostData();
			std::int32_t stride = posAttr->GetStride();
			if (stride <= 0) {
				stride = std::int32_t((2 + (texAttr != nullptr ? 2 : 0) + (idxAttr != nullptr ? 1 : 0)) * sizeof(float));
			}
			const std::size_t posOff = std::size_t(posAttr->GetBaseOffset()) + reinterpret_cast<std::uintptr_t>(posAttr->GetPointer());
			const std::size_t texOff = (texAttr != nullptr ? std::size_t(texAttr->GetBaseOffset()) + reinterpret_cast<std::uintptr_t>(texAttr->GetPointer()) : 0);
			const std::size_t idxOff = (idxAttr != nullptr ? std::size_t(idxAttr->GetBaseOffset()) + reinterpret_cast<std::uintptr_t>(idxAttr->GetPointer()) : 0);

			// The whole vertex range must be inside the buffer store (the offsets address into each vertex)
			std::size_t maxAttrEnd = posOff + 2 * sizeof(float);
			if (texAttr != nullptr) {
				maxAttrEnd = std::max(maxAttrEnd, texOff + 2 * sizeof(float));
			}
			if (idxAttr != nullptr) {
				maxAttrEnd = std::max(maxAttrEnd, idxOff + sizeof(std::int32_t));
			}
			if (vboData == nullptr || firstVertex < 0 ||
			    (std::size_t(firstVertex) + std::size_t(numVertices) - 1) * std::size_t(stride) + maxAttrEnd > vbo->GetSize()) {
				LOGW("Skipped draw: Vertex range exceeds the bound vertex buffer");
				return;
			}

			// The no-texture mesh variants drop texRect from the instance block, which moves spriteSize up
			const bool hasTexRect = (texAttr != nullptr);
			const std::uint32_t spriteSizeOffset = (hasTexRect ? kSpriteSizeOffset : kSpriteSizeNoTexOffset);

			const std::uint32_t uniformsSize = generatedShader.uniformsSize;
			std::uint8_t uniformScratch[MaxFragmentShaderUserDataSize];
			// Reused per run; safe because both the immediate rasterizer and the tile renderer's submit
			// (which snapshots the vertices into the deferred command) consume it before Draw() returns
			static std::vector<float> vertexScratch;

			auto vertexBytes = [&](std::int32_t index) {
				return vboData + (std::size_t(firstVertex) + std::size_t(index)) * std::size_t(stride);
			};
			auto meshIndexAt = [&](std::int32_t index) -> std::int32_t {
				std::int32_t value;
				std::memcpy(&value, vertexBytes(index) + idxOff, sizeof(value));
				return (value >= 0 ? value : 0);
			};

			std::int32_t runBegin = 0;
			while (runBegin < numVertices) {
				const std::int32_t meshIndex = (idxAttr != nullptr ? meshIndexAt(runBegin) : 0);
				std::int32_t runEnd = (idxAttr != nullptr ? runBegin + 1 : numVertices);
				while (idxAttr != nullptr && runEnd < numVertices && meshIndexAt(runEnd) == meshIndex) {
					runEnd++;
				}
				const std::int32_t runCount = runEnd - runBegin;

				const std::size_t instOffset = std::size_t(meshIndex) * instanceStride;
				if (blockDataSize != 0 && instOffset + spriteSizeOffset + 2 * sizeof(float) > blockDataSize) {
					runBegin = runEnd;
					continue;	// Malformed mesh index - skip the run rather than read past the bound block
				}
				const std::uint8_t* inst = blockData + instOffset;

				float mvp[16];
				Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
				const float* spriteSize = reinterpret_cast<const float*>(inst + spriteSizeOffset);
				const float* texRect = (hasTexRect ? reinterpret_cast<const float*>(inst + kTexRectOffset) : nullptr);

				vertexScratch.resize(std::size_t(runCount) * 4);
				float* out = vertexScratch.data();
				for (std::int32_t i = runBegin; i < runEnd; i++, out += 4) {
					float pos[2];
					std::memcpy(pos, vertexBytes(i) + posOff, sizeof(pos));
					const float wx = pos[0] * spriteSize[0];
					const float wy = pos[1] * spriteSize[1];
					float cx = mvp[0] * wx + mvp[4] * wy + mvp[12];
					float cy = mvp[1] * wx + mvp[5] * wy + mvp[13];
					const float cw = mvp[3] * wx + mvp[7] * wy + mvp[15];
					if (cw != 0.0f && cw != 1.0f) {
						// The 2D pipeline's projections keep w == 1; divide only when one does not
						cx /= cw;
						cy /= cw;
					}
					out[0] = cx;
					out[1] = cy;
					if (texRect != nullptr) {
						float tex[2];
						std::memcpy(tex, vertexBytes(i) + texOff, sizeof(tex));
						out[2] = tex[0] * texRect[0] + texRect[1];
						out[3] = tex[1] * texRect[2] + texRect[3];
					} else {
						out[2] = 0.0f;
						out[3] = 0.0f;
					}
				}

				DrawContext ctx;
				for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
					ctx.textures[u] = boundTextures[u];
				}
				std::memcpy(ctx.ff.color, inst + kColorOffset, sizeof(ctx.ff.color));
				ctx.ff.hasTexture = hasTexRect;
				ctx.ff.textureUnit = uTextureUnit;

				// Populate the generated fragment's uniforms + this instance's constant varyings, exactly
				// like the procedural-quad generated path
				if (uniformsSize > 0) {
					std::memset(uniformScratch, 0, uniformsSize);
				}
				for (std::uint32_t fi = 0; fi < generatedShader.uniformFieldCount; fi++) {
					const SwGeneratedUniformField& field = generatedShader.uniformFields[fi];
					const std::uint8_t* v = program->ResolveUniform(field.name);
					if (v != nullptr && field.offset + field.componentCount * sizeof(float) <= uniformsSize) {
						std::memcpy(uniformScratch + field.offset, v, field.componentCount * sizeof(float));
					}
				}
				if (generatedShader.computeVaryings != nullptr) {
					generatedShader.computeVaryings(uniformScratch, inst);
				}
				ctx.fragmentShader = generatedShader.fragment;
				ctx.fragmentShaderUserData = uniformScratch;
				ctx.fragmentShaderUserDataSize = uniformsSize;
				ctx.blendingEnabled = blendOn;
				ctx.blendSrc = bsrc;
				ctx.blendDst = bdst;
				ctx.scissorEnabled = scissorEnabled;
				ctx.scissorRect = scissorRect;
				ctx.vertexData = vertexScratch.data();
				ctx.vertexStride = 4 * sizeof(float);
				SwRaster::SetDrawContext(ctx);
				SwRaster::Draw(primitive, 0, runCount);

				runBegin = runEnd;
			}
		}
	}

	void SwDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		if (currentProgram_ == nullptr || numVertices <= 0) {
			return;
		}

		const SwEffect effect = currentProgram_->GetEffect();

		// The viewport compositor. The software backend renders the scene straight to the screen buffer and has no
		// shader post-processing, so a Combine draw does not run a fragment: it triggers the CPU dynamic-lighting
		// combine the compositor queued for this viewport (SetPendingSoftwareLighting), applied in place over the
		// screen buffer. Combine is only ever the compositor, so every Combine draw is intercepted here.
		if (effect == SwEffect::Combine) {
			ApplyPendingSoftwareLighting();
			return;
		}

		// Resolve the offline-transpiled generated fragment for this program (looked up by object label = the
		// shader name; FindGeneratedShader also resolves the labels that bake a variant into the name, such as
		// the "...Dither" ones). An Unknown-effect program requires one, else the draw is skipped as before.
		// TexturedBackground(+Circle) and PaletteRemap(+Batched) are dispatched entirely through this generated
		// fragment. DefaultSprite / DefaultBatchedSprites keep their dedicated no-fragment fast path and never
		// look one up.
		const bool prefersGenerated = (effect == SwEffect::Unknown ||
			effect == SwEffect::TexturedBackground || effect == SwEffect::TexturedBackgroundCircle ||
			effect == SwEffect::PaletteRemap || effect == SwEffect::BatchedPaletteRemap);
		const SwGeneratedShaderInfo* generatedShader = nullptr;
		if (prefersGenerated) {
			generatedShader = FindGeneratedShader(currentProgram_->GetObjectLabel());
			if (generatedShader == nullptr) {
				// The engine registers the nCine default programs under labels without the shader file's
				// "Default" prefix (e.g. "MeshSprite" for DefaultMeshSprite.shader, "BatchedMeshSprites"
				// for DefaultBatchedMeshSprites.shader); retry prefixed so the mesh family resolves its
				// transpiled fragment. Sprite-family labels never get here (they classify to a fast path).
				const char* label = currentProgram_->GetObjectLabel();
				if (label != nullptr && label[0] != '\0') {
					std::string prefixed = "Default";
					prefixed += label;
					generatedShader = FindGeneratedShader(prefixed.c_str());
				}
			}
			if (generatedShader == nullptr && effect == SwEffect::Unknown) {
				// Expected for shaders the offline transpiler declined - their visuals are simply absent on
				// the software backend. Warn once per program, not per draw.
				static std::vector<std::string> warnedLabels;
				const char* label = currentProgram_->GetObjectLabel();
				const std::string labelStr = (label != nullptr ? label : "<unlabeled>");
				if (std::find(warnedLabels.begin(), warnedLabels.end(), labelStr) == warnedLabels.end()) {
					warnedLabels.push_back(labelStr);
					LOGW("Skipping draws of program \"{}\": No transpiled C++ fragment", StringView(labelStr));
				}
				return;
			}
		}

		Framebuffer fb;
		if (!ResolveFramebuffer(fb)) {
			LOGW("Skipped draw: No render target bound");
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
			LOGW("Skipped draw: Program has no instance block");
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = boundUniformRanges_[binding].Data;
		if (blockData == nullptr) {
			LOGW("Skipped draw: No uniform block data bound");
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
		// procedural sprite quad (vertexData stays null, so FetchVertex synthesizes the four corners from ff).
		// userDataSize is the byte size of the block userData points at, so the tile renderer can snapshot it
		// when the draw is deferred (its storage is caller-stack memory); pass 0 when there is no callback.
		auto drawQuad = [&](const FFState& ff, FragmentShaderFn fragmentShader, void* userData, std::uint32_t userDataSize) {
			DrawContext ctx;
			for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
				ctx.textures[u] = boundTextures_[u];
			}
			ctx.ff = ff;
			ctx.fragmentShader = fragmentShader;
			ctx.fragmentShaderUserData = userData;
			ctx.fragmentShaderUserDataSize = userDataSize;
			// PaletteRemap(+Batched) draws qualify for the tile renderer's palette-LUT fast path (the
			// classified effect guarantees the fragment is the transpiled PaletteRemap one, whose parameter
			// block is a single vPaletteOffset float); the remaining constraints are validated at build time
			ctx.paletteRemapHint = ((effect == SwEffect::PaletteRemap || effect == SwEffect::BatchedPaletteRemap) &&
				fragmentShader != nullptr && userData != nullptr);
			// The no-texture sprite family's fragment is packColor(vColor) - a per-draw constant - so the
			// tile renderer may evaluate it once and run its constant-color fill/blend scanline path
			ctx.constantColorHint = ((effect == SwEffect::DefaultSpriteNoTexture || effect == SwEffect::DefaultBatchedSpritesNoTexture) &&
				fragmentShader != nullptr);
			ctx.blendingEnabled = blendOn;
			ctx.blendSrc = bsrc;
			ctx.blendDst = bdst;
			ctx.scissorEnabled = scissor_.Enabled;
			ctx.scissorRect = scissor_.Rect;
			SwRaster::SetDrawContext(ctx);
			SwRaster::Draw(PrimitiveType::TriangleStrip, 0, 4);
		};

		// A generated shader is batched when its instance block carries a BATCH_SIZE-sized array (instanceStride > 0)
		const bool batched = (effect == SwEffect::DefaultBatchedSprites || effect == SwEffect::DefaultBatchedSpritesNoTexture ||
			effect == SwEffect::BatchedPaletteRemap ||
			(generatedShader != nullptr && instanceStride > 0));
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
		}

		// Offline-transpiled generated fragment path. Reached for Unknown-effect programs and for the
		// Combine / TexturedBackground(+Circle) / PaletteRemap(+Batched) effects, which are dispatched entirely
		// here (the switch below only keeps the DefaultSprite / DefaultBatchedSprites fast path). Handles
		// batched and non-batched draws and fills any per-instance-constant varyings via the generated
		// computeVaryings hook.
		if (generatedShader != nullptr) {
			const std::uint32_t uniformsSize = generatedShader->uniformsSize;
			if (uniformsSize > MaxFragmentShaderUserDataSize) {
				LOGW("Skipped draw: Generated shader uniform block exceeds the callback storage");
				SwRaster::ClearDrawContext();
				return;
			}

			// General vertex-fed path (the MeshSprite family). Programs with real vertex attributes read
			// their geometry from the bound vertex buffer instead of synthesizing a procedural quad: each
			// vertex is transformed on the CPU exactly like the DefaultMeshSprite vertex shader (aPosition
			// scaled by the instance's spriteSize through modelMatrix and the projection, aTexCoords mapped
			// through texRect) and handed to the rasterizer as interleaved clip-space [x, y, u, v]. A
			// batched draw (aMeshIndex attribute) is split at instance boundaries so every run keeps its
			// own instance color / texRect / constant varyings; the stitching vertices the batcher
			// duplicates between meshes only ever form degenerate (zero-area) triangles inside a run.
			// In-game consumers: the HUD weapon wheel (LineStrip) and the multiplayer minimap ribbons
			// (TriangleStrip); plain quad sprites never have attributes, so they never take this path.
			SwVertexFormat::Attribute* posAttr = currentProgram_->GetAttribute("aPosition");
			if (posAttr != nullptr && posAttr->IsEnabled() && posAttr->GetVbo() != nullptr) {
				DispatchMeshVerticesImpl(primitive, firstVertex, numVertices, *generatedShader,
					currentProgram_, pv, blockData, boundUniformRanges_[binding].Size, instanceStride,
					samplerUnit("uTexture", 0), boundTextures_, blendOn, bsrc, bdst,
					scissor_.Enabled, scissor_.Rect);
				SwRaster::ClearDrawContext();
				return;
			}

			// The generated fragment reads the sprite's instance state (and any constant varying) at the fixed
			// sprite-family std140 offsets (through kPaletteOffsetOffset). Guard against a program whose instance
			// block does not follow that layout, so the reads below never run past the bound block.
			const std::uint32_t blockDataSize = boundUniformRanges_[binding].Size;
			const std::size_t maxByte = std::size_t(numInstances > 0 ? numInstances - 1 : 0) * instanceStride +
				(kPaletteOffsetOffset + sizeof(float));
			if (blockDataSize != 0 && maxByte > blockDataSize) {
				LOGW("Skipped draw: Generated shader instance block is not sprite-compatible");
				SwRaster::ClearDrawContext();
				return;
			}

			const std::int32_t uTextureUnit = samplerUnit("uTexture", 0);

			// The shield fragments' SOFTWARE_RENDERER variant reconstructs the GL path's interpolated
			// quad-local position from the interpolated texcoord, so those draws are fed an IDENTITY
			// texRect (u = corner.x, v = corner.y in [0,1]) instead of the instance texRect - for the
			// shield effects that field carries the animated shift values, not texture coordinates, and
			// it reaches the fragment separately through the vShieldRect constant varying computed by
			// computeVaryings from the untouched instance block (see Include/ShieldVs.inc).
			const bool quadLocalUV = (std::strstr(generatedShader->name, "Shield") != nullptr);

			std::uint8_t uniformScratch[MaxFragmentShaderUserDataSize];
			for (std::int32_t k = 0; k < numInstances; k++) {
				const std::uint8_t* inst = blockData + std::size_t(k) * instanceStride;
				FFState ff;
				Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), ff.mvpMatrix);
				std::memcpy(ff.color, inst + kColorOffset, sizeof(ff.color));
				std::memcpy(ff.texRect, inst + kTexRectOffset, sizeof(ff.texRect));
				std::memcpy(ff.spriteSize, inst + kSpriteSizeOffset, sizeof(ff.spriteSize));
				if (quadLocalUV) {
					ff.texRect[0] = 1.0f;
					ff.texRect[1] = 0.0f;
					ff.texRect[2] = 1.0f;
					ff.texRect[3] = 0.0f;
				}
				ff.hasTexture = true;
				ff.textureUnit = uTextureUnit;

				// Populate the shader's "<Program>_Uniforms" struct generically: zero it, then copy each
				// committed loose uniform to its offset (a uniform that was never set stays zero).
				if (uniformsSize > 0) {
					std::memset(uniformScratch, 0, uniformsSize);
				}
				for (std::uint32_t fi = 0; fi < generatedShader->uniformFieldCount; fi++) {
					const SwGeneratedUniformField& field = generatedShader->uniformFields[fi];
					const std::uint8_t* v = currentProgram_->ResolveUniform(field.name);
					if (v != nullptr && field.offset + field.componentCount * sizeof(float) <= uniformsSize) {
						std::memcpy(uniformScratch + field.offset, v, field.componentCount * sizeof(float));
					}
				}
				// Fill any per-instance-constant varyings for THIS instance (after the loose uniforms, which a
				// varying expression may reference). Reads the same instance block at baked std140 offsets.
				if (generatedShader->computeVaryings != nullptr) {
					generatedShader->computeVaryings(uniformScratch, inst);
				}
				drawQuad(ff, generatedShader->fragment, uniformScratch, uniformsSize);
			}
			SwRaster::ClearDrawContext();
			return;
		}

		switch (effect) {
			case SwEffect::DefaultSprite:
			case SwEffect::DefaultBatchedSprites: {
				// The promotion in SwTexture guarantees an RGBA8 store, so the primary sample is always 4 bytes
				const std::int32_t uTextureUnit = samplerUnit("uTexture", 0);
				const SwTexture* texture = GetBoundTexture(std::uint32_t(uTextureUnit));
				if (texture == nullptr || texture->GetPixels() == nullptr) {
					LOGW("Skipped draw: No texture bound to the sampler unit");
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
					drawQuad(ff, nullptr, nullptr, 0);
				}
				break;
			}

			case SwEffect::DefaultSpriteNoTexture:
			case SwEffect::DefaultBatchedSpritesNoTexture: {
				// Solid-colour sprite: no texture is bound (so there is no fast-blit) and the instance block
				// omits texRect, which places spriteSize at offset 80 (not 96). Run the transpiled no-texture
				// fragment (COLOR = vColor); it overwrites the pixel unconditionally, so an unbound sampler unit
				// is handled safely. hasTexture stays false so the rasterizer never dereferences a null texture.
				const FragmentShaderFn noTexFragment = (effect == SwEffect::DefaultBatchedSpritesNoTexture)
					? &DefaultBatchedSpritesNoTexture_Fragment : &DefaultSpriteNoTexture_Fragment;
				for (std::int32_t k = 0; k < numInstances; k++) {
					const std::uint8_t* inst = blockData + std::size_t(k) * instanceStride;
					FFState ff;
					Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), ff.mvpMatrix);
					std::memcpy(ff.color, inst + kColorOffset, sizeof(ff.color));
					std::memcpy(ff.spriteSize, inst + kSpriteSizeNoTexOffset, sizeof(ff.spriteSize));
					ff.hasTexture = false;
					drawQuad(ff, noTexFragment, nullptr, 0);
				}
				break;
			}

			default:
				// Every other effect is dispatched through the generated fragment path above (an Unknown
				// program that matched none already returned there). Nothing to do here.
				break;
		}

		SwRaster::ClearDrawContext();
	}
}
