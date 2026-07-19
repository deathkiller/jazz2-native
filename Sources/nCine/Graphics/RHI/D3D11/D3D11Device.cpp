#include "D3D11Device.h"
#include "D3D11ShaderProgram.h"
#include "D3D11RenderTarget.h"
#include "D3D11Texture.h"
#include "D3D11BufferObject.h"

#include <cstdint>
#include <cstring>
#include <string>

// The Windows / Direct3D 11 headers are pulled in only by this translation unit
#include <d3d11.h>
#include <dxgi.h>
// IDXGIFactory2 / DXGI_SWAP_CHAIN_DESC1 / CreateSwapChainForCoreWindow (the UWP flip-model swap chain).
// dxgi1_2.h extends dxgi.h and is available on both desktop and UWP, so it is included unconditionally.
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#include <Asserts.h>

#include "../../../Base/HashFunctions.h"

namespace nCine::RhiD3D11
{
	namespace
	{
		template<class T>
		void SafeRelease(T*& p)
		{
			if (p != nullptr) {
				p->Release();
				p = nullptr;
			}
		}

		// Flip-blit shader used at present: a fullscreen triangle (SV_VertexID) that samples the intermediate present
		// texture with a vertically flipped V. Every draw is rendered GL-bottom-up (clip-space Y flipped in the
		// projection, see BindConstantBuffers) into the present texture; this final flip turns that bottom-up
		// composite into the upright top-down image the DXGI back-buffer scans out.
		const char* kPresentVs =
			"struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
			"VOut VSMain(uint id : SV_VertexID) {\n"
			"  VOut o;\n"
			"  float2 t = float2((id << 1) & 2, id & 2);\n"
			"  o.pos = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n"
			"  o.uv = float2(t.x, 1.0 - t.y);\n"	// flip V
			"  return o;\n"
			"}\n";
		const char* kPresentPs =
			"Texture2D tex : register(t0);\n"
			"SamplerState smp : register(s0);\n"
			"float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
			"  return tex.Sample(smp, uv);\n"
			"}\n";

		// Builds a normal (top-down) D3D11 viewport for a target. The GL<->D3D vertical orientation is handled
		// entirely by flipping clip-space Y in the projection matrix (see BindConstantBuffers) plus the single
		// flip-blit at present - a negative-height viewport, the usual GL-on-D3D trick, is silently ignored by the
		// D3D11 runtime here (its sign has no effect), so the viewport is always kept positive.
		D3D11_VIEWPORT MakeViewport(const Recti& rect)
		{
			D3D11_VIEWPORT vp;
			vp.TopLeftX = static_cast<float>(rect.X);
			vp.TopLeftY = static_cast<float>(rect.Y);
			vp.Width = static_cast<float>(rect.W);
			vp.Height = static_cast<float>(rect.H);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			return vp;
		}

		// Cursor into the constant-buffer pool, reset at the start of each draw's cbuffer build so every
		// simultaneously-bound cbuffer gets a distinct backing buffer (aliasing them would make the second
		// WRITE_DISCARD Map discard the first slot's data)
		std::uint32_t s_cbufferCursor = 0;

		D3D11_PRIMITIVE_TOPOLOGY MapPrimitive(PrimitiveType primitive)
		{
			switch (primitive) {
				case PrimitiveType::Points: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
				case PrimitiveType::Lines: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
				case PrimitiveType::LineStrip:
				case PrimitiveType::LineLoop: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
				case PrimitiveType::Triangles: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				case PrimitiveType::TriangleStrip:
				case PrimitiveType::TriangleFan: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
				default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			}
		}

		// Compact 0..14 code for a blend factor, so four of them pack into a small collision-free cache key
		// (the raw enum values are GL constants up to 0x8004 and would not fit into bit fields)
		std::uint32_t BlendCode(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return 0;
				case nCine::BlendingFactor::One: return 1;
				case nCine::BlendingFactor::SrcColor: return 2;
				case nCine::BlendingFactor::OneMinusSrcColor: return 3;
				case nCine::BlendingFactor::SrcAlpha: return 4;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return 5;
				case nCine::BlendingFactor::DstAlpha: return 6;
				case nCine::BlendingFactor::OneMinusDstAlpha: return 7;
				case nCine::BlendingFactor::DstColor: return 8;
				case nCine::BlendingFactor::OneMinusDstColor: return 9;
				case nCine::BlendingFactor::SrcAlphaSaturate: return 10;
				case nCine::BlendingFactor::ConstantColor: return 11;
				case nCine::BlendingFactor::OneMinusConstantColor: return 12;
				case nCine::BlendingFactor::ConstantAlpha: return 13;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return 14;
				default: return 1;
			}
		}

