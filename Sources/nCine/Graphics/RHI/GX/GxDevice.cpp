#include "GxDevice.h"
#include "GxBuffer.h"
#include "GxShaderProgram.h"
#include "GxRenderTarget.h"
#include "GxTexture.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cmath>
#include <cstring>
#include <malloc.h>

#include <ogc/cache.h>

namespace nCine::RHI::GX
{
	namespace
	{
		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software
		// backend's decode (see SwDevice.cpp)
		constexpr std::uint32_t kModelMatrixOffset = 0;
		constexpr std::uint32_t kColorOffset = 64;
		constexpr std::uint32_t kTexRectOffset = 80;
		constexpr std::uint32_t kSpriteSizeOffset = 96;
		// The palette shaders park a flat palette index in the std140 tail padding after spriteSize
		constexpr std::uint32_t kPaletteOffsetOffset = 104;
		// The no-texture sprite family drops texRect, so spriteSize sits directly after color
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

		constexpr std::uint32_t GxFifoSize = 256 * 1024;

		const float IdentityMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		// Column-major 4x4 multiply, out = a * b (matches the software device)
		void Mat4Mul(const float* DEATH_RESTRICT a, const float* DEATH_RESTRICT b, float* DEATH_RESTRICT out)
		{
			for (std::int32_t col = 0; col < 4; col++) {
				for (std::int32_t row = 0; row < 4; row++) {
					out[col * 4 + row] =
						a[0 * 4 + row] * b[col * 4 + 0] +
						a[1 * 4 + row] * b[col * 4 + 1] +
						a[2 * 4 + row] * b[col * 4 + 2] +
						a[3 * 4 + row] * b[col * 4 + 3];
				}
			}
		}

		// Maps a pipeline-neutral blend factor onto the GX factor set. GX names the color-source factors
		// from the destination's viewpoint (GX_BL_SRCCLR is only valid as a destination factor and
		// GX_BL_DSTCLR as a source factor), which matches how the game uses them.
		std::uint8_t MapBlendGx(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return GX_BL_ZERO;
				case nCine::BlendingFactor::One:				return GX_BL_ONE;
				case nCine::BlendingFactor::SrcColor:			return GX_BL_SRCCLR;
				case nCine::BlendingFactor::OneMinusSrcColor:	return GX_BL_INVSRCCLR;
				case nCine::BlendingFactor::DstColor:			return GX_BL_DSTCLR;
				case nCine::BlendingFactor::OneMinusDstColor:	return GX_BL_INVDSTCLR;
				case nCine::BlendingFactor::SrcAlpha:			return GX_BL_SRCALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return GX_BL_INVSRCALPHA;
				case nCine::BlendingFactor::DstAlpha:			return GX_BL_DSTALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return GX_BL_INVDSTALPHA;
				default:										return GX_BL_ONE;
			}
		}

