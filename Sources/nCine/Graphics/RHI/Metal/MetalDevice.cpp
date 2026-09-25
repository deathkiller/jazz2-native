// Exactly one translation unit of the program defines the metal-cpp implementation symbols
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
// metal-cpp is included FIRST: the contract headers below pull in Death::Containers with a global using-directive,
// and metal-cpp's own NS::String / MTL::Function would otherwise be shadowed by that namespace's String and
// Function templates while its headers are parsed
#include "MetalCommon.h"

#include "MetalDevice.h"
#include "MetalShaderProgram.h"
#include "MetalRenderTarget.h"
#include "MetalTexture.h"
#include "MetalBufferObject.h"
#include "../../../Base/FrameStatistics.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

// The SDL Metal view/layer API is used only here (view creation lives in this translation unit)
#if defined(WITH_SDL2) || defined(WITH_SDL3)
#	if !defined(CMAKE_BUILD) && defined(__has_include)
#		if defined(WITH_SDL3) && __has_include("SDL3/SDL_metal.h")
#			define __HAS_LOCAL_SDL3_METAL
#		elif __has_include("SDL2/SDL_metal.h")
#			define __HAS_LOCAL_SDL_METAL
#		endif
#	endif
#	if defined(WITH_SDL3)
#		if defined(__HAS_LOCAL_SDL3_METAL)
#			include "SDL3/SDL.h"
#			include "SDL3/SDL_metal.h"
#		else
#			include <SDL3/SDL.h>
#			include <SDL3/SDL_metal.h>
#		endif
#	elif defined(__HAS_LOCAL_SDL_METAL)
#		include "SDL2/SDL.h"
#		include "SDL2/SDL_metal.h"
#	else
#		include <SDL.h>
#		include <SDL_metal.h>
#	endif
#endif

#include <Asserts.h>

namespace nCine::RHI::Metal
{
	namespace
	{
		// The [[buffer(N)]] index the vertex stream is bound at - the same index the offline MSL emitter documents
		// (MslEmitter::VertexBufferIndex), at the top of Metal's 31-slot buffer table so the uniform buffers 0..N
		// in reflection order can never collide with it
		constexpr NS::UInteger MetalVertexBufferIndex = 30;

		// -- Owned Metal objects --
		MTL::Device* s_device = nullptr;
		MTL::CommandQueue* s_queue = nullptr;
		bool s_unifiedMemory = false;

		// The window's Metal view (SDL_MetalView, owned by SDL) and its CAMetalLayer (owned by the view)
		void* s_sdlWindow = nullptr;
		void* s_metalView = nullptr;
		void* s_layer = nullptr;
		std::uint32_t s_layerFormat = std::uint32_t(MTL::PixelFormatBGRA8Unorm);
		bool s_vsync = true;
		bool s_ready = false;

		// The "screen" (default framebuffer): every no-render-target draw goes here, then PresentFrame() draws it
		// into the acquired drawable. The whole scene is rendered GL-bottom-up (the offline MSL negates clip-space
		// Y), so off-screen render targets round-trip exactly like GL; the present pass is the only scan-out
		// correction (mirrors the Vulkan backend's flipped blit and the D3D11 flip-blit).
		MTL::Texture* s_screenTexture = nullptr;
		std::int32_t s_screenWidth = 0;
		std::int32_t s_screenHeight = 0;
		constexpr MTL::PixelFormat ScreenFormat = MTL::PixelFormatRGBA8Unorm;
		bool s_screenNeedsClear = false;	// the first pass on the screen this frame clears it to opaque black

		// -- Frame recording --
		// One command buffer per frame; one frame in flight (BeginFrame waits for the previous frame's command
		// buffer). Serial execution is REQUIRED here for the same reason as on Vulkan: the engine renders every frame
		// into shared, single-instance render targets (scene/bloom/lighting/combine targets, the per-frame palette
		// texture), so a frame must not start writing those while the GPU still reads them for the previous one.
		NS::AutoreleasePool* s_framePool = nullptr;			// drains the frame's autoreleased Metal objects at present
		MTL::CommandBuffer* s_commandBuffer = nullptr;		// the frame being recorded (retained)
		MTL::CommandBuffer* s_inFlight = nullptr;			// the last committed frame (retained), waited on by BeginFrame
		bool s_frameActive = false;

		// The open render command encoder and what it draws into
		MTL::RenderCommandEncoder* s_encoder = nullptr;		// retained while open
		MetalRenderTarget* s_encoderTarget = nullptr;		// nullptr = the screen
		std::uint32_t s_encoderGeneration = 0;				// the target's attachment generation the pass was built for
		std::uint32_t s_encoderColorCount = 0;
		MTL::PixelFormat s_encoderFormats[MetalRenderTarget::MaxColorAttachments] = {};
		std::int32_t s_encoderWidth = 0;
		std::int32_t s_encoderHeight = 0;

		// Per-frame uniform ring (shared storage, persistently mapped): every draw's _Globals + uniform-block bytes
		// are copied here at 256-byte aligned offsets (macOS' constant buffer offset alignment) and bound by offset
		constexpr NS::UInteger UboAlignment = 256;
		constexpr NS::UInteger UboRingSize = 16u * 1024u * 1024u;
		MTL::Buffer* s_uboRing = nullptr;
		std::uint8_t* s_uboRingMapped = nullptr;
		NS::UInteger s_uboCursor = 0;
		// Within-frame reuse cache: draws with identical uniform CONTENTS share one ring region (keyed by 64-bit
		// FNV-1a, memcmp-verified on a hit, so it is collision-proof)
		std::unordered_map<std::uint64_t, NS::UInteger> s_frameUboByHash;
		std::vector<std::uint8_t> s_uboScratch;

		// 1x1 white fallback texture bound when a sampler has no texture (a shader reading an unbound texture slot
		// is a GPU fault on Metal)
		MTL::Texture* s_dummyTexture = nullptr;
		MTL::SamplerState* s_dummySampler = nullptr;

		// The present pass: a fullscreen triangle sampling the screen texture (or a secondary window's texture)
		MTL::Library* s_presentLibrary = nullptr;
		MTL::RenderPipelineState* s_presentPipeline = nullptr;
		MTL::SamplerState* s_presentSampler = nullptr;

		// -- Render pipeline cache --
		struct PipelineKey
		{
			std::uint32_t programHandle;
			std::uint64_t vertexLayoutHash;		// the vertex descriptor (stride + every attribute), 0 without vertex input
			std::uint32_t blendKey;
			std::uint32_t colorAttachmentCount;
			std::uint32_t colorFormats[MetalRenderTarget::MaxColorAttachments];
			bool operator==(const PipelineKey& o) const {
				if (programHandle != o.programHandle || vertexLayoutHash != o.vertexLayoutHash || blendKey != o.blendKey ||
					colorAttachmentCount != o.colorAttachmentCount) {
					return false;
				}
				for (std::uint32_t i = 0; i < colorAttachmentCount; i++) {
					if (colorFormats[i] != o.colorFormats[i]) {
						return false;
					}
				}
				return true;
			}
		};
		struct PipelineKeyHash
		{
			std::size_t operator()(const PipelineKey& k) const {
				std::uint64_t h = 1469598103934665603ull;
				auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
				mix(k.programHandle); mix(k.vertexLayoutHash); mix(k.blendKey); mix(k.colorAttachmentCount);
				for (std::uint32_t i = 0; i < k.colorAttachmentCount; i++) {
					mix(k.colorFormats[i]);
				}
				return std::size_t(h);
			}
		};
		std::unordered_map<PipelineKey, MTL::RenderPipelineState*, PipelineKeyHash> s_pipelines;

		// -- Last-bound state of the open encoder (redundant set* elimination); reset whenever an encoder opens --
		MTL::RenderPipelineState* s_lastPipeline = nullptr;
		const MTL::Buffer* s_lastVertexBuffer = nullptr;
		NS::UInteger s_lastVertexBufferOffset = 0;
		struct BoundBuffer { const MTL::Buffer* Buffer = nullptr; NS::UInteger Offset = 0; };
		BoundBuffer s_lastBuffers[16];
		const MTL::Texture* s_lastTextures[16] = {};
		const MTL::SamplerState* s_lastSamplers[16] = {};
		MTL::Viewport s_lastViewport = {};
		MTL::ScissorRect s_lastScissor = {};
		bool s_lastViewportValid = false;
		bool s_lastScissorValid = false;
		MTL::CullMode s_lastCullMode = MTL::CullModeNone;
		bool s_lastCullValid = false;