		D3D11_BLEND MapBlend(nCine::BlendingFactor factor, bool alpha)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return D3D11_BLEND_ZERO;
				case nCine::BlendingFactor::One: return D3D11_BLEND_ONE;
				case nCine::BlendingFactor::SrcColor: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
				case nCine::BlendingFactor::DstColor: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return D3D11_BLEND_SRC_ALPHA_SAT;
				case nCine::BlendingFactor::ConstantColor:
				case nCine::BlendingFactor::ConstantAlpha: return D3D11_BLEND_BLEND_FACTOR;
				case nCine::BlendingFactor::OneMinusConstantColor:
				case nCine::BlendingFactor::OneMinusConstantAlpha: return D3D11_BLEND_INV_BLEND_FACTOR;
				default: return D3D11_BLEND_ONE;
			}
		}
	}

	// -- Static state --

	D3D11Device::BlendingState D3D11Device::blending_;
	D3D11Device::DepthTestState D3D11Device::depthTest_;
	D3D11Device::CullFaceState D3D11Device::cullFace_;
	D3D11Device::ScissorState D3D11Device::scissor_;
	Recti D3D11Device::viewport_(0, 0, 0, 0);
	Colorf D3D11Device::clearColor_(0.0f, 0.0f, 0.0f, 0.0f);

	D3D11ShaderProgram* D3D11Device::currentProgram_ = nullptr;
	const D3D11Texture* D3D11Device::boundTextures_[MaxTextureUnits] = {};
	D3D11Device::UniformRange D3D11Device::boundUniformRanges_[MaxUniformBindings] = {};
	D3D11RenderTarget* D3D11Device::currentRenderTarget_ = nullptr;

	ID3D11Device* D3D11Device::device_ = nullptr;
	ID3D11DeviceContext* D3D11Device::context_ = nullptr;
	IDXGISwapChain* D3D11Device::swapchain_ = nullptr;
	ID3D11RenderTargetView* D3D11Device::backbufferRtv_ = nullptr;
	bool D3D11Device::vsync_ = true;
	std::int32_t D3D11Device::backbufferWidth_ = 0;
	std::int32_t D3D11Device::backbufferHeight_ = 0;
	std::int32_t D3D11Device::maxTextureDimension_ = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	ID3D11Texture2D* D3D11Device::presentTexture_ = nullptr;
	ID3D11RenderTargetView* D3D11Device::presentRtv_ = nullptr;
	ID3D11ShaderResourceView* D3D11Device::presentSrv_ = nullptr;
	ID3D11VertexShader* D3D11Device::presentVs_ = nullptr;
	ID3D11PixelShader* D3D11Device::presentPs_ = nullptr;
	ID3D11SamplerState* D3D11Device::presentSampler_ = nullptr;

	std::vector<D3D11Device::PooledCBuffer> D3D11Device::cbufferPool_;
	std::vector<std::uint8_t> D3D11Device::cbufferStaging_;
	std::vector<D3D11Device::BlendStateEntry> D3D11Device::blendStates_;
	std::vector<D3D11Device::RasterStateEntry> D3D11Device::rasterStates_;
	ID3D11DepthStencilState* D3D11Device::depthDisabledState_ = nullptr;

	ID3D11BlendState* D3D11Device::lastBlendState_ = nullptr;
	ID3D11RasterizerState* D3D11Device::lastRasterState_ = nullptr;
	bool D3D11Device::depthStateApplied_ = false;
	ID3D11VertexShader* D3D11Device::lastVs_ = nullptr;
	ID3D11PixelShader* D3D11Device::lastPs_ = nullptr;
	std::uint32_t D3D11Device::lastTopology_ = 0;
	ID3D11RenderTargetView* D3D11Device::lastRtvs_[D3D11Device::MaxRenderTargets] = {};
	std::uint32_t D3D11Device::lastRtvCount_ = 0;
	bool D3D11Device::lastRtvValid_ = false;
	Recti D3D11Device::lastViewport_(0, 0, 0, 0);
	bool D3D11Device::lastViewportValid_ = false;
	D3D11Device::CachedRect D3D11Device::lastScissorRect_;
	bool D3D11Device::lastScissorValid_ = false;
	ID3D11ShaderResourceView* D3D11Device::lastSrvs_[2][D3D11Device::MaxTextureUnits] = {};
	ID3D11SamplerState* D3D11Device::lastSamplers_[2][D3D11Device::MaxTextureUnits] = {};
	bool D3D11Device::srvShadowValid_ = false;

	void D3D11Device::InvalidateCachedState()
	{
		lastBlendState_ = nullptr;
		lastRasterState_ = nullptr;
		depthStateApplied_ = false;
		lastVs_ = nullptr;
		lastPs_ = nullptr;
		lastTopology_ = 0;
		std::memset(lastRtvs_, 0, sizeof(lastRtvs_));
		lastRtvCount_ = 0;
		lastRtvValid_ = false;
		lastViewportValid_ = false;
		lastScissorValid_ = false;
		std::memset(lastSrvs_, 0, sizeof(lastSrvs_));
		std::memset(lastSamplers_, 0, sizeof(lastSamplers_));
		srvShadowValid_ = false;
	}

	// -- Pipeline state (recorded) --

	void D3D11Device::SetBlendingEnabled(bool enabled) { blending_.Enabled = enabled; }

	void D3D11Device::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}

	D3D11Device::BlendingState D3D11Device::GetBlendingState() { return blending_; }
	void D3D11Device::SetBlendingState(const BlendingState& state) { blending_ = state; }

	void D3D11Device::SetDepthTestEnabled(bool enabled) { depthTest_.TestEnabled = enabled; }
	void D3D11Device::SetDepthMaskEnabled(bool enabled) { depthTest_.MaskEnabled = enabled; }
	D3D11Device::DepthTestState D3D11Device::GetDepthTestState() { return depthTest_; }
	void D3D11Device::SetDepthTestState(const DepthTestState& state) { depthTest_ = state; }

	void D3D11Device::SetCullFaceEnabled(bool enabled) { cullFace_.Enabled = enabled; }
	D3D11Device::CullFaceState D3D11Device::GetCullFaceState() { return cullFace_; }
	void D3D11Device::SetCullFaceState(const CullFaceState& state) { cullFace_ = state; }

	D3D11Device::ScissorState D3D11Device::GetScissorState() { return scissor_; }
	void D3D11Device::SetScissorState(const ScissorState& state) { scissor_ = state; }
	void D3D11Device::SetScissor(const Recti& rect) { scissor_.Enabled = true; scissor_.Rect = rect; }
	void D3D11Device::SetScissorTestEnabled(bool enabled) { scissor_.Enabled = enabled; }

	Recti D3D11Device::GetViewport() { return viewport_; }

	void D3D11Device::SetViewport(const Recti& rect)
	{
		viewport_ = rect;
		if (context_ != nullptr) {
			D3D11_VIEWPORT vp = MakeViewport(rect);
			context_->RSSetViewports(1, &vp);
			lastViewport_ = rect;
			lastViewportValid_ = true;
		}
	}

	void D3D11Device::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		viewport_ = Recti(x, y, width, height);
	}

	Colorf D3D11Device::GetClearColor() { return clearColor_; }
	void D3D11Device::SetClearColor(const Colorf& color) { clearColor_ = color; }

	void D3D11Device::Clear(ClearFlags flags)
	{
		if (context_ == nullptr) {
			return;
		}
		if ((flags & ClearFlags::Color) != ClearFlags::None) {
			const float c[4] = { clearColor_.R, clearColor_.G, clearColor_.B, clearColor_.A };
			if (currentRenderTarget_ != nullptr) {
				// The clear covers every bound color attachment (the contiguous attached run, bounded by the
				// draw-buffer count), matching glClear's semantics of clearing all enabled draw buffers
				ID3D11RenderTargetView* rtvs[D3D11RenderTarget::MaxColorAttachments];
				const std::uint32_t numRtvs = currentRenderTarget_->GetRTVs(rtvs);
				for (std::uint32_t i = 0; i < numRtvs; i++) {
					context_->ClearRenderTargetView(rtvs[i], c);
				}
			} else {
				ID3D11RenderTargetView* screenRtv = (presentRtv_ != nullptr ? presentRtv_ : backbufferRtv_);
				if (screenRtv != nullptr) {
					context_->ClearRenderTargetView(screenRtv, c);
				}
			}
		}
	}

	void D3D11Device::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 1, 0);
	}

	void D3D11Device::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, numInstances, 0);
	}

	void D3D11Device::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, 1, baseVertex);
	}

	void D3D11Device::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, numInstances, baseVertex);
	}

	void D3D11Device::DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
		bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		D3D11ShaderProgram* prog = currentProgram_;
		if (context_ == nullptr || prog == nullptr || prog->GetVertexShader() == nullptr || count == 0) {
			return;
		}

		BindCurrentRenderTarget();

		// The viewport is always positive/top-down; the GL<->D3D vertical flip is applied uniformly in the vertex
		// transform (projection matrix Y negated in BindConstantBuffers), which flips every draw to every target
		// consistently - so back-buffer geometry and off-screen render targets stay in agreement with no present-flip.
		if (!lastViewportValid_ || lastViewport_ != viewport_) {
			D3D11_VIEWPORT vp = MakeViewport(viewport_);
			context_->RSSetViewports(1, &vp);
			lastViewport_ = viewport_;
			lastViewportValid_ = true;
		}

		// SV_VertexID shaders (sprites, background, the fullscreen post-processing chain) need no vertex buffer;
		// attribute-based shaders (mesh sprites, tilemap) bind the input layout + vertex/index buffers built
		// from the reflected attributes and the vertex format the pipeline defined.
		if (prog->HasVertexAttributes()) {
			ID3D11InputLayout* layout = prog->GetInputLayout();
			const D3D11BufferObject* vbo = prog->GetBoundVbo();
			const std::uint32_t stride = prog->GetVertexStride();
			ID3D11Buffer* vb = (vbo != nullptr ? vbo->GetD3DBuffer() : nullptr);
			if (layout == nullptr || vb == nullptr || stride == 0) {
				return;
			}
			UINT strideU = stride;
			UINT vbOffset = 0;
			context_->IASetInputLayout(layout);
			context_->IASetVertexBuffers(0, 1, &vb, &strideU, &vbOffset);
			if (indexed) {
				const D3D11BufferObject* ibo = prog->GetBoundIbo();
				ID3D11Buffer* ib = (ibo != nullptr ? ibo->GetD3DBuffer() : nullptr);
				if (ib == nullptr) {
					return;
				}
				context_->IASetIndexBuffer(ib, indexFormat == IndexFormat::UInt32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
			}
		} else {
			context_->IASetInputLayout(nullptr);
		}

		const D3D11_PRIMITIVE_TOPOLOGY topology = MapPrimitive(primitive);
		if (lastTopology_ != std::uint32_t(topology)) {
			context_->IASetPrimitiveTopology(topology);
			lastTopology_ = std::uint32_t(topology);
		}
		if (lastVs_ != prog->GetVertexShader()) {
			lastVs_ = prog->GetVertexShader();
			context_->VSSetShader(lastVs_, nullptr, 0);
		}
		if (lastPs_ != prog->GetPixelShader()) {
			lastPs_ = prog->GetPixelShader();
			context_->PSSetShader(lastPs_, nullptr, 0);
		}
		BindConstantBuffers();
		BindTextures();
		ApplyRenderState();

		const UINT indexSize = (indexFormat == IndexFormat::UInt32 ? 4u : 2u);
		if (indexed) {
			if (numInstances > 1) {
				context_->DrawIndexedInstanced(count, std::uint32_t(numInstances), static_cast<UINT>(indexOffset / indexSize), baseVertex, 0);
			} else {
				context_->DrawIndexed(count, static_cast<UINT>(indexOffset / indexSize), baseVertex);
			}
		} else if (numInstances > 1) {
			context_->DrawInstanced(count, std::uint32_t(numInstances), std::uint32_t(firstVertex), 0);
		} else {
			context_->Draw(count, std::uint32_t(firstVertex));
		}
	}

	void D3D11Device::BindCurrentRenderTarget()
	{
		static_assert(MaxRenderTargets == D3D11RenderTarget::MaxColorAttachments,
			"The device's RTV shadow must span every color attachment a render target can hold");

		// "Screen" (no render target bound) is directed into the intermediate present texture; PresentFrame()
		// flip-blits it into the real back-buffer (the single GL bottom-up -> D3D top-down scan-out correction).
		// Falls back to the back-buffer if the present texture is absent. An off-screen render target binds
		// every color attachment it has enabled for drawing (the contiguous attached run, bounded by
		// SetDrawBuffers - the glDrawBuffers equivalent).
		ID3D11RenderTargetView* rtvs[MaxRenderTargets] = {};
		std::uint32_t numRtvs;
		if (currentRenderTarget_ != nullptr) {
			numRtvs = currentRenderTarget_->GetRTVs(rtvs);
			if (numRtvs == 0) {
				// Attachment 0 unusable: keep the historical behavior of explicitly binding a null target
				numRtvs = 1;
			}
		} else {
			rtvs[0] = (presentRtv_ != nullptr ? presentRtv_ : backbufferRtv_);
			numRtvs = 1;
		}
		if (lastRtvValid_ && lastRtvCount_ == numRtvs &&
			std::memcmp(lastRtvs_, rtvs, numRtvs * sizeof(ID3D11RenderTargetView*)) == 0) {
			return;
		}

		// Read/write hazard guard: if a texture becoming a render target attachment is still bound as an SRV
		// from an earlier pass, unbind it explicitly. Without this the runtime silently nulls the SRV slot at
		// draw time, which would desync the SRV shadow cache (it would still believe the SRV is bound and skip
		// a later rebind). Covers every bound color attachment, not just attachment 0.
		if (currentRenderTarget_ != nullptr) {
			ID3D11ShaderResourceView* nullSrv = nullptr;
			for (std::uint32_t a = 0; a < numRtvs; a++) {
				const D3D11Texture* rtTex = currentRenderTarget_->GetColorTexture(a);
				ID3D11ShaderResourceView* rtSrv = (rtTex != nullptr ? rtTex->GetSRV() : nullptr);
				if (rtSrv == nullptr) {
					continue;
				}
				for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
					if (lastSrvs_[0][u] == rtSrv) {
						context_->PSSetShaderResources(u, 1, &nullSrv);
						lastSrvs_[0][u] = nullptr;
					}
					if (lastSrvs_[1][u] == rtSrv) {
						context_->VSSetShaderResources(u, 1, &nullSrv);
						lastSrvs_[1][u] = nullptr;
					}
				}
			}
		}

		context_->OMSetRenderTargets(numRtvs, rtvs, nullptr);
		std::memcpy(lastRtvs_, rtvs, sizeof(lastRtvs_));
		lastRtvCount_ = numRtvs;
		lastRtvValid_ = true;
	}

	std::uint32_t D3D11Device::AcquireConstantBuffer(std::uint32_t size)
	{
		std::uint32_t size16 = (size + 15u) & ~15u;
		if (size16 == 0) {
			size16 = 16;
		}
		if (s_cbufferCursor >= cbufferPool_.size()) {
			cbufferPool_.push_back(PooledCBuffer{});
		}
		PooledCBuffer& entry = cbufferPool_[s_cbufferCursor];
		if (entry.Buffer == nullptr || entry.Size < size16) {
			SafeRelease(entry.Buffer);
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = size16;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device_->CreateBuffer(&desc, nullptr, &entry.Buffer))) {
				entry.Buffer = nullptr;
			}
			entry.Size = size16;
			entry.ContentSize = 0;	// fresh buffer: its previous contents are gone, so the skip cache is stale
		}
		return s_cbufferCursor++;
	}

	void D3D11Device::BindConstantBuffers()
	{
		D3D11ShaderProgram* prog = currentProgram_;
		s_cbufferCursor = 0;

		auto buildAndBind = [&](const std::vector<D3D11CBufferSlot>& slots, bool vertexStage) {
			for (const D3D11CBufferSlot& slot : slots) {
				std::uint32_t uploadSize = slot.ByteSize;
				// Assemble the exact bytes to upload into one contiguous span (`srcBytes`), so they can be
				// hashed against the pool buffer's current contents and copied in a single memcpy.
				const std::uint8_t* srcBytes = nullptr;
				if (slot.IsGlobals) {
					// Loose uniforms are scattered across the program's resolved-value pointers, so gather them
					// (plus the projection Y-flip) into the reusable staging buffer first.
					if (cbufferStaging_.size() < uploadSize) {
						cbufferStaging_.resize(uploadSize);
					}
					std::uint8_t* dst = cbufferStaging_.data();
					std::memset(dst, 0, uploadSize);
					for (const D3D11CBufferSlot::GlobalVar& gv : slot.Globals) {
						const std::uint8_t* src = prog->ResolveUniform(gv.Name.c_str());
						if (src != nullptr && gv.Offset + gv.Size <= uploadSize) {
							std::memcpy(dst + gv.Offset, src, gv.Size);
							// Orientation fix: the engine renders in the OpenGL convention, which is upside down on
							// D3D's top-down back-buffer and render targets (a negative-height viewport, the usual
							// remedy, is ignored by the D3D11 runtime here). Instead flip clip-space Y for every draw
							// by negating the projection matrix's second row (indices 1,5,9,13 of the column-major
							// mat4). Applied uniformly to every shader (all use uProjectionMatrix; ImGui uses
							// uGuiProjection), this renders every target bottom-up exactly like GL, keeping the scene
							// composite and direct-drawn HUD consistent; PresentFrame() flip-blits once for D3D scan-out.
							if (gv.Size >= 64 && (gv.Name == "uProjectionMatrix" || gv.Name == "uGuiProjection")) {
								float* m = reinterpret_cast<float*>(dst + gv.Offset);
								m[1] = -m[1];
								m[5] = -m[5];
								m[9] = -m[9];
								m[13] = -m[13];
							}
						}
					}
					srcBytes = dst;
				} else {
					if (slot.BlockIndex < 0 || slot.BlockIndex >= std::int32_t(MaxUniformBindings)) {
						continue;
					}
					const std::uint8_t* blockData = boundUniformRanges_[slot.BlockIndex].Data;
					const std::uint32_t blockSize = boundUniformRanges_[slot.BlockIndex].Size;
					if (blockData != nullptr) {
						// A uniform block is bound with exactly the bytes the shader reads (valid instances), so a
						// buffer sized to the uploaded range is enough and avoids allocating the full declared cbuffer
						if (blockSize > 0 && blockSize < uploadSize) {
							uploadSize = blockSize;
						}
						srcBytes = blockData;
					} else {
						if (cbufferStaging_.size() < uploadSize) {
							cbufferStaging_.resize(uploadSize);
						}
						std::memset(cbufferStaging_.data(), 0, uploadSize);
						srcBytes = cbufferStaging_.data();
					}
				}

				const std::uint32_t poolIndex = AcquireConstantBuffer(uploadSize);
				PooledCBuffer& entry = cbufferPool_[poolIndex];
				ID3D11Buffer* cb = entry.Buffer;
				if (cb == nullptr) {
					continue;
				}

				// Skip the Map/Unmap (a driver buffer-rename each draw) when this pool buffer already holds
				// exactly these bytes. AcquireConstantBuffer() clears ContentSize when it (re)creates the buffer,
				// so a stale slot never matches. The per-draw cursor keeps simultaneously-bound cbuffers in
				// distinct slots, so a skip only ever re-binds a buffer that genuinely still holds this data.
				const std::uint64_t hash = static_cast<std::uint64_t>(xxHash3(reinterpret_cast<const char*>(srcBytes), uploadSize));
				if (entry.ContentSize != uploadSize || entry.ContentHash != hash) {
					D3D11_MAPPED_SUBRESOURCE mapped;
					if (FAILED(context_->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
						continue;
					}
					std::memcpy(mapped.pData, srcBytes, uploadSize);
					context_->Unmap(cb, 0);
					entry.ContentHash = hash;
					entry.ContentSize = uploadSize;
				}

				if (vertexStage) {
					context_->VSSetConstantBuffers(slot.Register, 1, &cb);
				} else {
					context_->PSSetConstantBuffers(slot.Register, 1, &cb);
				}
			}
		};

		buildAndBind(prog->GetVsCBuffers(), true);
		buildAndBind(prog->GetPsCBuffers(), false);
	}

	void D3D11Device::BindTextures()
	{
		// Only touch the slots the current program's stages actually read (from bytecode reflection), and skip
		// the call when the slot already holds this SRV/sampler. The read/write hazard guard: a texture that is
		// any color attachment of the current render target is never bound as an SRV (the runtime would silently
		// null it, desyncing the shadow); it re-binds naturally on the first draw after the target changes.
		const D3D11ShaderProgram* prog = currentProgram_;
		const std::uint32_t psMask = prog->GetPsTextureMask();
		const std::uint32_t vsMask = prog->GetVsTextureMask();
		const D3D11Texture* rtTexs[D3D11RenderTarget::MaxColorAttachments];
		std::uint32_t numRtTexs = 0;
		if (currentRenderTarget_ != nullptr) {
			std::uint32_t boundCount = currentRenderTarget_->GetAttachedCount();
			const std::uint32_t numDrawBuffers = currentRenderTarget_->GetNumDrawBuffers();
			if (numDrawBuffers > 0 && numDrawBuffers < boundCount) {
				boundCount = numDrawBuffers;
			}
			for (std::uint32_t a = 0; a < boundCount; a++) {
				const D3D11Texture* rtTex = currentRenderTarget_->GetColorTexture(a);
				if (rtTex != nullptr) {
					rtTexs[numRtTexs++] = rtTex;
				}
			}
		}
		const bool force = !srvShadowValid_;

		std::uint32_t mask = psMask | vsMask;
		for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
			if ((mask & (1u << u)) == 0 && !force) {
				continue;
			}
			const D3D11Texture* tex = boundTextures_[u];
			for (std::uint32_t a = 0; a < numRtTexs; a++) {
				if (tex == rtTexs[a]) {
					tex = nullptr;
					break;
				}
			}
			ID3D11ShaderResourceView* srv = (tex != nullptr ? tex->GetSRV() : nullptr);
			ID3D11SamplerState* samp = (tex != nullptr ? tex->GetSampler() : nullptr);
			if (force || lastSrvs_[0][u] != srv) {
				context_->PSSetShaderResources(u, 1, &srv);
				lastSrvs_[0][u] = srv;
			}
			if (force || lastSamplers_[0][u] != samp) {
				context_->PSSetSamplers(u, 1, &samp);
				lastSamplers_[0][u] = samp;
			}
			if (force || lastSrvs_[1][u] != srv) {
				context_->VSSetShaderResources(u, 1, &srv);
				lastSrvs_[1][u] = srv;
			}
			if (force || lastSamplers_[1][u] != samp) {
				context_->VSSetSamplers(u, 1, &samp);
				lastSamplers_[1][u] = samp;
			}
		}
		srvShadowValid_ = true;
	}

	void D3D11Device::ApplyRenderState()
	{
		// Blend state (keyed on enabled + the four factors)
		{
			std::uint64_t key = (blending_.Enabled ? 1u : 0u);
			key |= std::uint64_t(BlendCode(blending_.SrcRgb)) << 1;
			key |= std::uint64_t(BlendCode(blending_.DstRgb)) << 5;
			key |= std::uint64_t(BlendCode(blending_.SrcAlpha)) << 9;
			key |= std::uint64_t(BlendCode(blending_.DstAlpha)) << 13;
			ID3D11BlendState* state = nullptr;
			for (const BlendStateEntry& e : blendStates_) {
				if (e.Key == key) {
					state = e.State;
					break;
				}
			}
			if (state == nullptr) {
				// The engine has a single blend state for all attachments (glBlendFunc applies to every draw
				// buffer), so IndependentBlendEnable stays FALSE (zero-init): D3D11 then applies the
				// RenderTarget[0] description to every simultaneously bound render target, which is exactly
				// the GL contract - no per-attachment replication needed.
				D3D11_BLEND_DESC desc = {};
				desc.RenderTarget[0].BlendEnable = blending_.Enabled ? TRUE : FALSE;
				desc.RenderTarget[0].SrcBlend = MapBlend(blending_.SrcRgb, false);
				desc.RenderTarget[0].DestBlend = MapBlend(blending_.DstRgb, false);
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = MapBlend(blending_.SrcAlpha, true);
				desc.RenderTarget[0].DestBlendAlpha = MapBlend(blending_.DstAlpha, true);
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				if (SUCCEEDED(device_->CreateBlendState(&desc, &state))) {
					blendStates_.push_back({ key, state });
				}
			}
			if (state != lastBlendState_ || state == nullptr) {
				const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context_->OMSetBlendState(state, blendFactor, 0xFFFFFFFF);
				lastBlendState_ = state;
			}
		}

		// Depth-stencil: the renderer is 2D and no depth buffer is attached, so depth is always disabled
		if (depthDisabledState_ == nullptr) {
			D3D11_DEPTH_STENCIL_DESC desc = {};
			desc.DepthEnable = FALSE;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
			desc.StencilEnable = FALSE;
			device_->CreateDepthStencilState(&desc, &depthDisabledState_);
			depthStateApplied_ = false;
		}
		if (!depthStateApplied_) {
			context_->OMSetDepthStencilState(depthDisabledState_, 0);
			depthStateApplied_ = true;
		}

		// Rasterizer (keyed on cull enabled + mode + scissor enabled)
		{
			std::uint32_t key = (cullFace_.Enabled ? 1u : 0u) | (std::uint32_t(cullFace_.Mode) << 1) | (scissor_.Enabled ? 0x10000u : 0u);
			ID3D11RasterizerState* state = nullptr;
			for (const RasterStateEntry& e : rasterStates_) {
				if (e.Key == key) {
					state = e.State;
					break;
				}
			}
			if (state == nullptr) {
				D3D11_RASTERIZER_DESC desc = {};
				desc.FillMode = D3D11_FILL_SOLID;
				if (!cullFace_.Enabled) {
					desc.CullMode = D3D11_CULL_NONE;
				} else {
					desc.CullMode = (cullFace_.Mode == CullFaceMode::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
				}
				desc.FrontCounterClockwise = TRUE;	// GL default winding
				desc.DepthClipEnable = TRUE;
				desc.ScissorEnable = scissor_.Enabled ? TRUE : FALSE;
				if (SUCCEEDED(device_->CreateRasterizerState(&desc, &state))) {
					rasterStates_.push_back({ key, state });
				}
			}
			if (state != lastRasterState_ || state == nullptr) {
				context_->RSSetState(state);
				lastRasterState_ = state;
			}
		}

		// Scissor rectangle. The engine specifies it in GL window space (bottom-left origin); convert it to match
		// the current target's viewport (see DrawCommon / MakeViewport). Off-screen render targets use a
		// negative-height (bottom-up) viewport, so their stored rows run bottom-up and GL Y maps straight to D3D
		// rows (top = glY). The back-buffer / screen path uses a normal (top-down) viewport, so its scissor needs
		// the standard flip against the target height (top = height - glY - glH).
		if (scissor_.Enabled) {
			std::int32_t targetHeight = backbufferHeight_;
			bool targetFlipped = false;
			if (currentRenderTarget_ != nullptr) {
				D3D11Texture* colorTex = currentRenderTarget_->GetColorTexture(0);
				if (colorTex != nullptr && colorTex->GetHeight() > 0) {
					targetHeight = colorTex->GetHeight();
				}
				targetFlipped = true;
			}
			const Recti& r = scissor_.Rect;
			D3D11_RECT sr;
			sr.left = r.X;
			sr.right = r.X + r.W;
			if (targetFlipped) {
				// Off-screen render target: rows are stored GL bottom-up, so GL Y maps straight to D3D rows
				sr.top = r.Y;
				sr.bottom = r.Y + r.H;
			} else {
				// Back-buffer / screen: flip against the target height (corrected again by the present flip-blit)
				sr.top = targetHeight - (r.Y + r.H);
				sr.bottom = targetHeight - r.Y;
			}
			if (!lastScissorValid_ || lastScissorRect_.L != sr.left || lastScissorRect_.T != sr.top ||
				lastScissorRect_.R != sr.right || lastScissorRect_.B != sr.bottom) {
				context_->RSSetScissorRects(1, &sr);
				lastScissorRect_ = { sr.left, sr.top, sr.right, sr.bottom };
				lastScissorValid_ = true;
			}
		}
	}

	FenceHandle D3D11Device::InsertFence() { return nullptr; }
	void D3D11Device::DeleteFence(FenceHandle& fence) { fence = nullptr; }
	bool D3D11Device::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void D3D11Device::SetupInitialState()
	{
		blending_ = BlendingState{};
		depthTest_ = DepthTestState{};
		cullFace_ = CullFaceState{};
		scissor_ = ScissorState{};
	}

	// -- Backend extensions (recorders) --

	void D3D11Device::BindProgram(D3D11ShaderProgram* program) { currentProgram_ = program; }
	D3D11ShaderProgram* D3D11Device::CurrentProgram() { return currentProgram_; }

	void D3D11Device::BindTexture(std::uint32_t unit, const D3D11Texture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	void D3D11Device::UnbindTexture(const D3D11Texture* texture)
	{
		// Called from ~D3D11Texture so a destroyed texture never lingers as a dangling pointer in the bound-texture
		// tracking (a later draw would then dereference freed memory in BindTextures - crashed during level changes
		// in splitscreen). Clear every unit it may be bound to. Only touches the static array (no D3D calls), so it
		// is safe at any time including shutdown. Mirrors GLTexture's destructor.
		for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
			if (boundTextures_[unit] == texture) {
				boundTextures_[unit] = nullptr;
			}
		}
		// Scrub the last-bound SRV/sampler shadows too (the views are released right after this call, and a new
		// texture recycling the same pointer would otherwise be mistaken for still-bound and its bind skipped).
		// Peek* returns the existing objects without lazily creating them (this runs from the destructor).
		if (texture != nullptr) {
			ID3D11ShaderResourceView* srv = texture->PeekSRV();
			ID3D11SamplerState* samp = texture->PeekSampler();
			for (std::uint32_t stage = 0; stage < 2; stage++) {
				for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
					if (srv != nullptr && lastSrvs_[stage][unit] == srv) {
						lastSrvs_[stage][unit] = nullptr;
						srvShadowValid_ = false;
					}
					if (samp != nullptr && lastSamplers_[stage][unit] == samp) {
						lastSamplers_[stage][unit] = nullptr;
						srvShadowValid_ = false;
					}
				}
			}
		}
	}

	const D3D11Texture* D3D11Device::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void D3D11Device::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void D3D11Device::SetRenderTarget(D3D11RenderTarget* renderTarget)
	{
		currentRenderTarget_ = renderTarget;
		if (context_ != nullptr) {
			BindCurrentRenderTarget();
		}
	}

	void D3D11Device::UnbindRenderTarget(const D3D11RenderTarget* renderTarget)
	{
		// Called from ~D3D11RenderTarget so a destroyed render target can't dangle as currentRenderTarget_ (a later
		// Clear/scissor/draw dereferences it). Reverts to the screen (nullptr); the pipeline binds a fresh target
		// before drawing to it. Only touches the static pointer (no D3D calls), so it is safe at any time.
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
		// The RTVs are released with the target; forget them so a recycled pointer can't match the shadow
		std::memset(lastRtvs_, 0, sizeof(lastRtvs_));
		lastRtvCount_ = 0;
		lastRtvValid_ = false;
	}

	void D3D11Device::OnRtvReleased(const ID3D11RenderTargetView* rtv)
	{
		// Called by D3D11RenderTarget whenever it releases one of its views (attachment change, lazy rebuild,
		// destruction). If the dying view is part of the last-bound shadow, forget the whole set so a new RTV
		// recycling the same pointer can't be mistaken for still bound (the next draw re-issues the bind).
		// Only touches the static shadow (no D3D calls), so it is safe at any time including shutdown.
		if (lastRtvValid_ && rtv != nullptr) {
			for (std::uint32_t i = 0; i < lastRtvCount_; i++) {
				if (lastRtvs_[i] == rtv) {
					std::memset(lastRtvs_, 0, sizeof(lastRtvs_));
					lastRtvCount_ = 0;
					lastRtvValid_ = false;
					break;
				}
			}
		}
	}

	void D3D11Device::OnProgramDestroyed(const D3D11ShaderProgram* program)
	{
		if (currentProgram_ == program) {
			currentProgram_ = nullptr;
		}
		if (program != nullptr) {
			if (lastVs_ != nullptr && lastVs_ == program->GetVertexShader()) {
				lastVs_ = nullptr;
			}
			if (lastPs_ != nullptr && lastPs_ == program->GetPixelShader()) {
				lastPs_ = nullptr;
			}
		}
	}

	// -- Direct3D 11 device / swap-chain lifecycle --

