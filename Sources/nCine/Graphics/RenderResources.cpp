#include "RenderResources.h"
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

#if defined(WITH_EMBEDDED_SHADERS)
#	include "shader_strings.h"
#else
#	include "../IO/FileSystem.h"	// for GetDataPath()
#endif

namespace nCine
{
	namespace
	{
		constexpr char BatchSizeFormatString[] = "#define BATCH_SIZE (%d)\n#line 0\n";
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
		GLShaderProgram* batchedShader = nullptr;
		auto it = batchedShaders_.find(shader);
		if (it != batchedShaders_.end()) {
			batchedShader = it->second;
		}
		return batchedShader;
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
		bool hasRemoved = false;
		if (!cameraUniformDataMap_.empty()) {
			hasRemoved = cameraUniformDataMap_.erase(shaderProgram);
		}
		return hasRemoved;
	}

	void RenderResources::setDefaultAttributesParameters(GLShaderProgram& shaderProgram)
	{
		if (shaderProgram.numAttributes() > 0) {
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
	}

	namespace
	{
		struct ShaderLoad
		{
			std::unique_ptr<GLShaderProgram>& shaderProgram;
			const char* vertexShader;
			const char* fragmentShader;
			GLShaderProgram::Introspection introspection;
			const char* objectLabel;
		};
	}

	void RenderResources::setCurrentCamera(Camera* camera)
	{
		currentCamera_ = (camera != nullptr ? camera : defaultCamera_.get());
	}

	void RenderResources::updateCameraUniforms()
	{
		// The buffer is shared among every shader program. There is no need to call `setFloatVector()` as `setDirty()` is enough.
		memcpy(cameraUniformsBuffer_, currentCamera_->projection().Data(), 64);
		memcpy(cameraUniformsBuffer_ + 64, currentCamera_->view().Data(), 64);
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
		LOGI("Creating a minimal set of rendering resources...");

		// `createMinimal()` cannot be called after `create()`
		ASSERT(binaryShaderCache_ == nullptr);
		ASSERT(buffersManager_ == nullptr);
		ASSERT(vaoPool_ == nullptr);
	
		const AppConfiguration& appCfg = theApplication().appConfiguration();
		binaryShaderCache_ = std::make_unique<BinaryShaderCache>(appCfg.shaderCachePath);
		buffersManager_ = std::make_unique<RenderBuffersManager>(appCfg.useBufferMapping, appCfg.vboSize, appCfg.iboSize);
		vaoPool_ = std::make_unique<RenderVaoPool>(appCfg.vaoPoolSize);
		
		LOGI("Minimal rendering resources created");
	}
	
	void RenderResources::create()
	{
		LOGI("Creating rendering resources...");
	
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
#if !defined(WITH_EMBEDDED_SHADERS)
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
#else
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
#endif
		};

		const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();
		// Clamping the value as some drivers report a maximum size similar to SSBO one
		const int maxUniformBlockSize = std::clamp(gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE), 0, 64 * 1024);

		char sourceString[48];
		const char *vertexStrings[3] = { nullptr, nullptr, nullptr };

		for (unsigned int i = 0; i < countof(shadersToLoad); i++) {
			const ShaderLoad& shaderToLoad = shadersToLoad[i];

			shaderToLoad.shaderProgram = std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate);
			if (RenderResources::binaryShaderCache().loadFromCache(shaderToLoad.objectLabel, DefaultShadersVersion, shaderToLoad.shaderProgram.get(), shaderToLoad.introspection)) {
				// Shader is already compiled
				continue;
			}

			// If the UBO is smaller than 64kb and fixed batch size is disabled, batched shaders need to be compiled twice to determine safe `BATCH_SIZE` define value
			const bool compileTwice = (maxUniformBlockSize < 64 * 1024 && appCfg.fixedBatchSize <= 0 && shaderToLoad.introspection == GLShaderProgram::Introspection::NoUniformsInBlocks);