		// Packs one RGBA8 palette entry into the RGB5A3 TLUT format (alpha-capable palette entries)
		inline std::uint16_t Rgb5a3FromRgba(std::uint32_t rgba)
		{
			const std::uint32_t r = rgba & 0xFF;
			const std::uint32_t g = (rgba >> 8) & 0xFF;
			const std::uint32_t b = (rgba >> 16) & 0xFF;
			const std::uint32_t a = (rgba >> 24) & 0xFF;
			if (a >= 224) {
				// Opaque: 0b1 RRRRR GGGGG BBBBB
				return std::uint16_t(0x8000u | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
			}
			// Translucent: 0b0 AAA RRRR GGGG BBBB
			return std::uint16_t(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		// Whether the vertex descriptor currently includes texture coordinates
		bool g_vertexModeTextured = true;

		void SetVertexModeTextured(bool textured)
		{
			if (g_vertexModeTextured == textured) {
				return;
			}
			g_vertexModeTextured = textured;
			GX_ClearVtxDesc();
			GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
			GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
			if (textured) {
				GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
			}
			GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
			GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
			if (textured) {
				GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
			}
			GX_SetNumTexGens(textured ? 1 : 0);
			if (textured) {
				GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
			}
			GX_SetNumChans(1);
			GX_SetNumTevStages(1);
			GX_SetTevOrder(GX_TEVSTAGE0, textured ? GX_TEXCOORD0 : GX_TEXCOORDNULL,
				textured ? GX_TEXMAP0 : GX_TEXMAP_NULL, GX_COLOR0A0);
			GX_SetTevOp(GX_TEVSTAGE0, textured ? GX_MODULATE : GX_PASSCLR);
		}
	}

	GxDevice::BlendingState GxDevice::blending_;
	GxDevice::DepthTestState GxDevice::depthTest_;
	GxDevice::CullFaceState GxDevice::cullFace_;
	GxDevice::ScissorState GxDevice::scissor_;
	Recti GxDevice::viewport_(0, 0, 0, 0);
	Colorf GxDevice::clearColor_(0.0f, 0.0f, 0.0f, 1.0f);

	GxShaderProgram* GxDevice::currentProgram_ = nullptr;
	const GxTexture* GxDevice::boundTextures_[GxDevice::MaxTextureUnits] = {};
	GxDevice::UniformRange GxDevice::boundUniformRanges_[GxDevice::MaxUniformBindings] = {};
	GxRenderTarget* GxDevice::currentRenderTarget_ = nullptr;

	GXRModeObj* GxDevice::rmode_ = nullptr;
	void* GxDevice::gxFifo_ = nullptr;
	bool GxDevice::gxInitialized_ = false;
	std::int32_t GxDevice::logicalWidth_ = 0;
	std::int32_t GxDevice::logicalHeight_ = 0;

	GxTexture* GxDevice::paletteTexture_ = nullptr;
	std::uint32_t GxDevice::paletteGeneration_ = 1;
	GxDevice::TlutSlot GxDevice::tlutSlots_[GxDevice::MaxTlutSlots] = {};
	std::uint32_t GxDevice::tlutUseCounter_ = 0;
	std::uint32_t GxDevice::frameCounter_ = 0;

	std::vector<GxDevice::PendingSoftwareLight> GxDevice::pendingSoftwareLights_;

	std::uint8_t* GxDevice::lightmapStore_ = nullptr;
	std::size_t GxDevice::lightmapStoreSize_ = 0;
	GXTexObj GxDevice::lightmapTexObj_;

	// ------------------------------------------------------------------ session

	void GxDevice::InitializeGx(GXRModeObj* rmode)
	{
		if (gxInitialized_) {
			rmode_ = rmode;
			return;
		}
		rmode_ = rmode;

		// The FIFO must be accessed through the uncached alias, so the GP sees the commands immediately
		gxFifo_ = MEM_K0_TO_K1(memalign(32, GxFifoSize));
		std::memset(gxFifo_, 0, GxFifoSize);
		GX_Init(gxFifo_, GxFifoSize);

		GXColor background = { 0, 0, 0, 255 };
		GX_SetCopyClear(background, GX_MAX_Z24);

		GX_SetViewport(0.0f, 0.0f, float(rmode->fbWidth), float(rmode->efbHeight), 0.0f, 1.0f);
		GX_SetDispCopyYScale(float(rmode->xfbHeight) / float(rmode->efbHeight));
		GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
		GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
		GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
		GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
		GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight ? GX_ENABLE : GX_DISABLE));
		GX_SetDispCopyGamma(GX_GM_1_0);

		// 2D painter's-order pipeline: no depth, no culling, premultiplied ortho quads
		GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
		GX_SetCullMode(GX_CULL_NONE);
		GX_SetColorUpdate(GX_TRUE);

		// Force the initial descriptor setup through the mode toggle
		g_vertexModeTextured = true;
		SetVertexModeTextured(false);

		logicalWidth_ = rmode->fbWidth;
		logicalHeight_ = rmode->efbHeight;

		gxInitialized_ = true;
	}

	void GxDevice::PresentToXfb(void* xfb)
	{
		if (!gxInitialized_) {
			return;
		}
		FlushCurrentRenderTarget();

		// The display copy runs asynchronously on the GP; GX_DrawDone() after it drains the FIFO and waits
		// until the copy has finished, so the following flip never displays a not-yet-copied buffer
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(xfb, GX_TRUE);	// The copy also clears the EFB for the next frame (GX_SetCopyClear)
		GX_DrawDone();
		frameCounter_++;
	}