		void ResetEncoderShadowState()
		{
			s_lastPipeline = nullptr;
			s_lastVertexBuffer = nullptr;
			s_lastVertexBufferOffset = 0;
			for (BoundBuffer& b : s_lastBuffers) {
				b = BoundBuffer{};
			}
			for (const MTL::Texture*& t : s_lastTextures) t = nullptr;
			for (const MTL::SamplerState*& s : s_lastSamplers) s = nullptr;
			s_lastViewportValid = false;
			s_lastScissorValid = false;
			s_lastCullValid = false;
		}

		// -- Secondary windows (the windows ImGui spawns when a panel is dragged out of the main one) --
		// Only the PRESENTATION of such a window lives here; its contents are rendered through the ordinary RHI
		// path into an off-screen render target, so every usual convention (bottom-up rows, scissor mapping,
		// pipeline cache) applies unchanged. QueueSecondaryPresent() hands that target's texture over for the
		// frame, and PresentFrame() draws each one into its window's drawable with the same present pass.
		struct SecondarySwapchain
		{
			void* SdlWindow = nullptr;
			void* View = nullptr;		// SDL_MetalView
			void* Layer = nullptr;		// its CAMetalLayer
			std::int32_t Width = 0;
			std::int32_t Height = 0;
			bool Ready = false;
		};
		struct PendingSecondaryPresent
		{
			SecondarySwapchain* Chain;
			const MetalTexture* Source;
		};
		std::vector<SecondarySwapchain*> s_secondarySwapchains;
		std::vector<PendingSecondaryPresent> s_pendingSecondaryPresents;

		MTL::PrimitiveType MapPrimitive(PrimitiveType p)
		{
			switch (p) {
				case PrimitiveType::Points: return MTL::PrimitiveTypePoint;
				case PrimitiveType::Lines: return MTL::PrimitiveTypeLine;
				case PrimitiveType::LineStrip: return MTL::PrimitiveTypeLineStrip;
				case PrimitiveType::Triangles: return MTL::PrimitiveTypeTriangle;
				case PrimitiveType::TriangleStrip: return MTL::PrimitiveTypeTriangleStrip;
				case PrimitiveType::LineLoop:
				case PrimitiveType::TriangleFan: {
					// Metal has neither; the engine issues neither (every strip/list caller maps directly), so the
					// nearest topology stands in and the first occurrence is reported rather than emulated
					static bool warned = false;
					if (!warned) {
						warned = true;
						LOGW("Metal has no {} topology, drawing as a strip", (p == PrimitiveType::LineLoop ? "line-loop" : "triangle-fan"));
					}
					return (p == PrimitiveType::LineLoop ? MTL::PrimitiveTypeLineStrip : MTL::PrimitiveTypeTriangleStrip);
				}
				default: return MTL::PrimitiveTypeTriangle;
			}
		}

		std::uint32_t BlendCode(nCine::BlendingFactor f)
		{
			switch (f) {
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

		// Blend factor for the RGB slot, or (alpha = true) the alpha slot. Metal applies a colour factor's alpha
		// component in the alpha slot like GL and Vulkan do, but the alpha spellings say the same thing without
		// relying on that, so the alpha slot gets them (the D3D11 backend HAS to remap, Metal merely may).
		MTL::BlendFactor MapBlend(nCine::BlendingFactor f, bool alpha)
		{
			switch (f) {
				case nCine::BlendingFactor::Zero: return MTL::BlendFactorZero;
				case nCine::BlendingFactor::One: return MTL::BlendFactorOne;
				case nCine::BlendingFactor::SrcColor: return alpha ? MTL::BlendFactorSourceAlpha : MTL::BlendFactorSourceColor;
				case nCine::BlendingFactor::OneMinusSrcColor: return alpha ? MTL::BlendFactorOneMinusSourceAlpha : MTL::BlendFactorOneMinusSourceColor;
				case nCine::BlendingFactor::SrcAlpha: return MTL::BlendFactorSourceAlpha;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return MTL::BlendFactorOneMinusSourceAlpha;
				case nCine::BlendingFactor::DstAlpha: return MTL::BlendFactorDestinationAlpha;
				case nCine::BlendingFactor::OneMinusDstAlpha: return MTL::BlendFactorOneMinusDestinationAlpha;
				case nCine::BlendingFactor::DstColor: return alpha ? MTL::BlendFactorDestinationAlpha : MTL::BlendFactorDestinationColor;
				case nCine::BlendingFactor::OneMinusDstColor: return alpha ? MTL::BlendFactorOneMinusDestinationAlpha : MTL::BlendFactorOneMinusDestinationColor;
				case nCine::BlendingFactor::SrcAlphaSaturate: return alpha ? MTL::BlendFactorOne : MTL::BlendFactorSourceAlphaSaturated;
				case nCine::BlendingFactor::ConstantColor: return alpha ? MTL::BlendFactorBlendAlpha : MTL::BlendFactorBlendColor;
				case nCine::BlendingFactor::OneMinusConstantColor: return alpha ? MTL::BlendFactorOneMinusBlendAlpha : MTL::BlendFactorOneMinusBlendColor;
				case nCine::BlendingFactor::ConstantAlpha: return MTL::BlendFactorBlendAlpha;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return MTL::BlendFactorOneMinusBlendAlpha;
				default: return MTL::BlendFactorOne;
			}
		}

		// The vertex descriptor format of an attribute. The recorded type uses the GL numeric constants (see
		// VertexAttribType); 0 (unset) means the stream is 32-bit and its interpretation follows the SHADER's
		// declared scalar type: a float input reads floats, an int/uint input reads 32-bit integers - the way
		// glVertexAttribIPointer feeds the batched-mesh `uint aMeshIndex` from the engine's int32 draw index.
		// Unsigned-byte attributes (the ImGui vertex color, 4 x u8 normalized) map to the UChar formats.
		MTL::VertexFormat AttributeFormat(std::int32_t componentCount, std::uint32_t type, bool normalized, std::uint8_t shaderScalar)
		{
			if (type == std::uint32_t(VertexAttribType::UnsignedByte)) {
				if (normalized) {
					switch (componentCount) {
						case 1: return MTL::VertexFormatUCharNormalized;
						case 2: return MTL::VertexFormatUChar2Normalized;
						case 3: return MTL::VertexFormatUChar3Normalized;
						default: return MTL::VertexFormatUChar4Normalized;
					}
				}
				switch (componentCount) {
					case 1: return MTL::VertexFormatUChar;
					case 2: return MTL::VertexFormatUChar2;
					case 3: return MTL::VertexFormatUChar3;
					default: return MTL::VertexFormatUChar4;
				}
			}
			if (shaderScalar == 1) {
				switch (componentCount) {
					case 1: return MTL::VertexFormatInt;
					case 2: return MTL::VertexFormatInt2;
					case 3: return MTL::VertexFormatInt3;
					default: return MTL::VertexFormatInt4;
				}
			}
			if (shaderScalar == 2) {
				switch (componentCount) {
					case 1: return MTL::VertexFormatUInt;
					case 2: return MTL::VertexFormatUInt2;
					case 3: return MTL::VertexFormatUInt3;
					default: return MTL::VertexFormatUInt4;
				}
			}
			switch (componentCount) {
				case 1: return MTL::VertexFormatFloat;
				case 2: return MTL::VertexFormatFloat2;
				case 3: return MTL::VertexFormatFloat3;
				default: return MTL::VertexFormatFloat4;
			}
		}

		// FNV-1a 64-bit accumulation for the within-frame reuse cache and the pipeline key
		inline void HashBytes(std::uint64_t& h, const void* data, std::size_t len)
		{
			const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
			for (std::size_t i = 0; i < len; i++) {
				h ^= p[i];
				h *= 1099511628211ull;
			}
		}
		inline void HashU64(std::uint64_t& h, std::uint64_t v)
		{
			HashBytes(h, &v, sizeof(v));
		}

		// -- Present pass shader (MSL, compiled once at device creation) --
		// A fullscreen triangle whose texture coordinate is the vertex's [0,1] corner: the drawable's TOP row
		// (clip y = +1) therefore samples v = 1, the screen texture's LAST row - which is the GL top row, since the
		// scene was rendered GL-bottom-up. That is the whole GL -> Metal scan-out correction (see MetalDevice.h).
		const char PresentShaderSource[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct PresentOut
{
	float4 position [[position]];
	float2 uv;
};

vertex PresentOut present_vs(uint vid [[vertex_id]])
{
	float2 t = float2(float((vid << 1) & 2u), float(vid & 2u));
	PresentOut o;
	o.position = float4(t * 2.0 - 1.0, 0.0, 1.0);
	o.uv = t;
	return o;
}

fragment float4 present_fs(PresentOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]])
{
	return float4(tex.sample(smp, in.uv).rgb, 1.0);
}
)MSL";