#if defined(D3D11_MRT_PROBE)
	namespace
	{
		// Synthetic multi-render-target self-test, compiled only when D3D11_MRT_PROBE is defined (no runtime
		// cost otherwise, mirrors the Vulkan backend's VULKAN_MRT_PROBE). Run once at device creation: attaches
		// two color textures to one render target, binds both through a single OMSetRenderTargets (the regular
		// BindCurrentRenderTarget path), verifies the multi-attachment device Clear() covers both, then clears
		// attachment 0 red and attachment 1 green through their per-attachment RTVs, reads both back through
		// the regular staging readback and logs PASS/FAIL. Exercises per-attachment RTV creation, the
		// contiguous multi-attachment bind, multi-attachment clears and the readback path.
		void RunMrtProbe()
		{
			constexpr std::int32_t Size = 4;
			D3D11Texture tex0(TextureTarget::Texture2D);
			D3D11Texture tex1(TextureTarget::Texture2D);
			tex0.TexStorage2D(1, PixelFormat::RGBA8, Size, Size);
			tex1.TexStorage2D(1, PixelFormat::RGBA8, Size, Size);

			D3D11RenderTarget rt;
			rt.AttachColorTexture(tex0, 0);
			rt.AttachColorTexture(tex1, 1);
			rt.SetDrawBuffers(2);
			rt.BindDraw();		// one bind covering both attachments (OMSetRenderTargets count 2)

			// Multi-attachment Clear(): both attachments must come out blue from the single call
			const Colorf previousClearColor = D3D11Device::GetClearColor();
			D3D11Device::SetClearColor(Colorf(0.0f, 0.0f, 1.0f, 1.0f));
			D3D11Device::Clear(ClearFlags::Color);
			D3D11Device::SetClearColor(previousClearColor);

			std::uint8_t px0[Size * Size * 4] = {};
			std::uint8_t px1[Size * Size * 4] = {};
			tex0.GetTexImage(0, PixelFormat::RGBA8, false, px0);
			tex1.GetTexImage(0, PixelFormat::RGBA8, false, px1);
			bool clearOk = true;
			for (std::int32_t i = 0; i < Size * Size; i++) {
				clearOk = clearOk && (px0[i * 4 + 0] == 0 && px0[i * 4 + 1] == 0 && px0[i * 4 + 2] == 255 && px0[i * 4 + 3] == 255);
				clearOk = clearOk && (px1[i * 4 + 0] == 0 && px1[i * 4 + 1] == 0 && px1[i * 4 + 2] == 255 && px1[i * 4 + 3] == 255);
			}

			// Distinct per-attachment clears through the lazily created per-attachment RTVs
			ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
			ID3D11RenderTargetView* rtv0 = rt.GetRTV(0);
			ID3D11RenderTargetView* rtv1 = rt.GetRTV(1);
			if (context == nullptr || rtv0 == nullptr || rtv1 == nullptr) {
				LOGE("MRT probe FAILED: context={}, rtv0={}, rtv1={}", reinterpret_cast<std::uint64_t>(context),
					reinterpret_cast<std::uint64_t>(rtv0), reinterpret_cast<std::uint64_t>(rtv1));
				rt.UnbindDraw();
				return;
			}
			const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
			const float green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
			context->ClearRenderTargetView(rtv0, red);
			context->ClearRenderTargetView(rtv1, green);

			tex0.GetTexImage(0, PixelFormat::RGBA8, false, px0);
			tex1.GetTexImage(0, PixelFormat::RGBA8, false, px1);
			bool ok0 = true, ok1 = true;
			for (std::int32_t i = 0; i < Size * Size; i++) {
				ok0 = ok0 && (px0[i * 4 + 0] == 255 && px0[i * 4 + 1] == 0 && px0[i * 4 + 2] == 0 && px0[i * 4 + 3] == 255);
				ok1 = ok1 && (px1[i * 4 + 0] == 0 && px1[i * 4 + 1] == 255 && px1[i * 4 + 2] == 0 && px1[i * 4 + 3] == 255);
			}
			rt.UnbindDraw();

			if (clearOk && ok0 && ok1) {
				LOGI("MRT probe PASSED: multi-attachment Clear() OK, attachment 0 = red, attachment 1 = green (2-attachment bind / per-attachment RTVs / clears / readback verified)");
			} else {
				LOGE("MRT probe FAILED: multi-clear {}, attachment 0 {} (got {},{},{},{}), attachment 1 {} (got {},{},{},{})",
					clearOk ? "OK" : "WRONG", ok0 ? "OK" : "WRONG", px0[0], px0[1], px0[2], px0[3],
					ok1 ? "OK" : "WRONG", px1[0], px1[1], px1[2], px1[3]);
			}
		}
	}