			vertexStrings[0] = nullptr;
			vertexStrings[1] = nullptr;
			if (appCfg.fixedBatchSize > 0 && shaderToLoad.introspection == GLShaderProgram::Introspection::NoUniformsInBlocks) {
				formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, appCfg.fixedBatchSize);
				vertexStrings[0] = sourceString;
			} else if (compileTwice) {
				// The first compilation of a batched shader needs a `BATCH_SIZE` defined as 1
				formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, 1);
				vertexStrings[0] = sourceString;
			}
			
#if defined(WITH_EMBEDDED_SHADERS)
			// The vertex shader source string can be either the first one or the second one, if the first defines the `BATCH_SIZE`
			vertexStrings[compileTwice ? 1 : 0] = shaderToLoad.vertexShader;

			const bool vertexCompiled = shaderToLoad.shaderProgram->attachShaderFromStrings(GL_VERTEX_SHADER, vertexStrings);
			const bool fragmentCompiled = shaderToLoad.shaderProgram->attachShaderFromString(GL_FRAGMENT_SHADER, shaderToLoad.fragmentShader);
#else
			const bool vertexCompiled = shaderToLoad.shaderProgram->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, fs::JoinPath({ fs::GetDataPath(), "Shaders"_s, StringView(shaderToLoad.vertexShader) }).data());
			const bool fragmentCompiled = shaderToLoad.shaderProgram->attachShaderFromFile(GL_FRAGMENT_SHADER, fs::JoinPath({ fs::GetDataPath(), "Shaders"_s, StringView(shaderToLoad.fragmentShader) }).data());
#endif
			ASSERT(vertexCompiled);
			ASSERT(fragmentCompiled);
			
			shaderToLoad.shaderProgram->setObjectLabel(shaderToLoad.objectLabel);
			// The first compilation of a batched shader needs the introspection
			const bool hasLinked = shaderToLoad.shaderProgram->link(compileTwice ? GLShaderProgram::Introspection::Enabled : shaderToLoad.introspection);
			FATAL_ASSERT(hasLinked);

			if (compileTwice) {
				GLShaderUniformBlocks blocks(shaderToLoad.shaderProgram.get(), Material::InstancesBlockName, nullptr);
				GLUniformBlockCache* block = blocks.uniformBlock(Material::InstancesBlockName);
				ASSERT(block != nullptr);
				if (block != nullptr) {
					const int size = block->size() - block->alignAmount();
					const int batchSize = maxUniformBlockSize / size;
					LOGD_X("Shader \"%s\" - block size: %d + %d align bytes, max batch size: %d", shaderToLoad.objectLabel, size, block->alignAmount(), batchSize);

					shaderToLoad.shaderProgram->reset();
					shaderToLoad.shaderProgram->setBatchSize(batchSize);
					formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);

#if defined(WITH_EMBEDDED_SHADERS)
					const bool vertexCompiled2 = shaderToLoad.shaderProgram->attachShaderFromStrings(GL_VERTEX_SHADER, vertexStrings);
					const bool fragmentCompiled2 = shaderToLoad.shaderProgram->attachShaderFromString(GL_FRAGMENT_SHADER, shaderToLoad.fragmentShader);
#else
					const bool vertexCompiled2 = shaderToLoad.shaderProgram->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, fs::JoinPath({ fs::GetDataPath(), "Shaders"_s, StringView(shaderToLoad.vertexShader) }).data());
					const bool fragmentCompiled2 = shaderToLoad.shaderProgram->attachShaderFromFile(GL_FRAGMENT_SHADER, fs::JoinPath({ fs::GetDataPath(), "Shaders"_s, StringView(shaderToLoad.fragmentShader) }).data());
#endif
					ASSERT(vertexCompiled2);
					ASSERT(fragmentCompiled2);

					const bool hasLinked2 = shaderToLoad.shaderProgram->link(shaderToLoad.introspection);
					FATAL_ASSERT(hasLinked2);
				}
			}

			RenderResources::binaryShaderCache().saveToCache(shaderToLoad.objectLabel, DefaultShadersVersion, shaderToLoad.shaderProgram.get());
		}

		registerDefaultBatchedShaders();

		// Calculating a default projection matrix for all shader programs
		int width = theApplication().width();
		int height = theApplication().height();
		defaultCamera_->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (+0.5f), height * (-0.5f));

		LOGI("Rendering resources created");
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
