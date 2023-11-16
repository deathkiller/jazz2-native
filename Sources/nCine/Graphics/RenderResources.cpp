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
#include "../../Common.h"

#include <cstddef>	// for `offsetof()`
#include <cstring>

#if defined(WITH_EMBEDDED_SHADERS)
#	include "shader_strings.h"
#else
#	include <IO/FileSystem.h>
using namespace Death::IO;
#endif

using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		static const char BatchSizeFormatString[] = "#ifndef BATCH_SIZE\n\t#define BATCH_SIZE (%d)\n#endif\n#line 0\n";

		struct ShaderLoad
		{
			std::unique_ptr<GLShaderProgram>& shaderProgram;
			const char* vertexShader;
			const char* fragmentShader;
			GLShaderProgram::Introspection introspection;
			const char* shaderName;
		};
	}

	std::unique_ptr<BinaryShaderCache> RenderResources::binaryShaderCache_;
	std::unique_ptr<RenderBuffersManager> RenderResources::buffersManager_;
	std::unique_ptr<RenderVaoPool> RenderResources::vaoPool_;
	std::unique_ptr<RenderCommandPool> RenderResources::renderCommandPool_;
	std::unique_ptr<RenderBatcher> RenderResources::renderBatcher_;

	std::unique_ptr<GLShaderProgram> RenderResources::defaultShaderPrograms_[DefaultShaderProgramsCount];
	HashMap<const GLShaderProgram*, GLShaderProgram*> RenderResources::batchedShaders_(32);

	unsigned char RenderResources::cameraUniformsBuffer_[UniformsBufferSize];
	HashMap<GLShaderProgram*, RenderResources::CameraUniformData> RenderResources::cameraUniformDataMap_(32);

	Camera* RenderResources::currentCamera_ = nullptr;
	std::unique_ptr<Camera> RenderResources::defaultCamera_;
	Viewport* RenderResources::currentViewport_ = nullptr;

	GLShaderProgram* RenderResources::shaderProgram(Material::ShaderProgramType shaderProgramType)
	{
		return (shaderProgramType != Material::ShaderProgramType::CUSTOM ? defaultShaderPrograms_[static_cast<int>(shaderProgramType)].get() : nullptr);
	}

	GLShaderProgram* RenderResources::batchedShader(const GLShaderProgram* shader)
	{
		auto it = batchedShaders_.find(shader);
		return (it != batchedShaders_.end() ? it->second : nullptr);
	}

	bool RenderResources::registerBatchedShader(const GLShaderProgram* shader, GLShaderProgram* batchedShader)
	{
		FATAL_ASSERT(shader != nullptr);
		FATAL_ASSERT(batchedShader != nullptr);
		FATAL_ASSERT(shader != batchedShader);

		return batchedShaders_.emplace(shader, batchedShader).second;
	}

	bool RenderResources::unregisterBatchedShader(const GLShaderProgram* shader)
	{
		ASSERT(shader != nullptr);
		return (batchedShaders_.erase(shader) > 0);
	}

	RenderResources::CameraUniformData* RenderResources::findCameraUniformData(GLShaderProgram* shaderProgram)
	{
		auto it = cameraUniformDataMap_.find(shaderProgram);
		return (it != cameraUniformDataMap_.end() ? &it->second : nullptr);
	}

	void RenderResources::insertCameraUniformData(GLShaderProgram* shaderProgram, CameraUniformData&& cameraUniformData)
	{
		FATAL_ASSERT(shaderProgram != nullptr);

		//if (cameraUniformDataMap_.loadFactor() >= 0.8f)
		//	cameraUniformDataMap_.rehash(cameraUniformDataMap_.capacity() * 2);

		cameraUniformDataMap_.emplace(shaderProgram, std::move(cameraUniformData));
	}

	bool RenderResources::removeCameraUniformData(GLShaderProgram* shaderProgram)
	{
		return cameraUniformDataMap_.erase(shaderProgram);
	}

	void RenderResources::setDefaultAttributesParameters(GLShaderProgram& shaderProgram)
	{
		if (shaderProgram.numAttributes() <= 0) {
			return;
		}

		GLVertexFormat::Attribute* positionAttribute = shaderProgram.attribute(Material::PositionAttributeName);
		GLVertexFormat::Attribute* texCoordsAttribute = shaderProgram.attribute(Material::TexCoordsAttributeName);
		GLVertexFormat::Attribute* meshIndexAttribute = shaderProgram.attribute(Material::MeshIndexAttributeName);

		// The stride check avoid overwriting VBO parameters for custom mesh shaders attributes
		if (positionAttribute != nullptr && texCoordsAttribute != nullptr && meshIndexAttribute != nullptr) {
			if (positionAttribute->stride() == 0) {
				positionAttribute->setVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, position)));
			}
			if (texCoordsAttribute->stride() == 0) {
				texCoordsAttribute->setVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, texcoords)));
			}
			if (meshIndexAttribute->stride() == 0) {
				meshIndexAttribute->setVboParameters(sizeof(VertexFormatPos2Tex2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2Index, drawindex)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute == nullptr && meshIndexAttribute != nullptr) {
			if (positionAttribute->stride() == 0) {
				positionAttribute->setVboParameters(sizeof(VertexFormatPos2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Index, position)));
			}
			if (meshIndexAttribute->stride() == 0) {
				meshIndexAttribute->setVboParameters(sizeof(VertexFormatPos2Index), reinterpret_cast<void*>(offsetof(VertexFormatPos2Index, drawindex)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute != nullptr && meshIndexAttribute == nullptr) {
			if (positionAttribute->stride() == 0) {
				positionAttribute->setVboParameters(sizeof(VertexFormatPos2Tex2), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2, position)));
			}
			if (texCoordsAttribute->stride() == 0) {
				texCoordsAttribute->setVboParameters(sizeof(VertexFormatPos2Tex2), reinterpret_cast<void*>(offsetof(VertexFormatPos2Tex2, texcoords)));
			}
		} else if (positionAttribute != nullptr && texCoordsAttribute == nullptr && meshIndexAttribute == nullptr) {
			if (positionAttribute->stride() == 0) {
				positionAttribute->setVboParameters(sizeof(VertexFormatPos2), reinterpret_cast<void*>(offsetof(VertexFormatPos2, position)));
			}
		}
	}

	void RenderResources::setCurrentCamera(Camera* camera)
	{
		currentCamera_ = (camera != nullptr ? camera : defaultCamera_.get());
	}

	void RenderResources::updateCameraUniforms()
	{
		// The buffer is shared among every shader program. There is no need to call `setFloatVector()` as `setDirty()` is enough.
		std::memcpy(cameraUniformsBuffer_, currentCamera_->projection().Data(), 64);
		std::memcpy(cameraUniformsBuffer_ + 64, currentCamera_->view().Data(), 64);
		for (auto i = cameraUniformDataMap_.begin(); i != cameraUniformDataMap_.end(); ++i) {
			CameraUniformData& cameraUniformData = i->second;

			if (cameraUniformData.camera != currentCamera_) {
				i->second.shaderUniforms.setDirty(true);
				cameraUniformData.camera = currentCamera_;
			} else {
				if (cameraUniformData.updateFrameProjectionMatrix < currentCamera_->updateFrameProjectionMatrix()) {
					i->second.shaderUniforms.uniform(Material::ProjectionMatrixUniformName)->setDirty(true);
				}
				if (cameraUniformData.updateFrameViewMatrix < currentCamera_->updateFrameViewMatrix()) {
					i->second.shaderUniforms.uniform(Material::ViewMatrixUniformName)->setDirty(true);
				}
			}

			cameraUniformData.updateFrameProjectionMatrix = currentCamera_->updateFrameProjectionMatrix();
			cameraUniformData.updateFrameViewMatrix = currentCamera_->updateFrameViewMatrix();
		}
	}

	void RenderResources::setCurrentViewport(Viewport* viewport)
	{
		FATAL_ASSERT(viewport != nullptr);
		currentViewport_ = viewport;
	}

	void RenderResources::createMinimal()
	{
		// `createMinimal()` cannot be called after `create()`
		ASSERT(binaryShaderCache_ == nullptr);
		ASSERT(buffersManager_ == nullptr);
		ASSERT(vaoPool_ == nullptr);
	
		const AppConfiguration& appCfg = theApplication().appConfiguration();
		binaryShaderCache_ = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		buffersManager_ = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.vboSize, appCfg.iboSize);
		vaoPool_ = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
	}
	
	void RenderResources::create()
	{
		// `create()` can be called after `createMinimal()`

		const AppConfiguration& appCfg = theApplication().appConfiguration();
		if (binaryShaderCache_ == nullptr) {
			binaryShaderCache_ = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		}
		if (buffersManager_ == nullptr) {
			buffersManager_ = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.vboSize, appCfg.iboSize);
		}
		if (vaoPool_ == nullptr) {
			vaoPool_ = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
		}
		renderCommandPool_ = std::make_unique<RenderCommandPool>(appCfg.vaoPoolSize);
		renderBatcher_ = std::make_unique<RenderBatcher>();
		defaultCamera_ = std::make_unique<Camera>();
		currentCamera_ = defaultCamera_.get();

		ShaderLoad shadersToLoad[] = {
#if defined(WITH_EMBEDDED_SHADERS)
						// Skipping the initial new line character of the raw string literal
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE)], ShaderStrings::sprite_vs + 1, ShaderStrings::sprite_fs + 1, GLShaderProgram::Introspection::Enabled, "Sprite" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_GRAY)], ShaderStrings::sprite_vs + 1, ShaderStrings::sprite_gray_fs + 1, GLShaderProgram::Introspection::Enabled, "Sprite_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_NO_TEXTURE)], ShaderStrings::sprite_notexture_vs + 1, ShaderStrings::sprite_notexture_fs + 1, GLShaderProgram::Introspection::Enabled, "Sprite_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE)], ShaderStrings::meshsprite_vs + 1, ShaderStrings::sprite_fs + 1, GLShaderProgram::Introspection::Enabled, "MeshSprite" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_GRAY)], ShaderStrings::meshsprite_vs + 1, ShaderStrings::sprite_gray_fs + 1, GLShaderProgram::Introspection::Enabled, "MeshSprite_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_NO_TEXTURE)], ShaderStrings::meshsprite_notexture_vs + 1, ShaderStrings::sprite_notexture_fs + 1, GLShaderProgram::Introspection::Enabled, "MeshSprite_NoTexture" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_ALPHA)], ShaderStrings::textnode_vs + 1, ShaderStrings::textnode_alpha_fs + 1, GLShaderProgram::Introspection::Enabled, "TextNode_Alpha" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_RED)], ShaderStrings::textnode_vs + 1, ShaderStrings::textnode_red_fs + 1, GLShaderProgram::Introspection::Enabled, "TextNode_Red" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES)], ShaderStrings::batched_sprites_vs + 1, ShaderStrings::sprite_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_GRAY)], ShaderStrings::batched_sprites_vs + 1, ShaderStrings::sprite_gray_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_NO_TEXTURE)], ShaderStrings::batched_sprites_notexture_vs + 1, ShaderStrings::sprite_notexture_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES)], ShaderStrings::batched_meshsprites_vs + 1, ShaderStrings::sprite_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_GRAY)], ShaderStrings::batched_meshsprites_vs + 1, ShaderStrings::sprite_gray_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_NO_TEXTURE)], ShaderStrings::batched_meshsprites_notexture_vs + 1, ShaderStrings::sprite_notexture_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_NoTexture" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_ALPHA)], ShaderStrings::batched_textnodes_vs + 1, ShaderStrings::textnode_alpha_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_TextNodes_Alpha" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_RED)], ShaderStrings::batched_textnodes_vs + 1, ShaderStrings::textnode_red_fs + 1, GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_TextNodes_Red" }
#else
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE)], "sprite_vs.glsl", "sprite_fs.glsl", GLShaderProgram::Introspection::Enabled, "Sprite" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_GRAY)], "sprite_vs.glsl", "sprite_gray_fs.glsl", GLShaderProgram::Introspection::Enabled, "Sprite_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_NO_TEXTURE)], "sprite_notexture_vs.glsl", "sprite_notexture_fs.glsl", GLShaderProgram::Introspection::Enabled, "Sprite_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE)], "meshsprite_vs.glsl", "sprite_fs.glsl", GLShaderProgram::Introspection::Enabled, "MeshSprite" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_GRAY)], "meshsprite_vs.glsl", "sprite_gray_fs.glsl", GLShaderProgram::Introspection::Enabled, "MeshSprite_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_NO_TEXTURE)], "meshsprite_notexture_vs.glsl", "sprite_notexture_fs.glsl", GLShaderProgram::Introspection::Enabled, "MeshSprite_NoTexture" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_ALPHA)], "textnode_vs.glsl", "textnode_alpha_fs.glsl", GLShaderProgram::Introspection::Enabled, "TextNode_Alpha" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_RED)], "textnode_vs.glsl", "textnode_red_fs.glsl", GLShaderProgram::Introspection::Enabled, "TextNode_Red" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES)], "batched_sprites_vs.glsl", "sprite_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_GRAY)], "batched_sprites_vs.glsl", "sprite_gray_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_NO_TEXTURE)], "batched_sprites_notexture_vs.glsl", "sprite_notexture_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_Sprites_NoTexture" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES)], "batched_meshsprites_vs.glsl", "sprite_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_GRAY)], "batched_meshsprites_vs.glsl", "sprite_gray_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_Gray" },
			{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_NO_TEXTURE)], "batched_meshsprites_notexture_vs.glsl", "sprite_notexture_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_MeshSprites_NoTexture" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_ALPHA)], "batched_textnodes_vs.glsl", "textnode_alpha_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_TextNodes_Alpha" },
			//{ RenderResources::defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_RED)], "batched_textnodes_vs.glsl", "textnode_red_fs.glsl", GLShaderProgram::Introspection::NoUniformsInBlocks, "Batched_TextNodes_Red" }