		bool CreatePresentPipeline()
		{
			NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
			NS::Error* error = nullptr;
			MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
			s_presentLibrary = s_device->newLibrary(NS::String::string(PresentShaderSource, NS::UTF8StringEncoding), options, &error);
			options->release();
			if (s_presentLibrary == nullptr) {
				LOGE("Failed to compile the Metal present shader: {}",
					(error != nullptr && error->localizedDescription() != nullptr) ? error->localizedDescription()->utf8String() : "unknown error");
				pool->release();
				return false;
			}
			MTL::Function* vs = s_presentLibrary->newFunction(NS::String::string("present_vs", NS::UTF8StringEncoding));
			MTL::Function* fs = s_presentLibrary->newFunction(NS::String::string("present_fs", NS::UTF8StringEncoding));
			bool ok = (vs != nullptr && fs != nullptr);
			if (ok) {
				MTL::RenderPipelineDescriptor* pd = MTL::RenderPipelineDescriptor::alloc()->init();
				pd->setVertexFunction(vs);
				pd->setFragmentFunction(fs);
				pd->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormat(s_layerFormat));
				s_presentPipeline = s_device->newRenderPipelineState(pd, &error);
				pd->release();
				if (s_presentPipeline == nullptr) {
					LOGE("Failed to create the Metal present pipeline: {}",
						(error != nullptr && error->localizedDescription() != nullptr) ? error->localizedDescription()->utf8String() : "unknown error");
					ok = false;
				}
			}
			if (vs != nullptr) vs->release();
			if (fs != nullptr) fs->release();

			MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
			sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
			sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
			sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
			sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
			s_presentSampler = s_device->newSamplerState(sd);
			sd->release();
			pool->release();
			return ok && s_presentSampler != nullptr;
		}

		bool CreateDummyTexture()
		{
			MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
			desc->setTextureType(MTL::TextureType2D);
			desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
			desc->setWidth(1);
			desc->setHeight(1);
			desc->setMipmapLevelCount(1);
			desc->setUsage(MTL::TextureUsageShaderRead);
#if defined(DEATH_TARGET_IOS)
			// iOS has no Managed mode; Shared is the CPU-writable storage of its unified memory
			desc->setStorageMode(MTL::StorageModeShared);
#else
			// Managed storage exists on every macOS GPU, so the one CPU write below is a plain replaceRegion
			desc->setStorageMode(MTL::StorageModeManaged);
#endif
			s_dummyTexture = s_device->newTexture(desc);
			desc->release();
			if (s_dummyTexture == nullptr) {
				return false;
			}
			const std::uint8_t white[4] = { 255, 255, 255, 255 };
			s_dummyTexture->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, white, 4);

			MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
			sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
			sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
			sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
			sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
			s_dummySampler = s_device->newSamplerState(sd);
			sd->release();
			return (s_dummySampler != nullptr);
		}

		void DestroyScreenTarget()
		{
			if (s_screenTexture != nullptr) {
				s_screenTexture->release();
				s_screenTexture = nullptr;
			}
			s_screenWidth = 0;
			s_screenHeight = 0;
		}

		bool CreateScreenTarget(std::int32_t width, std::int32_t height)
		{
			DestroyScreenTarget();
			MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
			desc->setTextureType(MTL::TextureType2D);
			desc->setPixelFormat(ScreenFormat);
			desc->setWidth(NS::UInteger(width));
			desc->setHeight(NS::UInteger(height));
			desc->setMipmapLevelCount(1);
			desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
			desc->setStorageMode(MTL::StorageModePrivate);
			s_screenTexture = s_device->newTexture(desc);
			desc->release();
			if (s_screenTexture == nullptr) {
				LOGE("Failed to create the {}x{} Metal screen texture", width, height);
				return false;
			}
			s_screenWidth = width;
			s_screenHeight = height;
			s_screenNeedsClear = true;
			return true;
		}

		void WaitForInFlightFrame()
		{
			if (s_inFlight != nullptr) {
				s_inFlight->waitUntilCompleted();
				// A completed command buffer knows when the GPU started and finished executing it, and here that is
				// the whole frame - there is one command buffer per frame, so no timer queries are needed
				if (FrameStatistics::IsEnabled() && s_inFlight->status() == MTL::CommandBufferStatusCompleted) {
					if (__builtin_available(macOS 10.15, iOS 10.3, tvOS 10.3, *)) {
						const double gpuTime = s_inFlight->GPUEndTime() - s_inFlight->GPUStartTime();
						if (gpuTime > 0.0) {
							FrameStatistics::AddCounter("GPU", float(gpuTime * 1000.0), FrameStatistics::Unit::Milliseconds);
						}
					}
				}
				s_inFlight->release();
				s_inFlight = nullptr;
			}
		}

		void EndEncoder()
		{
			if (s_encoder != nullptr) {
				s_encoder->endEncoding();
				s_encoder->release();
				s_encoder = nullptr;
			}
			s_encoderTarget = nullptr;
			s_encoderColorCount = 0;
		}

		// Abandons the frame being recorded without executing it (a lost layer / a resize mid-frame)
		void AbandonFrame()
		{
			EndEncoder();
			if (s_commandBuffer != nullptr) {
				s_commandBuffer->release();		// never committed: simply dropped
				s_commandBuffer = nullptr;
			}
			s_frameActive = false;
			if (s_framePool != nullptr) {
				s_framePool->release();
				s_framePool = nullptr;
			}
			s_pendingSecondaryPresents.clear();
		}

		void BeginCommandBuffer()
		{
			s_commandBuffer = s_queue->commandBuffer();
			s_commandBuffer->retain();
			ResetEncoderShadowState();
		}

		void BeginFrame()
		{
			if (s_frameActive || !s_ready || s_device == nullptr) {
				return;
			}
			// One frame in flight: the previous frame's GPU work is fully complete before this one records, so the
			// shared render targets, the uniform ring and every streamed buffer/texture are safe to reuse
			WaitForInFlightFrame();
			s_framePool = NS::AutoreleasePool::alloc()->init();
			BeginCommandBuffer();
			s_uboCursor = 0;
			s_frameUboByHash.clear();
			s_frameActive = true;
			// The screen is cleared to opaque black by the first pass that draws into it (the default framebuffer
			// starts cleared, like GL); the engine overwrites/clears again through its own passes
			s_screenNeedsClear = true;
		}

		// Applies the recorded cull state to the open encoder (Metal keeps it as encoder state, not pipeline state)
		void ApplyCullMode()
		{
			const MetalDevice::CullFaceState cull = MetalDevice::GetCullFaceState();
			MTL::CullMode mode = MTL::CullModeNone;
			if (cull.Enabled) {
				mode = (cull.Mode == CullFaceMode::Front ? MTL::CullModeFront : MTL::CullModeBack);
			}
			if (!s_lastCullValid || s_lastCullMode != mode) {
				s_encoder->setCullMode(mode);
				s_lastCullMode = mode;
				s_lastCullValid = true;
			}
		}

		/**
			Ensures a render command encoder is open for @p target (nullptr = the screen), opening a new pass when the
			target (or its attachments) changed. @p clearMask selects the attachments the new pass CLEARS to
			@p clearColor (a non-zero mask always opens a new pass: clearing is a load action in Metal).
		*/
		bool EnsureEncoder(MetalRenderTarget* target, std::uint32_t clearMask, const Colorf& clearColor)
		{
			if (s_encoder != nullptr && clearMask == 0) {
				const bool same = (target == nullptr)
					? (s_encoderTarget == nullptr)
					: (s_encoderTarget == target && s_encoderGeneration == target->GetAttachmentGeneration());
				if (same) {
					return true;
				}
			}
			EndEncoder();

			MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
			std::uint32_t count = 0;
			std::int32_t width = 0, height = 0;
			if (target == nullptr) {
				if (s_screenTexture == nullptr) {
					rp->release();
					return false;
				}
				MTL::RenderPassColorAttachmentDescriptor* att = rp->colorAttachments()->object(0);
				att->setTexture(s_screenTexture);
				const bool clear = ((clearMask & 1u) != 0) || s_screenNeedsClear;
				att->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
				att->setStoreAction(MTL::StoreActionStore);
				if ((clearMask & 1u) != 0) {
					att->setClearColor(MTL::ClearColor(clearColor.R, clearColor.G, clearColor.B, clearColor.A));
				} else {
					att->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));
				}
				s_screenNeedsClear = false;
				s_encoderFormats[0] = ScreenFormat;
				count = 1;
				width = s_screenWidth;
				height = s_screenHeight;
			} else {
				count = target->GetAttachedCount();
				MetalTexture* color0 = target->GetColorTexture(0);
				if (count == 0 || color0 == nullptr) {
					rp->release();
					return false;
				}
				for (std::uint32_t i = 0; i < count; i++) {
					MetalTexture* attachment = target->GetColorTexture(i);
					attachment->EnsureGpu();
					MTL::Texture* texture = static_cast<MTL::Texture*>(attachment->GpuTexture());
					if (texture == nullptr) {
						rp->release();
						return false;
					}
					MTL::RenderPassColorAttachmentDescriptor* att = rp->colorAttachments()->object(i);
					att->setTexture(texture);
					// Contents are preserved (the engine clears explicitly) unless this pass was opened to clear
					att->setLoadAction(((clearMask & (1u << i)) != 0) ? MTL::LoadActionClear : MTL::LoadActionLoad);
					att->setStoreAction(MTL::StoreActionStore);
					att->setClearColor(MTL::ClearColor(clearColor.R, clearColor.G, clearColor.B, clearColor.A));
					s_encoderFormats[i] = MTL::PixelFormat(attachment->GpuFormat());
				}
				width = color0->GetWidth();
				height = color0->GetHeight();
			}

			MTL::RenderCommandEncoder* encoder = s_commandBuffer->renderCommandEncoder(rp);
			rp->release();
			if (encoder == nullptr) {
				return false;
			}
			encoder->retain();
			s_encoder = encoder;
			s_encoderTarget = target;
			s_encoderGeneration = (target != nullptr ? target->GetAttachmentGeneration() : 0);
			s_encoderColorCount = count;
			s_encoderWidth = width;
			s_encoderHeight = height;
			ResetEncoderShadowState();

			// GL's counter-clockwise front faces come out clockwise after the clip-space Y flip the MSL applies
			encoder->setFrontFacingWinding(MTL::WindingClockwise);
			// The engine's GL ortho produces clip.z in [-1,1]; clamping instead of clipping keeps everything in
			// Metal's [0,1] range (depth testing is off anyway - a 2D renderer)
			encoder->setDepthClipMode(MTL::DepthClipModeClamp);
			ApplyCullMode();
			return true;
		}

		// Bump-allocates an aligned range within the frame's uniform ring; returns the byte offset into s_uboRing
		bool AllocUbo(NS::UInteger size, NS::UInteger& outOffset)
		{
			if (size == 0) {
				size = 16;
			}
			const NS::UInteger aligned = (s_uboCursor + UboAlignment - 1) & ~(UboAlignment - 1);
			if (aligned + size > UboRingSize) {
				return false;
			}
			outOffset = aligned;
			s_uboCursor = aligned + size;
			return true;
		}

		void BindBufferBothStages(const MTL::Buffer* buffer, NS::UInteger offset, std::uint32_t index)
		{
			if (index < 16 && s_lastBuffers[index].Buffer == buffer && s_lastBuffers[index].Offset == offset) {
				return;
			}
			s_encoder->setVertexBuffer(buffer, offset, index);
			s_encoder->setFragmentBuffer(buffer, offset, index);
			if (index < 16) {
				s_lastBuffers[index] = BoundBuffer{ buffer, offset };
			}
		}

		void BindTextureBothStages(const MTL::Texture* texture, const MTL::SamplerState* sampler, std::uint32_t index)
		{
			if (index < 16 && s_lastTextures[index] == texture && s_lastSamplers[index] == sampler) {
				return;
			}
			// Both stages get every texture: the fragment function declares them all, the vertex function only
			// the ones it reads - binding an argument a function does not declare is allowed
			s_encoder->setVertexTexture(texture, index);
			s_encoder->setFragmentTexture(texture, index);
			s_encoder->setVertexSamplerState(sampler, index);
			s_encoder->setFragmentSamplerState(sampler, index);
			if (index < 16) {
				s_lastTextures[index] = texture;
				s_lastSamplers[index] = sampler;
			}
		}

		void BindArguments(MetalShaderProgram* prog)
		{
			for (const MetalShaderProgram::DescriptorBinding& b : prog->GetDescriptorBindings()) {
				if (b.kind == MetalShaderProgram::DescriptorBinding::Kind::Sampler) {
					const MetalTexture* tex = MetalDevice::GetBoundTexture(std::uint32_t(b.slot >= 0 ? b.slot : 0));
					const MTL::Texture* view = (tex != nullptr ? static_cast<const MTL::Texture*>(tex->GpuView()) : nullptr);
					const MTL::SamplerState* sampler = (tex != nullptr ? static_cast<const MTL::SamplerState*>(tex->GpuSampler()) : nullptr);
					if (view == nullptr || sampler == nullptr) {
						view = s_dummyTexture;
						sampler = s_dummySampler;
					}
					BindTextureBothStages(view, sampler, b.binding);
					continue;
				}

				// A uniform buffer (Globals or a std140 block): gather the bytes into scratch, then dedupe by content
				NS::UInteger size = 0;
				const std::uint8_t* src = nullptr;
				if (b.kind == MetalShaderProgram::DescriptorBinding::Kind::Globals) {
					size = prog->GetGlobalsSize();
				} else {
					const std::uint32_t slot = std::uint32_t(b.slot);
					if (slot < MetalDevice::MaxUniformBindingsPublic()) {
						const std::uint8_t* rangeData = nullptr;
						std::uint32_t rangeSize = 0;
						MetalDevice::GetUniformRange(slot, rangeData, rangeSize);
						src = rangeData;
						size = rangeSize;
					}
				}
				if (size == 0) {
					size = 16;	// keep a valid (if unused) binding
				}
				if (s_uboScratch.size() < std::size_t(size)) {
					s_uboScratch.resize(std::size_t(size));
				}
				std::uint8_t* gather = s_uboScratch.data();
				std::memset(gather, 0, std::size_t(size));
				if (b.kind == MetalShaderProgram::DescriptorBinding::Kind::Globals) {
					// Gather the loose uniforms into their std140 slots from the program's committed values
					for (const MetalShaderProgram::LooseUniform& lu : prog->GetLooseUniforms()) {
						const std::uint8_t* value = prog->ResolveUniform(lu.Name.data());
						if (value != nullptr && lu.Offset + lu.Size <= size) {
							std::memcpy(gather + lu.Offset, value, lu.Size);
						}
					}
				} else if (src != nullptr) {
					std::memcpy(gather, src, std::size_t(size));
				}

				// Dedupe the ring region by content (hash then memcmp-verify => collision-proof)
				std::uint64_t contentHash = 1469598103934665603ull;
				HashU64(contentHash, std::uint64_t(size));
				HashBytes(contentHash, gather, std::size_t(size));
				NS::UInteger offset = 0;
				auto it = s_frameUboByHash.find(contentHash);
				if (it != s_frameUboByHash.end() && std::memcmp(s_uboRingMapped + it->second, gather, std::size_t(size)) == 0) {
					offset = it->second;
				} else {
					if (!AllocUbo(size, offset)) {
						static bool warnedRingExhausted = false;
						if (!warnedRingExhausted) {
							warnedRingExhausted = true;
							LOGW("Per-frame uniform ring exhausted ({} bytes), draws are rendered with stale uniforms", std::uint64_t(UboRingSize));
						}
						continue;
					}
					std::memcpy(s_uboRingMapped + offset, gather, std::size_t(size));
					s_frameUboByHash[contentHash] = offset;
				}
				BindBufferBothStages(s_uboRing, offset, b.binding);
			}
		}

		void ApplyViewportScissor()
		{
			const Recti vpRect = MetalDevice::GetViewport();
			const MetalDevice::ScissorState scState = MetalDevice::GetScissorState();
			// GL viewport/scissor coordinates count rows from memory row 0, and so do Metal's for a texture whose
			// memory keeps the GL row order (see MetalDevice.h) - no flip on either
			MTL::Viewport vp = {};
			vp.originX = double(vpRect.X);
			vp.originY = double(vpRect.Y);
			vp.width = double(vpRect.W);
			vp.height = double(vpRect.H);
			vp.znear = 0.0;
			vp.zfar = 1.0;
			if (!s_lastViewportValid || std::memcmp(&s_lastViewport, &vp, sizeof(vp)) != 0) {
				s_encoder->setViewport(vp);
				s_lastViewport = vp;
				s_lastViewportValid = true;
			}

			// Metal validates the scissor against the attachment size, so clamp it to the current render area
			MTL::ScissorRect sc = {};
			const std::int32_t extentW = s_encoderWidth;
			const std::int32_t extentH = s_encoderHeight;
			if (scState.Enabled && scState.Rect.W > 0 && scState.Rect.H > 0) {
				std::int32_t x = scState.Rect.X < 0 ? 0 : scState.Rect.X;
				std::int32_t y = scState.Rect.Y < 0 ? 0 : scState.Rect.Y;
				std::int32_t w = scState.Rect.W;
				std::int32_t h = scState.Rect.H;
				if (x > extentW) x = extentW;
				if (y > extentH) y = extentH;
				if (x + w > extentW) w = extentW - x;
				if (y + h > extentH) h = extentH - y;
				sc.x = NS::UInteger(x);
				sc.y = NS::UInteger(y);
				sc.width = NS::UInteger(w < 0 ? 0 : w);
				sc.height = NS::UInteger(h < 0 ? 0 : h);
			} else {
				sc.x = 0;
				sc.y = 0;
				sc.width = NS::UInteger(extentW);
				sc.height = NS::UInteger(extentH);
			}
			if (!s_lastScissorValid || std::memcmp(&s_lastScissor, &sc, sizeof(sc)) != 0) {
				s_encoder->setScissorRect(sc);
				s_lastScissor = sc;
				s_lastScissorValid = true;
			}
		}

		MTL::RenderPipelineState* GetOrCreatePipeline(MetalShaderProgram* prog, const MetalShaderProgram::VertexAttrib* attribs,
			std::uint32_t attribCount, std::uint32_t stride, bool hasVertexInput)
		{
			const MetalDevice::BlendingState blend = MetalDevice::GetBlendingState();
			const std::uint32_t blendKey = (blend.Enabled ? 1u : 0u) | (BlendCode(blend.SrcRgb) << 1) |
				(BlendCode(blend.DstRgb) << 5) | (BlendCode(blend.SrcAlpha) << 9) | (BlendCode(blend.DstAlpha) << 13);

			PipelineKey key = {};
			key.programHandle = prog->GetUniqueId();
			key.blendKey = blendKey;
			key.colorAttachmentCount = s_encoderColorCount;
			for (std::uint32_t i = 0; i < s_encoderColorCount; i++) {
				key.colorFormats[i] = std::uint32_t(s_encoderFormats[i]);
			}
			if (hasVertexInput && stride > 0) {
				std::uint64_t h = 1469598103934665603ull;
				HashU64(h, stride);
				for (std::uint32_t i = 0; i < attribCount; i++) {
					HashU64(h, attribs[i].Location);
					HashU64(h, std::uint64_t(std::uint32_t(attribs[i].ComponentCount)) | (std::uint64_t(attribs[i].Type) << 32));
					HashU64(h, std::uint64_t(attribs[i].Offset) | (std::uint64_t(attribs[i].Normalized ? 1u : 0u) << 32) | (std::uint64_t(attribs[i].ShaderScalar) << 40));
				}
				key.vertexLayoutHash = (h == 0 ? 1 : h);
			}

			auto it = s_pipelines.find(key);
			if (it != s_pipelines.end()) {
				return it->second;
			}

			NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
			MTL::RenderPipelineDescriptor* pd = MTL::RenderPipelineDescriptor::alloc()->init();
			pd->setVertexFunction(static_cast<MTL::Function*>(prog->GetVsFunction()));
			pd->setFragmentFunction(static_cast<MTL::Function*>(prog->GetFsFunction()));
			pd->setSampleCount(1);
			for (std::uint32_t i = 0; i < s_encoderColorCount; i++) {
				MTL::RenderPipelineColorAttachmentDescriptor* ca = pd->colorAttachments()->object(i);
				ca->setPixelFormat(s_encoderFormats[i]);
				ca->setBlendingEnabled(blend.Enabled);
				ca->setRgbBlendOperation(MTL::BlendOperationAdd);
				ca->setAlphaBlendOperation(MTL::BlendOperationAdd);
				ca->setSourceRGBBlendFactor(MapBlend(blend.SrcRgb, false));
				ca->setDestinationRGBBlendFactor(MapBlend(blend.DstRgb, false));
				ca->setSourceAlphaBlendFactor(MapBlend(blend.SrcAlpha, true));
				ca->setDestinationAlphaBlendFactor(MapBlend(blend.DstAlpha, true));
				ca->setWriteMask(MTL::ColorWriteMaskAll);
			}
			if (hasVertexInput && stride > 0 && attribCount > 0) {
				MTL::VertexDescriptor* vd = MTL::VertexDescriptor::alloc()->init();
				for (std::uint32_t i = 0; i < attribCount; i++) {
					MTL::VertexAttributeDescriptor* a = vd->attributes()->object(attribs[i].Location);
					a->setFormat(AttributeFormat(attribs[i].ComponentCount, attribs[i].Type, attribs[i].Normalized, attribs[i].ShaderScalar));
					a->setOffset(attribs[i].Offset);
					a->setBufferIndex(MetalVertexBufferIndex);
				}
				MTL::VertexBufferLayoutDescriptor* layout = vd->layouts()->object(MetalVertexBufferIndex);
				layout->setStride(stride);
				layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
				layout->setStepRate(1);
				pd->setVertexDescriptor(vd);
				vd->release();
			}
			NS::Error* error = nullptr;
			MTL::RenderPipelineState* pipeline = s_device->newRenderPipelineState(pd, &error);
			pd->release();
			if (pipeline == nullptr) {
				LOGE("newRenderPipelineState failed for program {}: {}", prog->GetUniqueId(),
					(error != nullptr && error->localizedDescription() != nullptr) ? error->localizedDescription()->utf8String() : "unknown error");
			} else {
				s_pipelines[key] = pipeline;
			}
			pool->release();
			return pipeline;
		}

		void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
		{
			MetalShaderProgram* prog = MetalDevice::CurrentProgram();
			if (!s_ready || prog == nullptr || !prog->HasPipelineState() || count == 0) {
				return;
			}
			if (!s_frameActive) {
				BeginFrame();
			}

			// Before (re)opening the draw's render pass, flush a pending CPU texel upload of every sampled texture
			// into this frame's command buffer (blits must be outside a render encoder). The engine's per-frame
			// palette upload lands here, ordered with the draws exactly like its GL counterpart.
			for (const MetalShaderProgram::DescriptorBinding& b : prog->GetDescriptorBindings()) {
				if (b.kind != MetalShaderProgram::DescriptorBinding::Kind::Sampler) {
					continue;
				}
				const MetalTexture* tex = MetalDevice::GetBoundTexture(std::uint32_t(b.slot >= 0 ? b.slot : 0));
				if (tex == nullptr) {
					continue;
				}
				tex->EnsureGpu();
				if (tex->HasPendingUpload()) {
					EndEncoder();
					tex->RecordStreamingUpload(s_commandBuffer);
				}
			}

			if (!EnsureEncoder(MetalDevice::currentRenderTargetInternal(), 0, Colorf())) {
				return;
			}

			MetalShaderProgram::VertexAttrib attribs[16];
			std::uint32_t attribCount = 0;
			std::uint32_t stride = 0;
			std::uint32_t baseOffset = 0;
			const bool hasVI = prog->HasVertexAttributes();
			if (hasVI) {
				attribCount = prog->GetVertexInput(attribs, 16, stride, baseOffset);
			}
			MTL::RenderPipelineState* pipeline = GetOrCreatePipeline(prog, attribs, attribCount, stride, hasVI);
			if (pipeline == nullptr) {
				return;
			}
			if (s_lastPipeline != pipeline) {
				s_encoder->setRenderPipelineState(pipeline);
				s_lastPipeline = pipeline;
			}
			ApplyCullMode();
			ApplyViewportScissor();
			BindArguments(prog);

			MTL::Buffer* indexBuffer = nullptr;
			if (hasVI) {
				const MetalBufferObject* vbo = prog->GetBoundVbo();
				MTL::Buffer* vertexBuffer = (vbo != nullptr ? static_cast<MTL::Buffer*>(vbo->GetMtlBuffer()) : nullptr);
				if (vertexBuffer == nullptr) {
					return;
				}
				if (s_lastVertexBuffer != vertexBuffer || s_lastVertexBufferOffset != baseOffset) {
					s_encoder->setVertexBuffer(vertexBuffer, baseOffset, MetalVertexBufferIndex);
					s_lastVertexBuffer = vertexBuffer;
					s_lastVertexBufferOffset = baseOffset;
				}
				if (indexed) {
					const MetalBufferObject* ibo = prog->GetBoundIbo();
					indexBuffer = (ibo != nullptr ? static_cast<MTL::Buffer*>(ibo->GetMtlBuffer()) : nullptr);
					if (indexBuffer == nullptr) {
						return;
					}
				}
			} else if (indexed) {
				const MetalBufferObject* ibo = prog->GetBoundIbo();
				indexBuffer = (ibo != nullptr ? static_cast<MTL::Buffer*>(ibo->GetMtlBuffer()) : nullptr);
				if (indexBuffer == nullptr) {
					return;
				}
			}

			const NS::UInteger instances = (numInstances > 1 ? NS::UInteger(numInstances) : 1u);
			const MTL::PrimitiveType type = MapPrimitive(primitive);
			if (indexed) {
				const MTL::IndexType indexType = (indexFormat == IndexFormat::UInt32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16);
				s_encoder->drawIndexedPrimitives(type, NS::UInteger(count), indexType, indexBuffer, NS::UInteger(indexOffset),
					instances, NS::Integer(baseVertex), 0);
			} else {
				s_encoder->drawPrimitives(type, NS::UInteger(firstVertex), NS::UInteger(count), instances);
			}
		}

		// Draws @p source into @p drawable's texture with the present pass
		void EncodePresent(CA::MetalDrawable* drawable, MTL::Texture* source)
		{
			MTL::RenderPassDescriptor* rp = MTL::RenderPassDescriptor::alloc()->init();
			MTL::RenderPassColorAttachmentDescriptor* att = rp->colorAttachments()->object(0);
			att->setTexture(drawable->texture());
			att->setLoadAction(MTL::LoadActionDontCare);
			att->setStoreAction(MTL::StoreActionStore);
			MTL::RenderCommandEncoder* encoder = s_commandBuffer->renderCommandEncoder(rp);
			rp->release();
			if (encoder == nullptr) {
				return;
			}
			encoder->setRenderPipelineState(s_presentPipeline);
			encoder->setFragmentTexture(source, 0);
			encoder->setFragmentSamplerState(s_presentSampler, 0);
			encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
			encoder->endEncoding();
		}

		// Current drawable size of the SDL window (0x0 while minimized), for the lost-layer recovery
		void QueryDrawableSize(std::int32_t& outW, std::int32_t& outH)
		{
			outW = 0;
			outH = 0;
#if defined(WITH_SDL2) || defined(WITH_SDL3)
			if (s_sdlWindow != nullptr) {
				int w = 0, h = 0;
#	if defined(WITH_SDL3)
				SDL_GetWindowSizeInPixels(reinterpret_cast<SDL_Window*>(s_sdlWindow), &w, &h);
#	else
				SDL_Metal_GetDrawableSize(reinterpret_cast<SDL_Window*>(s_sdlWindow), &w, &h);
#	endif
				outW = w;
				outH = h;
			}
#endif
		}
	}

	// -- Static state --

	MetalDevice::BlendingState MetalDevice::_blending;
	MetalDevice::DepthTestState MetalDevice::_depthTest;
	MetalDevice::CullFaceState MetalDevice::_cullFace;
	MetalDevice::ScissorState MetalDevice::_scissor;
	Recti MetalDevice::_viewport(0, 0, 0, 0);
	Colorf MetalDevice::_clearColor(0.0f, 0.0f, 0.0f, 0.0f);

	MetalShaderProgram* MetalDevice::_currentProgram = nullptr;
	const MetalTexture* MetalDevice::_boundTextures[MaxTextureUnits] = {};
	MetalDevice::UniformRange MetalDevice::_boundUniformRanges[MaxUniformBindings] = {};
	MetalRenderTarget* MetalDevice::_currentRenderTarget = nullptr;

	// -- Shared context accessors (declared in MetalCommon.h) --

	MTL::Device* MtlDevice() { return s_device; }
	MTL::CommandQueue* MtlQueue() { return s_queue; }
	bool MtlHasUnifiedMemory() { return s_unifiedMemory; }

	void MtlFlushFrameForReadback()
	{
		if (!s_frameActive || s_commandBuffer == nullptr) {
			return;
		}
		// Everything recorded so far executes now; the rest of the frame continues in a fresh command buffer
		EndEncoder();
		s_commandBuffer->commit();
		s_commandBuffer->waitUntilCompleted();
		s_commandBuffer->release();
		s_commandBuffer = nullptr;
		BeginCommandBuffer();
	}

	std::int32_t MetalDevice::GetMaxTextureDimension()
	{
#if defined(DEATH_TARGET_IOS)
		// 16384 from the A9 (Apple GPU family 3) on; the A8 devices that still run iOS 14/15 cap at 8192
		if (s_device != nullptr && !s_device->supportsFamily(MTL::GPUFamilyApple3)) {
			return 8192;
		}
#endif
		return 16384;
	}

	std::int32_t MetalDevice::GetUniformBufferOffsetAlignment()
	{
		return std::int32_t(UboAlignment);
	}

	std::int32_t MetalDevice::GetMaxUniformBufferRange()
	{
		return 64 * 1024;
	}

	// -- Pipeline state (recorded; applied at draw time) --

	void MetalDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }

	void MetalDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}

	MetalDevice::BlendingState MetalDevice::GetBlendingState() { return _blending; }
	void MetalDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void MetalDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void MetalDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	MetalDevice::DepthTestState MetalDevice::GetDepthTestState() { return _depthTest; }
	void MetalDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void MetalDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	MetalDevice::CullFaceState MetalDevice::GetCullFaceState() { return _cullFace; }
	void MetalDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	MetalDevice::ScissorState MetalDevice::GetScissorState() { return _scissor; }
	void MetalDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void MetalDevice::SetScissor(const Recti& rect) { _scissor.Enabled = true; _scissor.Rect = rect; }
	void MetalDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti MetalDevice::GetViewport() { return _viewport; }
	void MetalDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void MetalDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) { _viewport = Recti(x, y, width, height); }

	Colorf MetalDevice::GetClearColor() { return _clearColor; }
	void MetalDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void MetalDevice::Clear(ClearFlags flags)
	{
		if ((flags & ClearFlags::Color) == ClearFlags::None) {
			return;	// 2D renderer: no depth/stencil attachments
		}
		if (!s_ready) {
			return;
		}
		if (!s_frameActive) {
			BeginFrame();
		}
		// GL semantics: glClear covers every color attachment enabled for drawing. A clear is a render-pass load
		// action in Metal, so a new pass is opened on the target with those attachments cleared (bounded by the
		// target's draw-buffer count, mirroring glDrawBuffers); the draws that follow go into the same pass.
		std::uint32_t clearCount = 1;
		if (_currentRenderTarget != nullptr) {
			clearCount = _currentRenderTarget->GetAttachedCount();
			const std::uint32_t numDrawBuffers = _currentRenderTarget->GetNumDrawBuffers();
			if (numDrawBuffers > 0 && numDrawBuffers < clearCount) {
				clearCount = numDrawBuffers;
			}
			if (clearCount == 0) {
				return;
			}
		}
		const std::uint32_t mask = (clearCount >= 32 ? 0xFFFFFFFFu : ((1u << clearCount) - 1u));
		EnsureEncoder(_currentRenderTarget, mask, _clearColor);
	}

	// -- Draws --

	void MetalDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 1, 0);
	}
	void MetalDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, numInstances, 0);
	}
	void MetalDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, 1, baseVertex);
	}
	void MetalDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, numInstances, baseVertex);
	}

	FenceHandle MetalDevice::InsertFence() { return nullptr; }
	void MetalDevice::DeleteFence(FenceHandle& fence) { fence = nullptr; }
	bool MetalDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void MetalDevice::SetupInitialState()
	{
		_blending = BlendingState{};
		_depthTest = DepthTestState{};
		_cullFace = CullFaceState{};
		_scissor = ScissorState{};
	}

	// -- Backend extensions (recorders) --

	void MetalDevice::BindProgram(MetalShaderProgram* program) { _currentProgram = program; }
	MetalShaderProgram* MetalDevice::CurrentProgram() { return _currentProgram; }

	void MetalDevice::BindTexture(std::uint32_t unit, const MetalTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void MetalDevice::UnbindTexture(const MetalTexture* texture)
	{
		for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
			if (_boundTextures[unit] == texture) {
				_boundTextures[unit] = nullptr;
			}
		}
		// The open pass may draw into (or the shadow state may still name) this texture; both must let go
		if (s_encoder != nullptr && s_encoderTarget != nullptr) {
			for (std::uint32_t i = 0; i < s_encoderTarget->GetAttachedCount(); i++) {
				if (s_encoderTarget->GetColorTexture(i) == texture) {
					EndEncoder();
					break;
				}
			}
		}
		// The shadow state may still name this texture's view; a rebind on the next draw is cheaper than asking
		// the texture (whose accessors would materialize a GPU texture for an object that is being destroyed)
		for (std::uint32_t i = 0; i < 16; i++) {
			s_lastTextures[i] = nullptr;
			s_lastSamplers[i] = nullptr;
		}
	}

	const MetalTexture* MetalDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void MetalDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void MetalDevice::SetRenderTarget(MetalRenderTarget* renderTarget) { _currentRenderTarget = renderTarget; }

	void MetalDevice::UnbindRenderTarget(const MetalRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (s_encoderTarget == renderTarget && s_encoder != nullptr) {
			EndEncoder();
		}
	}

	void MetalDevice::OnRenderTargetChanged(const MetalRenderTarget* renderTarget)
	{
		if (s_encoderTarget == renderTarget && s_encoder != nullptr) {
			EndEncoder();
		}
	}

	void MetalDevice::OnShaderProgramDestroyed(MetalShaderProgram* program)
	{
		if (program == nullptr) {
			return;
		}
		const std::uint32_t handle = program->GetUniqueId();
		for (auto it = s_pipelines.begin(); it != s_pipelines.end(); ) {
			if (it->first.programHandle == handle) {
				// An in-flight command buffer that still uses the pipeline keeps it alive through its own reference
				if (s_lastPipeline == it->second) {
					s_lastPipeline = nullptr;
				}
				it->second->release();
				it = s_pipelines.erase(it);
			} else {
				++it;
			}
		}
		if (_currentProgram == program) {
			_currentProgram = nullptr;
		}
	}

	// Internal accessors used by the anonymous-namespace draw helpers
	MetalRenderTarget* MetalDevice::currentRenderTargetInternal() { return _currentRenderTarget; }
	std::uint32_t MetalDevice::MaxUniformBindingsPublic() { return MaxUniformBindings; }
	void MetalDevice::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = _boundUniformRanges[index].Data;
			size = _boundUniformRanges[index].Size;
		} else {
			data = nullptr;
			size = 0;
		}
	}

	// -- Device / layer lifecycle --

	bool MetalDevice::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
