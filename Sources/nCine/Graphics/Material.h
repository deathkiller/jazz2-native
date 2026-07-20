#pragma once

#include "RHI/RhiTypes.h"
#include "RHI/Rhi.h"
#include "Shader.h"

namespace nCine
{
	class Texture;

	/**
		@brief Contains material data for a drawable node
		
		Describes how a drawable node is rendered: the shader program (one of the predefined types or a custom
		one), the bound textures, the blending state and the per-draw uniform values. @ref RenderCommand uses
		this state to configure the GPU pipeline and to build the sort key for a draw call.
	*/
	class Material
	{
		friend class RenderCommand;

	public:
		/**
		 * @brief One of the predefined shader programs
		 */
		enum class ShaderProgramType
		{
			Sprite = 0,								/**< Shader program for `Sprite` classes */
			// Shader program for Sprite classes with grayscale font texture
			//SpriteGray,
			SpriteNoTexture,						/**< Shader program for `Sprite` classes with a solid color and no texture */
			MeshSprite,								/**< Shader program for `MeshSprite` classes */
			// Shader program for MeshSprite classes with grayscale font texture
			//MeshSpriteGray,
			MeshSpriteNoTexture,					/**< Shader program for `MeshSprite` classes with a solid color and no texture */
			// Shader program for TextNode classes with glyph data in alpha channel
			//TextNodeAlpha,
			// Shader program for TextNode classes with glyph data in red channel
			//TextNodeRed,
			BatchedSprites,							/**< Shader program for a batch of `Sprite` classes */
			// Shader program for a batch of Sprite classes with grayscale font texture
			//BatchedSpritesGray,
			BatchedSpritesNoTexture,				/**< Shader program for a batch of `Sprite` classes with solid colors and no texture */
			BatchedMeshSprites,						/**< Shader program for a batch of `MeshSprite` classes */
			// Shader program for a batch of MeshSprite classes with grayscale font texture
			//BatchedMeshSpritesGray,
			BatchedMeshSpritesNoTexture,			/**< Shader program for a batch of `MeshSprite` classes with solid colors and no texture */
			// Shader program for a batch of TextNode classes with color font texture
			//BatchedTextNodesAlpha,
			// Shader program for a batch of TextNode classes with grayscale font texture
			//BatchedTextNodesRed,
			Custom									/**< A custom shader program */
		};

		/** @{ @name Constants */

		// Shader uniform block and model matrix uniform names
		static constexpr char InstanceBlockName[] = "InstanceBlock";
		static constexpr char InstancesBlockName[] = "InstancesBlock"; // for batched shaders
		static constexpr char ModelMatrixUniformName[] = "modelMatrix";

		// Camera related shader uniform names
		static constexpr char GuiProjectionMatrixUniformName[] = "uGuiProjection";
		static constexpr char DepthUniformName[] = "uDepth";
		static constexpr char ProjectionMatrixUniformName[] = "uProjectionMatrix";
		static constexpr char ViewMatrixUniformName[] = "uViewMatrix";
		static constexpr char ProjectionViewMatrixExcludeString[] = "uProjectionMatrix\0uViewMatrix\0";

		// Shader uniform and attribute names
		static constexpr char TextureUniformName[] = "uTexture";
		static constexpr char ColorUniformName[] = "color";
		static constexpr char SpriteSizeUniformName[] = "spriteSize";
		static constexpr char PaletteOffsetUniformName[] = "palOffset";
		static constexpr char TexRectUniformName[] = "texRect";
		static constexpr char PositionAttributeName[] = "aPosition";
		static constexpr char TexCoordsAttributeName[] = "aTexCoords";
		static constexpr char MeshIndexAttributeName[] = "aMeshIndex";
		static constexpr char ColorAttributeName[] = "aColor";
		/** @brief OpenGL|ES 2.0 profile: the static per-vertex quad corner replacing gl_VertexID synthesis */
		static constexpr char QuadCornerAttributeName[] = "aQuadCorner";

		/** @} */

		/** @brief Default constructor */
		Material();
		Material(RHI::ShaderProgram* program, RHI::Texture* texture);

		inline bool IsBlendingEnabled() const {
			return isBlendingEnabled_;
		}
		inline void SetBlendingEnabled(bool blendingEnabled) {
			isBlendingEnabled_ = blendingEnabled;
		}

		inline BlendingFactor GetSrcBlendingFactor() const {
			return srcBlendingFactor_;
		}
		inline BlendingFactor GetDestBlendingFactor() const {
			return destBlendingFactor_;
		}
		inline BlendingFactor GetSrcAlphaBlendingFactor() const {
			return srcAlphaBlendingFactor_;
		}
		inline BlendingFactor GetDestAlphaBlendingFactor() const {
			return destAlphaBlendingFactor_;
		}
		/** @brief Sets the blending factors for both color and alpha */
		void SetBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor);
		/** @brief Sets separate blending factors for color and alpha (alpha typically `One`/`OneMinusSrcAlpha` so RGBA render targets accumulate correct coverage) */
		void SetBlendingFactors(BlendingFactor srcRgbBlendingFactor, BlendingFactor destRgbBlendingFactor, BlendingFactor srcAlphaBlendingFactor, BlendingFactor destAlphaBlendingFactor);

