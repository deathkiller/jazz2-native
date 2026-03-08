#pragma once

#include "RenderAPI/RHI.h"
#include "Shader.h"

namespace nCine
{
	class Texture;

	/// Contains material data for a drawable node
	class Material
	{
		friend class RenderCommand;

	public:
		/// One of the predefined shader programs
		enum class ShaderProgramType
		{
			/// Shader program for Sprite classes
			Sprite = 0,
			// Shader program for Sprite classes with grayscale font texture
			//SpriteGray,
			/// Shader program for Sprite classes with a solid color and no texture
			SpriteNoTexture,
			/// Shader program for MeshSprite classes
			MeshSprite,
			// Shader program for MeshSprite classes with grayscale font texture
			//MeshSpriteGray,
			/// Shader program for MeshSprite classes with a solid color and no texture
			MeshSpriteNoTexture,
			// Shader program for TextNode classes with glyph data in alpha channel
			//TextNodeAlpha,
			// Shader program for TextNode classes with glyph data in red channel
			//TextNodeRed,
			/// Shader program for a batch of Sprite classes
			BatchedSprites,
			// Shader program for a batch of Sprite classes with grayscale font texture
			//BatchedSpritesGray,
			/// Shader program for a batch of Sprite classes with solid colors and no texture
			BatchedSpritesNoTexture,
			/// Shader program for a batch of MeshSprite classes
			BatchedMeshSprites,
			// Shader program for a batch of MeshSprite classes with grayscale font texture
			//BatchedMeshSpritesGray,
			/// Shader program for a batch of MeshSprite classes with solid colors and no texture
			BatchedMeshSpritesNoTexture,
			// Shader program for a batch of TextNode classes with color font texture
			//BatchedTextNodesAlpha,
			// Shader program for a batch of TextNode classes with grayscale font texture
			//BatchedTextNodesRed,
			/// A custom shader program
			Custom
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
		static constexpr char TexRectUniformName[] = "texRect";
		static constexpr char PositionAttributeName[] = "aPosition";
		static constexpr char TexCoordsAttributeName[] = "aTexCoords";
		static constexpr char MeshIndexAttributeName[] = "aMeshIndex";
		static constexpr char ColorAttributeName[] = "aColor";

		/** @} */

		static constexpr std::uint32_t MaxTextureUnits = 8;

		/// Default constructor
		Material();
		Material(Rhi::ShaderProgram* program, Rhi::Texture* texture);

		inline bool IsBlendingEnabled() const {
			return isBlendingEnabled_;
		}
		inline void SetBlendingEnabled(bool blendingEnabled) {
			isBlendingEnabled_ = blendingEnabled;
		}

		inline Rhi::BlendFactor GetSrcBlendingFactor() const {
			return srcBlendingFactor_;
		}
		inline Rhi::BlendFactor GetDestBlendingFactor() const {
			return destBlendingFactor_;
		}
		void SetBlendingFactors(Rhi::BlendFactor srcBlendingFactor, Rhi::BlendFactor destBlendingFactor);

		inline ShaderProgramType GetShaderProgramType() const {
			return shaderProgramType_;
		}
		bool SetShaderProgramType(ShaderProgramType shaderProgramType);
		inline const Rhi::ShaderProgram* GetShaderProgram() const {
			return shaderProgram_;
		}
		void SetShaderProgram(Rhi::ShaderProgram* program);
		bool SetShader(Shader* shader);

		void SetDefaultAttributesParameters();
		void ReserveUniformsDataMemory();
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		/// Wrapper around `ShaderUniforms::HasUniform()`
		inline bool HasUniform(const char* name) const {
			return shaderUniforms_.HasUniform(name);
		}
		/// Wrapper around `ShaderUniformBlocks::HasUniformBlock()`
		inline bool HasUniformBlock(const char* name) const {
			return shaderUniformBlocks_.HasUniformBlock(name);
		}

		/// Wrapper around `ShaderUniforms::GetUniform()`
		inline Rhi::UniformCache* Uniform(const char* name) {
			return shaderUniforms_.GetUniform(name);
		}
		/// Wrapper around `ShaderUniformBlocks::GetUniformBlock()`
		inline Rhi::UniformBlockCache* UniformBlock(const char* name) {
			return shaderUniformBlocks_.GetUniformBlock(name);
		}

		/// Wrapper around `ShaderUniforms::GetAllUniforms()`
		inline const Rhi::ShaderUniforms::UniformHashMapType GetAllUniforms() const {
			return shaderUniforms_.GetAllUniforms();
		}
		/// Wrapper around `ShaderUniformBlocks::GetAllUniformBlocks()`
		inline const Rhi::ShaderUniformBlocks::UniformHashMapType GetAllUniformBlocks() const {
			return shaderUniformBlocks_.GetAllUniformBlocks();
		}

		const Rhi::Texture* GetTexture(std::uint32_t unit) const;
		bool SetTexture(std::uint32_t unit, const Rhi::Texture* texture);
		bool SetTexture(std::uint32_t unit, const Texture& texture);

		inline const Rhi::Texture* GetTexture() const {
			return GetTexture(0);
		}
		inline bool SetTexture(const Rhi::Texture* texture) {
			return SetTexture(0, texture);
		}
		inline bool SetTexture(const Texture& texture) {
			return SetTexture(0, texture);
		}

	private:
		bool isBlendingEnabled_;
		Rhi::BlendFactor srcBlendingFactor_;
		Rhi::BlendFactor destBlendingFactor_;

		ShaderProgramType shaderProgramType_;
		Rhi::ShaderProgram* shaderProgram_;
		Rhi::ShaderUniforms shaderUniforms_;
		Rhi::ShaderUniformBlocks shaderUniformBlocks_;
		const Rhi::Texture* textures_[MaxTextureUnits];

		/// The size of the memory buffer containing uniform values
		std::uint32_t uniformsHostBufferSize_;
		/// Memory buffer with uniform values to be sent to the GPU
		std::unique_ptr<std::uint8_t[]> uniformsHostBuffer_;

		void Bind();
		/// Wrapper around `ShaderUniforms::CommitUniforms()`
		inline void CommitUniforms() {
			shaderUniforms_.CommitUniforms();
		}
		/// Wrapper around `ShaderUniformBlocks::CommitUniformBlocks()`
		inline void CommitUniformBlocks() {
			shaderUniformBlocks_.CommitUniformBlocks();
		}
		/// Wrapper around `Program::DefineVertexFormat()`
		void DefineVertexFormat(const Rhi::Buffer* vbo, const Rhi::Buffer* ibo, std::uint32_t vboOffset);
		std::uint32_t GetSortKey();
	};

}