#endif
		};

		const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();
		const int maxUniformBlockSize = gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		char sourceString[48];
		const char *vertexStrings[3] = { nullptr, nullptr, nullptr };

		for (unsigned int i = 0; i < countof(shadersToLoad); i++) {
			const ShaderLoad& shaderToLoad = shadersToLoad[i];

#if defined(WITH_EMBEDDED_SHADERS)
			constexpr uint64_t shaderVersion = EmbeddedShadersVersion;
#else
			String vertexPath = fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, StringView(shaderToLoad.vertexShader) });
			String fragmentPath = fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, StringView(shaderToLoad.fragmentShader) });
			uint64_t shaderVersion = (uint64_t)std::max(fs::GetLastModificationTime(vertexPath).GetValue(), fs::GetLastModificationTime(fragmentPath).GetValue());
#endif

			shaderToLoad.shaderProgram = std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate);
			if (binaryShaderCache_->loadFromCache(shaderToLoad.shaderName, shaderVersion, shaderToLoad.shaderProgram.get(), shaderToLoad.introspection)) {
				// Shader is already compiled and up-to-date
				continue;
			}

			// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
			bool compileTwice = false;

			vertexStrings[0] = nullptr;
			vertexStrings[1] = nullptr;
			if (appCfg.fixedBatchSize > 0 && shaderToLoad.introspection == GLShaderProgram::Introspection::NoUniformsInBlocks) {
				// If fixed batch size is used, it's compiled only once with specified batch size
				shaderToLoad.shaderProgram->setBatchSize(appCfg.fixedBatchSize);

				formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, appCfg.fixedBatchSize);
				vertexStrings[0] = sourceString;
