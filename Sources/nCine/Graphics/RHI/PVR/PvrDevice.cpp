#include "PvrDevice.h"
#include "PvrBuffer.h"
#include "PvrShaderProgram.h"
#include "PvrRenderTarget.h"
#include "PvrTexture.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nCine::RHI::PVR
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
		constexpr std::uint32_t kPaletteOffsetOffset = 104;
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

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

		// Maps a pipeline-neutral blend factor onto the PVR factor set
		std::int32_t MapBlendPvr(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return PVR_BLEND_ZERO;
				case nCine::BlendingFactor::One:				return PVR_BLEND_ONE;
				case nCine::BlendingFactor::SrcColor:			return PVR_BLEND_DESTCOLOR;		// Valid as a dst factor only; the src slot maps below
				case nCine::BlendingFactor::OneMinusSrcColor:	return PVR_BLEND_INVDESTCOLOR;
				case nCine::BlendingFactor::DstColor:			return PVR_BLEND_DESTCOLOR;
				case nCine::BlendingFactor::OneMinusDstColor:	return PVR_BLEND_INVDESTCOLOR;
				case nCine::BlendingFactor::SrcAlpha:			return PVR_BLEND_SRCALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return PVR_BLEND_INVSRCALPHA;
				case nCine::BlendingFactor::DstAlpha:			return PVR_BLEND_DESTALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return PVR_BLEND_INVDESTALPHA;
				default:										return PVR_BLEND_ONE;
			}
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		inline std::uint32_t PackArgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
		}

		// Clamps one screen-space quad edge pair (a..b with linearly mapped texture coordinates ua..ub)
		// into [lo, hi]; returns false when the whole span lies outside. Works for either edge direction.
		bool ClipQuadEdge(float& a, float& b, float& ua, float& ub, float lo, float hi)
		{
			if ((a <= lo && b <= lo) || (a >= hi && b >= hi)) {
				return false;
			}
			const float d = b - a;
			if (d != 0.0f) {
				const float du = (ub - ua) / d;
				const float na = (a < lo ? lo : (a > hi ? hi : a));
				const float nb = (b < lo ? lo : (b > hi ? hi : b));
				ua += (na - a) * du;
				ub += (nb - b) * du;
				a = na;
				b = nb;
			}
			return true;
		}

		// Submits one quad to the open translucent list as a 4-vertex strip. The corner order matches the
		// procedural sprite strip (v0, v1, v2, v3) exactly like the software FetchVertex synthesizes it.
		// The offset colour is added after texturing (only when the polygon enables it), which is how the
		// actor state effects brighten or tint the sprite - see the effect handling in Dispatch.
		void SubmitQuad(const pvr_poly_hdr_t& hdr, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t argb, std::uint32_t oargb = 0, float dx = 0.0f, float dy = 0.0f)
		{
			pvr_prim(const_cast<pvr_poly_hdr_t*>(&hdr), sizeof(hdr));
			pvr_vertex_t vert;
			vert.oargb = oargb;
			vert.argb = argb;
			vert.z = 1.0f;
			for (std::int32_t i = 0; i < 4; i++) {
				vert.flags = (i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
				vert.x = px[i] + dx;
				vert.y = py[i] + dy;
				vert.u = pu[i];
				vert.v = pv[i];
				pvr_prim(&vert, sizeof(vert));
			}
		}
	}

	PvrDevice::BlendingState PvrDevice::blending_;
	PvrDevice::DepthTestState PvrDevice::depthTest_;
	PvrDevice::CullFaceState PvrDevice::cullFace_;
	PvrDevice::ScissorState PvrDevice::scissor_;
	Recti PvrDevice::viewport_(0, 0, 0, 0);
	Colorf PvrDevice::clearColor_(0.0f, 0.0f, 0.0f, 1.0f);

	PvrShaderProgram* PvrDevice::currentProgram_ = nullptr;
	const PvrTexture* PvrDevice::boundTextures_[PvrDevice::MaxTextureUnits] = {};
	PvrDevice::UniformRange PvrDevice::boundUniformRanges_[PvrDevice::MaxUniformBindings] = {};
	PvrRenderTarget* PvrDevice::currentRenderTarget_ = nullptr;

	bool PvrDevice::pvrInitialized_ = false;
	std::int32_t PvrDevice::logicalWidth_ = 640;
	std::int32_t PvrDevice::logicalHeight_ = 480;
	PvrDevice::SceneTarget PvrDevice::sceneTarget_ = PvrDevice::SceneTarget::None;
	std::uint32_t PvrDevice::sceneCounter_ = 0;
	PvrRenderTarget* PvrDevice::sceneRenderTarget_ = nullptr;

	PvrTexture* PvrDevice::paletteTexture_ = nullptr;
	std::uint32_t PvrDevice::paletteGeneration_ = 1;
	PvrDevice::PaletteBank PvrDevice::paletteBanks_[PvrDevice::MaxPaletteBanks] = {};
	std::uint32_t PvrDevice::paletteUseCounter_ = 0;

	std::vector<PvrDevice::PendingSoftwareLight> PvrDevice::pendingSoftwareLights_;

	pvr_ptr_t PvrDevice::lightmapVram_ = nullptr;
	std::size_t PvrDevice::lightmapVramSize_ = 0;
	std::int32_t PvrDevice::lightmapW_ = 0;
	std::int32_t PvrDevice::lightmapH_ = 0;

	// ------------------------------------------------------------------ session

	void PvrDevice::InitializePvr()
	{
		if (pvrInitialized_) {
			return;
		}

		// Everything renders through the translucent list with autosort DISABLED, so the list preserves
		// submission order - the engine's painter's-order queue maps 1:1 (splitting opaque/color-keyed
		// sprites into the punch-through list to save fill rate is a later optimization here)
		pvr_init_params_t params = {
			// Opaque, opaque modifier, translucent, translucent modifier, punch-through
			{ PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0 },
			512 * 1024,		// Vertex buffer size
			0,				// DMA disabled (store-queue submission)
			0,				// No FSAA
			1,				// Autosort DISABLED (submission order = draw order)
			2				// Extra OPB overflow buffers
		};
		pvr_init(&params);
		pvr_set_bg_color(0.0f, 0.0f, 0.0f);
		pvr_set_pal_format(PVR_PAL_ARGB8888);

		pvrInitialized_ = true;
	}

	void PvrDevice::EnsureScene()
	{
		const SceneTarget wanted = (currentRenderTarget_ != nullptr ? SceneTarget::RenderTexture : SceneTarget::Screen);
		if (sceneTarget_ == wanted && (wanted != SceneTarget::RenderTexture || sceneRenderTarget_ == currentRenderTarget_)) {
			return;
		}
		FinishScene();

		pvr_wait_ready();
		if (wanted == SceneTarget::RenderTexture) {
			PvrTexture* texture = currentRenderTarget_->GetColorTexture(0);
			if (texture == nullptr || texture->GetVramPointer() == nullptr) {
				return;		// No surface to render into; draws will be skipped
			}
			// Render-to-texture scene into the target's RGB565 surface
			std::uint32_t rx = std::uint32_t(texture->GetPaddedWidth());
			std::uint32_t ry = std::uint32_t(texture->GetPaddedHeight());
			pvr_scene_begin_txr(texture->GetVramPointer(), &rx, &ry);
			sceneRenderTarget_ = currentRenderTarget_;
		} else {
			pvr_scene_begin();
			sceneRenderTarget_ = nullptr;
		}
		pvr_list_begin(PVR_LIST_TR_POLY);
		sceneTarget_ = wanted;
	}

	void PvrDevice::FinishScene()
	{
		if (sceneTarget_ == SceneTarget::None) {
			return;
		}
		pvr_list_finish();
		pvr_scene_finish();
		sceneTarget_ = SceneTarget::None;
		sceneRenderTarget_ = nullptr;
		sceneCounter_++;
	}

	void PvrDevice::PresentFrame()
	{
		if (!pvrInitialized_) {
			return;
		}
		if (sceneTarget_ == SceneTarget::None) {
			// Nothing was drawn this frame; run an empty scene to keep the display pacing
			pvr_wait_ready();
			pvr_scene_begin();
			pvr_list_begin(PVR_LIST_TR_POLY);
			sceneTarget_ = SceneTarget::Screen;
		}
		FinishScene();
	}

	void PvrDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			logicalWidth_ = width;
			logicalHeight_ = height;
		}
	}

	void PvrDevice::GetTargetScale(float& scaleX, float& scaleY, float& offsetX, float& offsetY)
	{
		offsetX = 0.0f;
		offsetY = 0.0f;
		if (currentRenderTarget_ != nullptr) {
			// Render-to-texture scenes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (logicalWidth_ > 0 ? 640.0f / float(logicalWidth_) : 1.0f);
			scaleY = (logicalHeight_ > 0 ? 480.0f / float(logicalHeight_) : 1.0f);
		}
	}

	// ------------------------------------------------------------------ state

	void PvrDevice::SetBlendingEnabled(bool enabled) { blending_.Enabled = enabled; }
	void PvrDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}
	PvrDevice::BlendingState PvrDevice::GetBlendingState() { return blending_; }
	void PvrDevice::SetBlendingState(const BlendingState& state) { blending_ = state; }

	void PvrDevice::SetDepthTestEnabled(bool enabled) { depthTest_.TestEnabled = enabled; }
	void PvrDevice::SetDepthMaskEnabled(bool enabled) { depthTest_.MaskEnabled = enabled; }
	PvrDevice::DepthTestState PvrDevice::GetDepthTestState() { return depthTest_; }
	void PvrDevice::SetDepthTestState(const DepthTestState& state) { depthTest_ = state; }

	void PvrDevice::SetCullFaceEnabled(bool enabled) { cullFace_.Enabled = enabled; }
	PvrDevice::CullFaceState PvrDevice::GetCullFaceState() { return cullFace_; }
	void PvrDevice::SetCullFaceState(const CullFaceState& state) { cullFace_ = state; }

	PvrDevice::ScissorState PvrDevice::GetScissorState() { return scissor_; }
	void PvrDevice::SetScissorState(const ScissorState& state) { scissor_ = state; }
	void PvrDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like
		// RenderCommand and Viewport rely on it and restore via SetScissorState afterwards)
		scissor_.Enabled = true;
		scissor_.Rect = rect;
	}
	void PvrDevice::SetScissorTestEnabled(bool enabled) { scissor_.Enabled = enabled; }

	Recti PvrDevice::GetViewport() { return viewport_; }
	void PvrDevice::SetViewport(const Recti& rect) { viewport_ = rect; }
	void PvrDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		viewport_ = Recti(x, y, width, height);
	}

	Colorf PvrDevice::GetClearColor() { return clearColor_; }
	void PvrDevice::SetClearColor(const Colorf& color)
	{
		clearColor_ = color;
		if (pvrInitialized_) {
			pvr_set_bg_color(color.R, color.G, color.B);
		}
	}

	void PvrDevice::Clear(ClearFlags flags)
	{
		static_cast<void>(flags);
		if (!pvrInitialized_) {
			return;
		}
		// The scene background provides the frame clear; an explicit mid-scene clear draws a flat quad
		EnsureScene();
		if (sceneTarget_ == SceneTarget::None) {
			return;
		}
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float w = float(currentRenderTarget_ != nullptr ? viewport_.W : logicalWidth_) * scaleX;
		const float h = float(currentRenderTarget_ != nullptr ? viewport_.H : logicalHeight_) * scaleY;

		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		cxt.blend.src = PVR_BLEND_ONE;
		cxt.blend.dst = PVR_BLEND_ZERO;
		pvr_poly_hdr_t hdr;
		pvr_poly_compile(&hdr, &cxt);

		const std::uint32_t argb = PackArgb(QuantizeChannel(clearColor_.R), QuantizeChannel(clearColor_.G),
			QuantizeChannel(clearColor_.B), QuantizeChannel(clearColor_.A));
		const float px[4] = { w, w, 0.0f, 0.0f };
		const float py[4] = { 0.0f, h, 0.0f, h };
		const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		SubmitQuad(hdr, px, py, uv, uv, argb);
	}

	// ------------------------------------------------------------------ draw entry points

	void PvrDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void PvrDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void PvrDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void PvrDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle PvrDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void PvrDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool PvrDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void PvrDevice::SetupInitialState()
	{
		blending_ = BlendingState();
		depthTest_ = DepthTestState();
		cullFace_ = CullFaceState();
		scissor_ = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void PvrDevice::BindProgram(PvrShaderProgram* program) { currentProgram_ = program; }
	PvrShaderProgram* PvrDevice::CurrentProgram() { return currentProgram_; }

	void PvrDevice::BindTexture(std::uint32_t unit, const PvrTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	void PvrDevice::UnbindTexture(const PvrTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (boundTextures_[i] == texture) {
				boundTextures_[i] = nullptr;
			}
		}
		if (paletteTexture_ == texture) {
			paletteTexture_ = nullptr;
		}
		// Drop palette banks built from the destroyed palette so a stale pointer can never match
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (paletteBanks_[i].Palette == texture) {
				paletteBanks_[i].PaletteOffset = -1;
				paletteBanks_[i].Palette = nullptr;
			}
		}
	}

	const PvrTexture* PvrDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void PvrDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void PvrDevice::SetRenderTarget(PvrRenderTarget* renderTarget)
	{
		// The scene state machine reacts lazily at the next draw (EnsureScene); an in-flight scene for a
		// different target is finished there
		currentRenderTarget_ = renderTarget;
	}

	void PvrDevice::UnbindRenderTarget(const PvrRenderTarget* renderTarget)
	{
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
		if (sceneRenderTarget_ == renderTarget) {
			FinishScene();
		}
	}

	// ------------------------------------------------------------------ palette banks

	void PvrDevice::RegisterPaletteTexture(PvrTexture* texture)
	{
		paletteTexture_ = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void PvrDevice::NotifyPaletteTextureChanged(PvrTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != paletteTexture_) {
			return;
		}
		paletteGeneration_++;
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (paletteBanks_[i].PaletteOffset >= (firstRow - 1) * 256 && paletteBanks_[i].PaletteOffset < (firstRow + rowCount) * 256) {
				paletteBanks_[i].PaletteOffset = -1;
			}
		}
	}

	std::int32_t PvrDevice::AcquirePaletteBankForRow(const PvrTexture* palette, std::int32_t paletteOffset)
	{
		// The offset is a flat index into the palette texture and does not need to be row-aligned
		// (e.g. the gem gradients pack two palettes into a single 256-entry row). The palette is usually
		// the registered global one, but effects like the profile character previews bind their own
		// recolored palette texture instead.
		const std::int32_t maxOffset = palette != nullptr
			? palette->GetWidth() * palette->GetHeight() - 256 : 0;
		if (palette == nullptr || palette->GetPixels() == nullptr ||
			paletteOffset < 0 || paletteOffset > maxOffset) {
			return -1;
		}

		paletteUseCounter_++;

		std::int32_t bank = -1;
		std::uint32_t oldestUse = UINT32_MAX;
		std::int32_t oldestBank = 0;
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (paletteBanks_[i].PaletteOffset == paletteOffset && paletteBanks_[i].Palette == palette &&
				paletteBanks_[i].PaletteVersion == palette->GetContentVersion()) {
				bank = std::int32_t(i);
				break;
			}
			if (paletteBanks_[i].LastUse < oldestUse) {
				oldestUse = paletteBanks_[i].LastUse;
				oldestBank = std::int32_t(i);
			}
		}

		if (bank < 0) {
			bank = oldestBank;
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
				palette->GetPixels()) + paletteOffset;
			for (std::int32_t i = 0; i < 256; i++) {
				const std::uint32_t rgba = entries[i];
				pvr_set_pal_entry(std::uint32_t(bank) * 256 + std::uint32_t(i),
					PackArgb(std::uint8_t(rgba & 0xFF), std::uint8_t((rgba >> 8) & 0xFF),
						std::uint8_t((rgba >> 16) & 0xFF), std::uint8_t((rgba >> 24) & 0xFF)));
			}
			paletteBanks_[bank].PaletteOffset = paletteOffset;
			paletteBanks_[bank].Palette = palette;
			paletteBanks_[bank].PaletteVersion = palette->GetContentVersion();
		}

		paletteBanks_[bank].LastUse = paletteUseCounter_;
		return bank;
	}

	// ------------------------------------------------------------------ lighting hook

	void PvrDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
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

	void PvrDevice::EndFrame()
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

	void PvrDevice::ApplyPendingSoftwareLighting()
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

		EnsureScene();
		if (sceneTarget_ == SceneTarget::None) {
			return;
		}
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			// Multiply factor from the CPU lightmap: out ≈ scene * (r*(1+g) + amb*(1-r)) per channel (the
			// multiply-only approximation shared with the GX backend), as an ARGB4444 texture drawn with a
			// dst * src blend over the viewport
			std::int32_t texW = 8, texH = 8;
			while (texW < light.LmW && texW < 1024) texW <<= 1;
			while (texH < light.LmH && texH < 1024) texH <<= 1;
			const std::size_t size = std::size_t(texW) * std::size_t(texH) * 2;
			if (lightmapVram_ == nullptr || lightmapVramSize_ < size) {
				if (lightmapVram_ != nullptr) {
					pvr_mem_free(lightmapVram_);
				}
				lightmapVram_ = pvr_mem_malloc(size);
				lightmapVramSize_ = size;
			}
			if (lightmapVram_ != nullptr) {
				std::vector<std::uint16_t> staging(std::size_t(texW) * std::size_t(texH), 0xFFFF);
				for (std::int32_t y = 0; y < light.LmH; y++) {
					const float* src = light.Lightmap + std::size_t(y) * light.LmW * 2;
					std::uint16_t* dst = staging.data() + std::size_t(y) * texW;
					for (std::int32_t x = 0; x < light.LmW; x++) {
						float r = src[x * 2];
						float g = src[x * 2 + 1];
						r = (r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r));
						g = (g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g));
						const float lit = r * (1.0f + g);
						const std::uint8_t fr = QuantizeChannel(lit + light.AmbR * (1.0f - r));
						const std::uint8_t fg = QuantizeChannel(lit + light.AmbG * (1.0f - r));
						const std::uint8_t fb = QuantizeChannel(lit + light.AmbB * (1.0f - r));
						dst[x] = std::uint16_t(0xF000 | ((fr >> 4) << 8) | ((fg >> 4) << 4) | (fb >> 4));
					}
				}
				pvr_txr_load_ex(staging.data(), lightmapVram_, std::uint32_t(texW), std::uint32_t(texH), PVR_TXRLOAD_16BPP);
				lightmapW_ = texW;
				lightmapH_ = texH;

				pvr_poly_cxt_t cxt;
				pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED,
					texW, texH, lightmapVram_, PVR_FILTER_BILINEAR);
				cxt.gen.culling = PVR_CULLING_NONE;
				cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
				cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				cxt.blend.src = PVR_BLEND_DESTCOLOR;	// out = dst * src
				cxt.blend.dst = PVR_BLEND_ZERO;
				pvr_poly_hdr_t hdr;
				pvr_poly_compile(&hdr, &cxt);

				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer convention), so V runs (used/texH) -> 0 top -> bottom
				const float uMax = float(light.LmW) / float(texW);
				const float vMax = float(light.LmH) / float(texH);
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
				const float pu[4] = { uMax, uMax, 0.0f, 0.0f };
				const float pv[4] = { vMax, 0.0f, vMax, 0.0f };
				SubmitQuad(hdr, px, py, pu, pv, PackArgb(255, 255, 255, 255));
			}
		}

		if (hasWater) {
			// Water v1: constant underwater tint band + above-deep-water darkening (shared with GX)
			pvr_poly_cxt_t cxt;
			pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
			cxt.gen.culling = PVR_CULLING_NONE;
			cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
			cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
			cxt.blend.src = PVR_BLEND_SRCALPHA;
			cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
			pvr_poly_hdr_t hdr;
			pvr_poly_compile(&hdr, &cxt);

			const float waterTop = vpY + light.WaterLevelPx * scaleY;
			const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (waterTop < vpY + vpH) {
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { waterTop, vpY + vpH, waterTop, vpY + vpH };
				SubmitQuad(hdr, px, py, uv, uv, PackArgb(102, 153, 204, 102));
			}
			const float waterLevelNorm = (light.VpH > 0 ? light.WaterLevelPx / float(light.VpH) : 1.0f);
			if (waterLevelNorm < 0.4f && waterTop > vpY) {
				const std::uint8_t a = QuantizeChannel(0.4f - waterLevelNorm);
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, waterTop, vpY, waterTop };
				SubmitQuad(hdr, px, py, uv, uv,
					PackArgb(QuantizeChannel(light.AmbR), QuantizeChannel(light.AmbG), QuantizeChannel(light.AmbB), a));
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void PvrDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		static_cast<void>(firstVertex);
		if (currentProgram_ == nullptr || numVertices <= 0 || !pvrInitialized_) {
			return;
		}

		const PvrEffect effect = currentProgram_->GetEffect();

		// The Combine draw is the direct-tier lighting hook (see the software backend)
		if (effect == PvrEffect::Combine) {
			ApplyPendingSoftwareLighting();
			return;
		}

		// v1 renders the procedural sprite-quad families only (see the GX device for the same policy)
		const bool isQuadFamily = (effect == PvrEffect::WhiteMask || effect == PvrEffect::BatchedWhiteMask ||
			effect == PvrEffect::PartialWhiteMask || effect == PvrEffect::BatchedPartialWhiteMask ||
			effect == PvrEffect::FrozenMask || effect == PvrEffect::BatchedFrozenMask ||
			effect == PvrEffect::Outline || effect == PvrEffect::BatchedOutline ||
			effect == PvrEffect::ShieldFire || effect == PvrEffect::BatchedShieldFire ||
			effect == PvrEffect::ShieldLightning || effect == PvrEffect::BatchedShieldLightning ||
			effect == PvrEffect::Transition ||
			effect == PvrEffect::DefaultSprite || effect == PvrEffect::DefaultBatchedSprites ||
			effect == PvrEffect::DefaultSpriteNoTexture || effect == PvrEffect::DefaultBatchedSpritesNoTexture ||
			effect == PvrEffect::Colorized || effect == PvrEffect::BatchedColorized ||
			effect == PvrEffect::PaletteRemap || effect == PvrEffect::BatchedPaletteRemap ||
			effect == PvrEffect::TexturedBackground || effect == PvrEffect::TexturedBackgroundCircle);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			if (!currentProgram_->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the PVR v1 dispatch", currentProgram_->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = currentProgram_->ResolveUniform("uProjectionMatrix");
		const std::uint8_t* viewBytes = currentProgram_->ResolveUniform("uViewMatrix");
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		const PvrUniformBlock* block = currentProgram_->FindBlock("InstanceBlock");
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

		const bool batched = (effect == PvrEffect::DefaultBatchedSprites || effect == PvrEffect::DefaultBatchedSpritesNoTexture ||
			effect == PvrEffect::BatchedPaletteRemap || effect == PvrEffect::BatchedColorized ||
			effect == PvrEffect::BatchedWhiteMask || effect == PvrEffect::BatchedPartialWhiteMask ||
			effect == PvrEffect::BatchedFrozenMask || effect == PvrEffect::BatchedOutline ||
			effect == PvrEffect::BatchedShieldFire || effect == PvrEffect::BatchedShieldLightning || instanceStride > 0);
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

		// The transition covers the screen with a flat colour, but its uniform block carries texRect (so the
		// sprite size sits at the textured offset) - the layout and the sampling are decided separately
		const bool hasTexture = (effect != PvrEffect::DefaultSpriteNoTexture && effect != PvrEffect::DefaultBatchedSpritesNoTexture &&
			effect != PvrEffect::Transition);
		const bool texturedLayout = (hasTexture || effect == PvrEffect::Transition);
		// Every effect that samples indexed sprites through the palette texture: PaletteRemap and the
		// "...Palette" variants of the actor state effects (reported by the program itself)
		const bool isPaletteRemap = (effect == PvrEffect::PaletteRemap || effect == PvrEffect::BatchedPaletteRemap ||
			currentProgram_->UsesPalette());

		// The actor state effects express their colour transform through the offset colour, which has to
		// be enabled on the polygon itself
		const bool usesOffsetColor = (effect == PvrEffect::WhiteMask || effect == PvrEffect::BatchedWhiteMask ||
			effect == PvrEffect::PartialWhiteMask || effect == PvrEffect::BatchedPartialWhiteMask ||
			effect == PvrEffect::FrozenMask || effect == PvrEffect::BatchedFrozenMask ||
			effect == PvrEffect::Outline || effect == PvrEffect::BatchedOutline ||
			effect == PvrEffect::ShieldFire || effect == PvrEffect::BatchedShieldFire ||
			effect == PvrEffect::ShieldLightning || effect == PvrEffect::BatchedShieldLightning);
		const std::int32_t textureUnit = samplerUnit("uTexture", 0);
		PvrTexture* texture = const_cast<PvrTexture*>(hasTexture
			? boundTextures_[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		if (hasTexture && texture == nullptr) {
			return;
		}

		// The palette to remap with is whatever the material bound to the palette sampler (e.g. the
		// recolored preview palettes of the profile menu); the registered global palette is the fallback
		const PvrTexture* paletteTex = nullptr;
		if (isPaletteRemap) {
			const std::int32_t paletteUnit = samplerUnit("uTexturePalette", 1);
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? boundTextures_[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = paletteTexture_;
			}
		}

		EnsureScene();
		if (sceneTarget_ == SceneTarget::None) {
			return;
		}

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = boundUniformRanges_[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		const Recti viewport = (viewport_.W > 0 && viewport_.H > 0)
			? viewport_ : Recti(0, 0, logicalWidth_, logicalHeight_);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);

		const std::int32_t blendSrc = (blending_.Enabled ? MapBlendPvr(blending_.SrcRgb) : PVR_BLEND_ONE);
		const std::int32_t blendDst = (blending_.Enabled ? MapBlendPvr(blending_.DstRgb) : PVR_BLEND_ZERO);
		const std::int32_t filter = (hasTexture && texture->GetMagFilter() == nCine::SamplerFilter::Linear
			? PVR_FILTER_BILINEAR : PVR_FILTER_NEAREST);

		pvr_poly_hdr_t hdr;
		bool hdrValid = false;
		pvr_ptr_t lastVram = nullptr;
		std::int32_t lastBank = -2;

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the PVR scans out its buffer top-down directly, so screen passes mirror NDC here
		// instead (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is
		// what the sampling passes already expect.
		const bool screenPass = (currentRenderTarget_ == nullptr);

		// The PVR rasterizer has no scissor for the general case, so scissored quads are clipped
		// geometrically. The rect maps to raster coordinates the same way the vertices do (screen passes
		// mirror NDC, so the engine rect's Y addresses raster rows directly - see the GX device); only
		// screen passes are clipped, which covers every scissor user on this tier (menu clipping,
		// splitscreen viewports)
		const bool clipActive = (scissor_.Enabled && screenPass);
		float clipX0 = 0.0f, clipY0 = 0.0f, clipX1 = 0.0f, clipY1 = 0.0f;
		if (clipActive) {
			clipX0 = float(scissor_.Rect.X) * scaleX + offsetX;
			clipY0 = float(scissor_.Rect.Y) * scaleY + offsetY;
			clipX1 = float(scissor_.Rect.X + scissor_.Rect.W) * scaleX + offsetX;
			clipY1 = float(scissor_.Rect.Y + scissor_.Rect.H) * scaleY + offsetY;
		}

		for (std::int32_t k = 0; k < numInstances; k++) {
			const std::uint8_t* inst = blockData + std::size_t(k) * (batched ? instanceStride : 0);

			float mvp[16];
			Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
			float color[4];
			std::memcpy(color, inst + kColorOffset, sizeof(color));
			float texRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			float spriteSize[2];
			if (texturedLayout) {
				std::memcpy(texRect, inst + kTexRectOffset, sizeof(texRect));
				std::memcpy(spriteSize, inst + kSpriteSizeOffset, sizeof(spriteSize));
			} else {
				std::memcpy(spriteSize, inst + kSpriteSizeNoTexOffset, sizeof(spriteSize));
			}

			// Select this instance's texture variant and (re)compile the poly header when it changes
			float uvScaleU = 1.0f, uvScaleV = 1.0f;
			if (hasTexture) {
				pvr_ptr_t vram = nullptr;
				std::uint32_t format = 0;
				std::int32_t bank = -1;
				if (isPaletteRemap && texture->IsIndexed()) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					bank = AcquirePaletteBankForRow(paletteTex, std::int32_t(palOffset + 0.5f));
					if (bank < 0) {
						bank = 0;
					}
					vram = texture->AcquireVramPointer();
					format = texture->GetVramFormat() | PVR_TXRFMT_8BPP_PAL(std::uint32_t(bank));
				} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
					const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
						paletteTex->GetPixels()) + paletteOffset;
					vram = texture->EnsureBakedArgb4444(entries, paletteOffset,
						(paletteTex == paletteTexture_ ? paletteGeneration_ : paletteTex->GetContentVersion()), paletteTex);
					format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
				} else {
					vram = texture->AcquireVramPointer();
					format = texture->GetVramFormat();
				}
				if (vram == nullptr) {
					continue;
				}
				uvScaleU = texture->GetUScale();
				uvScaleV = texture->GetVScale();
				if (!hdrValid || vram != lastVram || bank != lastBank) {
					pvr_poly_cxt_t cxt;
					pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, int(format),
						texture->GetPaddedWidth(), texture->GetPaddedHeight(), vram, pvr_filter_mode_t(filter));
					cxt.gen.culling = PVR_CULLING_NONE;
					cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
					cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
					cxt.blend.src = pvr_blend_mode_t(blendSrc);
					cxt.blend.dst = pvr_blend_mode_t(blendDst);
					cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
					// The offset colour is added to the texturing result, which is how the actor state
					// effects brighten and tint the sprite (see the effect handling below)
					if (usesOffsetColor) {
						cxt.gen.specular = PVR_SPECULAR_ENABLE;
					}
					pvr_poly_compile(&hdr, &cxt);
					hdrValid = true;
					lastVram = vram;
					lastBank = bank;
				}
			} else if (!hdrValid) {
				pvr_poly_cxt_t cxt;
				pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
				cxt.gen.culling = PVR_CULLING_NONE;
				cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
				cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
				cxt.blend.src = pvr_blend_mode_t(blendSrc);
				cxt.blend.dst = pvr_blend_mode_t(blendDst);
				pvr_poly_compile(&hdr, &cxt);
				hdrValid = true;
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, then scale the
			// logical pixels to the display (or 1:1 for a render-to-texture pass)
			float px[4], py[4], pu[4], pvv[4];
			for (std::int32_t i = 0; i < 4; i++) {
				const float ax = ((i & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (i & 1) ? 1.0f : 0.0f;
				const float wx = ax * spriteSize[0];
				const float wy = ay * spriteSize[1];
				const float ndcX = mvp[0] * wx + mvp[4] * wy + mvp[12];
				const float ndcY = mvp[1] * wx + mvp[5] * wy + mvp[13];
				px[i] = ((ndcX + 1.0f) * 0.5f * float(viewport.W) + float(viewport.X)) * scaleX + offsetX;
				py[i] = ((screenPass ? (ndcY + 1.0f) : (1.0f - ndcY)) * 0.5f * float(viewport.H) + float(viewport.Y)) * scaleY + offsetY;
				pu[i] = (ax * texRect[0] + texRect[1]) * uvScaleU;
				pvv[i] = (ay * texRect[2] + texRect[3]) * uvScaleV;
			}

			if (clipActive) {
				// Corners 2/3 share the left edge and 0/1 the right one (ax); 0/2 share the top edge and
				// 1/3 the bottom one (ay) - see the corner synthesis above
				const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
				if (axisAligned) {
					float xA = px[2], xB = px[0], uA = pu[2], uB = pu[0];
					if (!ClipQuadEdge(xA, xB, uA, uB, clipX0, clipX1)) {
						continue;
					}
					px[2] = px[3] = xA; px[0] = px[1] = xB;
					pu[2] = pu[3] = uA; pu[0] = pu[1] = uB;
					float yA = py[0], yB = py[1], vA = pvv[0], vB = pvv[1];
					if (!ClipQuadEdge(yA, yB, vA, vB, clipY0, clipY1)) {
						continue;
					}
					py[0] = py[2] = yA; py[1] = py[3] = yB;
					pvv[0] = pvv[2] = vA; pvv[1] = pvv[3] = vB;
				} else {
					// Rotated quad: conservative bounding-box reject only (exact clipping of rotated
					// sprites is not worth it for the scissor users on this tier)
					const float minX = std::min(std::min(px[0], px[1]), std::min(px[2], px[3]));
					const float maxX = std::max(std::max(px[0], px[1]), std::max(px[2], px[3]));
					const float minY = std::min(std::min(py[0], py[1]), std::min(py[2], py[3]));
					const float maxY = std::max(std::max(py[0], py[1]), std::max(py[2], py[3]));
					if (maxX <= clipX0 || minX >= clipX1 || maxY <= clipY0 || minY >= clipY1) {
						continue;
					}
				}
			}

			if (effect == PvrEffect::Colorized || effect == PvrEffect::BatchedColorized) {
				// Amplified dye of the Colorized shader (the grayscale step is dropped, but the affected
				// textures - mostly font glyphs - are grayscale already)
				for (std::int32_t c = 0; c < 4; c++) {
					color[c] = 1.0f + (color[c] - 0.5f) * 4.0f;
				}
			}

			switch (effect) {
				case PvrEffect::WhiteMask:
				case PvrEffect::BatchedWhiteMask: {
					// The shader saturates the luma (x6), which the offset colour reproduces by adding
					// white on top of the sampled sprite; the alpha still comes from the texture
					const std::uint32_t argb = PackArgb(0, 0, 0, QuantizeChannel(color[3]));
					const std::uint32_t oargb = PackArgb(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
						QuantizeChannel(color[2]), 0);
					SubmitQuad(hdr, px, py, pu, pvv, argb, oargb);
					break;
				}
				case PvrEffect::PartialWhiteMask:
				case PvrEffect::BatchedPartialWhiteMask: {
					// Brightened but still shaded (the shader's luma x2.5): keep the sprite and lift it
					const std::uint32_t argb = PackArgb(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
						QuantizeChannel(color[2]), QuantizeChannel(color[3]));
					const std::uint32_t oargb = PackArgb(96, 96, 96, 0);
					SubmitQuad(hdr, px, py, pu, pvv, argb, oargb);
					break;
				}
				case PvrEffect::FrozenMask:
				case PvrEffect::BatchedFrozenMask: {
					// color = (1/texWidth, 1/texHeight, unused, transition). Scaling the sprite down by
					// the transition and adding the ice colour scaled by it is exactly the shader's mix
					const float t = (color[3] < 0.0f ? 0.0f : (color[3] > 1.0f ? 1.0f : color[3]));
					const float keep = 1.0f - t;
					const std::uint32_t argb = PackArgb(QuantizeChannel(keep), QuantizeChannel(keep),
						QuantizeChannel(keep), 255);
					const std::uint32_t oargb = PackArgb(QuantizeChannel(0.2f * t), QuantizeChannel(0.82f * t),
						QuantizeChannel(0.8f * t), 0);
					SubmitQuad(hdr, px, py, pu, pvv, argb, oargb);
					break;
				}
				case PvrEffect::Outline:
				case PvrEffect::BatchedOutline: {
					// color = (1/texWidth, 1/texHeight, outline grey, alpha). The shader finds the border
					// by summing eight neighbour taps, which is drawn here instead as eight silhouettes
					// offset by one texel (a black sprite lifted to the outline colour), covered by the
					// sprite itself. (The shader's dimmer second ring at two texels is dropped.)
					const float alpha = color[3];
					if (alpha > 0.0f && texRect[0] != 0.0f && texRect[2] != 0.0f) {
						// One texel maps to this fraction of the quad's on-screen extent (the padding scale
						// applies to both the texel size and the quad's span, so it cancels out)
						const float dx = (px[0] - px[2]) * (color[0] / texRect[0]);
						const float dy = (py[1] - py[0]) * (color[1] / texRect[2]);
						const std::uint32_t oargb = PackArgb(QuantizeChannel(color[2]), QuantizeChannel(color[2]),
							QuantizeChannel(color[2]), 0);
						const std::uint32_t argb = PackArgb(0, 0, 0, QuantizeChannel(alpha));
						for (std::int32_t oy = -1; oy <= 1; oy++) {
							for (std::int32_t ox = -1; ox <= 1; ox++) {
								if (ox != 0 || oy != 0) {
									SubmitQuad(hdr, px, py, pu, pvv, argb, oargb, dx * ox, dy * oy);
								}
							}
						}
					}
					SubmitQuad(hdr, px, py, pu, pvv, PackArgb(255, 255, 255, 255));
					break;
				}
				case PvrEffect::Transition: {
					// The GLSL wipe clears a growing circle out of a black screen; flattened to a plain
					// fade whose opacity tracks the same progress (fully clear once the circle covers the
					// furthest corner, fully black at zero)
					const float progress = color[3] / 0.927f;
					const float alpha = (progress < 0.0f ? 1.0f : (progress > 1.0f ? 0.0f : 1.0f - progress));
					if (alpha > 0.0f) {
						SubmitQuad(hdr, px, py, pu, pvv, PackArgb(0, 0, 0, QuantizeChannel(alpha)));
					}
					break;
				}
				case PvrEffect::ShieldFire:
				case PvrEffect::BatchedShieldFire:
				case PvrEffect::ShieldLightning:
				case PvrEffect::BatchedShieldLightning: {
					// color = (scaleX, scaleY, darkness, alpha). The shader's animated noise sphere is out
					// of reach here, so the shield becomes a flat glow in its own colour
					const bool fire = (effect == PvrEffect::ShieldFire || effect == PvrEffect::BatchedShieldFire);
					const float darkness = color[2];
					const std::uint32_t argb = PackArgb(0, 0, 0, QuantizeChannel(color[3] * 0.5f));
					const std::uint32_t oargb = (fire
						? PackArgb(QuantizeChannel(darkness), QuantizeChannel(darkness * 0.45f), QuantizeChannel(darkness * 0.1f), 0)
						: PackArgb(QuantizeChannel(darkness * 0.6f), QuantizeChannel(darkness * 0.8f), QuantizeChannel(darkness), 0));
					SubmitQuad(hdr, px, py, pu, pvv, argb, oargb);
					break;
				}
				default: {
					const std::uint32_t argb = PackArgb(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
						QuantizeChannel(color[2]), QuantizeChannel(color[3]));
					SubmitQuad(hdr, px, py, pu, pvv, argb);
					break;
				}
			}
		}
	}
}