#if defined(WITH_SDL2) || defined(WITH_SDL3)
		s_vsync = vsync;
		s_sdlWindow = windowHandle;
		SDL_Window* window = reinterpret_cast<SDL_Window*>(windowHandle);

		s_device = MTL::CreateSystemDefaultDevice();
		if (s_device == nullptr) {
			LOGE("No Metal device is available on this machine (MTLCreateSystemDefaultDevice() returned null) - a virtual machine without GPU acceleration, or a Mac without Metal support");
			return false;
		}
		s_unifiedMemory = s_device->hasUnifiedMemory();
		s_queue = s_device->newCommandQueue();
		if (s_queue == nullptr) {
			LOGE("Failed to create the Metal command queue");
			DestroySwapchain();
			return false;
		}

		// The Metal view SDL attaches to the window owns the CAMetalLayer the frames are presented through
		s_metalView = SDL_Metal_CreateView(window);
		if (s_metalView == nullptr) {
			LOGE("SDL_Metal_CreateView() failed: {}", SDL_GetError());
			DestroySwapchain();
			return false;
		}
		s_layer = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(s_metalView));
		if (s_layer == nullptr) {
			LOGE("SDL_Metal_GetLayer() returned null");
			DestroySwapchain();
			return false;
		}
		MtlLayerSetup(s_layer, s_device, vsync);
		MtlLayerSetDrawableSize(s_layer, width < 1 ? 1 : width, height < 1 ? 1 : height);
		s_layerFormat = MtlLayerPixelFormat(s_layer);

		if (!CreatePresentPipeline() || !CreateDummyTexture()) {
			DestroySwapchain();
			return false;
		}

		// Uniform ring (shared storage, persistently mapped)
		s_uboRing = s_device->newBuffer(UboRingSize, MTL::ResourceStorageModeShared);
		if (s_uboRing == nullptr) {
			LOGE("Failed to create the Metal uniform ring buffer");
			DestroySwapchain();
			return false;
		}
		s_uboRingMapped = static_cast<std::uint8_t*>(s_uboRing->contents());

		if (width > 0 && height > 0) {
			if (!CreateScreenTarget(width, height)) {
				DestroySwapchain();
				return false;
			}
			SetViewport(Recti(0, 0, width, height));
			s_ready = true;
		} else {
			s_ready = false;	// minimized / zero-sized: PresentFrame() recreates once the window is restored
		}