	void GxDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			logicalWidth_ = width;
			logicalHeight_ = height;
		}
	}

	// ------------------------------------------------------------------ state

	void GxDevice::SetBlendingEnabled(bool enabled) { blending_.Enabled = enabled; }
	void GxDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}
	GxDevice::BlendingState GxDevice::GetBlendingState() { return blending_; }
	void GxDevice::SetBlendingState(const BlendingState& state) { blending_ = state; }

	void GxDevice::SetDepthTestEnabled(bool enabled) { depthTest_.TestEnabled = enabled; }
	void GxDevice::SetDepthMaskEnabled(bool enabled) { depthTest_.MaskEnabled = enabled; }
	GxDevice::DepthTestState GxDevice::GetDepthTestState() { return depthTest_; }
	void GxDevice::SetDepthTestState(const DepthTestState& state) { depthTest_ = state; }

	void GxDevice::SetCullFaceEnabled(bool enabled) { cullFace_.Enabled = enabled; }
	GxDevice::CullFaceState GxDevice::GetCullFaceState() { return cullFace_; }
	void GxDevice::SetCullFaceState(const CullFaceState& state) { cullFace_ = state; }

	GxDevice::ScissorState GxDevice::GetScissorState() { return scissor_; }
	void GxDevice::SetScissorState(const ScissorState& state) { scissor_ = state; }
	void GxDevice::SetScissor(const Recti& rect) { scissor_.Rect = rect; }
	void GxDevice::SetScissorTestEnabled(bool enabled) { scissor_.Enabled = enabled; }

	Recti GxDevice::GetViewport() { return viewport_; }
	void GxDevice::SetViewport(const Recti& rect) { viewport_ = rect; }
	void GxDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		viewport_ = Recti(x, y, width, height);
	}

	Colorf GxDevice::GetClearColor() { return clearColor_; }
	void GxDevice::SetClearColor(const Colorf& color)
	{
		clearColor_ = color;
		if (gxInitialized_) {
			GXColor c = { QuantizeChannel(color.R), QuantizeChannel(color.G), QuantizeChannel(color.B), QuantizeChannel(color.A) };
			GX_SetCopyClear(c, GX_MAX_Z24);
		}
	}

	void GxDevice::Clear(ClearFlags flags)
	{
		static_cast<void>(flags);
		if (!gxInitialized_) {
			return;
		}
		// Immediate clear: a full-target flat quad (the EFB copy-clear only applies at copy time)
		ApplyProjection();
		GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
		const std::int32_t w = (currentRenderTarget_ != nullptr ? viewport_.W : logicalWidth_);
		const std::int32_t h = (currentRenderTarget_ != nullptr ? viewport_.H : logicalHeight_);
		SetVertexModeTextured(false);
		const std::uint8_t r = QuantizeChannel(clearColor_.R), g = QuantizeChannel(clearColor_.G);
		const std::uint8_t b = QuantizeChannel(clearColor_.B), a = QuantizeChannel(clearColor_.A);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(0.0f, 0.0f, 0.0f);				GX_Color4u8(r, g, b, a);
			GX_Position3f32(float(w), 0.0f, 0.0f);			GX_Color4u8(r, g, b, a);
			GX_Position3f32(float(w), float(h), 0.0f);		GX_Color4u8(r, g, b, a);
			GX_Position3f32(0.0f, float(h), 0.0f);			GX_Color4u8(r, g, b, a);
		GX_End();
	}

	// ------------------------------------------------------------------ per-draw state application

	void GxDevice::ApplyProjection()
	{
		// Ortho projection over the current target's logical space; the GX viewport spans the whole EFB for
		// the screen (that maps the logical resolution onto the display copy = free upscale) and the target
		// rect for a render-target pass (rendered 1:1 into the EFB corner, then copied out)
		Mtx44 proj;
		std::int32_t w, h;
		if (currentRenderTarget_ != nullptr) {
			GxTexture* texture = currentRenderTarget_->GetColorTexture(0);
			w = (texture != nullptr ? texture->GetWidth() : viewport_.W);
			h = (texture != nullptr ? texture->GetHeight() : viewport_.H);
			GX_SetViewport(0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f);
		} else {
			w = (logicalWidth_ > 0 ? logicalWidth_ : (rmode_ != nullptr ? rmode_->fbWidth : 640));
			h = (logicalHeight_ > 0 ? logicalHeight_ : (rmode_ != nullptr ? rmode_->efbHeight : 480));
			if (rmode_ != nullptr) {
				GX_SetViewport(0.0f, 0.0f, float(rmode_->fbWidth), float(rmode_->efbHeight), 0.0f, 1.0f);
			}
		}
		guOrtho(proj, 0.0f, float(h), 0.0f, float(w), 0.0f, 1.0f);
		GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

		Mtx identity;
		guMtxIdentity(identity);
		GX_LoadPosMtxImm(identity, GX_PNMTX0);
		GX_SetCurrentMtx(GX_PNMTX0);
	}

	void GxDevice::ApplyRenderState()
	{
		if (blending_.Enabled) {
			GX_SetBlendMode(GX_BM_BLEND, MapBlendGx(blending_.SrcRgb), MapBlendGx(blending_.DstRgb), GX_LO_CLEAR);
		} else {
			GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
		}

		// The engine hands scissor rectangles in bottom-up (OpenGL) window coordinates of the logical
		// space; GX scissors in top-down EFB pixels, so flip and scale
		std::int32_t targetW = logicalWidth_, targetH = logicalHeight_;
		float scaleX = 1.0f, scaleY = 1.0f;
		if (currentRenderTarget_ == nullptr && rmode_ != nullptr && targetW > 0 && targetH > 0) {
			scaleX = float(rmode_->fbWidth) / float(targetW);
			scaleY = float(rmode_->efbHeight) / float(targetH);
		}
		if (scissor_.Enabled && targetH > 0) {
			// Screen passes mirror NDC (see Dispatch), so the engine's bottom-up scissor maps to raster
			// rows directly; render-to-texture passes keep the unmirrored top-down store and flip it
			const std::int32_t rasterY = (currentRenderTarget_ == nullptr
				? scissor_.Rect.Y : targetH - scissor_.Rect.Y - scissor_.Rect.H);
			GX_SetScissor(std::uint32_t(float(scissor_.Rect.X) * scaleX), std::uint32_t(float(rasterY) * scaleY),
				std::uint32_t(float(scissor_.Rect.W) * scaleX), std::uint32_t(float(scissor_.Rect.H) * scaleY));
		} else if (rmode_ != nullptr) {
			GX_SetScissor(0, 0, rmode_->fbWidth, rmode_->efbHeight);
		}
	}

	void GxDevice::FlushCurrentRenderTarget()
	{
		if (currentRenderTarget_ == nullptr) {
			return;
		}
		GxTexture* texture = currentRenderTarget_->GetColorTexture(0);
		if (texture == nullptr || texture->GetRenderTargetStore() == nullptr) {
			return;
		}
		// Copy the rendered EFB region out into the texture's tiled store (and leave the EFB cleared for
		// whatever pass follows)
		const std::uint16_t w = std::uint16_t(texture->GetWidth());
		const std::uint16_t h = std::uint16_t(texture->GetHeight());
		GX_DrawDone();
		GX_SetTexCopySrc(0, 0, w, h);
		GX_SetTexCopyDst(w, h, GX_TF_RGBA8, GX_FALSE);
		GX_CopyTex(texture->GetRenderTargetStore(), GX_TRUE);
		GX_PixModeSync();
		GX_InvalidateTexAll();
	}

	// ------------------------------------------------------------------ draw entry points

	void GxDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GxDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GxDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void GxDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle GxDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void GxDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool GxDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;	// The FIFO is drained at present; fences signal immediately
	}

	void GxDevice::SetupInitialState()
	{
		blending_ = BlendingState();
		depthTest_ = DepthTestState();
		cullFace_ = CullFaceState();
		scissor_ = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void GxDevice::BindProgram(GxShaderProgram* program) { currentProgram_ = program; }
	GxShaderProgram* GxDevice::CurrentProgram() { return currentProgram_; }

	void GxDevice::BindTexture(std::uint32_t unit, const GxTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	void GxDevice::UnbindTexture(const GxTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (boundTextures_[i] == texture) {
				boundTextures_[i] = nullptr;
			}
		}
		if (paletteTexture_ == texture) {
			paletteTexture_ = nullptr;
		}
	}

	const GxTexture* GxDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void GxDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void GxDevice::SetRenderTarget(GxRenderTarget* renderTarget)
	{
		if (renderTarget == currentRenderTarget_) {
			return;
		}
		// Leaving a target resolves it into its texture before anything else renders over the EFB
		FlushCurrentRenderTarget();
		currentRenderTarget_ = renderTarget;
	}

	void GxDevice::UnbindRenderTarget(const GxRenderTarget* renderTarget)
	{
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
	}

	// ------------------------------------------------------------------ palette TLUTs

	void GxDevice::RegisterPaletteTexture(GxTexture* texture)
	{
		paletteTexture_ = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void GxDevice::NotifyPaletteTextureChanged(GxTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != paletteTexture_) {
			return;
		}
		paletteGeneration_++;
		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (tlutSlots_[i].PaletteRow >= firstRow && tlutSlots_[i].PaletteRow < firstRow + rowCount) {
				tlutSlots_[i].PaletteRow = -1;
			}
		}
	}

	std::int32_t GxDevice::AcquireTlutForRow(std::int32_t paletteRow)
	{
		if (paletteTexture_ == nullptr || paletteTexture_->GetPixels() == nullptr ||
			paletteRow < 0 || paletteRow >= paletteTexture_->GetHeight()) {
			return -1;
		}

		tlutUseCounter_++;

		// Reuse a slot already holding this row, or evict the least recently used one
		std::int32_t slot = -1;
		std::uint32_t oldestUse = UINT32_MAX;
		std::int32_t oldestSlot = 0;
		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (tlutSlots_[i].PaletteRow == paletteRow) {
				slot = std::int32_t(i);
				break;
			}
			if (tlutSlots_[i].LastUse < oldestUse) {
				oldestUse = tlutSlots_[i].LastUse;
				oldestSlot = std::int32_t(i);
			}
		}

		if (slot < 0) {
			slot = oldestSlot;
			TlutSlot& s = tlutSlots_[slot];
			if (s.Data == nullptr) {
				s.Data = static_cast<std::uint16_t*>(memalign(32, 256 * sizeof(std::uint16_t)));
				if (s.Data == nullptr) {
					return -1;
				}
			}
			const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(
				paletteTexture_->GetPixels() + std::size_t(paletteRow) * paletteTexture_->GetStrideBytes());
			for (std::int32_t i = 0; i < 256; i++) {
				s.Data[i] = Rgb5a3FromRgba(row[i]);
			}
			DCFlushRange(s.Data, 256 * sizeof(std::uint16_t));

			GXTlutObj tlut;
			GX_InitTlutObj(&tlut, s.Data, GX_TL_RGB5A3, 256);
			GX_LoadTlut(&tlut, GX_TLUT0 + std::uint32_t(slot));
			s.PaletteRow = paletteRow;
		}

		tlutSlots_[slot].LastUse = tlutUseCounter_;
		return slot;
	}

	// ------------------------------------------------------------------ lighting hook

	void GxDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		PendingSoftwareLight light;
		light.Lightmap = lightmap;
		light.LmW = lmW;
		light.LmH = lmH;
		light.Scale = (scale > 0 ? scale : 1);
		light.VpX = vpX;
		light.VpY = vpY;
		light.VpW = vpW;
		light.VpH = vpH;
		light.AmbR = ambR;
		light.AmbG = ambG;
		light.AmbB = ambB;
		light.WaterActive = waterActive;
		light.WaterLevelPx = waterLevelPx;
		light.WaterTime = waterTime;
		light.WaterCamY = waterCamY;
		pendingSoftwareLights_.push_back(light);
	}

	void GxDevice::EndFrame()
	{
		if (!pendingSoftwareLights_.empty()) {
			static bool warnedLeftoverLights = false;
			if (!warnedLeftoverLights) {
				warnedLeftoverLights = true;
				LOGW("Dropping {} unconsumed software-lighting entries", pendingSoftwareLights_.size());
			}
			pendingSoftwareLights_.clear();
		}
	}

	void GxDevice::ApplyPendingSoftwareLighting()
	{
		if (pendingSoftwareLights_.empty()) {
			return;
		}
		const PendingSoftwareLight light = pendingSoftwareLights_.front();
		pendingSoftwareLights_.erase(pendingSoftwareLights_.begin());

		const bool hasLighting = (light.Lightmap != nullptr && light.LmW > 0 && light.LmH > 0);
		const bool hasWater = light.WaterActive;
		if (!hasLighting && !hasWater) {
			return;
		}

		ApplyProjection();
		const float vpX = float(light.VpX), vpY = float(light.VpY);
		const float vpW = float(light.VpW), vpH = float(light.VpH);

		if (hasLighting) {
			// Build the per-texel multiply factor from the CPU lightmap: out ≈ scene * (r*(1+g) + amb*(1-r))
			// per channel - the multiply-only approximation of the software combine (the small additive glow
			// term of very bright lights is dropped). Tiled RGBA8, drawn as one linear-filtered quad with a
			// dst*src blend over the viewport.
			const std::int32_t w = light.LmW, h = light.LmH;
			const std::size_t linearSize = std::size_t(w) * std::size_t(h) * 4;
			const std::size_t tiledSize = std::size_t((w + 3) & ~3) * std::size_t((h + 3) & ~3) * 4;
			if (lightmapStore_ == nullptr || lightmapStoreSize_ < tiledSize) {
				if (lightmapStore_ != nullptr) {
					free(lightmapStore_);
				}
				lightmapStore_ = static_cast<std::uint8_t*>(memalign(32, tiledSize));
				lightmapStoreSize_ = tiledSize;
			}
			if (lightmapStore_ != nullptr) {
				std::vector<std::uint8_t> linear(linearSize);
				for (std::int32_t y = 0; y < h; y++) {
					const float* src = light.Lightmap + std::size_t(y) * w * 2;
					std::uint8_t* dst = linear.data() + std::size_t(y) * w * 4;
					for (std::int32_t x = 0; x < w; x++) {
						float r = src[x * 2];
						float g = src[x * 2 + 1];
						r = (r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r));
						g = (g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g));
						const float lit = r * (1.0f + g);
						dst[x * 4 + 0] = QuantizeChannel(lit + light.AmbR * (1.0f - r));
						dst[x * 4 + 1] = QuantizeChannel(lit + light.AmbG * (1.0f - r));
						dst[x * 4 + 2] = QuantizeChannel(lit + light.AmbB * (1.0f - r));
						dst[x * 4 + 3] = 255;
					}
				}

				// Tile 4x4 RGBA8 (same layout GxTexture uses)
				const std::int32_t tilesX = (w + 3) / 4;
				const std::int32_t tilesY = (h + 3) / 4;
				for (std::int32_t ty = 0; ty < tilesY; ty++) {
					for (std::int32_t tx = 0; tx < tilesX; tx++) {
						std::uint8_t* tile = lightmapStore_ + std::size_t(ty * tilesX + tx) * 64;
						for (std::int32_t row = 0; row < 4; row++) {
							for (std::int32_t col = 0; col < 4; col++) {
								const std::int32_t x = tx * 4 + col;
								const std::int32_t y = ty * 4 + row;
								std::uint8_t r = 255, g = 255, b = 255, a = 255;
								if (x < w && y < h) {
									const std::uint8_t* px = linear.data() + (std::size_t(y) * w + x) * 4;
									r = px[0]; g = px[1]; b = px[2]; a = px[3];
								}
								const std::int32_t i = row * 4 + col;
								tile[i * 2 + 0] = a;
								tile[i * 2 + 1] = r;
								tile[32 + i * 2 + 0] = g;
								tile[32 + i * 2 + 1] = b;
							}
						}
					}
				}
				DCFlushRange(lightmapStore_, std::uint32_t(tiledSize));
				GX_InvalidateTexAll();

				GX_InitTexObj(&lightmapTexObj_, lightmapStore_, std::uint16_t(w), std::uint16_t(h), GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
				GX_InitTexObjFilterMode(&lightmapTexObj_, GX_LINEAR, GX_LINEAR);
				GX_LoadTexObj(&lightmapTexObj_, GX_TEXMAP0);

				SetVertexModeTextured(true);
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);	// out = dst * src
				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer is bottom-up and flipped at present), so V runs 1 -> 0 top -> bottom
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
					GX_Position3f32(vpX, vpY, 0.0f);				GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(0.0f, 1.0f);
					GX_Position3f32(vpX + vpW, vpY, 0.0f);			GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(1.0f, 1.0f);
					GX_Position3f32(vpX + vpW, vpY + vpH, 0.0f);	GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(1.0f, 0.0f);
					GX_Position3f32(vpX, vpY + vpH, 0.0f);			GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(0.0f, 0.0f);
				GX_End();
			}
		}

		if (hasWater) {
			// Water v1: the constant underwater tint band (out = main * 0.6 + waterColor * 0.4) and the
			// above-deep-water darkening; the per-row wave displacement and surface glow of the software
			// combine are dropped (TODO(GX): reproduce them with per-row quads if it matters visually)
			SetVertexModeTextured(false);
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			const float waterTop = vpY + light.WaterLevelPx;
			if (waterTop < vpY + vpH) {
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
					GX_Position3f32(vpX, waterTop, 0.0f);			GX_Color4u8(102, 153, 204, 102);
					GX_Position3f32(vpX + vpW, waterTop, 0.0f);		GX_Color4u8(102, 153, 204, 102);
					GX_Position3f32(vpX + vpW, vpY + vpH, 0.0f);	GX_Color4u8(102, 153, 204, 102);
					GX_Position3f32(vpX, vpY + vpH, 0.0f);			GX_Color4u8(102, 153, 204, 102);
				GX_End();
			}
			const float waterLevelNorm = (vpH > 0.0f ? light.WaterLevelPx / vpH : 1.0f);
			if (waterLevelNorm < 0.4f && waterTop > vpY) {
				const std::uint8_t a = QuantizeChannel(0.4f - waterLevelNorm);
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
					GX_Position3f32(vpX, vpY, 0.0f);				GX_Color4u8(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a);
					GX_Position3f32(vpX + vpW, vpY, 0.0f);			GX_Color4u8(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a);
					GX_Position3f32(vpX + vpW, waterTop, 0.0f);		GX_Color4u8(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a);
					GX_Position3f32(vpX, waterTop, 0.0f);			GX_Color4u8(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a);
				GX_End();
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void GxDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		static_cast<void>(firstVertex);
		if (currentProgram_ == nullptr || numVertices <= 0 || !gxInitialized_) {
			return;
		}

		const GxEffect effect = currentProgram_->GetEffect();

		// The Combine draw is the direct-tier lighting hook (see the software backend)
		if (effect == GxEffect::Combine) {
			ApplyPendingSoftwareLighting();
			return;
		}

		// v1 renders the procedural sprite-quad families only; vertex-fed meshes (LineStrip weapon wheel,
		// mesh sprites) and unclassified effects are skipped with a one-time warning
		const bool isQuadFamily = (effect == GxEffect::DefaultSprite || effect == GxEffect::DefaultBatchedSprites ||
			effect == GxEffect::DefaultSpriteNoTexture || effect == GxEffect::DefaultBatchedSpritesNoTexture ||
			effect == GxEffect::Colorized || effect == GxEffect::BatchedColorized ||
			effect == GxEffect::PaletteRemap || effect == GxEffect::BatchedPaletteRemap ||
			effect == GxEffect::TexturedBackground || effect == GxEffect::TexturedBackgroundCircle);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			if (!currentProgram_->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the GX v1 dispatch", currentProgram_->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = currentProgram_->ResolveUniform("uProjectionMatrix");
		const std::uint8_t* viewBytes = currentProgram_->ResolveUniform("uViewMatrix");
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		const GxUniformBlock* block = currentProgram_->FindBlock("InstanceBlock");
		if (block == nullptr) {
			block = currentProgram_->FindBlock("InstancesBlock");
		}
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = boundUniformRanges_[binding].Data;
		if (blockData == nullptr) {
			return;
		}

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

		float pv[16];
		Mat4Mul(projMat, viewMat, pv);

		const bool batched = (effect == GxEffect::DefaultBatchedSprites || effect == GxEffect::DefaultBatchedSpritesNoTexture ||
			effect == GxEffect::BatchedPaletteRemap || effect == GxEffect::BatchedColorized || instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
			if (instanceStride == 0) {
				instanceStride = 112;
			}
		}

		const bool hasTexture = (effect != GxEffect::DefaultSpriteNoTexture && effect != GxEffect::DefaultBatchedSpritesNoTexture);
		const bool isPaletteRemap = (effect == GxEffect::PaletteRemap || effect == GxEffect::BatchedPaletteRemap);
		const std::int32_t textureUnit = samplerUnit("uTexture", 0);
		const GxTexture* texture = (hasTexture ? boundTextures_[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		if (hasTexture && texture == nullptr) {
			return;
		}

		ApplyProjection();
		ApplyRenderState();
		SetVertexModeTextured(hasTexture);

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = boundUniformRanges_[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		// The viewport rect maps NDC to logical pixels exactly like the software FetchVertex
		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the EFB is scanned out top-down directly, so screen passes mirror NDC here instead
		// (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is what the
		// sampling passes already expect.
		const bool screenPass = (currentRenderTarget_ == nullptr);

		const Recti viewport = (viewport_.W > 0 && viewport_.H > 0)
			? viewport_ : Recti(0, 0, logicalWidth_, logicalHeight_);

		std::int32_t lastTlutSlot = -2;
		GXTexObj* loadedTexObj = nullptr;

		for (std::int32_t k = 0; k < numInstances; k++) {
			const std::uint8_t* inst = blockData + std::size_t(k) * (batched ? instanceStride : 0);

			float mvp[16];
			Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
			float color[4];
			std::memcpy(color, inst + kColorOffset, sizeof(color));
			float texRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			float spriteSize[2];
			if (hasTexture) {
				std::memcpy(texRect, inst + kTexRectOffset, sizeof(texRect));
				std::memcpy(spriteSize, inst + kSpriteSizeOffset, sizeof(spriteSize));
			} else {
				std::memcpy(spriteSize, inst + kSpriteSizeNoTexOffset, sizeof(spriteSize));
			}

			// Bind the instance's texture variant (TLUT row for CI8 indices, palette-baked copy for RG8)
			if (hasTexture) {
				GXTexObj* texObj = nullptr;
				GxTexture* mutableTexture = const_cast<GxTexture*>(texture);
				if (isPaletteRemap && texture->IsIndexed()) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::int32_t paletteRow = std::int32_t(palOffset + 0.5f) / 256;
					const std::int32_t slot = AcquireTlutForRow(paletteRow);
					texObj = mutableTexture->GetTexObj();
					if (texObj != nullptr && slot >= 0 && slot != lastTlutSlot) {
						GX_InitTexObjTlut(texObj, GX_TLUT0 + std::uint32_t(slot));
						lastTlutSlot = slot;
						loadedTexObj = nullptr;		// Force a reload with the new TLUT binding
					}
				} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTexture_ != nullptr && paletteTexture_->GetPixels() != nullptr) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::uint32_t paletteRow = std::uint32_t(std::int32_t(palOffset + 0.5f) / 256);
					const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(
						paletteTexture_->GetPixels() + std::size_t(paletteRow) * paletteTexture_->GetStrideBytes());
					texObj = mutableTexture->EnsureBakedRgba(row, paletteRow, paletteGeneration_);
				} else {
					texObj = mutableTexture->GetTexObj();
				}
				if (texObj == nullptr) {
					continue;
				}
				if (texObj != loadedTexObj) {
					GX_LoadTexObj(texObj, GX_TEXMAP0);
					loadedTexObj = texObj;
				}
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex: unit corner ->
			// spriteSize scale -> mvp -> NDC -> viewport pixels (top-down logical space)
			float px[4], py[4], pu[4], pvv[4];
			for (std::int32_t i = 0; i < 4; i++) {
				const float ax = ((i & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (i & 1) ? 1.0f : 0.0f;
				const float wx = ax * spriteSize[0];
				const float wy = ay * spriteSize[1];
				const float ndcX = mvp[0] * wx + mvp[4] * wy + mvp[12];
				const float ndcY = mvp[1] * wx + mvp[5] * wy + mvp[13];
				px[i] = (ndcX + 1.0f) * 0.5f * float(viewport.W) + float(viewport.X);
				py[i] = (screenPass ? (ndcY + 1.0f) : (1.0f - ndcY)) * 0.5f * float(viewport.H) + float(viewport.Y);
				pu[i] = ax * texRect[0] + texRect[1];
				pvv[i] = ay * texRect[2] + texRect[3];
			}

			if (effect == GxEffect::Colorized || effect == GxEffect::BatchedColorized) {
				// Amplified dye of the Colorized shader (the grayscale step is dropped, but the affected
				// textures - mostly font glyphs - are grayscale already)
				for (std::int32_t c = 0; c < 4; c++) {
					color[c] = 1.0f + (color[c] - 0.5f) * 4.0f;
				}
			}
			const std::uint8_t r = QuantizeChannel(color[0]);
			const std::uint8_t g = QuantizeChannel(color[1]);
			const std::uint8_t b = QuantizeChannel(color[2]);
			const std::uint8_t a = QuantizeChannel(color[3]);

			// Strip order (v0, v1, v2, v3) forms the quad perimeter (v0, v1, v3, v2)
			GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			if (hasTexture) {
				GX_Position3f32(px[0], py[0], 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(pu[0], pvv[0]);
				GX_Position3f32(px[1], py[1], 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(pu[1], pvv[1]);
				GX_Position3f32(px[3], py[3], 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(pu[3], pvv[3]);
				GX_Position3f32(px[2], py[2], 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(pu[2], pvv[2]);
			} else {
				GX_Position3f32(px[0], py[0], 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(px[1], py[1], 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(px[3], py[3], 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(px[2], py[2], 0.0f);	GX_Color4u8(r, g, b, a);
			}
			GX_End();
		}
	}
}
