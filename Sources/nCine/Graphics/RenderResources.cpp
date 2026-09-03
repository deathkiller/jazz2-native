#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "RenderBuffersManager.h"
#include "RenderVaoPool.h"
#include "RenderCommandPool.h"
#include "RenderBatcher.h"
#include "Camera.h"
#include "RHI/IRhiCapabilities.h"
#include "../Application.h"
#include "../ServiceLocator.h"
#include "../../Main.h"

#include "../../Shaders/Generated/ShadersGen.h"

#include <algorithm>
#include <cstddef>	// for offsetof()
#include <cstring>

using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		static const char BatchSizeFormatString[] = "#ifndef BATCH_SIZE\n\t#define BATCH_SIZE ({})\n#endif\n#line 0\n";

		// Cache-busting version of the default shader set - bump whenever any "Default*.shader" source in
		// "Sources/Shaders/" or the ShaderCompiler artifact format changes, so stale binary program caches
		// are invalidated (12 = the switch from embedded/file sources to ShaderCompiler-generated artifacts)
		static constexpr std::uint64_t DefaultShadersVersion = 13;

		struct ShaderLoad
		{
			std::unique_ptr<RHI::ShaderProgram>& shaderProgram;
			const ShaderCompiler::Program& program;
			RHI::ShaderProgram::Introspection introspection;
			const char* shaderName;
		};
	}

	std::unique_ptr<BinaryShaderCache> RenderResources::_binaryShaderCache;
	std::unique_ptr<RenderBuffersManager> RenderResources::_buffersManager;
	std::unique_ptr<RenderVaoPool> RenderResources::_vaoPool;
	std::unique_ptr<RenderCommandPool> RenderResources::_renderCommandPool;
	std::unique_ptr<RenderBatcher> RenderResources::_renderBatcher;

#if defined(RHI_GL_PROFILE_ES2)
	std::unique_ptr<RHI::Buffer> RenderResources::_quadCornerVbo;