#if defined(WITH_EMBEDDED_SHADERS)
				vertexStrings[1] = shaderToLoad.vertexShader;
#endif
			} else if (shaderToLoad.introspection == GLShaderProgram::Introspection::NoUniformsInBlocks && maxUniformBlockSize < 64 * 1024) {
				compileTwice = true;

				// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
				formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, 1);
				vertexStrings[0] = sourceString;
#if defined(WITH_EMBEDDED_SHADERS)
				vertexStrings[1] = shaderToLoad.vertexShader;
#endif
			} else {
#if defined(WITH_EMBEDDED_SHADERS)
				vertexStrings[0] = shaderToLoad.vertexShader;
#endif
			}
			
#if defined(WITH_EMBEDDED_SHADERS)
			const bool vertexCompiled = shaderToLoad.shaderProgram->attachShaderFromStrings(GL_VERTEX_SHADER, vertexStrings);
			const bool fragmentCompiled = shaderToLoad.shaderProgram->attachShaderFromString(GL_FRAGMENT_SHADER, shaderToLoad.fragmentShader);
#else
			const bool vertexCompiled = shaderToLoad.shaderProgram->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, vertexPath);
			const bool fragmentCompiled = shaderToLoad.shaderProgram->attachShaderFromFile(GL_FRAGMENT_SHADER, fragmentPath);
