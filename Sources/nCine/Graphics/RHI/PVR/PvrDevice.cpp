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

#include <dc/sq.h>

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

		// Both draw paths only ever transform points of the form (x, y, 0, 1), so just six of the sixteen
		// products of projection*view*model are ever read back. Sprites pay this per instance, which made
		// the full 4x4 multiply the most expensive step of the per-instance loop.
		struct Transform2D
		{
			float Xx, Xy;	// Column 0, rows 0 and 1
			float Yx, Yy;	// Column 1, rows 0 and 1
			float Tx, Ty;	// Column 3, rows 0 and 1
		};

		void Mat4MulTransform2D(const float* DEATH_RESTRICT pv, const float* DEATH_RESTRICT model, Transform2D& out)
		{
			out.Xx = pv[0] * model[0] + pv[4] * model[1] + pv[8] * model[2] + pv[12] * model[3];
			out.Xy = pv[1] * model[0] + pv[5] * model[1] + pv[9] * model[2] + pv[13] * model[3];
			out.Yx = pv[0] * model[4] + pv[4] * model[5] + pv[8] * model[6] + pv[12] * model[7];
			out.Yy = pv[1] * model[4] + pv[5] * model[5] + pv[9] * model[6] + pv[13] * model[7];
			out.Tx = pv[0] * model[12] + pv[4] * model[13] + pv[8] * model[14] + pv[12] * model[15];
			out.Ty = pv[1] * model[12] + pv[5] * model[13] + pv[9] * model[14] + pv[13] * model[15];
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

		// Straight to the 4 bits an ARGB4444 channel actually keeps, skipping the round trip through 8 bits
		inline std::uint32_t Quantize4Bit(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint32_t(v * 15.0f + 0.5f);
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

		// The tile accelerator is a state machine: a polygon header stays in effect for every strip that
		// follows it within the open list, so a header identical to the last submitted one does not have
		// to go out again - it is 32 of the 160 bytes of a typical quad, and batches reuse one header for
		// hundreds of primitives. Cleared whenever a new list opens (see InvalidateSubmittedHeader()).
		std::uint32_t lastHeaderWords[8];
		bool lastHeaderValid = false;

		void InvalidateSubmittedHeader()
		{
			lastHeaderValid = false;
		}

		// Writes the header into the store queues only when it differs from the last submitted one, and
		// returns the queue pointer for the vertices that follow
		DEATH_ALWAYS_INLINE std::uint32_t* SubmitHeaderIfChanged(const pvr_poly_hdr_t& hdr)
		{
			std::uint32_t* DEATH_RESTRICT sq = SQ_MASK_DEST(PVR_TA_INPUT);
			const std::uint32_t* DEATH_RESTRICT header = reinterpret_cast<const std::uint32_t*>(&hdr);
			if (lastHeaderValid &&
					lastHeaderWords[0] == header[0] && lastHeaderWords[1] == header[1] &&
					lastHeaderWords[2] == header[2] && lastHeaderWords[3] == header[3] &&
					lastHeaderWords[4] == header[4] && lastHeaderWords[5] == header[5] &&
					lastHeaderWords[6] == header[6] && lastHeaderWords[7] == header[7]) {
				return sq;
			}
			sq[0] = lastHeaderWords[0] = header[0]; sq[1] = lastHeaderWords[1] = header[1];
			sq[2] = lastHeaderWords[2] = header[2]; sq[3] = lastHeaderWords[3] = header[3];
			sq[4] = lastHeaderWords[4] = header[4]; sq[5] = lastHeaderWords[5] = header[5];
			sq[6] = lastHeaderWords[6] = header[6]; sq[7] = lastHeaderWords[7] = header[7];
			lastHeaderValid = true;
			sq_flush(sq);
			return sq + 8;
		}

		// Submits one strip of `count` vertices (3 = a single triangle, 4 = a quad) under the given header
		// to the open translucent list. The corner order matches the procedural sprite strip (v0, v1, v2,
		// v3) exactly like the software FetchVertex synthesizes it. The offset colour is added after
		// texturing (only when the polygon enables it), which is how the actor state effects brighten or
		// tint the sprite - see the effect handling in Dispatch.
		//
		// The primitives are written straight into the store queues rather than assembled in main memory
		// and handed to pvr_prim(): that path copies every 32-byte primitive a second time on its way out,
		// which at four vertices per sprite and per tile was a large part of the submission cost. The queue
		// pointer advances a block at a time exactly as sq_cpy() does, which alternates the two hardware
		// banks so a bank is never rewritten while its write-back is still in flight. The PVR driver has
		// already pointed the queues at the TA FIFO (pvr_prim() itself relies on that), so no lock is taken.
		void SubmitStrip(const pvr_poly_hdr_t& hdr, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, std::uint32_t argb, std::uint32_t oargb = 0, float dx = 0.0f, float dy = 0.0f)
		{
			static_assert(sizeof(pvr_vertex_t) == 32 && sizeof(pvr_poly_hdr_t) == 32,
				"The store queues submit whole 32 byte blocks");

			std::uint32_t* DEATH_RESTRICT sq = SubmitHeaderIfChanged(hdr);

			for (std::int32_t i = 0; i < count; i++) {
				sq[0] = (i == count - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
				reinterpret_cast<float*>(sq)[1] = px[i] + dx;
				reinterpret_cast<float*>(sq)[2] = py[i] + dy;
				reinterpret_cast<float*>(sq)[3] = 1.0f;
				reinterpret_cast<float*>(sq)[4] = pu[i];
				reinterpret_cast<float*>(sq)[5] = pv[i];
				sq[6] = argb;
				sq[7] = oargb;
				sq_flush(sq);
				sq += 8;
			}
		}

		void SubmitQuad(const pvr_poly_hdr_t& hdr, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t argb, std::uint32_t oargb = 0, float dx = 0.0f, float dy = 0.0f)
		{
			SubmitStrip(hdr, px, py, pu, pv, 4, argb, oargb, dx, dy);
		}

		// As SubmitStrip(), but each vertex carries its own colour so the rasterizer interpolates it across
		// the primitive - which is how a gradient is expressed without a fragment shader
		void SubmitStripShaded(const pvr_poly_hdr_t& hdr, const float* px, const float* py, std::int32_t count,
			const std::uint32_t* argb)
		{
			std::uint32_t* DEATH_RESTRICT sq = SubmitHeaderIfChanged(hdr);

			for (std::int32_t i = 0; i < count; i++) {
				sq[0] = (i == count - 1 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
				reinterpret_cast<float*>(sq)[1] = px[i];
				reinterpret_cast<float*>(sq)[2] = py[i];
				reinterpret_cast<float*>(sq)[3] = 1.0f;
				reinterpret_cast<float*>(sq)[4] = 0.0f;
				reinterpret_cast<float*>(sq)[5] = 0.0f;
				sq[6] = argb[i];
				sq[7] = 0;
				sq_flush(sq);
				sq += 8;
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
			// Render-to-texture scene into the target's RGB565 surface. Deliberately not through
			// pvr_scene_begin_txr(): that wrapper is deprecated, and it forwards the *screen* dimensions as
			// the render size while using the width it is given only as the stride. For any target narrower
			// than the display that trips the "stride < width" check inside pvr_scene_begin_rtt(), which
			// then returns without starting a scene at all - so nothing was ever rendered and the target
			// kept whatever its memory held.
			const std::uint32_t renderWidth = std::uint32_t(texture->GetPaddedWidth());
			const std::uint32_t renderHeight = std::uint32_t(texture->GetPaddedHeight());
			if (pvr_scene_begin_rtt(texture->GetVramPointer(), renderWidth, renderHeight, renderWidth) < 0) {
				LOGE("Cannot start a render-to-texture scene for a {}x{} target", renderWidth, renderHeight);
				return;
			}
			sceneRenderTarget_ = currentRenderTarget_;
		} else {
			pvr_scene_begin();
			sceneRenderTarget_ = nullptr;
		}
		pvr_list_begin(PVR_LIST_TR_POLY);
		// A new list starts with no polygon-header state in the tile accelerator
		InvalidateSubmittedHeader();
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
			InvalidateSubmittedHeader();
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
		if (currentRenderTarget_ == nullptr && sceneTarget_ == SceneTarget::None) {
			// The first clear of a screen frame is provided for free by the scene background plane -
			// pushing ~300k blended pixels through the translucent pipe for it again would be one of
			// the most expensive draws of the whole frame. Only mid-scene clears (and render targets,
			// which have no reliable background plane) paint the quad below.
			pvr_set_bg_color(clearColor_.R, clearColor_.G, clearColor_.B);
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

		return AcquirePaletteBank(palette, paletteOffset, palette->GetContentVersion(),
			reinterpret_cast<const std::uint32_t*>(palette->GetPixels()) + paletteOffset);
	}

	std::int32_t PvrDevice::AcquirePaletteBank(const PvrTexture* palette, std::int32_t paletteOffset,
		std::uint32_t version, const std::uint32_t* entries)
	{
		if (palette == nullptr || entries == nullptr) {
			return -1;
		}

		paletteUseCounter_++;

		std::int32_t bank = -1;
		std::uint32_t oldestUse = UINT32_MAX;
		std::int32_t oldestBank = 0;
		for (std::uint32_t i = 0; i < MaxPaletteBanks; i++) {
			if (paletteBanks_[i].PaletteOffset == paletteOffset && paletteBanks_[i].Palette == palette &&
				paletteBanks_[i].PaletteVersion == version) {
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
			for (std::int32_t i = 0; i < 256; i++) {
				const std::uint32_t rgba = entries[i];
				pvr_set_pal_entry(std::uint32_t(bank) * 256 + std::uint32_t(i),
					PackArgb(std::uint8_t(rgba & 0xFF), std::uint8_t((rgba >> 8) & 0xFF),
						std::uint8_t((rgba >> 16) & 0xFF), std::uint8_t((rgba >> 24) & 0xFF)));
			}
			paletteBanks_[bank].PaletteOffset = paletteOffset;
			paletteBanks_[bank].Palette = palette;
			paletteBanks_[bank].PaletteVersion = version;
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
			bool layoutChanged = (lightmapW_ != texW || lightmapH_ != texH);
			if (lightmapVram_ == nullptr || lightmapVramSize_ < size) {
				if (lightmapVram_ != nullptr) {
					pvr_mem_free(lightmapVram_);
				}
				lightmapVram_ = pvr_mem_malloc(size);
				lightmapVramSize_ = size;
				layoutChanged = true;
			}
			if (lightmapVram_ != nullptr) {
				lightmapW_ = texW;
				lightmapH_ = texH;
				// The factors are written straight into video memory as a non-twiddled surface. This is a
				// single screen-aligned quad, so the interleaved texel order would buy nothing at sampling
				// time while costing a full twiddling pass (plus a same-sized staging copy) every frame -
				// the same trade-off the sprite uploads make in PvrTexture::RefreshVramStore().
				std::uint16_t* const surface = static_cast<std::uint16_t*>(lightmapVram_);
				if (layoutChanged) {
					// Only the used LmW x LmH region is rewritten per frame; the padding is sampled through
					// the compensated texture coordinates only at the very edge, and is filled just once.
					// Spelled out as word stores - video memory only takes 16/32-bit accesses, and libc
					// memset does not guarantee that (the size is always a multiple of four here)
					std::uint32_t* DEATH_RESTRICT fill = reinterpret_cast<std::uint32_t*>(surface);
					for (std::size_t i = 0, n = size / 4; i < n; i++) {
						fill[i] = 0xFFFFFFFFu;
					}
				}
				for (std::int32_t y = 0; y < light.LmH; y++) {
					const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(y) * light.LmW * 2;
					std::uint16_t* DEATH_RESTRICT dst = surface + std::size_t(y) * texW;
					// Unlit runs repeat the same pair of factors across long spans, so remembering the last
					// converted texel turns most of the surface into a compare and a store
					float prevR = -1.0f, prevG = -1.0f;
					std::uint16_t prevTexel = 0;
					for (std::int32_t x = 0; x < light.LmW; x++) {
						const float rawR = src[x * 2];
						const float rawG = src[x * 2 + 1];
						if (rawR == prevR && rawG == prevG) {
							dst[x] = prevTexel;
							continue;
						}
						prevR = rawR;
						prevG = rawG;
						const float r = (rawR < 0.0f ? 0.0f : (rawR > 1.0f ? 1.0f : rawR));
						const float g = (rawG < 0.0f ? 0.0f : (rawG > 1.0f ? 1.0f : rawG));
						const float lit = r * (1.0f + g);
						const float inv = 1.0f - r;
						const std::uint32_t fr = Quantize4Bit(lit + light.AmbR * inv);
						const std::uint32_t fg = Quantize4Bit(lit + light.AmbG * inv);
						const std::uint32_t fb = Quantize4Bit(lit + light.AmbB * inv);
						prevTexel = std::uint16_t(0xF000 | (fr << 8) | (fg << 4) | fb);
						dst[x] = prevTexel;
					}
				}

				pvr_poly_cxt_t cxt;
				pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
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

	void PvrDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const PvrBuffer* vbo = currentProgram_->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		const std::size_t firstFloat = (std::size_t(currentProgram_->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		const std::size_t floatCount = std::size_t(numVertices) * FloatsPerVertex;
		if ((firstFloat + floatCount) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;

		const PvrUniformBlock* block = currentProgram_->FindBlock("InstanceBlock");
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

		PvrTexture* texture = const_cast<PvrTexture*>(boundTextures_[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = currentProgram_->GetResolvedProjection();
		const std::uint8_t* viewBytes = currentProgram_->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		float pv[16];
		Mat4Mul(projMat, viewMat, pv);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path)
		const bool isPaletteRemap = (currentProgram_->GetEffect() == PvrEffect::TileMapMeshPalette ||
			currentProgram_->UsesPalette());
		const PvrTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed()) {
			paletteTex = boundTextures_[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = paletteTexture_;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the texture
		// residency, the palette bank and the polygon header are resolved once for the entire layer
		pvr_ptr_t vram = nullptr;
		std::uint32_t format = 0;
		if (texture->IsIndexed()) {
			// An 8bpp store can only be read through a palette, whatever it is being drawn with - the lookup
			// belongs to the texture read rather than to the effect. An effect that remaps takes the row from
			// the instance; anything else (the fonts, which are palette indices too) uses the base row.
			std::int32_t paletteOffset = 0;
			if (isPaletteRemap) {
				float palOffset = 0.0f;
				std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
				paletteOffset = std::int32_t(palOffset + 0.5f);
			}
			std::int32_t bank = AcquirePaletteBankForRow(paletteTex, paletteOffset);
			if (bank < 0) {
				bank = 0;
			}
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat() | PVR_TXRFMT_8BPP_PAL(std::uint32_t(bank));
		} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels()) + paletteOffset;
			vram = texture->EnsureBakedArgb4444(entries, paletteOffset,
				(paletteTex == paletteTexture_ ? paletteGeneration_ : paletteTex->GetContentVersion()), paletteTex);
			format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
		} else {
			vram = texture->AcquireVramPointer();
			format = texture->GetVramFormat();
		}
		if (vram == nullptr) {
			return;
		}

		EnsureScene();
		if (sceneTarget_ == SceneTarget::None) {
			return;
		}

		const Recti viewport = (viewport_.W > 0 && viewport_.H > 0)
			? viewport_ : Recti(0, 0, logicalWidth_, logicalHeight_);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const bool screenPass = (currentRenderTarget_ == nullptr);
		const float uvScaleU = texture->GetUScale();
		const float uvScaleV = texture->GetVScale();

		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, int(format), texture->GetPaddedWidth(), texture->GetPaddedHeight(),
			vram, (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? PVR_FILTER_BILINEAR : PVR_FILTER_NEAREST));
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		cxt.blend.src = (blending_.Enabled ? pvr_blend_mode_t(MapBlendPvr(blending_.SrcRgb)) : PVR_BLEND_ONE);
		cxt.blend.dst = (blending_.Enabled ? pvr_blend_mode_t(MapBlendPvr(blending_.DstRgb)) : PVR_BLEND_ZERO);
		cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
		pvr_poly_hdr_t hdr;
		pvr_poly_compile(&hdr, &cxt);

		const bool clipActive = (scissor_.Enabled && screenPass);
		float clipX0 = 0.0f, clipY0 = 0.0f, clipX1 = 0.0f, clipY1 = 0.0f;
		if (clipActive) {
			clipX0 = float(scissor_.Rect.X) * scaleX + offsetX;
			clipY0 = float(scissor_.Rect.Y) * scaleY + offsetY;
			clipX1 = float(scissor_.Rect.X + scissor_.Rect.W) * scaleX + offsetX;
			clipY1 = float(scissor_.Rect.Y + scissor_.Rect.H) * scaleY + offsetY;
		}

		// Projects one mesh vertex into raster space, matching the sprite path's corner synthesis
		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex - every vertex then costs one multiply-add
		// per axis. A screen pass mirrors NDC, which is just the sign of the Y scale (see below).
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU;
			outV = v[3] * uvScaleV;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the
		// four clamp+float-to-int quantizations run once per change instead of once per tile
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		std::uint32_t lastArgb = 0;
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the third and fourth repeat
			// the first and third. Recognizing that pattern lets a tile go out as a single four-vertex
			// strip rather than two three-vertex ones, which is a third less vertex traffic and half the
			// polygon headers. Anything that doesn't match is emitted as plain triangles.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			float px[4], py[4], pu[4], pvv[4];
			std::int32_t cornerCount;
			if (isQuad) {
				// Strip order (see SubmitQuad): the two corners of one edge, then the two of the opposite
				// one - vertices 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(group + std::size_t(QuadOrder[i]) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				cornerCount = 4;
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				cornerCount = 3;
				triangle++;
			}

			if (clipActive) {
				// The bounding-box reject is exact for the fully outside case
				float minX = px[0], maxX = px[0], minY = py[0], maxY = py[0];
				for (std::int32_t i = 1; i < cornerCount; i++) {
					minX = std::min(minX, px[i]); maxX = std::max(maxX, px[i]);
					minY = std::min(minY, py[i]); maxY = std::max(maxY, py[i]);
				}
				if (maxX <= clipX0 || minX >= clipX1 || maxY <= clipY0 || minY >= clipY1) {
					continue;
				}
				// A tile straddling the scissor edge is clipped exactly like the sprite path clips its
				// axis-aligned quads: there is no hardware scissor on this tier, and on the splitscreen
				// boundary an unclipped tile would draw up to a full tile into the other player's viewport.
				// The corner-sharing test mirrors the sprite path; anything else (the raw-triangle fallback,
				// a rotated layer) keeps the conservative bounding-box reject above.
				if (cornerCount == 4 && px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]) {
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
				}
			}

			// Every vertex of a tile carries the same colour, so it only has to be packed once per change
			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				lastArgb = PackArgb(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}
			SubmitStrip(hdr, px, py, pu, pvv, cornerCount, lastArgb);
		}
	}

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

		// A whole tile layer arrives as one mesh instead of one command per tile
		if (effect == PvrEffect::TileMapMesh || effect == PvrEffect::TileMapMeshPalette) {
			DispatchTileMesh(primitive, firstVertex, numVertices);
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

		const std::uint8_t* projBytes = currentProgram_->GetResolvedProjection();
		const std::uint8_t* viewBytes = currentProgram_->GetResolvedView();
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
		if (isPaletteRemap || (texture != nullptr && texture->IsIndexed())) {
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
		// An additive twin of the same polygon, for effects that build their result out of several passes
		const bool needsAdditivePass = (effect == PvrEffect::Colorized || effect == PvrEffect::BatchedColorized);
		pvr_poly_hdr_t hdrAdditive;
		bool hdrAdditiveValid = false;

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the PVR scans out its buffer top-down directly, so screen passes mirror NDC here
		// instead (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is
		// what the sampling passes already expect - which is just the sign of the raster Y scale below.
		const bool screenPass = (currentRenderTarget_ == nullptr);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;

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

			Transform2D mvp;
			Mat4MulTransform2D(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
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
				if (texture->IsIndexed()) {
					// See the mesh path: a paletted store needs a bank selected under every effect
					std::int32_t paletteOffset = 0;
					if (isPaletteRemap) {
						float palOffset = 0.0f;
						std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
						paletteOffset = std::int32_t(palOffset + 0.5f);
					}
					bank = AcquirePaletteBankForRow(paletteTex, paletteOffset);
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
					if (needsAdditivePass) {
						cxt.blend.src = PVR_BLEND_SRCALPHA;
						cxt.blend.dst = PVR_BLEND_ONE;
						pvr_poly_compile(&hdrAdditive, &cxt);
						hdrAdditiveValid = true;
					}
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

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform (see DispatchTileMesh) so a corner costs one
			// multiply-add per axis. The corner weights are 0 or 1, so the sprite's extent in raster space
			// is just the transformed axes scaled by its size, and the corners are sums of those.
			const float originX = mvp.Tx * rasterScaleX + rasterBiasX;
			const float originY = mvp.Ty * rasterScaleY + rasterBiasY;
			const float spanXx = mvp.Xx * rasterScaleX * spriteSize[0];
			const float spanXy = mvp.Xy * rasterScaleY * spriteSize[0];
			const float spanYx = mvp.Yx * rasterScaleX * spriteSize[1];
			const float spanYy = mvp.Yy * rasterScaleY * spriteSize[1];
			float px[4], py[4], pu[4], pvv[4];
			for (std::int32_t i = 0; i < 4; i++) {
				const float ax = ((i & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (i & 1) ? 1.0f : 0.0f;
				px[i] = originX + ax * spanXx + ay * spanYx;
				py[i] = originY + ax * spanXy + ay * spanYy;
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
					// The GLSL effect is a circular iris: black everywhere outside a circle of radius
					// `progress`, clear inside it, with a soft 0.22-wide edge in between (see
					// Transition.shader, whose iso-distance curves are circles of radius
					// progress * max(spriteSize) about the sprite centre).
					//
					// Without a fragment shader the same shape is built out of geometry instead: a fan of
					// segments approximating the soft edge, with the alpha interpolated across it by the
					// vertex colours, then a second fan filling everything from there out past the corners.
					// The centre and extent come from the transformed sprite axes rather than the corner
					// array, so scissor clipping of the quad cannot distort the circle.
					constexpr std::int32_t Segments = 32;
					constexpr float EdgeWidth = 0.22f;
					constexpr float TwoPi = 6.28318530718f;

					const float centreX = originX + 0.5f * (spanXx + spanYx);
					const float centreY = originY + 0.5f * (spanXy + spanYy);
					const float extentX = std::abs(spanXx) + std::abs(spanYx);
					const float extentY = std::abs(spanXy) + std::abs(spanYy);
					const float radiusScale = (extentX > extentY ? extentX : extentY);
					// Far enough that the polygon's flat edges still cover the corners
					const float corner = 0.5f * std::sqrt(extentX * extentX + extentY * extentY) * 1.05f;

					const float progress = color[3];
					const float outer = progress * radiusScale;
					const float innerRaw = (progress - EdgeWidth) * radiusScale;
					const float inner = (innerRaw > 0.0f ? innerRaw : 0.0f);
					if (outer >= corner) {
						break;		// The iris has swallowed the whole screen, nothing left to darken
					}

					const std::uint32_t opaque = PackArgb(0, 0, 0, 255);
					const std::uint32_t clear = PackArgb(0, 0, 0, 0);
					for (std::int32_t i = 0; i < Segments; i++) {
						const float a0 = float(i) * (TwoPi / float(Segments));
						const float a1 = float(i + 1) * (TwoPi / float(Segments));
						const float c0 = std::cos(a0), s0 = std::sin(a0);
						const float c1 = std::cos(a1), s1 = std::sin(a1);

						// Soft edge: transparent at the inner radius, fully black at the outer one
						if (outer > inner) {
							const float rx[4] = { centreX + inner * c0, centreX + outer * c0,
								centreX + inner * c1, centreX + outer * c1 };
							const float ry[4] = { centreY + inner * s0, centreY + outer * s0,
								centreY + inner * s1, centreY + outer * s1 };
							const std::uint32_t shade[4] = { clear, opaque, clear, opaque };
							SubmitStripShaded(hdr, rx, ry, 4, shade);
						}
						// Solid black from the edge out past the corners
						const float fx[4] = { centreX + outer * c0, centreX + corner * c0,
							centreX + outer * c1, centreX + corner * c1 };
						const float fy[4] = { centreY + outer * s0, centreY + corner * s0,
							centreY + outer * s1, centreY + corner * s1 };
						const std::uint32_t solid[4] = { opaque, opaque, opaque, opaque };
						SubmitStripShaded(hdr, fx, fy, 4, solid);
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
				case PvrEffect::TexturedBackgroundCircle:
				case PvrEffect::TexturedBackground: {
					// The warped background of TexturedBackground.shader, rebuilt out of geometry.
					//
					// Its mapping is affine along every screen row - texturePos.x is linear in UV.x for a fixed
					// row and texturePos.y depends on UV.y alone - so a stack of horizontal bands carrying
					// interpolated texture coordinates reproduces it. The horizon tint is likewise a per-row
					// value, so it goes on as a second pass whose vertex alpha the rasterizer interpolates down
					// the band.
					//
					// The shader relies on the texture repeating: the warp spans roughly two periods across the
					// screen. The source here is a render target, which the tile accelerator writes as a linear
					// surface that the hardware cannot tile, so instead each band is cut at every whole-texture
					// boundary and the pieces are emitted with coordinates inside a single period. A cut follows
					// the line where the coordinate crosses the boundary, which is slanted (the row's span
					// widens toward the horizon) - so the pieces are trapezoids rather than columns, and their
					// corners land exactly on the boundary. That is exact, not an approximation: the shader's
					// mapping is affine across each row and so is the interpolation across each trapezoid.
					//
					// The two curve approximations are the shader's own SOFTWARE_RENDERER branch (distance for
					// pow(distance, 1.4), distance squared for pow(distance, 1.5)), which is what every tier
					// without real shaders already draws; it drops the per-pixel star field along with them.
					constexpr std::int32_t BandsPerHalf = 16;
					constexpr std::int32_t MaxHorizontalPieces = 8;

					const std::uint8_t* viewSizeBytes = currentProgram_->ResolveUniform("uViewSize");
					const std::uint8_t* shiftBytes = currentProgram_->ResolveUniform("uShift");
					const std::uint8_t* horizonBytes = currentProgram_->ResolveUniform("uHorizonColor");
					if (viewSizeBytes == nullptr || shiftBytes == nullptr || horizonBytes == nullptr) {
						SubmitQuad(hdr, px, py, pu, pvv, PackArgb(255, 255, 255, 255));
						break;
					}
					float viewSize[2], shift[2], horizon[4];
					std::memcpy(viewSize, viewSizeBytes, sizeof(viewSize));
					std::memcpy(shift, shiftBytes, sizeof(shift));
					std::memcpy(horizon, horizonBytes, sizeof(horizon));

					const float correction = (viewSize[1] > 0.0f ? (viewSize[0] * 9.0f) / (viewSize[1] * 16.0f) : 1.0f);
					const float shiftU = shift[0] / 256.0f;
					const float shiftV = shift[1] / 256.0f;
					const float leftX = originX;
					const float spanX = spanXx;

					// Row geometry of the effect, straight from the shader
					auto distanceAt = [](float t) {
						return 1.3f - std::abs(2.0f * t - 1.0f);
					};
					auto halfSpanAt = [&](float t) {
						return 0.5f * (0.5f + 1.5f * distanceAt(t)) * correction;
					};
					auto textureVAt = [&](float t, float yShift) {
						return shiftV + (t - yShift) * 1.4f * distanceAt(t);
					};
					auto horizonAlphaAt = [&](float t) {
						const float d = distanceAt(t);
						const float opacity = d * d - 0.3f;
						return (opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity));
					};

					// A flat-colour twin of the polygon, for the horizon pass
					pvr_poly_hdr_t horizonHdr;
					{
						pvr_poly_cxt_t cxt;
						pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
						cxt.gen.culling = PVR_CULLING_NONE;
						cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
						cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
						cxt.blend.src = PVR_BLEND_SRCALPHA;
						cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
						pvr_poly_compile(&horizonHdr, &cxt);
					}
					const std::uint32_t horizonRgb = PackArgb(QuantizeChannel(horizon[0]),
						QuantizeChannel(horizon[1]), QuantizeChannel(horizon[2]), 0) & 0x00FFFFFFu;

					for (std::int32_t half = 0; half < 2; half++) {
						const float yShift = float(half);
						for (std::int32_t band = 0; band < BandsPerHalf; band++) {
							float t0 = (float(half) + float(band) / float(BandsPerHalf)) * 0.5f;
							float t1 = (float(half) + float(band + 1) / float(BandsPerHalf)) * 0.5f;

							// The vertical coordinate rises monotonically across each half, so a band crosses at
							// most one whole-texture boundary; splitting there keeps every piece inside a period
							float split = -1.0f;
							{
								const float vStart = textureVAt(t0, yShift);
								const float vEnd = textureVAt(t1, yShift);
								if (std::floor(vStart) != std::floor(vEnd)) {
									const float boundary = std::floor(vEnd);
									float lo = t0, hi = t1;
									for (std::int32_t i = 0; i < 12; i++) {
										const float mid = 0.5f * (lo + hi);
										if (textureVAt(mid, yShift) < boundary) {
											lo = mid;
										} else {
											hi = mid;
										}
									}
									split = 0.5f * (lo + hi);
								}
							}

							for (std::int32_t part = 0; part < 2; part++) {
								float bandT0 = (part == 0 ? t0 : split);
								float bandT1 = (part == 0 ? (split > 0.0f ? split : t1) : t1);
								if (part == 1 && split <= 0.0f) {
									break;
								}
								if (bandT1 <= bandT0) {
									continue;
								}

								const float y0 = originY + spanYy * bandT0;
								const float y1 = originY + spanYy * bandT1;

								// One period of the vertical coordinate for the whole band. The period is taken from
								// the band's middle rather than an end, because a band produced by the split above
								// starts exactly on a boundary and an endpoint would pick the neighbouring period.
								float v0 = textureVAt(bandT0, yShift);
								float v1 = textureVAt(bandT1, yShift);
								const float vBase = std::floor(0.5f * (v0 + v1));
								v0 -= vBase;
								v1 -= vBase;

								const float halfSpan0 = halfSpanAt(bandT0);
								const float halfSpan1 = halfSpanAt(bandT1);
								const float uLeft0 = shiftU - halfSpan0, uRight0 = shiftU + halfSpan0;
								const float uLeft1 = shiftU - halfSpan1, uRight1 = shiftU + halfSpan1;

								const std::int32_t firstPiece = (std::int32_t)std::floor(uLeft0 < uLeft1 ? uLeft0 : uLeft1);
								const std::int32_t lastPiece = (std::int32_t)std::ceil(uRight0 > uRight1 ? uRight0 : uRight1);
								std::int32_t emitted = 0;
								for (std::int32_t piece = firstPiece; piece < lastPiece && emitted < MaxHorizontalPieces; piece++) {
									// Where this period begins and ends along each edge of the band. Deliberately not
									// clamped to the band: a piece at either end reaches past the screen because the
									// row's span differs between the band's edges, and letting it do so keeps the
									// corner exactly on the boundary - so its coordinate is exactly 0 or 1 - while
									// the rasterizer discards whatever falls outside. Clamping instead would pull
									// the coordinate off the boundary and tear the seam open.
									auto edgeFraction = [](float u, float uLeft, float uRight) {
										const float span = uRight - uLeft;
										return (span > 0.0f ? (u - uLeft) / span : 0.0f);
									};
									const float topA = edgeFraction(float(piece), uLeft0, uRight0);
									const float topB = edgeFraction(float(piece + 1), uLeft0, uRight0);
									const float botA = edgeFraction(float(piece), uLeft1, uRight1);
									const float botB = edgeFraction(float(piece + 1), uLeft1, uRight1);
									// Wholly off one side of the screen along both edges
									if ((topB <= 0.0f && botB <= 0.0f) || (topA >= 1.0f && botA >= 1.0f)) {
										continue;
									}
									emitted++;

									// Strip order matches the sprite corners: the far edge first, then the near one
									const float bx[4] = { leftX + topB * spanX, leftX + botB * spanX,
										leftX + topA * spanX, leftX + botA * spanX };
									const float by[4] = { y0, y1, y0, y1 };
									const float bu[4] = {
										((uLeft0 + topB * (uRight0 - uLeft0)) - float(piece)) * uvScaleU,
										((uLeft1 + botB * (uRight1 - uLeft1)) - float(piece)) * uvScaleU,
										((uLeft0 + topA * (uRight0 - uLeft0)) - float(piece)) * uvScaleU,
										((uLeft1 + botA * (uRight1 - uLeft1)) - float(piece)) * uvScaleU
									};
									const float bv[4] = { v0 * uvScaleV, v1 * uvScaleV, v0 * uvScaleV, v1 * uvScaleV };
									SubmitStrip(hdr, bx, by, bu, bv, 4, PackArgb(255, 255, 255, 255));
								}

								const float alpha0 = horizonAlphaAt(bandT0);
								const float alpha1 = horizonAlphaAt(bandT1);
								if (alpha0 > 0.0f || alpha1 > 0.0f) {
									const float hx[4] = { leftX + spanX, leftX + spanX, leftX, leftX };
									const float hy[4] = { y0, y1, y0, y1 };
									const std::uint32_t a0 = std::uint32_t(QuantizeChannel(alpha0)) << 24;
									const std::uint32_t a1 = std::uint32_t(QuantizeChannel(alpha1)) << 24;
									const std::uint32_t shade[4] = { horizonRgb | a0, horizonRgb | a1,
										horizonRgb | a0, horizonRgb | a1 };
									SubmitStripShaded(horizonHdr, hx, hy, 4, shade);
								}
							}
						}
					}
					break;
				}
				case PvrEffect::Colorized:
				case PvrEffect::BatchedColorized: {
					// gray = (r + g + b) * 0.5 and COLOR = gray * dye, with dye = 1 + (color - 0.5) * 4.
					// The textures this runs on are grayscale (fonts), so r = g = b and that "average" is
					// really a 1.5x brightening; the product reaches 4.5 for a fully bright tint.
					//
					// A vertex colour cannot carry a multiplier above 1.0, and neither workaround alone is
					// right: folding the excess into the offset colour adds a constant, which lifts a
					// glyph's dark texels as much as its bright ones and blows the antialiased edges out,
					// while simply clamping the multiplier leaves bright tints looking washed out. So the
					// multiplier is split into whole units drawn as successive additive passes - the sum
					// stays proportional to the texel, and the framebuffer saturates it exactly where the
					// shader's own clamp would.
					constexpr float GrayGain = 1.5f;
					constexpr std::int32_t MaxColorizePasses = 3;
					float gain[3];
					float maxGain = 0.0f;
					for (std::int32_t c = 0; c < 3; c++) {
						gain[c] = GrayGain * (1.0f + (color[c] - 0.5f) * 4.0f);
						if (gain[c] > maxGain) {
							maxGain = gain[c];
						}
					}
					const std::uint8_t alpha = QuantizeChannel(1.0f + (color[3] - 0.5f) * 4.0f);
					std::int32_t passes = std::int32_t(std::ceil(maxGain));
					const std::int32_t passLimit = (hdrAdditiveValid ? MaxColorizePasses : 1);
					passes = (passes < 1 ? 1 : (passes > passLimit ? passLimit : passes));
					for (std::int32_t p = 0; p < passes; p++) {
						// Pass p carries whatever of the multiplier is left above p, clamped to one unit
						const std::uint32_t argb = PackArgb(QuantizeChannel(gain[0] - float(p)),
							QuantizeChannel(gain[1] - float(p)), QuantizeChannel(gain[2] - float(p)), alpha);
						SubmitQuad(p == 0 ? hdr : hdrAdditive, px, py, pu, pvv, argb);
					}
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
