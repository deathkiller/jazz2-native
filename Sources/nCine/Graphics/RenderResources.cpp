#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "RenderBuffersManager.h"
#include "RenderVaoPool.h"
#include "RenderCommandPool.h"
#include "RenderBatcher.h"
#include "Camera.h"
#include "IGfxCapabilities.h"
#include "../Application.h"
#include "../ServiceLocator.h"
#include "../../Main.h"

#include "../../Shaders/Generated/ShadersGen.h"

#include <cstddef>	// for offsetof()
#include <cstring>

using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		static const char BatchSizeFormatString[] = "#ifndef BATCH_SIZE\n\t#define BATCH_SIZE ({})\n#endif\n#line 0\n";

		// Cache-busting version of the default shader set — bump whenever any "Default*.shader" source in
		// "Sources/Shaders/" or the ShaderCompiler artifact format changes, so stale binary program caches
		// are invalidated (12 = the switch from embedded/file sources to ShaderCompiler-generated artifacts)
		static constexpr std::uint64_t DefaultShadersVersion = 22;

		struct ShaderLoad
		{
			std::unique_ptr<Rhi::ShaderProgram>& shaderProgram;
			const ShaderCompiler::Program& program;
			Rhi::ShaderProgram::Introspection introspection;
			const char* shaderName;
		};
	}

	std::unique_ptr<BinaryShaderCache> RenderResources::binaryShaderCache_;
	std::unique_ptr<RenderBuffersManager> RenderResources::buffersManager_;
	std::unique_ptr<RenderVaoPool> RenderResources::vaoPool_;
	std::unique_ptr<RenderCommandPool> RenderResources::renderCommandPool_;
	std::unique_ptr<RenderBatcher> RenderResources::renderBatcher_;

#if defined(RHI_GL_PROFILE_ES2)
	std::unique_ptr<Rhi::Buffer> RenderResources::quadCornerVbo_;