#endif
			ASSERT(vertexCompiled);
			ASSERT(fragmentCompiled);
			
			shaderToLoad.shaderProgram->setObjectLabel(shaderToLoad.shaderName);

			if (compileTwice) {
				const bool hasLinked = shaderToLoad.shaderProgram->link(GLShaderProgram::Introspection::Enabled);
				FATAL_ASSERT(hasLinked);

				GLShaderUniformBlocks blocks(shaderToLoad.shaderProgram.get(), Material::InstancesBlockName, nullptr);
				GLUniformBlockCache* block = blocks.uniformBlock(Material::InstancesBlockName);
				if (block != nullptr) {
					int batchSize = maxUniformBlockSize / block->size();
					LOGI("Shader \"%s\" - block size: %d + %d align bytes, max batch size: %d", shaderToLoad.shaderName,
						block->size() - block->alignAmount(), block->alignAmount(), batchSize);

					bool hasLinkedFinal = false;
					while (batchSize > 0) {
						shaderToLoad.shaderProgram->reset();
						shaderToLoad.shaderProgram->setBatchSize(batchSize);
						formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);

#if defined(WITH_EMBEDDED_SHADERS)
						const bool vertexFinalCompiled = shaderToLoad.shaderProgram->attachShaderFromStrings(GL_VERTEX_SHADER, vertexStrings);
						const bool fragmentFinalCompiled = shaderToLoad.shaderProgram->attachShaderFromString(GL_FRAGMENT_SHADER, shaderToLoad.fragmentShader);
#else
						const bool vertexFinalCompiled = shaderToLoad.shaderProgram->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, vertexPath);
						const bool fragmentFinalCompiled = shaderToLoad.shaderProgram->attachShaderFromFile(GL_FRAGMENT_SHADER, fragmentPath);
#endif
						if (vertexFinalCompiled && fragmentFinalCompiled) {
							hasLinkedFinal = shaderToLoad.shaderProgram->link(shaderToLoad.introspection);
							if (hasLinkedFinal) {
								break;
							}
						}

						batchSize--;
						LOGW("Failed to compile the shader, recompiling with batch size: %i", batchSize);
					}

					FATAL_ASSERT_MSG(hasLinkedFinal, "Failed to compile shader \"%s\"", shaderToLoad.shaderName);
				}
			} else {
				const bool hasLinked = shaderToLoad.shaderProgram->link(shaderToLoad.introspection);
				FATAL_ASSERT_MSG(hasLinked, "Failed to compile shader \"%s\"", shaderToLoad.shaderName);
			}

			binaryShaderCache_->saveToCache(shaderToLoad.shaderName, shaderVersion, shaderToLoad.shaderProgram.get());
		}

		registerDefaultBatchedShaders();

		// Calculating a default projection matrix for all shader programs
		const int width = theApplication().width();
		const int height = theApplication().height();
		defaultCamera_->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (+0.5f), height * (-0.5f));
	}

	void RenderResources::dispose()
	{
		for (auto& shaderProgram : defaultShaderPrograms_) {
			shaderProgram.reset(nullptr);
		}

		ASSERT(cameraUniformDataMap_.empty());

		defaultCamera_.reset(nullptr);
		renderBatcher_.reset(nullptr);
		renderCommandPool_.reset(nullptr);
		vaoPool_.reset(nullptr);
		buffersManager_.reset(nullptr);

		LOGI("Rendering resources disposed");
	}

	void RenderResources::registerDefaultBatchedShaders()
	{
		batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_GRAY)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_GRAY)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::SPRITE_NO_TEXTURE)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_SPRITES_NO_TEXTURE)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_GRAY)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_GRAY)].get());
		batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::MESH_SPRITE_NO_TEXTURE)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_MESH_SPRITES_NO_TEXTURE)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_ALPHA)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_ALPHA)].get());
		//batchedShaders_.emplace(defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::TEXTNODE_RED)].get(), defaultShaderPrograms_[static_cast<int>(Material::ShaderProgramType::BATCHED_TEXTNODES_RED)].get());
	}
}