#endif

	std::unique_ptr<std::uint16_t[]> RenderResources::_quadIndices;

	std::unique_ptr<RHI::ShaderProgram> RenderResources::_defaultShaderPrograms[DefaultShaderProgramsCount];
	HashMap<const RHI::ShaderProgram*, RHI::ShaderProgram*> RenderResources::_batchedShaders(32);

	std::uint8_t RenderResources::_cameraUniformsBuffer[UniformsBufferSize];
	HashMap<RHI::ShaderProgram*, RenderResources::CameraUniformData> RenderResources::_cameraUniformDataMap(32);

	Camera* RenderResources::_currentCamera = nullptr;
	std::unique_ptr<Camera> RenderResources::_defaultCamera;
	Viewport* RenderResources::_currentViewport = nullptr;

	std::uint32_t RenderResources::GetMaxQuadsForIndices()
	{
		// A 16-bit index addresses 65536 corners, and sceGxm silently drops a draw whose indices reach past
		// 63999 (see GxmDevice::DrawCommon()), so the guard sits below both. Neither bound is reached with the
		// shipped buffer sizes - the element buffer runs out long before - but a larger one must not pass it.
		constexpr std::uint32_t MaxIndexableQuads = 63999 / VerticesPerQuad;

		const std::uint32_t maxIndexDataSize = _buffersManager->Specs(RenderBuffersManager::BufferTypes::ElementArray).maxSize;
		return std::min<std::uint32_t>(maxIndexDataSize / (IndicesPerQuad * std::uint32_t(sizeof(std::uint16_t))), MaxIndexableQuads);
	}

	std::uint32_t RenderResources::GetMaxQuadsPerDraw(std::uint32_t floatsPerVertex)
	{
		DEATH_ASSERT(floatsPerVertex > 0);

		const std::uint32_t maxVertexDataSize = _buffersManager->Specs(RenderBuffersManager::BufferTypes::Array).maxSize;
		const std::uint32_t vertexBytesPerQuad = VerticesPerQuad * floatsPerVertex * std::uint32_t(sizeof(float));
		// Buffers too small for even a single quad would leave a caller's chunk loop making no progress at all.
		// Reporting one quad instead lets the buffers manager name the buffer that could not hold it.
		return std::max<std::uint32_t>(std::min<std::uint32_t>(maxVertexDataSize / vertexBytesPerQuad, GetMaxQuadsForIndices()), 1);
	}

	const std::uint16_t* RenderResources::GetQuadIndices()
	{
		if (_quadIndices == nullptr) {
			// Sized for the longest draw any mesh can submit, so it is written once and never moves afterwards
			const std::uint32_t quadCount = std::max<std::uint32_t>(GetMaxQuadsForIndices(), 1);
			_quadIndices = std::make_unique<std::uint16_t[]>(std::size_t(quadCount) * IndicesPerQuad);

			std::uint16_t* dst = _quadIndices.get();
			for (std::uint32_t quad = 0, firstVertex = 0; quad < quadCount; quad++, firstVertex += VerticesPerQuad) {
				*dst++ = std::uint16_t(firstVertex);
				*dst++ = std::uint16_t(firstVertex + 1);
				*dst++ = std::uint16_t(firstVertex + 2);
				*dst++ = std::uint16_t(firstVertex);
				*dst++ = std::uint16_t(firstVertex + 2);
				*dst++ = std::uint16_t(firstVertex + 3);
			}
		}

		return _quadIndices.get();
	}

	RHI::ShaderProgram* RenderResources::GetShaderProgram(Material::ShaderProgramType shaderProgramType)
	{
		return (shaderProgramType != Material::ShaderProgramType::Custom ? _defaultShaderPrograms[std::int32_t(shaderProgramType)].get() : nullptr);
	}

	RHI::ShaderProgram* RenderResources::GetBatchedShader(const RHI::ShaderProgram* shader)
	{
#if !defined(RHI_CAP_BATCHING)
		// The backend does not batch. Reporting no batched counterpart is the whole mechanism: RenderBatcher
		// only collects a run when one exists, so every command then passes through on its own. Doing it here
		// covers nCine's and the game's registrations alike, without touching either one's call sites.
		static_cast<void>(shader);
		return nullptr;
#else
		auto it = _batchedShaders.find(shader);
		return (it != _batchedShaders.end() ? it->second : nullptr);
#endif
	}

	bool RenderResources::RegisterBatchedShader(const RHI::ShaderProgram* shader, RHI::ShaderProgram* batchedShader)
	{
		FATAL_ASSERT(shader != nullptr);
		FATAL_ASSERT(batchedShader != nullptr);
		FATAL_ASSERT(shader != batchedShader);

		return _batchedShaders.emplace(shader, batchedShader).second;
	}

	bool RenderResources::UnregisterBatchedShader(const RHI::ShaderProgram* shader)
	{
		DEATH_ASSERT(shader != nullptr);
		return (_batchedShaders.erase(shader) > 0);
	}

	RenderResources::CameraUniformData* RenderResources::FindCameraUniformData(RHI::ShaderProgram* shaderProgram)
	{
		auto it = _cameraUniformDataMap.find(shaderProgram);
		return (it != _cameraUniformDataMap.end() ? &it->second : nullptr);
	}

	void RenderResources::InsertCameraUniformData(RHI::ShaderProgram* shaderProgram, CameraUniformData&& cameraUniformData)
	{
		FATAL_ASSERT(shaderProgram != nullptr);

		//if (_cameraUniformDataMap.loadFactor() >= 0.8f)
		//	_cameraUniformDataMap.rehash(_cameraUniformDataMap.capacity() * 2);

		_cameraUniformDataMap.emplace(shaderProgram, std::move(cameraUniformData));
	}

	bool RenderResources::RemoveCameraUniformData(RHI::ShaderProgram* shaderProgram)
	{
		return _cameraUniformDataMap.erase(shaderProgram);
	}

	void RenderResources::SetDefaultAttributesParameters(RHI::ShaderProgram& shaderProgram)
	{
		if (shaderProgram.GetAttributeCount() <= 0) {
			return;
		}

		RHI::VertexFormat::Attribute* positionAttribute = shaderProgram.GetAttribute(Material::PositionAttributeName);
		RHI::VertexFormat::Attribute* texCoordsAttribute = shaderProgram.GetAttribute(Material::TexCoordsAttributeName);
		RHI::VertexFormat::Attribute* meshIndexAttribute = shaderProgram.GetAttribute(Material::MeshIndexAttributeName);

		// The stride check avoid overwriting VBO parameters for custom mesh shaders attributes
		if (positionAttribute != nullptr && texCoordsAttribute != nullptr && meshIndexAttribute != nullptr) {
			if (positionAttribute->GetStride() == 0) {
				positionAttribute->SetVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, position)));
			}
			if (texCoordsAttribute->GetStride() == 0) {
				texCoordsAttribute->SetVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, texcoords)));
			}
			if (meshIndexAttribute->GetStride() == 0) {
				meshIndexAttribute->SetVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, drawindex)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute == nullptr && meshIndexAttribute != nullptr) {
			if (positionAttribute->GetStride() == 0) {
				positionAttribute->SetVboParameters(sizeof(VertexFormatPos2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Index, position)));
			}
			if (meshIndexAttribute->GetStride() == 0) {
				meshIndexAttribute->SetVboParameters(sizeof(VertexFormatPos2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Index, drawindex)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute != nullptr && meshIndexAttribute == nullptr) {
			if (positionAttribute->GetStride() == 0) {
				positionAttribute->SetVboParameters(sizeof(VertexFormatPos2Tex2), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2, position)));
			}
			if (texCoordsAttribute->GetStride() == 0) {
				texCoordsAttribute->SetVboParameters(sizeof(VertexFormatPos2Tex2), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2, texcoords)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute == nullptr && meshIndexAttribute == nullptr) {
			if (positionAttribute->GetStride() == 0) {
				positionAttribute->SetVboParameters(sizeof(VertexFormatPos2), reinterpret_cast<void*>(offsetof(VertexFormatPos2, position)));
			}
		}
	}

	void RenderResources::SetCurrentCamera(Camera* camera)
	{
		_currentCamera = (camera != nullptr ? camera : _defaultCamera.get());
	}

	void RenderResources::UpdateCameraUniforms()
	{
		// The buffer is shared among every shader program. There is no need to call `setFloatVector()` as `setDirty()` is enough.
		std::memcpy(_cameraUniformsBuffer, _currentCamera->GetProjection().Data(), 64);
		std::memcpy(_cameraUniformsBuffer + 64, _currentCamera->GetView().Data(), 64);
		for (auto i = _cameraUniformDataMap.begin(); i != _cameraUniformDataMap.end(); ++i) {
			CameraUniformData& cameraUniformData = i->second;

			if (cameraUniformData.camera != _currentCamera) {
				i->second.shaderUniforms.SetDirty(true);
				cameraUniformData.camera = _currentCamera;
			} else {
				if (cameraUniformData.updateFrameProjectionMatrix < _currentCamera->UpdateFrameProjectionMatrix()) {
					i->second.shaderUniforms.GetUniform(Material::ProjectionMatrixUniformName)->SetDirty(true);
				}
				if (cameraUniformData.updateFrameViewMatrix < _currentCamera->UpdateFrameViewMatrix()) {
					i->second.shaderUniforms.GetUniform(Material::ViewMatrixUniformName)->SetDirty(true);
				}
			}

			cameraUniformData.updateFrameProjectionMatrix = _currentCamera->UpdateFrameProjectionMatrix();
			cameraUniformData.updateFrameViewMatrix = _currentCamera->UpdateFrameViewMatrix();
		}
	}

	void RenderResources::SetCurrentViewport(Viewport* viewport)
	{
		FATAL_ASSERT(viewport != nullptr);
		_currentViewport = viewport;
	}

	void RenderResources::CreateMinimal()
	{
		// `CreateMinimal()` cannot be called after `Create()`
		DEATH_ASSERT(_binaryShaderCache == nullptr);
		DEATH_ASSERT(_buffersManager == nullptr);
		DEATH_ASSERT(_vaoPool == nullptr);
	
		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		_binaryShaderCache = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		_buffersManager = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.useBufferStorage, appCfg.vboSize, appCfg.iboSize);
		_vaoPool = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
	}

	void RenderResources::Create()
	{
		// `Create()` can be called after `CreateMinimal()`

		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		if (_binaryShaderCache == nullptr) {
			_binaryShaderCache = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		}
		if (_buffersManager == nullptr) {
			_buffersManager = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.useBufferStorage, appCfg.vboSize, appCfg.iboSize);
		}
		// The backend places committed uniform blocks into the streaming uniform buffer through this hook,
		// so it doesn't have to know the pipeline's buffer suballocator
		RHI::ShaderUniformBlocks::SetUniformRangeAllocator([](std::uint32_t bytes) {
			return _buffersManager->AcquireMemory(RenderBuffersManager::BufferTypes::Uniform, bytes);
		});
		if (_vaoPool == nullptr) {
			_vaoPool = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
		}
		_renderCommandPool = std::make_unique<RenderCommandPool>(appCfg.renderCommandPoolSize);
		_renderBatcher = std::make_unique<RenderBatcher>();
		_defaultCamera = std::make_unique<Camera>();
		_currentCamera = _defaultCamera.get();