#endif

	std::unique_ptr<Rhi::ShaderProgram> RenderResources::defaultShaderPrograms_[DefaultShaderProgramsCount];
	HashMap<const Rhi::ShaderProgram*, Rhi::ShaderProgram*> RenderResources::batchedShaders_(32);

	std::uint8_t RenderResources::cameraUniformsBuffer_[UniformsBufferSize];
	HashMap<Rhi::ShaderProgram*, RenderResources::CameraUniformData> RenderResources::cameraUniformDataMap_(32);

	Camera* RenderResources::currentCamera_ = nullptr;
	std::unique_ptr<Camera> RenderResources::defaultCamera_;
	Viewport* RenderResources::currentViewport_ = nullptr;

	Rhi::ShaderProgram* RenderResources::GetShaderProgram(Material::ShaderProgramType shaderProgramType)
	{
		return (shaderProgramType != Material::ShaderProgramType::Custom ? defaultShaderPrograms_[std::int32_t(shaderProgramType)].get() : nullptr);
	}

	Rhi::ShaderProgram* RenderResources::GetBatchedShader(const Rhi::ShaderProgram* shader)
	{
		auto it = batchedShaders_.find(shader);
		return (it != batchedShaders_.end() ? it->second : nullptr);
	}

	bool RenderResources::RegisterBatchedShader(const Rhi::ShaderProgram* shader, Rhi::ShaderProgram* batchedShader)
	{
		FATAL_ASSERT(shader != nullptr);
		FATAL_ASSERT(batchedShader != nullptr);
		FATAL_ASSERT(shader != batchedShader);

		return batchedShaders_.emplace(shader, batchedShader).second;
	}

	bool RenderResources::UnregisterBatchedShader(const Rhi::ShaderProgram* shader)
	{
		DEATH_ASSERT(shader != nullptr);
		return (batchedShaders_.erase(shader) > 0);
	}

	RenderResources::CameraUniformData* RenderResources::FindCameraUniformData(Rhi::ShaderProgram* shaderProgram)
	{
		auto it = cameraUniformDataMap_.find(shaderProgram);
		return (it != cameraUniformDataMap_.end() ? &it->second : nullptr);
	}

	void RenderResources::InsertCameraUniformData(Rhi::ShaderProgram* shaderProgram, CameraUniformData&& cameraUniformData)
	{
		FATAL_ASSERT(shaderProgram != nullptr);

		//if (cameraUniformDataMap_.loadFactor() >= 0.8f)
		//	cameraUniformDataMap_.rehash(cameraUniformDataMap_.capacity() * 2);

		cameraUniformDataMap_.emplace(shaderProgram, std::move(cameraUniformData));
	}

	bool RenderResources::RemoveCameraUniformData(Rhi::ShaderProgram* shaderProgram)
	{
		return cameraUniformDataMap_.erase(shaderProgram);
	}

	void RenderResources::SetDefaultAttributesParameters(Rhi::ShaderProgram& shaderProgram)
	{
		if (shaderProgram.GetAttributeCount() <= 0) {
			return;
		}

		Rhi::VertexFormat::Attribute* positionAttribute = shaderProgram.GetAttribute(Material::PositionAttributeName);
		Rhi::VertexFormat::Attribute* texCoordsAttribute = shaderProgram.GetAttribute(Material::TexCoordsAttributeName);
		Rhi::VertexFormat::Attribute* meshIndexAttribute = shaderProgram.GetAttribute(Material::MeshIndexAttributeName);

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
		currentCamera_ = (camera != nullptr ? camera : defaultCamera_.get());
	}

	void RenderResources::UpdateCameraUniforms()
	{
		// The buffer is shared among every shader program. There is no need to call `setFloatVector()` as `setDirty()` is enough.
		std::memcpy(cameraUniformsBuffer_, currentCamera_->GetProjection().Data(), 64);
		std::memcpy(cameraUniformsBuffer_ + 64, currentCamera_->GetView().Data(), 64);
		for (auto i = cameraUniformDataMap_.begin(); i != cameraUniformDataMap_.end(); ++i) {
			CameraUniformData& cameraUniformData = i->second;

			if (cameraUniformData.camera != currentCamera_) {
				i->second.shaderUniforms.SetDirty(true);
				cameraUniformData.camera = currentCamera_;
			} else {
				if (cameraUniformData.updateFrameProjectionMatrix < currentCamera_->UpdateFrameProjectionMatrix()) {
					i->second.shaderUniforms.GetUniform(Material::ProjectionMatrixUniformName)->SetDirty(true);
				}
				if (cameraUniformData.updateFrameViewMatrix < currentCamera_->UpdateFrameViewMatrix()) {
					i->second.shaderUniforms.GetUniform(Material::ViewMatrixUniformName)->SetDirty(true);
				}
			}

			cameraUniformData.updateFrameProjectionMatrix = currentCamera_->UpdateFrameProjectionMatrix();
			cameraUniformData.updateFrameViewMatrix = currentCamera_->UpdateFrameViewMatrix();
		}
	}

	void RenderResources::SetCurrentViewport(Viewport* viewport)
	{
		FATAL_ASSERT(viewport != nullptr);
		currentViewport_ = viewport;
	}

	void RenderResources::CreateMinimal()
	{
		// `CreateMinimal()` cannot be called after `Create()`
		DEATH_ASSERT(binaryShaderCache_ == nullptr);
		DEATH_ASSERT(buffersManager_ == nullptr);
		DEATH_ASSERT(vaoPool_ == nullptr);
	
		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		binaryShaderCache_ = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		buffersManager_ = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.useBufferStorage, appCfg.vboSize, appCfg.iboSize);
		vaoPool_ = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
	}

	void RenderResources::Create()
	{
		// `Create()` can be called after `CreateMinimal()`

		const AppConfiguration& appCfg = theApplication().GetAppConfiguration();
		if (binaryShaderCache_ == nullptr) {
			binaryShaderCache_ = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		}
		if (buffersManager_ == nullptr) {
			buffersManager_ = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.useBufferStorage, appCfg.vboSize, appCfg.iboSize);
		}
		// The backend places committed uniform blocks into the streaming uniform buffer through this hook,
		// so it doesn't have to know the pipeline's buffer suballocator
		Rhi::ShaderUniformBlocks::SetUniformRangeAllocator([](std::uint32_t bytes) {
			return buffersManager_->AcquireMemory(RenderBuffersManager::BufferTypes::Uniform, bytes);
		});
		if (vaoPool_ == nullptr) {
			vaoPool_ = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
		}
		renderCommandPool_ = std::make_unique<RenderCommandPool>(appCfg.renderCommandPoolSize);
		renderBatcher_ = std::make_unique<RenderBatcher>();
		defaultCamera_ = std::make_unique<Camera>();
		currentCamera_ = defaultCamera_.get();

#if defined(RHI_GL_PROFILE_ES2)
		// The ES2 sprite/full-screen shaders read the quad corner from this static attribute instead of
		// synthesizing it from gl_VertexID. The four corners are in the order of the single-quad 4-vertex
		// TRIANGLE_STRIP draw (matching the old "vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2))").
		if (quadCornerVbo_ == nullptr) {
			static const float quadCorners[] = { 1.0f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f,  0.0f, 1.0f };
			quadCornerVbo_ = std::make_unique<Rhi::Buffer>(BufferTarget::Vertex);
			quadCornerVbo_->BufferData(sizeof(quadCorners), quadCorners, BufferUsage::StaticDraw);
			quadCornerVbo_->SetObjectLabel("QuadCornerVBO");
		}
