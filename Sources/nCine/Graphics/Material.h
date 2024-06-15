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

	/// The class containing material data for a drawable node
	class Material
	{
	public:
		/// One of the predefined shader programs
		enum class ShaderProgramType
		{
			/// Shader program for Sprite classes
			Sprite = 0,
			/// Shader program for Sprite classes with grayscale font texture
			//SpriteGray,
			/// Shader program for Sprite classes with a solid color and no texture
			SpriteNoTexture,
			/// Shader program for MeshSprite classes
			MeshSprite,
			/// Shader program for MeshSprite classes with grayscale font texture
			//MeshSpriteGray,
			/// Shader program for MeshSprite classes with a solid color and no texture
			MeshSpriteNoTexture,
			/// Shader program for TextNode classes with glyph data in alpha channel
			//TextNodeAlpha,
			/// Shader program for TextNode classes with glyph data in red channel
			//TextNodeRed,
			/// Shader program for a batch of Sprite classes
			BatchedSprites,
			/// Shader program for a batch of Sprite classes with grayscale font texture
			//BatchedSpritesGray,
			/// Shader program for a batch of Sprite classes with solid colors and no texture
			BatchedSpritesNoTexture,
			/// Shader program for a batch of MeshSprite classes
			BatchedMeshSprites,
			/// Shader program for a batch of MeshSprite classes with grayscale font texture
			//BatchedMeshSpritesGray,
			/// Shader program for a batch of MeshSprite classes with solid colors and no texture
			BatchedMeshSpritesNoTexture,
			/// Shader program for a batch of TextNode classes with color font texture
			//BatchedTextNodesAlpha,
			/// Shader program for a batch of TextNode classes with grayscale font texture
			//BatchedTextNodesRed,
			/// A custom shader program
			Custom
		};

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
		static constexpr char TexRectUniformName[] = "texRect";
		static constexpr char PositionAttributeName[] = "aPosition";
		static constexpr char TexCoordsAttributeName[] = "aTexCoords";
		static constexpr char MeshIndexAttributeName[] = "aMeshIndex";
		static constexpr char ColorAttributeName[] = "aColor";

		/// Default constructor
		Material();
		Material(GLShaderProgram* program, GLTexture* texture);

		inline bool isBlendingEnabled() const {
			return isBlendingEnabled_;
		}
		inline void setBlendingEnabled(bool blendingEnabled) {
			isBlendingEnabled_ = blendingEnabled;
		}

		inline GLenum srcBlendingFactor() const {
			return srcBlendingFactor_;
		}
		inline GLenum destBlendingFactor() const {
			return destBlendingFactor_;
		}
		void setBlendingFactors(GLenum srcBlendingFactor, GLenum destBlendingFactor);

		inline ShaderProgramType shaderProgramType() const {
			return shaderProgramType_;
		}
		bool setShaderProgramType(ShaderProgramType shaderProgramType);
		inline const GLShaderProgram* shaderProgram() const {
			return shaderProgram_;
		}
		void setShaderProgram(GLShaderProgram* program);
		bool setShader(Shader* shader);

		void setDefaultAttributesParameters();
		void reserveUniformsDataMemory();
		void setUniformsDataPointer(GLubyte* dataPointer);

		/// Wrapper around `GLShaderUniforms::hasUniform()`
		inline bool hasUniform(const char* name) const {
			return shaderUniforms_.hasUniform(name);
		}
		/// Wrapper around `GLShaderUniformBlocks::hasUniformBlock()`
		inline bool hasUniformBlock(const char* name) const {
			return shaderUniformBlocks_.hasUniformBlock(name);
		}

		/// Wrapper around `GLShaderUniforms::uniform()`
		inline GLUniformCache* uniform(const char* name) {
			return shaderUniforms_.uniform(name);
		}
		/// Wrapper around `GLShaderUniformBlocks::uniformBlock()`
		inline GLUniformBlockCache* uniformBlock(const char* name) {
			return shaderUniformBlocks_.uniformBlock(name);
		}

		/// Wrapper around `GLShaderUniforms::allUniforms()`
		inline const GLShaderUniforms::UniformHashMapType allUniforms() const {
			return shaderUniforms_.allUniforms();
		}
		/// Wrapper around `GLShaderUniformBlocks::allUniformBlocks()`
		inline const GLShaderUniformBlocks::UniformHashMapType allUniformBlocks() const {
			return shaderUniformBlocks_.allUniformBlocks();
		}

		const GLTexture* texture(unsigned int unit) const;
		bool setTexture(unsigned int unit, const GLTexture* texture);
		bool setTexture(unsigned int unit, const Texture& texture);

		inline const GLTexture* texture() const {
			return texture(0);
		}
		inline bool setTexture(const GLTexture* texture) {
			return setTexture(0, texture);
		}
		inline bool setTexture(const Texture& texture) {
			return setTexture(0, texture);
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

		/// The size of the memory buffer containing uniform values
		unsigned int uniformsHostBufferSize_;
		/// Memory buffer with uniform values to be sent to the GPU
		std::unique_ptr<GLubyte[]> uniformsHostBuffer_;

		void bind();
		/// Wrapper around `GLShaderUniforms::commitUniforms()`
		inline void commitUniforms() {
			shaderUniforms_.commitUniforms();
		}
		/// Wrapper around `GLShaderUniformBlocks::commitUniformBlocks()`
		inline void commitUniformBlocks() {
			shaderUniformBlocks_.commitUniformBlocks();
		}
		/// Wrapper around `GLShaderProgram::defineVertexFormat()`
		void defineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, unsigned int vboOffset);
		uint32_t sortKey();

		friend class RenderCommand;
	};

}