#if defined(DEATH_TARGET_IOS)
		// (the low-power query is a macOS-only API - every iOS GPU is the integrated one)
		LOGI("Metal device ready: {} ({} memory)", s_device->name()->utf8String(), s_unifiedMemory ? "unified" : "discrete");
#else
		LOGI("Metal device ready: {} ({} memory, {} GPU)", s_device->name()->utf8String(),
			s_unifiedMemory ? "unified" : "discrete", s_device->lowPower() ? "low-power" : "high-performance");
#endif
		return true;
#else
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(vsync);
		LOGE("Metal backend requires the SDL2 or SDL3 window backend");
		return false;
#endif
	}

	void MetalDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		if (s_device == nullptr || s_layer == nullptr) {
			return;
		}
		// Idle first so the screen texture can be safely replaced; a frame being recorded is abandoned
		WaitForInFlightFrame();
		AbandonFrame();
		if (width <= 0 || height <= 0) {
			// Minimized / zero-size window: stay not-ready without recreating. PresentFrame polls the drawable
			// size each frame and recreates once the window is restored.
			s_ready = false;
			return;
		}
		MtlLayerSetDrawableSize(s_layer, width, height);
		if (CreateScreenTarget(width, height)) {
			SetViewport(Recti(0, 0, width, height));
			s_ready = true;
		} else {
			s_ready = false;
		}
	}

	void* MetalDevice::CreateSecondarySwapchain(void* windowHandle, std::int32_t width, std::int32_t height)
	{
#if defined(WITH_SDL2) || defined(WITH_SDL3)
		if (s_device == nullptr || windowHandle == nullptr) {
			return nullptr;
		}
		SecondarySwapchain* sc = new SecondarySwapchain();
		sc->SdlWindow = windowHandle;
		sc->View = SDL_Metal_CreateView(reinterpret_cast<SDL_Window*>(windowHandle));
		if (sc->View == nullptr) {
			LOGE("SDL_Metal_CreateView() failed for a secondary window: {}", SDL_GetError());
			delete sc;
			return nullptr;
		}
		sc->Layer = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(sc->View));
		if (sc->Layer == nullptr) {
			SDL_Metal_DestroyView(static_cast<SDL_MetalView>(sc->View));
			delete sc;
			return nullptr;
		}
		// Always synced: these windows carry UI, and nextDrawable's pacing is what keeps them from running ahead
		MtlLayerSetup(sc->Layer, s_device, true);
		s_secondarySwapchains.push_back(sc);
		ResizeSecondarySwapchain(sc, width, height);
		return sc;
