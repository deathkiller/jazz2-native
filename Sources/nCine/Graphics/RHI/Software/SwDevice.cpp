#include "SwDevice.h"
#include "SwRaster.h"
#include "SwShaderProgram.h"
#include "SwRenderTarget.h"
#include "SwTexture.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"
#include "../../../../Shaders/Generated/SwGeneratedShaders.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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

	void SwDevice::FlushSoftwareRenderer()
	{
		SwRaster::Flush();
	}

	void SwDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB)
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
		// Pushed in Visit/OnDraw order; the matching Combine draws are dispatched in that same order, so the queue
		// is consumed front-to-back (see ApplyPendingSoftwareLighting)
		pendingSoftwareLights_.push_back(entry);
	}

	void SwDevice::ApplyPendingSoftwareLighting()
	{
		if (pendingSoftwareLights_.empty()) {
			// No lighting queued for this Combine draw: the scene stays as rasterized (fully lit)
			return;
		}
		const PendingSoftwareLight light = pendingSoftwareLights_.front();
		pendingSoftwareLights_.erase(pendingSoftwareLights_.begin());

		if (light.Lightmap == nullptr || light.LmW <= 0 || light.LmH <= 0) {
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

		// In-place combine over the viewport region, matching the core of Combine.shader with the (heavy,
		// blur-dependent) grayscale night-vision term dropped:
		//   lit = main * (1 + g) + max(g - 0.7, 0)
		//   out = mix(lit, ambientRGB, clamp(1 - r, 0, 1))
		// The half-resolution lightmap is sampled point-wise. Rows share the screen buffer's top-down convention.
		for (std::int32_t y = 0; y < vpH; y++) {
			const std::int32_t lmY = std::min(y / scale, lmH - 1);
			const float* texelBase = light.Lightmap + (std::size_t)lmY * lmW * 2;
			std::uint8_t* px = fb.pixels + (std::size_t)(vpY + y) * fb.strideBytes + (std::size_t)vpX * 4;
			for (std::int32_t x = 0; x < vpW; x++, px += 4) {
				const std::int32_t lmX = std::min(x / scale, lmW - 1);
				// Clamp to [0,1] to match the shader path's RG8 lighting buffer: a negative Brightness (some
				// light emitters ramp it below zero, e.g. Player fire-shield parts) is clamped to 0 there and
				// simply adds nothing. Without this clamp g<0 makes lit=(1+g)<1, which darkens the scene into a
				// "black light" instead of a no-op.
				const float r = std::clamp(texelBase[lmX * 2], 0.0f, 1.0f);
				const float g = std::clamp(texelBase[lmX * 2 + 1], 0.0f, 1.0f);
				const float darkT = std::clamp(1.0f - r, 0.0f, 1.0f);
				if (darkT <= 0.0f && g <= 0.0f) {
					continue; // Fully lit, no brightness core: leave the scene pixel as-is
				}
				const float lit = 1.0f + g;
				const float core = (g > 0.7f ? g - 0.7f : 0.0f);
				float cr = (px[0] / 255.0f) * lit + core;
				float cg = (px[1] / 255.0f) * lit + core;
				float cb = (px[2] / 255.0f) * lit + core;
				cr += (ambR - cr) * darkT;
				cg += (ambG - cg) * darkT;
				cb += (ambB - cb) * darkT;
				px[0] = (std::uint8_t)std::clamp(cr * 255.0f + 0.5f, 0.0f, 255.0f);
				px[1] = (std::uint8_t)std::clamp(cg * 255.0f + 0.5f, 0.0f, 255.0f);
				px[2] = (std::uint8_t)std::clamp(cb * 255.0f + 0.5f, 0.0f, 255.0f);
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

	void SwDevice::Dispatch(PrimitiveType primitive, std::int32_t numVertices)
	{
		static_cast<void>(primitive);
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
			if (generatedShader == nullptr && effect == SwEffect::Unknown) {
				std::fprintf(stderr, "[SwDevice] Skipped draw: no C++ effect for the bound program\n");
				return;
			}
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
			ctx.blendingEnabled = blendOn;
			ctx.blendSrc = bsrc;
			ctx.blendDst = bdst;
			ctx.scissorEnabled = scissor_.Enabled;
			ctx.scissorRect = scissor_.Rect;
			SwRaster::SetDrawContext(ctx);
			SwRaster::Draw(PrimitiveType::TriangleStrip, 0, 4);
		};

		// A generated shader is batched when its instance block carries a BATCH_SIZE-sized array (instanceStride > 0)
		const bool batched = (effect == SwEffect::DefaultBatchedSprites || effect == SwEffect::BatchedPaletteRemap ||
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
				std::fprintf(stderr, "[SwDevice] Skipped draw: generated shader uniform block exceeds the callback storage\n");
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
				std::fprintf(stderr, "[SwDevice] Skipped draw: generated shader instance block is not sprite-compatible\n");
				SwRaster::ClearDrawContext();
				return;
			}

			const std::int32_t uTextureUnit = samplerUnit("uTexture", 0);
			std::uint8_t uniformScratch[MaxFragmentShaderUserDataSize];
			for (std::int32_t k = 0; k < numInstances; k++) {
				const std::uint8_t* inst = blockData + std::size_t(k) * instanceStride;
				FFState ff;
				Mat4Mul(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), ff.mvpMatrix);
				std::memcpy(ff.color, inst + kColorOffset, sizeof(ff.color));
				std::memcpy(ff.texRect, inst + kTexRectOffset, sizeof(ff.texRect));
				std::memcpy(ff.spriteSize, inst + kSpriteSizeOffset, sizeof(ff.spriteSize));
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
					drawQuad(ff, nullptr, nullptr, 0);
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