#endif

		ShaderLoad shadersToLoad[] = {
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::Sprite)], ShadersGen::DefaultSprite, Rhi::ShaderProgram::Introspection::Enabled, "Sprite" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::SpriteNoTexture)], ShadersGen::DefaultSpriteNoTexture, Rhi::ShaderProgram::Introspection::Enabled, "Sprite_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::MeshSprite)], ShadersGen::DefaultMeshSprite, Rhi::ShaderProgram::Introspection::Enabled, "MeshSprite" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::MeshSpriteNoTexture)], ShadersGen::DefaultMeshSpriteNoTexture, Rhi::ShaderProgram::Introspection::Enabled, "MeshSprite_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedSprites)], ShadersGen::DefaultBatchedSprites, Rhi::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedSpritesNoTexture)], ShadersGen::DefaultBatchedSpritesNoTexture, Rhi::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedMeshSprites)], ShadersGen::DefaultBatchedMeshSprites, Rhi::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites" },
			{ RenderResources::defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesNoTexture)], ShadersGen::DefaultBatchedMeshSpritesNoTexture, Rhi::ShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_NoTexture" },
		};

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		std::int32_t maxUniformBlockSize = gfxCaps.GetValue(IGfxCapabilities::IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		char sourceString[64];
		StringView vertexStrings[2];

		for (std::uint32_t i = 0; i < std::uint32_t(arraySize(shadersToLoad)); i++) {
			const ShaderLoad& shaderToLoad = shadersToLoad[i];
			// All default programs use the base variant of their generated ShaderCompiler artifact
			const ShaderCompiler::ProgramVariant& variant = shaderToLoad.program.Variants[0];

			shaderToLoad.shaderProgram = std::make_unique<Rhi::ShaderProgram>(Rhi::ShaderProgram::QueryPhase::Immediate);
			// Uniforms, blocks and attributes come from the offline reflection instead of GL introspection
			shaderToLoad.shaderProgram->SetReflection(&variant);
#if defined(RHI_GL_PROFILE_ES2)
			// ES2 disables CPU batching (no UBOs), so the batched programs are never used. Their ESSL 100
			// form is also not valid ES2 (a "uint aMeshIndex" integer attribute), so skip compiling them -
			// the program object stays created (RegisterDefaultBatchedShaders only stores its pointer) but
			// unlinked, which is fine because nothing ever draws with it.
			if (shaderToLoad.introspection == Rhi::ShaderProgram::Introspection::NoUniformsInBlocks) {
				continue;
			}
#endif
			if (binaryShaderCache_->LoadFromCache(shaderToLoad.shaderName, DefaultShadersVersion, shaderToLoad.shaderProgram.get(), shaderToLoad.introspection)) {
				// Shader is already compiled and up-to-date
				continue;
			}

			// Batched shaders whose UBO is smaller than the 64 KB the in-shader BATCH_SIZE fallbacks assume get
			// their batch size from the std140 instance stride reflected offline by ShaderCompiler - this replaces
			// the probe compilation that used to run each batched shader twice
			std::int32_t batchSize = 0;
			bool hasBatchSizeDefine = false;
			if (shaderToLoad.introspection == Rhi::ShaderProgram::Introspection::NoUniformsInBlocks) {
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
						const std::int32_t offsetAlignment = gfxCaps.GetValue(IGfxCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
						std::int32_t alignedStride = instanceStride;
						if (offsetAlignment > 0) {
							alignedStride += (offsetAlignment - instanceStride % offsetAlignment) % offsetAlignment;
						}
						batchSize = maxUniformBlockSize / alignedStride;
						LOGI("Shader \"{}\" - instance stride: {} + {} align bytes, max batch size: {}", shaderToLoad.shaderName,
							instanceStride, alignedStride - instanceStride, batchSize);
						hasBatchSizeDefine = true;
					}
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
			if (shaderToLoad.introspection == Rhi::ShaderProgram::Introspection::NoUniformsInBlocks &&
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
					// Reset() on a retry clears the reflection, so it is set (again) right before linking
					shaderToLoad.shaderProgram->SetReflection(&variant);
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
			binaryShaderCache_->SaveToCache(shaderToLoad.shaderName, DefaultShadersVersion, shaderToLoad.shaderProgram.get());
		}
		RegisterDefaultBatchedShaders();

		// Calculating a default projection matrix for all shader programs
		auto res = theApplication().GetResolution();
		defaultCamera_->SetOrthoProjection(0.0f, float(res.X), 0.0f, float(res.Y));
	}

	void RenderResources::Dispose()
	{
		Rhi::ShaderUniformBlocks::SetUniformRangeAllocator(nullptr);

		for (auto& shaderProgram : defaultShaderPrograms_) {
			shaderProgram.reset(nullptr);
		}

		DEATH_ASSERT(cameraUniformDataMap_.empty());

		defaultCamera_.reset(nullptr);
		renderBatcher_.reset(nullptr);
		renderCommandPool_.reset(nullptr);
		vaoPool_.reset(nullptr);
		buffersManager_.reset(nullptr);

		LOGI("Rendering resources disposed");
	}

	void RenderResources::RegisterDefaultBatchedShaders()
	{
		batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::Sprite)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedSprites)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::SpriteGray)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedSpritesGray)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::SpriteNoTexture)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedSpritesNoTexture)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::MeshSprite)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedMeshSprites)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::MeshSpriteGray)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesGray)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::MeshSpriteNoTexture)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedMeshSpritesNoTexture)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::TextNodeAlpha)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedTextNodesAlpha)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::TextNodeRed)].get(), defaultShaderPrograms_[std::int32_t(Material::ShaderProgramType::BatchedTextNodesRed)].get());
	}
}