		inline ShaderProgramType GetShaderProgramType() const {
			return shaderProgramType_;
		}
		bool SetShaderProgramType(ShaderProgramType shaderProgramType);
		inline const RHI::ShaderProgram* GetShaderProgram() const {
			return shaderProgram_;
		}
		void SetShaderProgram(RHI::ShaderProgram* program);
		bool SetShader(Shader* shader);

		void SetDefaultAttributesParameters();
		void ReserveUniformsDataMemory();
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		/** @brief Wrapper around `RHI::ShaderUniforms::HasUniform()` */
		inline bool HasUniform(const char* name) const {
			return shaderUniforms_.HasUniform(name);
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::HasUniformBlock()` */
		inline bool HasUniformBlock(const char* name) const {
			return shaderUniformBlocks_.HasUniformBlock(name);
		}

		/** @brief Wrapper around `RHI::ShaderUniforms::GetUniform()` */
		inline RHI::UniformCache* Uniform(const char* name) {
			return shaderUniforms_.GetUniform(name);
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::GetUniformBlock()` */
		inline RHI::UniformBlockCache* UniformBlock(const char* name) {
			return shaderUniformBlocks_.GetUniformBlock(name);
		}

		/** @brief Wrapper around `RHI::ShaderUniforms::GetAllUniforms()` */
		inline const RHI::ShaderUniforms::UniformHashMapType& GetAllUniforms() const {
			return shaderUniforms_.GetAllUniforms();
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::GetAllUniformBlocks()` */
		inline const RHI::ShaderUniformBlocks::UniformHashMapType& GetAllUniformBlocks() const {
			return shaderUniformBlocks_.GetAllUniformBlocks();
		}

		const RHI::Texture* GetTexture(std::uint32_t unit) const;
		bool SetTexture(std::uint32_t unit, const RHI::Texture* texture);
		bool SetTexture(std::uint32_t unit, const Texture& texture);
		bool SetTexture(std::uint32_t unit, std::nullptr_t);

		inline const RHI::Texture* GetTexture() const {
			return GetTexture(0);
		}
		inline bool SetTexture(const RHI::Texture* texture) {
			return SetTexture(0, texture);
		}
		inline bool SetTexture(const Texture& texture) {
			return SetTexture(0, texture);
		}

	private:
		bool isBlendingEnabled_;
		// Whether the cached sort key has to be recomputed
		bool sortKeyDirty_;
		// Number of texture units in use, i.e. the highest unit with a texture plus one
		std::uint8_t usedTextureUnits_;
		BlendingFactor srcBlendingFactor_;
		BlendingFactor destBlendingFactor_;
		BlendingFactor srcAlphaBlendingFactor_;
		BlendingFactor destAlphaBlendingFactor_;
		// Cached result of GetSortKey(), recomputed only when the hashed state changes
		std::uint32_t sortKey_;
		// Incremented every time SetShaderProgram() rebuilds the uniform caches, so
		// that pointers into them can be cached and safely invalidated by observers
		std::uint32_t shaderChangeCounter_;
		ShaderProgramType shaderProgramType_;
		RHI::ShaderProgram* shaderProgram_;
		RHI::ShaderUniforms shaderUniforms_;
		RHI::ShaderUniformBlocks shaderUniformBlocks_;
		const RHI::Texture* textures_[RHI::Texture::MaxTextureUnits];

		/** @brief The size of the memory buffer containing uniform values */
		std::uint32_t uniformsHostBufferSize_;
		/** @brief Memory buffer with uniform values to be sent to the GPU */
		std::unique_ptr<std::uint8_t[]> uniformsHostBuffer_;

		void Bind();
		// Maintains the used texture unit count after a texture change on the specified unit
		void UpdateUsedTextureUnits(std::uint32_t unit, bool textureSet);
		/** @brief Wrapper around `RHI::ShaderUniforms::CommitUniforms()` */
		inline void CommitUniforms() {
			shaderUniforms_.CommitUniforms();
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::CommitUniformBlocks()` */
		inline void CommitUniformBlocks() {
			shaderUniformBlocks_.CommitUniformBlocks();
		}
		/** @brief Wrapper around `RHI::ShaderProgram::DefineVertexFormat()` */
		void DefineVertexFormat(const RHI::Buffer* vbo, const RHI::Buffer* ibo, std::uint32_t vboOffset);
		std::uint32_t GetSortKey();
	};

}

