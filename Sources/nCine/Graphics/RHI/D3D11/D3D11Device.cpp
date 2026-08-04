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

namespace nCine::RHI::D3D11
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

	D3D11Device::BlendingState D3D11Device::_blending;
	D3D11Device::DepthTestState D3D11Device::_depthTest;
	D3D11Device::CullFaceState D3D11Device::_cullFace;
	D3D11Device::ScissorState D3D11Device::_scissor;
	Recti D3D11Device::_viewport(0, 0, 0, 0);
	Colorf D3D11Device::_clearColor(0.0f, 0.0f, 0.0f, 0.0f);

	D3D11ShaderProgram* D3D11Device::_currentProgram = nullptr;
	const D3D11Texture* D3D11Device::_boundTextures[MaxTextureUnits] = {};
	D3D11Device::UniformRange D3D11Device::_boundUniformRanges[MaxUniformBindings] = {};
	D3D11RenderTarget* D3D11Device::_currentRenderTarget = nullptr;

	ID3D11Device* D3D11Device::_device = nullptr;
	ID3D11DeviceContext* D3D11Device::_context = nullptr;
	IDXGISwapChain* D3D11Device::_swapchain = nullptr;
	ID3D11RenderTargetView* D3D11Device::_backbufferRtv = nullptr;
	bool D3D11Device::_vsync = true;
	std::uint32_t D3D11Device::_swapchainFlags = 0;
	std::int32_t D3D11Device::_backbufferWidth = 0;
	std::int32_t D3D11Device::_backbufferHeight = 0;
	std::int32_t D3D11Device::_maxTextureDimension = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	ID3D11Texture2D* D3D11Device::_presentTexture = nullptr;
	ID3D11RenderTargetView* D3D11Device::_presentRtv = nullptr;
	ID3D11ShaderResourceView* D3D11Device::_presentSrv = nullptr;
	ID3D11VertexShader* D3D11Device::_presentVs = nullptr;
	ID3D11PixelShader* D3D11Device::_presentPs = nullptr;
	ID3D11SamplerState* D3D11Device::_presentSampler = nullptr;
	ID3D11RenderTargetView* D3D11Device::_secondaryTargetRtv = nullptr;
	std::int32_t D3D11Device::_secondaryTargetHeight = 0;

	SmallVector<D3D11Device::PooledCBuffer, 0> D3D11Device::_cbufferPool;
	SmallVector<std::uint8_t, 0> D3D11Device::_cbufferStaging;
	SmallVector<D3D11Device::BlendStateEntry, 8> D3D11Device::_blendStates;
	SmallVector<D3D11Device::RasterStateEntry, 8> D3D11Device::_rasterStates;
	ID3D11DepthStencilState* D3D11Device::_depthDisabledState = nullptr;

	ID3D11BlendState* D3D11Device::_lastBlendState = nullptr;
	ID3D11RasterizerState* D3D11Device::_lastRasterState = nullptr;
	bool D3D11Device::_depthStateApplied = false;
	ID3D11VertexShader* D3D11Device::_lastVs = nullptr;
	ID3D11PixelShader* D3D11Device::_lastPs = nullptr;
	std::uint32_t D3D11Device::_lastTopology = 0;
	ID3D11RenderTargetView* D3D11Device::_lastRtvs[D3D11Device::MaxRenderTargets] = {};
	std::uint32_t D3D11Device::_lastRtvCount = 0;
	bool D3D11Device::_lastRtvValid = false;
	Recti D3D11Device::_lastViewport(0, 0, 0, 0);
	bool D3D11Device::_lastViewportValid = false;
	D3D11Device::CachedRect D3D11Device::_lastScissorRect;
	bool D3D11Device::_lastScissorValid = false;
	ID3D11ShaderResourceView* D3D11Device::_lastSrvs[2][D3D11Device::MaxTextureUnits] = {};
	ID3D11SamplerState* D3D11Device::_lastSamplers[2][D3D11Device::MaxTextureUnits] = {};
	bool D3D11Device::_srvShadowValid = false;

	void D3D11Device::InvalidateCachedState()
	{
		_lastBlendState = nullptr;
		_lastRasterState = nullptr;
		_depthStateApplied = false;
		_lastVs = nullptr;
		_lastPs = nullptr;
		_lastTopology = 0;
		std::memset(_lastRtvs, 0, sizeof(_lastRtvs));
		_lastRtvCount = 0;
		_lastRtvValid = false;
		_lastViewportValid = false;
		_lastScissorValid = false;
		std::memset(_lastSrvs, 0, sizeof(_lastSrvs));
		std::memset(_lastSamplers, 0, sizeof(_lastSamplers));
		_srvShadowValid = false;
	}

	// -- Pipeline state (recorded) --

	void D3D11Device::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }

	void D3D11Device::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}

	D3D11Device::BlendingState D3D11Device::GetBlendingState() { return _blending; }
	void D3D11Device::SetBlendingState(const BlendingState& state) { _blending = state; }

	void D3D11Device::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void D3D11Device::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	D3D11Device::DepthTestState D3D11Device::GetDepthTestState() { return _depthTest; }
	void D3D11Device::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void D3D11Device::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	D3D11Device::CullFaceState D3D11Device::GetCullFaceState() { return _cullFace; }
	void D3D11Device::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	D3D11Device::ScissorState D3D11Device::GetScissorState() { return _scissor; }
	void D3D11Device::SetScissorState(const ScissorState& state) { _scissor = state; }
	void D3D11Device::SetScissor(const Recti& rect) { _scissor.Enabled = true; _scissor.Rect = rect; }
	void D3D11Device::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti D3D11Device::GetViewport() { return _viewport; }

	void D3D11Device::SetViewport(const Recti& rect)
	{
		_viewport = rect;
		if (_context != nullptr) {
			D3D11_VIEWPORT vp = MakeViewport(rect);
			_context->RSSetViewports(1, &vp);
			_lastViewport = rect;
			_lastViewportValid = true;
		}
	}

	void D3D11Device::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf D3D11Device::GetClearColor() { return _clearColor; }
	void D3D11Device::SetClearColor(const Colorf& color) { _clearColor = color; }

	void D3D11Device::Clear(ClearFlags flags)
	{
		if (_context == nullptr) {
			return;
		}
		if ((flags & ClearFlags::Color) != ClearFlags::None) {
			const float c[4] = { _clearColor.R, _clearColor.G, _clearColor.B, _clearColor.A };
			if (_currentRenderTarget != nullptr) {
				// The clear covers every bound color attachment (the contiguous attached run, bounded by the
				// draw-buffer count), matching glClear's semantics of clearing all enabled draw buffers
				ID3D11RenderTargetView* rtvs[D3D11RenderTarget::MaxColorAttachments];
				const std::uint32_t numRtvs = _currentRenderTarget->GetRTVs(rtvs);
				for (std::uint32_t i = 0; i < numRtvs; i++) {
					_context->ClearRenderTargetView(rtvs[i], c);
				}
			} else {
				ID3D11RenderTargetView* screenRtv = ScreenRtv();
				if (screenRtv != nullptr) {
					_context->ClearRenderTargetView(screenRtv, c);
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
		D3D11ShaderProgram* prog = _currentProgram;
		if (_context == nullptr || prog == nullptr || prog->GetVertexShader() == nullptr || count == 0) {
			return;
		}

		BindCurrentRenderTarget();

		// The viewport is always positive/top-down; the GL<->D3D vertical flip is applied uniformly in the vertex
		// transform (projection matrix Y negated in BindConstantBuffers), which flips every draw to every target
		// consistently - so back-buffer geometry and off-screen render targets stay in agreement with no present-flip.
		if (!_lastViewportValid || _lastViewport != _viewport) {
			D3D11_VIEWPORT vp = MakeViewport(_viewport);
			_context->RSSetViewports(1, &vp);
			_lastViewport = _viewport;
			_lastViewportValid = true;
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
			_context->IASetInputLayout(layout);
			_context->IASetVertexBuffers(0, 1, &vb, &strideU, &vbOffset);
			if (indexed) {
				const D3D11BufferObject* ibo = prog->GetBoundIbo();
				ID3D11Buffer* ib = (ibo != nullptr ? ibo->GetD3DBuffer() : nullptr);
				if (ib == nullptr) {
					return;
				}
				_context->IASetIndexBuffer(ib, indexFormat == IndexFormat::UInt32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
			}
		} else {
			_context->IASetInputLayout(nullptr);
		}

		const D3D11_PRIMITIVE_TOPOLOGY topology = MapPrimitive(primitive);
		if (_lastTopology != std::uint32_t(topology)) {
			_context->IASetPrimitiveTopology(topology);
			_lastTopology = std::uint32_t(topology);
		}
		if (_lastVs != prog->GetVertexShader()) {
			_lastVs = prog->GetVertexShader();
			_context->VSSetShader(_lastVs, nullptr, 0);
		}
		if (_lastPs != prog->GetPixelShader()) {
			_lastPs = prog->GetPixelShader();
			_context->PSSetShader(_lastPs, nullptr, 0);
		}
		BindConstantBuffers();
		BindTextures();
		ApplyRenderState();

		const UINT indexSize = (indexFormat == IndexFormat::UInt32 ? 4u : 2u);
		if (indexed) {
			if (numInstances > 1) {
				_context->DrawIndexedInstanced(count, std::uint32_t(numInstances), static_cast<UINT>(indexOffset / indexSize), baseVertex, 0);
			} else {
				_context->DrawIndexed(count, static_cast<UINT>(indexOffset / indexSize), baseVertex);
			}
		} else if (numInstances > 1) {
			_context->DrawInstanced(count, std::uint32_t(numInstances), std::uint32_t(firstVertex), 0);
		} else {
			_context->Draw(count, std::uint32_t(firstVertex));
		}
	}

	ID3D11RenderTargetView* D3D11Device::ScreenRtv()
	{
		// "Screen" is the intermediate present texture (flip-blitted into the back-buffer at present time), or the
		// back-buffer itself if that texture is missing - unless drawing is currently redirected into a secondary
		// swap chain, whose back-buffer then stands in for the screen target
		if (_secondaryTargetRtv != nullptr) {
			return _secondaryTargetRtv;
		}
		return (_presentRtv != nullptr ? _presentRtv : _backbufferRtv);
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
		if (_currentRenderTarget != nullptr) {
			numRtvs = _currentRenderTarget->GetRTVs(rtvs);
			if (numRtvs == 0) {
				// Attachment 0 unusable: keep the historical behavior of explicitly binding a null target
				numRtvs = 1;
			}
		} else {
			rtvs[0] = ScreenRtv();
			numRtvs = 1;
		}
		if (_lastRtvValid && _lastRtvCount == numRtvs &&
			std::memcmp(_lastRtvs, rtvs, numRtvs * sizeof(ID3D11RenderTargetView*)) == 0) {
			return;
		}

		// Read/write hazard guard: if a texture becoming a render target attachment is still bound as an SRV
		// from an earlier pass, unbind it explicitly. Without this the runtime silently nulls the SRV slot at
		// draw time, which would desync the SRV shadow cache (it would still believe the SRV is bound and skip
		// a later rebind). Covers every bound color attachment, not just attachment 0.
		if (_currentRenderTarget != nullptr) {
			ID3D11ShaderResourceView* nullSrv = nullptr;
			for (std::uint32_t a = 0; a < numRtvs; a++) {
				const D3D11Texture* rtTex = _currentRenderTarget->GetColorTexture(a);
				ID3D11ShaderResourceView* rtSrv = (rtTex != nullptr ? rtTex->GetSRV() : nullptr);
				if (rtSrv == nullptr) {
					continue;
				}
				for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
					if (_lastSrvs[0][u] == rtSrv) {
						_context->PSSetShaderResources(u, 1, &nullSrv);
						_lastSrvs[0][u] = nullptr;
					}
					if (_lastSrvs[1][u] == rtSrv) {
						_context->VSSetShaderResources(u, 1, &nullSrv);
						_lastSrvs[1][u] = nullptr;
					}
				}
			}
		}

		_context->OMSetRenderTargets(numRtvs, rtvs, nullptr);
		std::memcpy(_lastRtvs, rtvs, sizeof(_lastRtvs));
		_lastRtvCount = numRtvs;
		_lastRtvValid = true;
	}

	std::uint32_t D3D11Device::AcquireConstantBuffer(std::uint32_t size)
	{
		std::uint32_t size16 = (size + 15u) & ~15u;
		if (size16 == 0) {
			size16 = 16;
		}
		if (s_cbufferCursor >= _cbufferPool.size()) {
			_cbufferPool.push_back(PooledCBuffer{});
		}
		PooledCBuffer& entry = _cbufferPool[s_cbufferCursor];
		if (entry.Buffer == nullptr || entry.Size < size16) {
			SafeRelease(entry.Buffer);
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = size16;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(_device->CreateBuffer(&desc, nullptr, &entry.Buffer))) {
				entry.Buffer = nullptr;
			}
			entry.Size = size16;
			entry.ContentSize = 0;	// fresh buffer: its previous contents are gone, so the skip cache is stale
		}
		return s_cbufferCursor++;
	}

	void D3D11Device::BindConstantBuffers()
	{
		D3D11ShaderProgram* prog = _currentProgram;
		s_cbufferCursor = 0;

		auto buildAndBind = [&](const CBufferSlotList& slots, bool vertexStage) {
			for (const D3D11CBufferSlot& slot : slots) {
				std::uint32_t uploadSize = slot.ByteSize;
				// Assemble the exact bytes to upload into one contiguous span (`srcBytes`), so they can be
				// hashed against the pool buffer's current contents and copied in a single memcpy.
				const std::uint8_t* srcBytes = nullptr;
				if (slot.IsGlobals) {
					// Loose uniforms are scattered across the program's resolved-value pointers, so gather them
					// (plus the projection Y-flip) into the reusable staging buffer first.
					if (_cbufferStaging.size() < uploadSize) {
						_cbufferStaging.resize(uploadSize);
					}
					std::uint8_t* dst = _cbufferStaging.data();
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
							// The exception is a secondary swap chain (an ImGui platform window): it is presented
							// directly, with no flip-blit to undo the flip, so it is drawn top-down as D3D expects.
							if (_secondaryTargetHeight == 0 && gv.Size >= 64 && (gv.Name == "uProjectionMatrix" || gv.Name == "uGuiProjection")) {
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
					const std::uint8_t* blockData = _boundUniformRanges[slot.BlockIndex].Data;
					const std::uint32_t blockSize = _boundUniformRanges[slot.BlockIndex].Size;
					if (blockData != nullptr) {
						// A uniform block is bound with exactly the bytes the shader reads (valid instances), so a
						// buffer sized to the uploaded range is enough and avoids allocating the full declared cbuffer
						if (blockSize > 0 && blockSize < uploadSize) {
							uploadSize = blockSize;
						}
						srcBytes = blockData;
					} else {
						if (_cbufferStaging.size() < uploadSize) {
							_cbufferStaging.resize(uploadSize);
						}
						std::memset(_cbufferStaging.data(), 0, uploadSize);
						srcBytes = _cbufferStaging.data();
					}
				}

				const std::uint32_t poolIndex = AcquireConstantBuffer(uploadSize);
				PooledCBuffer& entry = _cbufferPool[poolIndex];
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
					if (FAILED(_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
						continue;
					}
					std::memcpy(mapped.pData, srcBytes, uploadSize);
					_context->Unmap(cb, 0);
					entry.ContentHash = hash;
					entry.ContentSize = uploadSize;
				}

				if (vertexStage) {
					_context->VSSetConstantBuffers(slot.Register, 1, &cb);
				} else {
					_context->PSSetConstantBuffers(slot.Register, 1, &cb);
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
		const D3D11ShaderProgram* prog = _currentProgram;
		const std::uint32_t psMask = prog->GetPsTextureMask();
		const std::uint32_t vsMask = prog->GetVsTextureMask();
		const D3D11Texture* rtTexs[D3D11RenderTarget::MaxColorAttachments];
		std::uint32_t numRtTexs = 0;
		if (_currentRenderTarget != nullptr) {
			std::uint32_t boundCount = _currentRenderTarget->GetAttachedCount();
			const std::uint32_t numDrawBuffers = _currentRenderTarget->GetNumDrawBuffers();
			if (numDrawBuffers > 0 && numDrawBuffers < boundCount) {
				boundCount = numDrawBuffers;
			}
			for (std::uint32_t a = 0; a < boundCount; a++) {
				const D3D11Texture* rtTex = _currentRenderTarget->GetColorTexture(a);
				if (rtTex != nullptr) {
					rtTexs[numRtTexs++] = rtTex;
				}
			}
		}
		const bool force = !_srvShadowValid;

		std::uint32_t mask = psMask | vsMask;
		for (std::uint32_t u = 0; u < MaxTextureUnits; u++) {
			if ((mask & (1u << u)) == 0 && !force) {
				continue;
			}
			const D3D11Texture* tex = _boundTextures[u];
			for (std::uint32_t a = 0; a < numRtTexs; a++) {
				if (tex == rtTexs[a]) {
					tex = nullptr;
					break;
				}
			}
			ID3D11ShaderResourceView* srv = (tex != nullptr ? tex->GetSRV() : nullptr);
			ID3D11SamplerState* samp = (tex != nullptr ? tex->GetSampler() : nullptr);
			if (force || _lastSrvs[0][u] != srv) {
				_context->PSSetShaderResources(u, 1, &srv);
				_lastSrvs[0][u] = srv;
			}
			if (force || _lastSamplers[0][u] != samp) {
				_context->PSSetSamplers(u, 1, &samp);
				_lastSamplers[0][u] = samp;
			}
			if (force || _lastSrvs[1][u] != srv) {
				_context->VSSetShaderResources(u, 1, &srv);
				_lastSrvs[1][u] = srv;
			}
			if (force || _lastSamplers[1][u] != samp) {
				_context->VSSetSamplers(u, 1, &samp);
				_lastSamplers[1][u] = samp;
			}
		}
		_srvShadowValid = true;
	}

	void D3D11Device::ApplyRenderState()
	{
		// Blend state (keyed on enabled + the four factors)
		{
			std::uint64_t key = (_blending.Enabled ? 1u : 0u);
			key |= std::uint64_t(BlendCode(_blending.SrcRgb)) << 1;
			key |= std::uint64_t(BlendCode(_blending.DstRgb)) << 5;
			key |= std::uint64_t(BlendCode(_blending.SrcAlpha)) << 9;
			key |= std::uint64_t(BlendCode(_blending.DstAlpha)) << 13;
			ID3D11BlendState* state = nullptr;
			for (const BlendStateEntry& e : _blendStates) {
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
				desc.RenderTarget[0].BlendEnable = _blending.Enabled ? TRUE : FALSE;
				desc.RenderTarget[0].SrcBlend = MapBlend(_blending.SrcRgb, false);
				desc.RenderTarget[0].DestBlend = MapBlend(_blending.DstRgb, false);
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = MapBlend(_blending.SrcAlpha, true);
				desc.RenderTarget[0].DestBlendAlpha = MapBlend(_blending.DstAlpha, true);
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				if (SUCCEEDED(_device->CreateBlendState(&desc, &state))) {
					_blendStates.push_back({ key, state });
				}
			}
			if (state != _lastBlendState || state == nullptr) {
				const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				_context->OMSetBlendState(state, blendFactor, 0xFFFFFFFF);
				_lastBlendState = state;
			}
		}

		// Depth-stencil: the renderer is 2D and no depth buffer is attached, so depth is always disabled
		if (_depthDisabledState == nullptr) {
			D3D11_DEPTH_STENCIL_DESC desc = {};
			desc.DepthEnable = FALSE;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
			desc.StencilEnable = FALSE;
			_device->CreateDepthStencilState(&desc, &_depthDisabledState);
			_depthStateApplied = false;
		}
		if (!_depthStateApplied) {
			_context->OMSetDepthStencilState(_depthDisabledState, 0);
			_depthStateApplied = true;
		}

		// Rasterizer (keyed on cull enabled + mode + scissor enabled)
		{
			std::uint32_t key = (_cullFace.Enabled ? 1u : 0u) | (std::uint32_t(_cullFace.Mode) << 1) | (_scissor.Enabled ? 0x10000u : 0u);
			ID3D11RasterizerState* state = nullptr;
			for (const RasterStateEntry& e : _rasterStates) {
				if (e.Key == key) {
					state = e.State;
					break;
				}
			}
			if (state == nullptr) {
				D3D11_RASTERIZER_DESC desc = {};
				desc.FillMode = D3D11_FILL_SOLID;
				if (!_cullFace.Enabled) {
					desc.CullMode = D3D11_CULL_NONE;
				} else {
					desc.CullMode = (_cullFace.Mode == CullFaceMode::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
				}
				desc.FrontCounterClockwise = TRUE;	// GL default winding
				// A command's layer is turned into a clip-space Z spanning the camera's near..far planes, which are
				// GL's [-1,1] - so the upper half of the 16-bit layer range lands on a negative Z. D3D clips against
				// [0,w] (unlike GL's [-w,w]), which would silently discard every draw on a layer above ~32767 (the
				// splitscreen HUD overlay composite, for one). Skipping Z from the clip test keeps the whole layer
				// range renderable; there is nothing to lose, the renderer is 2D and never attaches a depth buffer
				// (see the always-disabled depth-stencil state above). Mirrors the Vulkan backend's depth clamp.
				desc.DepthClipEnable = FALSE;
				desc.ScissorEnable = _scissor.Enabled ? TRUE : FALSE;
				if (SUCCEEDED(_device->CreateRasterizerState(&desc, &state))) {
					_rasterStates.push_back({ key, state });
				}
			}
			if (state != _lastRasterState || state == nullptr) {
				_context->RSSetState(state);
				_lastRasterState = state;
			}
		}

		// Scissor rectangle. The engine specifies it in GL window space (bottom-left origin), and every draw is
		// rasterized bottom-up because clip-space Y is flipped in the projection (see BindConstantBuffers), so
		// virtually every surface drawn into stores its rows bottom-up: off-screen render targets and equally the
		// intermediate present texture the "screen" path is redirected to. A GL Y therefore maps straight to a
		// D3D row index (top = glY) - the correction is PresentFrame()'s flip-blit. The real back-buffer is
		// top-down but is never scissored into (that blit runs with the default rasterizer state, which has
		// scissoring disabled); a secondary swap chain, drawn top-down because it skips the flip-blit, is not.
		if (_scissor.Enabled) {
			const Recti& r = _scissor.Rect;
			// The one top-down surface is a secondary swap chain's back-buffer (see _secondaryTargetHeight), whose
			// rows therefore need the standard flip against the target height
			std::int32_t top = r.Y;
			if (_secondaryTargetHeight > 0) {
				top = _secondaryTargetHeight - (r.Y + r.H);
			}
			D3D11_RECT sr;
			// A rectangle may reach outside the target (an ImGui window dragged past the left/top edge gives
			// negative coordinates); D3D11 rejects negative scissor bounds outright, which would leave the
			// previous rectangle in effect, so clamp instead - oversized bounds the rasterizer handles itself
			sr.left = (r.X > 0 ? r.X : 0);
			sr.top = (top > 0 ? top : 0);
			sr.right = (r.X + r.W > sr.left ? r.X + r.W : sr.left);
			sr.bottom = (top + r.H > sr.top ? top + r.H : sr.top);
			if (!_lastScissorValid || _lastScissorRect.L != sr.left || _lastScissorRect.T != sr.top ||
				_lastScissorRect.R != sr.right || _lastScissorRect.B != sr.bottom) {
				_context->RSSetScissorRects(1, &sr);
				_lastScissorRect = { sr.left, sr.top, sr.right, sr.bottom };
				_lastScissorValid = true;
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
		_blending = BlendingState{};
		_depthTest = DepthTestState{};
		_cullFace = CullFaceState{};
		_scissor = ScissorState{};
	}

	// -- Backend extensions (recorders) --

	void D3D11Device::BindProgram(D3D11ShaderProgram* program) { _currentProgram = program; }
	D3D11ShaderProgram* D3D11Device::CurrentProgram() { return _currentProgram; }

	void D3D11Device::BindTexture(std::uint32_t unit, const D3D11Texture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void D3D11Device::UnbindTexture(const D3D11Texture* texture)
	{
		// Called from ~D3D11Texture so a destroyed texture never lingers as a dangling pointer in the bound-texture
		// tracking (a later draw would then dereference freed memory in BindTextures - crashed during level changes
		// in splitscreen). Clear every unit it may be bound to. Only touches the static array (no D3D calls), so it
		// is safe at any time including shutdown. Mirrors GLTexture's destructor.
		for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
			if (_boundTextures[unit] == texture) {
				_boundTextures[unit] = nullptr;
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
					if (srv != nullptr && _lastSrvs[stage][unit] == srv) {
						_lastSrvs[stage][unit] = nullptr;
						_srvShadowValid = false;
					}
					if (samp != nullptr && _lastSamplers[stage][unit] == samp) {
						_lastSamplers[stage][unit] = nullptr;
						_srvShadowValid = false;
					}
				}
			}
		}
	}

	const D3D11Texture* D3D11Device::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void D3D11Device::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void D3D11Device::SetRenderTarget(D3D11RenderTarget* renderTarget)
	{
		_currentRenderTarget = renderTarget;
		if (_context != nullptr) {
			BindCurrentRenderTarget();
		}
	}

	void D3D11Device::UnbindRenderTarget(const D3D11RenderTarget* renderTarget)
	{
		// Called from ~D3D11RenderTarget so a destroyed render target can't dangle as _currentRenderTarget (a later
		// Clear/scissor/draw dereferences it). Reverts to the screen (nullptr); the pipeline binds a fresh target
		// before drawing to it. Only touches the static pointer (no D3D calls), so it is safe at any time.
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		// The RTVs are released with the target; forget them so a recycled pointer can't match the shadow
		std::memset(_lastRtvs, 0, sizeof(_lastRtvs));
		_lastRtvCount = 0;
		_lastRtvValid = false;
	}

	void D3D11Device::OnRtvReleased(const ID3D11RenderTargetView* rtv)
	{
		// Called by D3D11RenderTarget whenever it releases one of its views (attachment change, lazy rebuild,
		// destruction). If the dying view is part of the last-bound shadow, forget the whole set so a new RTV
		// recycling the same pointer can't be mistaken for still bound (the next draw re-issues the bind).
		// Only touches the static shadow (no D3D calls), so it is safe at any time including shutdown.
		if (_lastRtvValid && rtv != nullptr) {
			for (std::uint32_t i = 0; i < _lastRtvCount; i++) {
				if (_lastRtvs[i] == rtv) {
					std::memset(_lastRtvs, 0, sizeof(_lastRtvs));
					_lastRtvCount = 0;
					_lastRtvValid = false;
					break;
				}
			}
		}
	}

	void D3D11Device::OnProgramDestroyed(const D3D11ShaderProgram* program)
	{
		if (_currentProgram == program) {
			_currentProgram = nullptr;
		}
		if (program != nullptr) {
			if (_lastVs != nullptr && _lastVs == program->GetVertexShader()) {
				_lastVs = nullptr;
			}
			if (_lastPs != nullptr && _lastPs == program->GetPixelShader()) {
				_lastPs = nullptr;
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
		_vsync = vsync;
		_swapchainFlags = 0;	// negotiated below on the desktop path; the UWP CoreWindow chain carries no flags

		if (width <= 0) width = 1;
		if (height <= 0) height = 1;

		const D3D_FEATURE_LEVEL requestedLevels[] = {
			D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
		};
		const UINT numRequestedLevels = static_cast<UINT>(sizeof(requestedLevels) / sizeof(requestedLevels[0]));
		D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

		// Presentation model of the swap chain, reported in the one summary trace at the end. The UWP CoreWindow
		// chain below is unconditionally a two-buffer flip-model one with no tearing flag; the desktop path
		// negotiates its model and overwrites these.
		const char* swapchainModel = "flip";
		std::uint32_t swapchainBuffers = 2;
		bool swapchainTearing = false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		// -- UWP (Windows Store / Xbox) path --
		// UWP has no HWND: the window is a CoreWindow (passed here as an opaque IUnknown*). Create the device
		// first, then a *flip-model* swap chain bound to the CoreWindow via IDXGIFactory2::CreateSwapChainForCoreWindow.
		// UWP forbids the desktop bitblt DXGI_SWAP_EFFECT_DISCARD swap chain, so a flip-model swap chain
		// (FLIP_SEQUENTIAL, BufferCount 2) is mandatory. D3D11_CREATE_DEVICE_BGRA_SUPPORT keeps it interoperable
		// with Direct2D/DirectWrite and matches the ANGLE/UWP configuration.
		const UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
			requestedLevels, numRequestedLevels, D3D11_SDK_VERSION, &_device, &obtainedLevel, &_context);
		if (FAILED(hr)) {
			LOGW("D3D11CreateDevice (hardware) failed (0x{:.8x}), falling back to WARP", static_cast<std::uint32_t>(hr));
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
				requestedLevels, numRequestedLevels, D3D11_SDK_VERSION, &_device, &obtainedLevel, &_context);
		}
		if (FAILED(hr)) {
			LOGE("D3D11CreateDevice failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}

		// Fetch the DXGI factory that owns the device's adapter (needed for CreateSwapChainForCoreWindow)
		IDXGIDevice1* dxgiDevice = nullptr;
		hr = _device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice));
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
		hr = factory->CreateSwapChainForCoreWindow(_device, reinterpret_cast<IUnknown*>(windowHandle), &sd, nullptr, &swapchain1);
		SafeRelease(factory);
		if (FAILED(hr) || swapchain1 == nullptr) {
			LOGE("IDXGIFactory2::CreateSwapChainForCoreWindow() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}
		_swapchain = swapchain1;							// IDXGISwapChain1 derives from IDXGISwapChain
#else
		// -- Desktop (SDL2 HWND) path: a windowed swap chain created from the native HWND --
		// The *flip model* is preferred over the legacy bitblt one: its buffers can be handed straight to the
		// display (DirectFlip) when the window covers the whole output, which is what makes a borderless
		// fullscreen window actually cover the taskbar instead of being composited under it - a bitblt swap
		// chain always goes through the compositor, so the (topmost) taskbar stays painted on top. It is also
		// the lower-latency, lower-power path Microsoft recommends for every new app. The presentation model is
		// negotiated downwards, most capable first, because each step needs a newer Windows/DXGI than the last:
		// flip + tearing (an uncapped, tearing present with vsync off - the legacy model's behavior, which the
		// flip model otherwise replaces with a vblank-paced discard), then plain flip, then legacy bitblt.
		auto createDeviceAndSwapchain = [&](D3D_DRIVER_TYPE driverType, DXGI_SWAP_EFFECT swapEffect, UINT flags) -> HRESULT {
			// A failed attempt may have handed back some of the objects; start from a clean slate every time
			SafeRelease(_swapchain);
			SafeRelease(_context);
			SafeRelease(_device);
			_swapchainFlags = flags;

			DXGI_SWAP_CHAIN_DESC sd = {};
			// The flip model requires at least two buffers (and forbids multisampling, which is unused here)
			sd.BufferCount = (swapEffect == DXGI_SWAP_EFFECT_DISCARD ? 1 : 2);
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
			sd.SwapEffect = swapEffect;
			sd.Flags = flags;

			return D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, 0,
				requestedLevels, numRequestedLevels, D3D11_SDK_VERSION,
				&sd, &_swapchain, &_device, &obtainedLevel, &_context);
		};
		auto negotiateSwapchain = [&](D3D_DRIVER_TYPE driverType) -> HRESULT {
			HRESULT result = createDeviceAndSwapchain(driverType, DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
			if (FAILED(result)) {
				result = createDeviceAndSwapchain(driverType, DXGI_SWAP_EFFECT_FLIP_DISCARD, 0);
			}
			if (FAILED(result)) {
				result = createDeviceAndSwapchain(driverType, DXGI_SWAP_EFFECT_DISCARD, 0);
			}
			return result;
		};

		HRESULT hr = negotiateSwapchain(D3D_DRIVER_TYPE_HARDWARE);
		if (FAILED(hr)) {
			LOGW("D3D11CreateDeviceAndSwapChain (hardware) failed (0x{:.8x}), falling back to WARP", static_cast<std::uint32_t>(hr));
			hr = negotiateSwapchain(D3D_DRIVER_TYPE_WARP);
		}

		if (FAILED(hr)) {
			LOGE("D3D11CreateDeviceAndSwapChain failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			DestroySwapchain();
			return false;
		}

		{
			// Remember the negotiated model for the summary trace below - it is worth knowing when a presentation
			// problem is reported (a bitblt chain cannot cover the taskbar in borderless fullscreen, nor tear)
			DXGI_SWAP_CHAIN_DESC obtained = {};
			if (SUCCEEDED(_swapchain->GetDesc(&obtained))) {
				swapchainModel = (obtained.SwapEffect == DXGI_SWAP_EFFECT_DISCARD || obtained.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL ? "bitblt" : "flip");
				swapchainBuffers = static_cast<std::uint32_t>(obtained.BufferCount);
			}
			swapchainTearing = ((_swapchainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0);
		}

		// DXGI hooks the output window and turns Alt+Enter into its own exclusive-fullscreen toggle. The game
		// drives fullscreen itself (F11 / Alt+Enter -> a borderless window sized to the display, through SDL),
		// so both handlers would react to the same keystroke and leave the window and the swap chain
		// disagreeing about the fullscreen state. Opt out and leave SDL as the only authority.
		{
			IDXGIFactory* factory = nullptr;
			if (SUCCEEDED(_swapchain->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) && factory != nullptr) {
				factory->MakeWindowAssociation(reinterpret_cast<HWND>(windowHandle), DXGI_MWA_NO_ALT_ENTER);
				SafeRelease(factory);
			}
		}
#endif

		// The real texture-dimension limit of the obtained feature level: D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
		// (16384) on 11_x, 8192 (the D3D10_REQ_ equivalent) on the 10.x fallback levels
		_maxTextureDimension = (obtainedLevel >= D3D_FEATURE_LEVEL_11_0 ? D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION : 8192);

		if (!CreateBackbufferRtv()) {
			DestroySwapchain();
			return false;
		}

		_backbufferWidth = width;
		_backbufferHeight = height;
		CreatePresentResources(width, height);

		SetViewport(Recti(0, 0, width, height));

		LOGI("Direct3D 11 device created (feature level {}.{}), {}x{} {}-model swap chain ({} buffers, tearing {})",
			(static_cast<int>(obtainedLevel) >> 12) & 0xF, (static_cast<int>(obtainedLevel) >> 8) & 0xF, width, height,
			swapchainModel, swapchainBuffers, swapchainTearing ? "allowed" : "unavailable");

#if defined(D3D11_MRT_PROBE)
		RunMrtProbe();
#endif
		return true;
	}

	bool D3D11Device::CreateBackbufferRtv()
	{
		if (_device == nullptr || _swapchain == nullptr) {
			return false;
		}

		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = _swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
		if (FAILED(hr) || backBuffer == nullptr) {
			LOGE("IDXGISwapChain::GetBuffer() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return false;
		}

		hr = _device->CreateRenderTargetView(backBuffer, nullptr, &_backbufferRtv);
		backBuffer->Release();
		if (FAILED(hr)) {
			LOGE("ID3D11Device::CreateRenderTargetView() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return false;
		}

		_context->OMSetRenderTargets(1, &_backbufferRtv, nullptr);
		InvalidateCachedState();
		return true;
	}

	void D3D11Device::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		if (_swapchain == nullptr || _context == nullptr || width <= 0 || height <= 0) {
			return;
		}

		_context->OMSetRenderTargets(0, nullptr, nullptr);
		SafeRelease(_backbufferRtv);

		// The creation flags must be repeated here (0 would silently drop DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING and
		// make the next tearing present fail); 0 buffers / DXGI_FORMAT_UNKNOWN keep the existing count and format
		HRESULT hr = _swapchain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
			DXGI_FORMAT_UNKNOWN, static_cast<UINT>(_swapchainFlags));
		if (FAILED(hr)) {
			LOGE("IDXGISwapChain::ResizeBuffers() failed: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			return;
		}

		CreateBackbufferRtv();
		_backbufferWidth = width;
		_backbufferHeight = height;
		CreatePresentResources(width, height);
		SetViewport(Recti(0, 0, width, height));
	}

	bool D3D11Device::CreatePresentResources(std::int32_t width, std::int32_t height)
	{
		if (_device == nullptr || width <= 0 || height <= 0) {
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
		if (FAILED(_device->CreateTexture2D(&desc, nullptr, &_presentTexture))) {
			return false;
		}
		_device->CreateRenderTargetView(_presentTexture, nullptr, &_presentRtv);
		_device->CreateShaderResourceView(_presentTexture, nullptr, &_presentSrv);

		// Compile the flip-blit shaders and sampler once (kept across resizes)
		if (_presentVs == nullptr || _presentPs == nullptr) {
			ID3DBlob* vsBlob = nullptr;
			ID3DBlob* psBlob = nullptr;
			if (SUCCEEDED(D3DCompile(kPresentVs, std::strlen(kPresentVs), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsBlob, nullptr)) &&
				SUCCEEDED(D3DCompile(kPresentPs, std::strlen(kPresentPs), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psBlob, nullptr))) {
				_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &_presentVs);
				_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &_presentPs);
			} else {
				LOGE("Failed to compile the Direct3D 11 present flip-blit shaders");
			}
			SafeRelease(vsBlob);
			SafeRelease(psBlob);
		}
		if (_presentSampler == nullptr) {
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			_device->CreateSamplerState(&sd, &_presentSampler);
		}
		return true;
	}

	void D3D11Device::ReleasePresentResources()
	{
		SafeRelease(_presentSrv);
		SafeRelease(_presentRtv);
		SafeRelease(_presentTexture);
	}

	void D3D11Device::PresentFrame()
	{
		// Every draw was rendered GL-bottom-up (clip-space Y flipped in the projection) into the present texture;
		// flip-blit it vertically into the real back-buffer, then present. This is the single GL->D3D scan-out
		// correction (the software backend's SDL_FLIP_VERTICAL equivalent), applied only at the final output so it
		// is uniform across every path regardless of how many off-screen round-trips it made.
		if (_context != nullptr && _backbufferRtv != nullptr && _presentSrv != nullptr &&
			_presentVs != nullptr && _presentPs != nullptr) {
			_context->OMSetRenderTargets(1, &_backbufferRtv, nullptr);
			D3D11_VIEWPORT vp;
			vp.TopLeftX = 0.0f;
			vp.TopLeftY = 0.0f;
			vp.Width = static_cast<float>(_backbufferWidth);
			vp.Height = static_cast<float>(_backbufferHeight);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			_context->RSSetViewports(1, &vp);
			_context->IASetInputLayout(nullptr);
			_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			_context->RSSetState(nullptr);
			_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			_context->VSSetShader(_presentVs, nullptr, 0);
			_context->PSSetShader(_presentPs, nullptr, 0);
			_context->PSSetShaderResources(0, 1, &_presentSrv);
			_context->PSSetSamplers(0, 1, &_presentSampler);
			_context->Draw(3, 0);
			// Unbind so the present texture can be bound as a render target again next frame (no read/write hazard)
			ID3D11ShaderResourceView* nullSrv = nullptr;
			_context->PSSetShaderResources(0, 1, &nullSrv);
			// The blit bound its own shaders/state/target directly; drop the shadow state so the next frame's
			// first draw re-issues everything
			InvalidateCachedState();
		}
		if (_swapchain != nullptr) {
			// With vsync off, a flip-model chain created with ALLOW_TEARING must also be *presented* with the
			// tearing flag to actually run uncapped; without it the flip model paces every present to vblank
			// (the flag is only legal at sync interval 0, which is exactly the vsync-off case)
			const UINT presentFlags = (!_vsync && (_swapchainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0
				? DXGI_PRESENT_ALLOW_TEARING : 0u);
			_swapchain->Present(_vsync ? 1 : 0, presentFlags);
		}
	}

	namespace
	{
		// Opaque payload behind the void* handle the *Secondary* functions hand out (ImGui keeps it in
		// ImGuiViewport::RendererUserData). Deliberately not a member type: nothing outside this file needs it.
		struct SecondarySwapchain
		{
			IDXGISwapChain* Swapchain = nullptr;
			ID3D11RenderTargetView* Rtv = nullptr;
			std::int32_t Width = 0;
			std::int32_t Height = 0;
		};
	}

	void* D3D11Device::CreateSecondarySwapchain(void* windowHandle, std::int32_t width, std::int32_t height)
	{
		if (_device == nullptr || _swapchain == nullptr || windowHandle == nullptr) {
			return nullptr;
		}
		if (width <= 0) width = 1;
		if (height <= 0) height = 1;

		// Every swap chain of a device must come from the factory that owns its adapter - reuse the main one's
		IDXGIFactory* factory = nullptr;
		if (FAILED(_swapchain->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) || factory == nullptr) {
			LOGE("Failed to obtain the DXGI factory for a secondary swap chain");
			return nullptr;
		}

		// Same presentation-model preference as the main window (flip first, bitblt as the fallback), without
		// tearing: these windows carry UI, not gameplay, so they always present at the display's pace
		auto createChain = [&](DXGI_SWAP_EFFECT swapEffect) -> IDXGISwapChain* {
			DXGI_SWAP_CHAIN_DESC sd = {};
			sd.BufferCount = (swapEffect == DXGI_SWAP_EFFECT_DISCARD ? 1 : 2);
			sd.BufferDesc.Width = static_cast<UINT>(width);
			sd.BufferDesc.Height = static_cast<UINT>(height);
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = reinterpret_cast<HWND>(windowHandle);
			sd.SampleDesc.Count = 1;
			sd.Windowed = TRUE;
			sd.SwapEffect = swapEffect;
			IDXGISwapChain* result = nullptr;
			if (FAILED(factory->CreateSwapChain(_device, &sd, &result))) {
				return nullptr;
			}
			return result;
		};

		IDXGISwapChain* chain = createChain(DXGI_SWAP_EFFECT_FLIP_DISCARD);
		if (chain == nullptr) {
			chain = createChain(DXGI_SWAP_EFFECT_DISCARD);
		}
		// The Alt+Enter association is deliberately left pointing at the main window (it is per factory, so
		// re-associating it here would move DXGI's fullscreen handling onto a transient UI window)
		SafeRelease(factory);
		if (chain == nullptr) {
			LOGE("Failed to create a secondary swap chain");
			return nullptr;
		}

		SecondarySwapchain* sc = new SecondarySwapchain();
		sc->Swapchain = chain;
		sc->Width = width;
		sc->Height = height;

		ID3D11Texture2D* backBuffer = nullptr;
		if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) && backBuffer != nullptr) {
			_device->CreateRenderTargetView(backBuffer, nullptr, &sc->Rtv);
			backBuffer->Release();
		}
		if (sc->Rtv == nullptr) {
			LOGE("Failed to create the render target view of a secondary swap chain");
			DestroySecondarySwapchain(sc);
			return nullptr;
		}
		return sc;
	}

	void D3D11Device::DestroySecondarySwapchain(void* handle)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc == nullptr) {
			return;
		}
		if (_secondaryTargetRtv == sc->Rtv) {
			EndSecondaryFrame();
		}
		if (sc->Rtv != nullptr) {
			// Scrub the view from the last-bound shadow first: it is released right below and a new view
			// recycling the same pointer would otherwise be mistaken for still bound and its bind skipped
			OnRtvReleased(sc->Rtv);
		}
		SafeRelease(sc->Rtv);
		SafeRelease(sc->Swapchain);
		delete sc;
	}

	void D3D11Device::ResizeSecondarySwapchain(void* handle, std::int32_t width, std::int32_t height)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc == nullptr || sc->Swapchain == nullptr || _device == nullptr || width <= 0 || height <= 0) {
			return;
		}
		if (sc->Width == width && sc->Height == height) {
			return;
		}

		// Every reference to the back-buffer must be gone before ResizeBuffers
		if (_secondaryTargetRtv == sc->Rtv) {
			EndSecondaryFrame();
		}
		if (sc->Rtv != nullptr) {
			OnRtvReleased(sc->Rtv);
		}
		SafeRelease(sc->Rtv);

		if (FAILED(sc->Swapchain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
			LOGE("IDXGISwapChain::ResizeBuffers() failed for a secondary swap chain");
			return;
		}
		sc->Width = width;
		sc->Height = height;

		ID3D11Texture2D* backBuffer = nullptr;
		if (SUCCEEDED(sc->Swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) && backBuffer != nullptr) {
			_device->CreateRenderTargetView(backBuffer, nullptr, &sc->Rtv);
			backBuffer->Release();
		}
	}

	void D3D11Device::BeginSecondaryFrame(void* handle, bool clear)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (_context == nullptr || sc == nullptr || sc->Rtv == nullptr) {
			return;
		}

		// The redirection works through the "screen" target (see ScreenRtv()), so any off-screen render target
		// left bound by the scene must be cleared first, and the shadow state dropped so the next draw rebinds
		_currentRenderTarget = nullptr;
		_secondaryTargetRtv = sc->Rtv;
		_secondaryTargetHeight = sc->Height;
		InvalidateCachedState();

		if (clear) {
			const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			_context->ClearRenderTargetView(sc->Rtv, black);
		}
	}

	void D3D11Device::EndSecondaryFrame()
	{
		if (_secondaryTargetRtv == nullptr) {
			return;
		}
		_secondaryTargetRtv = nullptr;
		_secondaryTargetHeight = 0;
		// The next draw must not keep writing into the window that is no longer bound
		InvalidateCachedState();
	}

	void D3D11Device::PresentSecondaryFrame(void* handle)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc != nullptr && sc->Swapchain != nullptr) {
			sc->Swapchain->Present(0, 0);
		}
	}

	void D3D11Device::ReleasePipelineObjects()
	{
		for (PooledCBuffer& e : _cbufferPool) {
			SafeRelease(e.Buffer);
		}
		_cbufferPool.clear();
		for (BlendStateEntry& e : _blendStates) {
			SafeRelease(e.State);
		}
		_blendStates.clear();
		for (RasterStateEntry& e : _rasterStates) {
			SafeRelease(e.State);
		}
		_rasterStates.clear();
		SafeRelease(_depthDisabledState);
	}

	void D3D11Device::DestroySwapchain()
	{
		if (_context != nullptr) {
			_context->OMSetRenderTargets(0, nullptr, nullptr);
			_context->ClearState();
			_context->Flush();
		}
		// Secondary swap chains are owned by whoever created them (ImGui destroys its platform windows on
		// shutdown); just make sure a redirection cannot outlive the device
		_secondaryTargetRtv = nullptr;
		_secondaryTargetHeight = 0;
		InvalidateCachedState();
		ReleasePipelineObjects();
		ReleasePresentResources();
		SafeRelease(_presentSampler);
		SafeRelease(_presentPs);
		SafeRelease(_presentVs);
		SafeRelease(_backbufferRtv);
		SafeRelease(_swapchain);
		SafeRelease(_context);
		SafeRelease(_device);
		_swapchainFlags = 0;
	}

	ID3D11Device* D3D11Device::GetD3DDevice() { return _device; }
	ID3D11DeviceContext* D3D11Device::GetD3DContext() { return _context; }

	std::int32_t D3D11Device::GetMaxTextureDimension() { return _maxTextureDimension; }
}