#if defined(RHI_GL_PROFILE_ES2)
		// The ES2 sprite/full-screen shaders read the quad corner from this static attribute instead of
		// synthesizing it from gl_VertexID. The four corners are in the order of the single-quad 4-vertex
		// TRIANGLE_STRIP draw (matching the old "vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2))").
		if (_quadCornerVbo == nullptr) {
			static const float quadCorners[] = { 1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f,  0.0f, 1.0f };
			_quadCornerVbo = std::make_unique<RHI::Buffer>(BufferTarget::Vertex);
			_quadCornerVbo->BufferData(sizeof(quadCorners), quadCorners, BufferUsage::StaticDraw);
			_quadCornerVbo->SetObjectLabel("QuadCornerVBO");
		}
#endif

		ShaderLoad shadersToLoad[] = {
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::Sprite)], ShadersGen::DefaultSprite, RHI::ShaderProgram::Introspection::Enabled, "Sprite" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::SpriteNoTexture)], ShadersGen::DefaultSpriteNoTexture, RHI::ShaderProgram::Introspection::Enabled, "Sprite_NoTexture" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::MeshSprite)], ShadersGen::DefaultMeshSprite, RHI::ShaderProgram::Introspection::Enabled, "MeshSprite" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::MeshSpriteNoTexture)], ShadersGen::DefaultMeshSpriteNoTexture, RHI::ShaderProgram::Introspection::Enabled, "MeshSprite_NoTexture" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedSprites)], ShadersGen::DefaultBatchedSprites, RHI::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedSpritesNoTexture)], ShadersGen::DefaultBatchedSpritesNoTexture, RHI::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_NoTexture" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedMeshSprites)], ShadersGen::DefaultBatchedMeshSprites, RHI::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites" },
			{ RenderResources::_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesNoTexture)], ShadersGen::DefaultBatchedMeshSpritesNoTexture, RHI::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_NoTexture" },
		};

		const RHI::IRhiCapabilities& caps = theServiceLocator().GetRhiCapabilities();
		std::int32_t maxUniformBlockSize = caps.GetValue(RHI::IRhiCapabilities::IntValues::MaxUniformBlockSizeNormalized);
		// A backend whose instance array is not a bindable buffer cannot address more instances than its
		// shaders were compiled for, however much block space it publishes (see IntValues::MaxBatchSize)
		const std::int32_t maxBatchSize = caps.GetValue(RHI::IRhiCapabilities::IntValues::MaxBatchSize);

		char sourceString[64];
		StringView vertexStrings[2];

		for (std::uint32_t i = 0; i < std::uint32_t(arraySize(shadersToLoad)); i++) {
			const ShaderLoad& shaderToLoad = shadersToLoad[i];
			// All default programs use the base variant of their generated ShaderCompiler artifact
			const ShaderCompiler::ProgramVariant& variant = shaderToLoad.program.Variants[0];

			shaderToLoad.shaderProgram = std::make_unique<RHI::ShaderProgram>(RHI::ShaderProgram::QueryPhase::Immediate);
			// Uniforms, blocks and attributes come from the offline reflection instead of GL introspection
			shaderToLoad.shaderProgram->SetReflection(&variant);
			// The generated program name + variant name are the identity the fixed-function console
			// backends resolve their generated effect tables from (the shaderName is only a label)
			shaderToLoad.shaderProgram->SetProgramIdentity(shaderToLoad.program.Name, variant.Name);
#if !defined(RHI_CAP_BATCHING)
			// Where the backend does not batch, the batched programs are never used, so skip compiling them -
			// the program object stays created (RegisterDefaultBatchedShaders only stores its pointer) but
			// unlinked, which is fine because nothing ever draws with it. NoUniformsInBlocks is what marks a
			// batched program here. On ES2 this is not merely an optimization: their ESSL 100 form is not
			// valid there at all (a "uint aMeshIndex" integer attribute), so compiling them would fail.
			if (shaderToLoad.introspection == RHI::ShaderProgram::Introspection::NoUniformsInBlocks) {
				continue;
			}
#endif
			if (_binaryShaderCache->LoadFromCache(shaderToLoad.shaderName, DefaultShadersVersion, shaderToLoad.shaderProgram.get(), shaderToLoad.introspection)) {
				// Shader is already compiled and up-to-date
				continue;
			}

			// Batched shaders whose UBO is smaller than the 64 KB the in-shader BATCH_SIZE fallbacks assume get
			// their batch size from the std140 instance stride reflected offline by ShaderCompiler - this replaces
			// the probe compilation that used to run each batched shader twice
			std::int32_t batchSize = 0;
			bool hasBatchSizeDefine = false;
			if (shaderToLoad.introspection == RHI::ShaderProgram::Introspection::NoUniformsInBlocks) {
				if (appCfg.fixedBatchSize > 0) {
					batchSize = appCfg.fixedBatchSize;
					hasBatchSizeDefine = true;
				} else if (maxUniformBlockSize < 64 * 1024) {
					std::int32_t instanceStride = 0;
					for (std::size_t j = 0; j < variant.BlockCount; j++) {
						if (variant.Blocks[j].InstanceStride > 0) {
							instanceStride = std::int32_t(variant.Blocks[j].InstanceStride);
							break;
						}
					}
					DEATH_ASSERT(instanceStride > 0);
					if (instanceStride > 0) {
						// The whole per-batch block is suballocated from a uniform buffer, so its size has to
						// respect the uniform buffer offset alignment, exactly like the introspected size did
						const std::int32_t offsetAlignment = caps.GetValue(RHI::IRhiCapabilities::IntValues::UniformBufferOffsetAlignment);
						std::int32_t alignedStride = instanceStride;
						if (offsetAlignment > 0) {
							alignedStride += (offsetAlignment - instanceStride % offsetAlignment) % offsetAlignment;
						}
						batchSize = maxUniformBlockSize / alignedStride;
						if (maxBatchSize > 0 && batchSize > maxBatchSize) {
							batchSize = maxBatchSize;
						}
						LOGI("Shader \"{}\" - instance stride: {} + {} align bytes, max batch size: {}", shaderToLoad.shaderName,
							instanceStride, alignedStride - instanceStride, batchSize);
						hasBatchSizeDefine = true;
					}
				}

				// The ceiling the device published bounds whatever the steps above arrived at - including the
				// case where they arrived at nothing. A batch size of 0 here means "let the in-shader
				// BATCH_SIZE fallback decide", and that fallback is the 64 KB-worth of instances, which is
				// always above a ceiling a device bothered to publish. Leaving it at 0 would compile the
				// instance array for 64 KB while RenderBatcher fills only `maxBatchSize` instances of it,
				// and the bound buffer range would then be smaller than the block the program declares -
				// undefined on desktop GL, a rejected draw on WebGL.
				if (maxBatchSize > 0 && (batchSize <= 0 || batchSize > maxBatchSize)) {
					batchSize = maxBatchSize;
					hasBatchSizeDefine = true;
				}
			}

			// OpenGL|ES 2.0 profile consumes the ESSL 100 (Essl100Emitter) stage sources baked alongside the
			// modern ones; the GL 3.3 / ES 3.0 path uses the modern VsSource/FsSource byte-for-byte unchanged.
			const char* vsSource = variant.VsSource;
			const char* fsSource = variant.FsSource;
#if defined(RHI_GL_PROFILE_ES2)
			if (variant.VsSource100 != nullptr) {
				vsSource = variant.VsSource100;
			}
			if (variant.FsSource100 != nullptr) {
				fsSource = variant.FsSource100;
			}
			// ES2 has no UBOs: the InstancesBlock becomes a "uniform Instance instances[BATCH_SIZE];" array
			// that must fit in the (small) ES2 vertex uniform space, so force a small batch regardless of the
			// reported max uniform block size (desktop ANGLE reports 64 KB, which would keep the 682 default).
			if (shaderToLoad.introspection == RHI::ShaderProgram::Introspection::NoUniformsInBlocks &&
				(batchSize <= 0 || batchSize > 12)) {
				batchSize = 8;
				hasBatchSizeDefine = true;
			}
#endif

			bool hasLinked = false;
			bool isRetry = false;
			while (true) {
				if (isRetry) {
					shaderToLoad.shaderProgram->Reset();
				}

				std::int32_t stringsCount = 0;
				if (hasBatchSizeDefine) {
					shaderToLoad.shaderProgram->SetBatchSize(batchSize);
					std::size_t length = formatInto(sourceString, BatchSizeFormatString, batchSize);
					vertexStrings[stringsCount++] = { sourceString, length };
				}
				vertexStrings[stringsCount++] = vsSource;

				bool vertexCompiled = shaderToLoad.shaderProgram->AttachShaderFromStrings(ShaderStage::Vertex, arrayView(vertexStrings, stringsCount));
				// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
				// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
				vertexStrings[stringsCount - 1] = fsSource;
				bool fragmentCompiled = shaderToLoad.shaderProgram->AttachShaderFromStrings(ShaderStage::Fragment, arrayView(vertexStrings, stringsCount));
				if (vertexCompiled && fragmentCompiled) {
					shaderToLoad.shaderProgram->SetObjectLabel(shaderToLoad.shaderName);
					// Reset() on a retry clears the reflection and the identity, so both are set (again)
					// right before linking
					shaderToLoad.shaderProgram->SetReflection(&variant);
					shaderToLoad.shaderProgram->SetProgramIdentity(shaderToLoad.program.Name, variant.Name);
					hasLinked = shaderToLoad.shaderProgram->Link(shaderToLoad.introspection);
				}
				if (hasLinked || !hasBatchSizeDefine || batchSize <= 1) {
					break;
				}

				batchSize--;
				isRetry = true;
				LOGW("Failed to compile the shader, recompiling with batch size: {}", batchSize);
			}

			FATAL_ASSERT_MSG(hasLinked, "Failed to compile shader \"{}\"", shaderToLoad.shaderName);
			_binaryShaderCache->SaveToCache(shaderToLoad.shaderName, DefaultShadersVersion, shaderToLoad.shaderProgram.get());
		}
		RegisterDefaultBatchedShaders();

		// Calculating a default projection matrix for all shader programs
		auto res = theApplication().GetResolution();
		_defaultCamera->SetOrthoProjection(0.0f, float(res.X), 0.0f, float(res.Y));
	}

	void RenderResources::Dispose()
	{
		RHI::ShaderUniformBlocks::SetUniformRangeAllocator(nullptr);

		for (auto& shaderProgram : _defaultShaderPrograms) {
			shaderProgram.reset(nullptr);
		}

		DEATH_ASSERT(_cameraUniformDataMap.empty());

		_quadIndices.reset(nullptr);

		_defaultCamera.reset(nullptr);
		_renderBatcher.reset(nullptr);
		_renderCommandPool.reset(nullptr);
		_vaoPool.reset(nullptr);
		_buffersManager.reset(nullptr);

		LOGI("Rendering resources disposed");
	}

	void RenderResources::RegisterDefaultBatchedShaders()
	{
		_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::Sprite)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedSprites)].get());
		//_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::SpriteGray)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedSpritesGray)].get());
		_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::SpriteNoTexture)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedSpritesNoTexture)].get());
		_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::MeshSprite)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedMeshSprites)].get());
		//_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::MeshSpriteGray)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesGray)].get());
		_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::MeshSpriteNoTexture)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesNoTexture)].get());
		//_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::TextNodeAlpha)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedTextNodesAlpha)].get());
		//_batchedShaders.emplace(_defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::TextNodeRed)].get(), _defaultShaderPrograms[std::int32_t(Material::ShaderProgramType::BatchedTextNodesRed)].get());
	}
}
