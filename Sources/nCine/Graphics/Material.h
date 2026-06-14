#pragma once

#include "GL/GLShaderUniforms.h"
#include "GL/GLShaderUniformBlocks.h"
#include "GL/GLTexture.h"
#include "Shader.h"

namespace nCine
{
	class GLShaderProgram;
	class GLTexture;
	class Texture;
	class GLUniformCache;
	class GLAttribute;

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

		/** @} */

		/** @brief Default constructor */
		Material();
		Material(GLShaderProgram* program, GLTexture* texture);

		inline bool IsBlendingEnabled() const {
			return isBlendingEnabled_;
		}
		inline void SetBlendingEnabled(bool blendingEnabled) {
			isBlendingEnabled_ = blendingEnabled;
		}

		inline GLenum GetSrcBlendingFactor() const {
			return srcBlendingFactor_;
		}
		inline GLenum GetDestBlendingFactor() const {
			return destBlendingFactor_;
		}
		void SetBlendingFactors(GLenum srcBlendingFactor, GLenum destBlendingFactor);

		inline ShaderProgramType GetShaderProgramType() const {
			return shaderProgramType_;
		}
		bool SetShaderProgramType(ShaderProgramType shaderProgramType);
		inline const GLShaderProgram* GetShaderProgram() const {
			return shaderProgram_;
		}
		void SetShaderProgram(GLShaderProgram* program);
		bool SetShader(Shader* shader);

		void SetDefaultAttributesParameters();
		void ReserveUniformsDataMemory();
		void SetUniformsDataPointer(GLubyte* dataPointer);

		/** @brief Wrapper around `GLShaderUniforms::HasUniform()` */
		inline bool HasUniform(const char* name) const {
			return shaderUniforms_.HasUniform(name);
		}
		/** @brief Wrapper around `GLShaderUniformBlocks::HasUniformBlock()` */
		inline bool HasUniformBlock(const char* name) const {
			return shaderUniformBlocks_.HasUniformBlock(name);
		}

		/** @brief Wrapper around `GLShaderUniforms::GetUniform()` */
		inline GLUniformCache* Uniform(const char* name) {
			return shaderUniforms_.GetUniform(name);
		}
		/** @brief Wrapper around `GLShaderUniformBlocks::GetUniformBlock()` */
		inline GLUniformBlockCache* UniformBlock(const char* name) {
			return shaderUniformBlocks_.GetUniformBlock(name);
		}

		/** @brief Wrapper around `GLShaderUniforms::GetAllUniforms()` */
		inline const GLShaderUniforms::UniformHashMapType GetAllUniforms() const {
			return shaderUniforms_.GetAllUniforms();
		}
		/** @brief Wrapper around `GLShaderUniformBlocks::GetAllUniformBlocks()` */
		inline const GLShaderUniformBlocks::UniformHashMapType GetAllUniformBlocks() const {
			return shaderUniformBlocks_.GetAllUniformBlocks();
		}

		const GLTexture* GetTexture(std::uint32_t unit) const;
		bool SetTexture(std::uint32_t unit, const GLTexture* texture);
		bool SetTexture(std::uint32_t unit, const Texture& texture);
		bool SetTexture(std::uint32_t unit, std::nullptr_t);

		inline const GLTexture* GetTexture() const {
			return GetTexture(0);
		}
		inline bool SetTexture(const GLTexture* texture) {
			return SetTexture(0, texture);
		}
		inline bool SetTexture(const Texture& texture) {
			return SetTexture(0, texture);
		}

	private:
		bool isBlendingEnabled_;
		GLenum srcBlendingFactor_;
		GLenum destBlendingFactor_;

		ShaderProgramType shaderProgramType_;
		GLShaderProgram* shaderProgram_;
		GLShaderUniforms shaderUniforms_;
		GLShaderUniformBlocks shaderUniformBlocks_;
		const GLTexture* textures_[GLTexture::MaxTextureUnits];

		/** @brief The size of the memory buffer containing uniform values */
		std::uint32_t uniformsHostBufferSize_;
		/** @brief Memory buffer with uniform values to be sent to the GPU */
		std::unique_ptr<GLubyte[]> uniformsHostBuffer_;

		void Bind();
		/** @brief Wrapper around `GLShaderUniforms::CommitUniforms()` */
		inline void CommitUniforms() {
			shaderUniforms_.CommitUniforms();
		}
		/** @brief Wrapper around `GLShaderUniformBlocks::CommitUniformBlocks()` */
		inline void CommitUniformBlocks() {
			shaderUniformBlocks_.CommitUniformBlocks();
		}
		/** @brief Wrapper around `GLShaderProgram::DefineVertexFormat()` */
		void DefineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, std::uint32_t vboOffset);
		std::uint32_t GetSortKey();
	};

}