#endif

	bool D3D11Device::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
		vsync_ = vsync;

		if (width <= 0) width = 1;
		if (height <= 0) height = 1;

		const D3D_FEATURE_LEVEL requestedLevels[] = {
			D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
		};
		const UINT numRequestedLevels = static_cast<UINT>(sizeof(requestedLevels) / sizeof(requestedLevels[0]));
		D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

#if defined(DEATH_TARGET_WINDOWS_RT)
		// -- UWP (Windows Store / Xbox) path --
		// UWP has no HWND: the window is a CoreWindow (passed here as an opaque IUnknown*). Create the device
		// first, then a *flip-model* swap chain bound to the CoreWindow via IDXGIFactory2::CreateSwapChainForCoreWindow.
		// UWP forbids the desktop bitblt DXGI_SWAP_EFFECT_DISCARD swap chain, so a flip-model swap chain
		// (FLIP_SEQUENTIAL, BufferCount 2) is mandatory. D3D11_CREATE_DEVICE_BGRA_SUPPORT keeps it interoperable
		// with Direct2D/DirectWrite and matches the ANGLE/UWP configuration.
		const UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
			requestedLevels, numRequestedLevels, D3D11_SDK_VERSION, &device_, &obtainedLevel, &context_);
		if (FAILED(hr)) {
			LOGW("D3D11CreateDevice (hardware) failed (0x{:.8x}), falling back to WARP", static_cast<std::uint32_t>(hr));
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
				requestedLevels, numRequestedLevels, D3D11_SDK_VERSION, &device_, &obtainedLevel, &context_);
		}
		if (FAILED(hr)) {
			LOGE("D3D11CreateDevice failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}

		// Fetch the DXGI factory that owns the device's adapter (needed for CreateSwapChainForCoreWindow)
		IDXGIDevice1* dxgiDevice = nullptr;
		hr = device_->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice));
		if (FAILED(hr) || dxgiDevice == nullptr) {
			LOGE("Failed to query IDXGIDevice1 from the Direct3D 11 device: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}
		// Cap the number of queued frames to reduce latency (Windows Store certification recommendation)
		dxgiDevice->SetMaximumFrameLatency(1);

		IDXGIAdapter* adapter = nullptr;
		IDXGIFactory2* factory = nullptr;
		if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
			adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
		}
		SafeRelease(adapter);
		SafeRelease(dxgiDevice);
		if (factory == nullptr) {
			LOGE("Failed to obtain the DXGI factory for the CoreWindow swap chain");
			DestroySwapchain();
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width = static_cast<UINT>(width);
		sd.Height = static_cast<UINT>(height);
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Stereo = FALSE;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;									// flip-model requires at least two buffers
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;	// flip-model (bitblt DISCARD is illegal on UWP)
		sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		sd.Flags = 0;

		IDXGISwapChain1* swapchain1 = nullptr;
		hr = factory->CreateSwapChainForCoreWindow(device_, reinterpret_cast<IUnknown*>(windowHandle), &sd, nullptr, &swapchain1);
		SafeRelease(factory);
		if (FAILED(hr) || swapchain1 == nullptr) {
			LOGE("IDXGIFactory2::CreateSwapChainForCoreWindow() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}
		swapchain_ = swapchain1;							// IDXGISwapChain1 derives from IDXGISwapChain
#else
		// -- Desktop (SDL2 HWND) path: an ordinary windowed bitblt swap chain created from the native HWND --
		DXGI_SWAP_CHAIN_DESC sd = {};
		sd.BufferCount = 1;
		sd.BufferDesc.Width = static_cast<UINT>(width);
		sd.BufferDesc.Height = static_cast<UINT>(height);
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 0;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = reinterpret_cast<HWND>(windowHandle);
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		sd.Flags = 0;

		HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			requestedLevels, numRequestedLevels, D3D11_SDK_VERSION,
			&sd, &swapchain_, &device_, &obtainedLevel, &context_);

		if (FAILED(hr)) {
			LOGW("D3D11CreateDeviceAndSwapChain (hardware) failed (0x{:.8x}), falling back to WARP", static_cast<std::uint32_t>(hr));
			hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				requestedLevels, numRequestedLevels, D3D11_SDK_VERSION,
				&sd, &swapchain_, &device_, &obtainedLevel, &context_);
		}

		if (FAILED(hr)) {
			LOGE("D3D11CreateDeviceAndSwapChain failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}
#endif

		// The real texture-dimension limit of the obtained feature level: D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
		// (16384) on 11_x, 8192 (the D3D10_REQ_ equivalent) on the 10.x fallback levels
		maxTextureDimension_ = (obtainedLevel >= D3D_FEATURE_LEVEL_11_0 ? D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION : 8192);

		if (!CreateBackbufferRtv()) {
			DestroySwapchain();
			return false;
		}

		backbufferWidth_ = width;
		backbufferHeight_ = height;
		CreatePresentResources(width, height);

		SetViewport(Recti(0, 0, width, height));

		LOGI("Direct3D 11 device created (feature level {}.{}), {}x{} swap chain",
			(static_cast<int>(obtainedLevel) >> 12) & 0xF, (static_cast<int>(obtainedLevel) >> 8) & 0xF, width, height);

#if defined(D3D11_MRT_PROBE)
		RunMrtProbe();
#endif
		return true;
	}

	bool D3D11Device::CreateBackbufferRtv()
	{
		if (device_ == nullptr || swapchain_ == nullptr) {
			return false;
		}

		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
		if (FAILED(hr) || backBuffer == nullptr) {
			LOGE("IDXGISwapChain::GetBuffer() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return false;
		}

		hr = device_->CreateRenderTargetView(backBuffer, nullptr, &backbufferRtv_);
		backBuffer->Release();
		if (FAILED(hr)) {
			LOGE("ID3D11Device::CreateRenderTargetView() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return false;
		}

		context_->OMSetRenderTargets(1, &backbufferRtv_, nullptr);
		InvalidateCachedState();
		return true;
	}

	void D3D11Device::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		if (swapchain_ == nullptr || context_ == nullptr || width <= 0 || height <= 0) {
			return;
		}

		context_->OMSetRenderTargets(0, nullptr, nullptr);
		SafeRelease(backbufferRtv_);

		HRESULT hr = swapchain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr)) {
			LOGE("IDXGISwapChain::ResizeBuffers() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return;
		}

		CreateBackbufferRtv();
		backbufferWidth_ = width;
		backbufferHeight_ = height;
		CreatePresentResources(width, height);
		SetViewport(Recti(0, 0, width, height));
	}

	bool D3D11Device::CreatePresentResources(std::int32_t width, std::int32_t height)
	{
		if (device_ == nullptr || width <= 0 || height <= 0) {
			return false;
		}

		ReleasePresentResources();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device_->CreateTexture2D(&desc, nullptr, &presentTexture_))) {
			return false;
		}
		device_->CreateRenderTargetView(presentTexture_, nullptr, &presentRtv_);
		device_->CreateShaderResourceView(presentTexture_, nullptr, &presentSrv_);

		// Compile the flip-blit shaders and sampler once (kept across resizes)
		if (presentVs_ == nullptr || presentPs_ == nullptr) {
			ID3DBlob* vsBlob = nullptr;
			ID3DBlob* psBlob = nullptr;
			if (SUCCEEDED(D3DCompile(kPresentVs, std::strlen(kPresentVs), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsBlob, nullptr)) &&
				SUCCEEDED(D3DCompile(kPresentPs, std::strlen(kPresentPs), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psBlob, nullptr))) {
				device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &presentVs_);
				device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &presentPs_);
			} else {
				LOGE("Failed to compile the Direct3D 11 present flip-blit shaders");
			}
			SafeRelease(vsBlob);
			SafeRelease(psBlob);
		}
		if (presentSampler_ == nullptr) {
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			device_->CreateSamplerState(&sd, &presentSampler_);
		}
		return true;
	}

	void D3D11Device::ReleasePresentResources()
	{
		SafeRelease(presentSrv_);
		SafeRelease(presentRtv_);
		SafeRelease(presentTexture_);
	}

	void D3D11Device::PresentFrame()
	{
		// Every draw was rendered GL-bottom-up (clip-space Y flipped in the projection) into the present texture;
		// flip-blit it vertically into the real back-buffer, then present. This is the single GL->D3D scan-out
		// correction (the software backend's SDL_FLIP_VERTICAL equivalent), applied only at the final output so it
		// is uniform across every path regardless of how many off-screen round-trips it made.
		if (context_ != nullptr && backbufferRtv_ != nullptr && presentSrv_ != nullptr &&
			presentVs_ != nullptr && presentPs_ != nullptr) {
			context_->OMSetRenderTargets(1, &backbufferRtv_, nullptr);
			D3D11_VIEWPORT vp;
			vp.TopLeftX = 0.0f;
			vp.TopLeftY = 0.0f;
			vp.Width = static_cast<float>(backbufferWidth_);
			vp.Height = static_cast<float>(backbufferHeight_);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			context_->RSSetViewports(1, &vp);
			context_->IASetInputLayout(nullptr);
			context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context_->RSSetState(nullptr);
			context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			context_->VSSetShader(presentVs_, nullptr, 0);
			context_->PSSetShader(presentPs_, nullptr, 0);
			context_->PSSetShaderResources(0, 1, &presentSrv_);
			context_->PSSetSamplers(0, 1, &presentSampler_);
			context_->Draw(3, 0);
			// Unbind so the present texture can be bound as a render target again next frame (no read/write hazard)
			ID3D11ShaderResourceView* nullSrv = nullptr;
			context_->PSSetShaderResources(0, 1, &nullSrv);
			// The blit bound its own shaders/state/target directly; drop the shadow state so the next frame's
			// first draw re-issues everything
			InvalidateCachedState();
		}
		if (swapchain_ != nullptr) {
			swapchain_->Present(vsync_ ? 1 : 0, 0);
		}
	}

	void D3D11Device::ReleasePipelineObjects()
	{
		for (PooledCBuffer& e : cbufferPool_) {
			SafeRelease(e.Buffer);
		}
		cbufferPool_.clear();
		for (BlendStateEntry& e : blendStates_) {
			SafeRelease(e.State);
		}
		blendStates_.clear();
		for (RasterStateEntry& e : rasterStates_) {
			SafeRelease(e.State);
		}
		rasterStates_.clear();
		SafeRelease(depthDisabledState_);
	}

	void D3D11Device::DestroySwapchain()
	{
		if (context_ != nullptr) {
			context_->OMSetRenderTargets(0, nullptr, nullptr);
			context_->ClearState();
			context_->Flush();
		}
		InvalidateCachedState();
		ReleasePipelineObjects();
		ReleasePresentResources();
		SafeRelease(presentSampler_);
		SafeRelease(presentPs_);
		SafeRelease(presentVs_);
		SafeRelease(backbufferRtv_);
		SafeRelease(swapchain_);
		SafeRelease(context_);
		SafeRelease(device_);
	}

	ID3D11Device* D3D11Device::GetD3DDevice() { return device_; }
	ID3D11DeviceContext* D3D11Device::GetD3DContext() { return context_; }

	std::int32_t D3D11Device::GetMaxTextureDimension() { return maxTextureDimension_; }
}