#else
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);
		return nullptr;
#endif
	}

	void MetalDevice::DestroySecondarySwapchain(void* handle)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc == nullptr) {
			return;
		}
		// Anything queued for this chain this frame must go before its objects do
		for (std::size_t i = 0; i < s_pendingSecondaryPresents.size();) {
			if (s_pendingSecondaryPresents[i].Chain == sc) {
				s_pendingSecondaryPresents.erase(s_pendingSecondaryPresents.begin() + i);
			} else {
				i++;
			}
		}
		for (std::size_t i = 0; i < s_secondarySwapchains.size(); i++) {
			if (s_secondarySwapchains[i] == sc) {
				s_secondarySwapchains.erase(s_secondarySwapchains.begin() + i);
				break;
			}
		}
		// Its last drawable may still be presenting from the frame that just committed
		WaitForInFlightFrame();
#if defined(WITH_SDL2) || defined(WITH_SDL3)
		if (sc->View != nullptr) {
			SDL_Metal_DestroyView(static_cast<SDL_MetalView>(sc->View));
		}
#endif
		delete sc;
	}

	void MetalDevice::ResizeSecondarySwapchain(void* handle, std::int32_t width, std::int32_t height)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc == nullptr || sc->Layer == nullptr) {
			return;
		}
		if (sc->Ready && sc->Width == width && sc->Height == height) {
			return;
		}
		sc->Width = width;
		sc->Height = height;
		sc->Ready = (width > 0 && height > 0);
		if (sc->Ready) {
			MtlLayerSetDrawableSize(sc->Layer, width, height);
		}
	}

	void MetalDevice::QueueSecondaryPresent(void* handle, const MetalTexture* source)
	{
		SecondarySwapchain* sc = static_cast<SecondarySwapchain*>(handle);
		if (sc == nullptr || !sc->Ready || source == nullptr) {
			return;
		}
		for (PendingSecondaryPresent& pending : s_pendingSecondaryPresents) {
			if (pending.Chain == sc) {
				pending.Source = source;	// re-rendered within the same frame: the last content wins
				return;
			}
		}
		s_pendingSecondaryPresents.push_back(PendingSecondaryPresent{ sc, source });
	}

	void MetalDevice::PresentFrame()
	{
		// The queue of undocked ImGui windows is refilled every frame, so it must not survive this one - not even
		// through the early returns below that abandon the frame outright
		struct PendingPresentsReset
		{
			~PendingPresentsReset() {
				s_pendingSecondaryPresents.clear();
			}
		} pendingPresentsReset;

		if (s_device == nullptr) {
			return;
		}
		// Recover a lost / zero-size layer (after a minimize): try to recreate at the window's current drawable
		// size; while it is still 0x0 (minimized) drop the frame instead of blocking
		if (!s_ready) {
			std::int32_t dw = 0, dh = 0;
			QueryDrawableSize(dw, dh);
			if (dw > 0 && dh > 0) {
				ResizeSwapchain(dw, dh);
			}
			if (!s_ready) {
				AbandonFrame();
				return;
			}
		}

		// A frame with no draws still presents a (black) screen texture
		if (!s_frameActive) {
			BeginFrame();
			if (!s_frameActive) {
				return;
			}
		}
		EndEncoder();

		// The screen texture is cleared lazily by the first pass into it; a frame that never drew needs that pass
		// now so the presented image is defined
		if (s_screenNeedsClear) {
			if (EnsureEncoder(nullptr, 0, Colorf())) {
				EndEncoder();
			}
		}

		// Acquire the drawable as late as possible (it blocks while every drawable is in flight, which is what
		// paces the game to the display under vsync) and draw the screen texture into it
		CA::MetalDrawable* drawable = MtlLayerNextDrawable(s_layer);
		if (drawable != nullptr) {
			EncodePresent(drawable, s_screenTexture);
			s_commandBuffer->presentDrawable(drawable);
		}

		// Undocked ImGui windows: each one's contents were rendered into an off-screen target earlier this frame
		// (through the ordinary RHI path), so all that is left is the same present pass the main window gets. The
		// loop does not run at all while nothing is undocked.
		std::vector<CA::MetalDrawable*> secondaryDrawables;
		for (PendingSecondaryPresent& pending : s_pendingSecondaryPresents) {
			SecondarySwapchain& sc = *pending.Chain;
			if (!sc.Ready || sc.Layer == nullptr) {
				continue;
			}
			MTL::Texture* source = static_cast<MTL::Texture*>(pending.Source->GpuTexture());
			if (source == nullptr) {
				continue;
			}
			CA::MetalDrawable* secondary = MtlLayerNextDrawable(sc.Layer);
			if (secondary == nullptr) {
				continue;	// nobody is consuming this window's drawables: keep last frame's contents, retry next frame
			}
			EncodePresent(secondary, source);
			s_commandBuffer->presentDrawable(secondary);
			secondaryDrawables.push_back(secondary);
		}

		// Commit: the command buffer retains the drawables and every resource it references until it completes,
		// so our references can go now. The next BeginFrame waits for this buffer (one frame in flight).
		s_commandBuffer->commit();
		s_inFlight = s_commandBuffer;
		s_commandBuffer = nullptr;
		s_frameActive = false;
		if (drawable != nullptr) {
			drawable->release();
		}
		for (CA::MetalDrawable* secondary : secondaryDrawables) {
			secondary->release();
		}
		if (s_framePool != nullptr) {
			s_framePool->release();
			s_framePool = nullptr;
		}
	}

	void MetalDevice::DestroySwapchain()
	{
		WaitForInFlightFrame();
		AbandonFrame();
		s_ready = false;

		for (auto& kv : s_pipelines) {
			kv.second->release();
		}
		s_pipelines.clear();
		s_lastPipeline = nullptr;

		for (SecondarySwapchain* sc : s_secondarySwapchains) {
#if defined(WITH_SDL2) || defined(WITH_SDL3)
			if (sc->View != nullptr) {
				SDL_Metal_DestroyView(static_cast<SDL_MetalView>(sc->View));
			}
#endif
			delete sc;
		}
		s_secondarySwapchains.clear();

		DestroyScreenTarget();
		if (s_presentPipeline != nullptr) { s_presentPipeline->release(); s_presentPipeline = nullptr; }
		if (s_presentSampler != nullptr) { s_presentSampler->release(); s_presentSampler = nullptr; }
		if (s_presentLibrary != nullptr) { s_presentLibrary->release(); s_presentLibrary = nullptr; }
		if (s_dummySampler != nullptr) { s_dummySampler->release(); s_dummySampler = nullptr; }
		if (s_dummyTexture != nullptr) { s_dummyTexture->release(); s_dummyTexture = nullptr; }
		if (s_uboRing != nullptr) { s_uboRing->release(); s_uboRing = nullptr; s_uboRingMapped = nullptr; }
		s_frameUboByHash.clear();

#if defined(WITH_SDL2) || defined(WITH_SDL3)
		if (s_metalView != nullptr) {
			SDL_Metal_DestroyView(static_cast<SDL_MetalView>(s_metalView));
		}
#endif
		s_metalView = nullptr;
		s_layer = nullptr;
		s_sdlWindow = nullptr;

		if (s_queue != nullptr) { s_queue->release(); s_queue = nullptr; }
		if (s_device != nullptr) { s_device->release(); s_device = nullptr; }
	}
}
