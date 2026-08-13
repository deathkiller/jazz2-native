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
			return _isBlendingEnabled;
		}
		inline void SetBlendingEnabled(bool blendingEnabled) {
			_isBlendingEnabled = blendingEnabled;
		}

		inline BlendingFactor GetSrcBlendingFactor() const {
			return _srcBlendingFactor;
		}
		inline BlendingFactor GetDestBlendingFactor() const {
			return _destBlendingFactor;
		}
		inline BlendingFactor GetSrcAlphaBlendingFactor() const {
			return _srcAlphaBlendingFactor;
		}
		inline BlendingFactor GetDestAlphaBlendingFactor() const {
			return _destAlphaBlendingFactor;
		}
		/** @brief Sets the blending factors for both color and alpha */
		void SetBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor);
		/** @brief Sets separate blending factors for color and alpha (alpha typically `One`/`OneMinusSrcAlpha` so RGBA render targets accumulate correct coverage) */
		void SetBlendingFactors(BlendingFactor srcRgbBlendingFactor, BlendingFactor destRgbBlendingFactor, BlendingFactor srcAlphaBlendingFactor, BlendingFactor destAlphaBlendingFactor);

		/** @brief Returns `true` when the drawn content was hinted fully opaque (see @ref SetOpaqueContentHint()) */
		inline bool GetOpaqueContentHint() const {
			return _hasOpaqueContentHint;
		}
		/**
		 * @brief Hints that every pixel this material draws is fully opaque, making its blending an identity
		 *
		 * Unlike disabling blending - which moves the command to the front-to-back opaque queue and so
		 * changes its draw order - a hinted command stays in the transparent queue in painter's order, but
		 * a backend that gains from the knowledge (the software rasterizer: overwriting instead of
		 * blending, and occlusion-culling every draw the content hides) may run the draw with blending
		 * off, which is pixel-for-pixel identical for opaque content under src-over. The hint is part of
		 * the material sort key, so hinted and unhinted draws never share a batch.
		 */
		inline void SetOpaqueContentHint(bool hasOpaqueContent) {
			if (_hasOpaqueContentHint != hasOpaqueContent) {
				_hasOpaqueContentHint = hasOpaqueContent;
				_sortKeyDirty = true;
			}
		}

		inline ShaderProgramType GetShaderProgramType() const {
			return _shaderProgramType;
		}
		bool SetShaderProgramType(ShaderProgramType shaderProgramType);
		inline const RHI::ShaderProgram* GetShaderProgram() const {
			return _shaderProgram;
		}
		void SetShaderProgram(RHI::ShaderProgram* program);
		bool SetShader(Shader* shader);

		void SetDefaultAttributesParameters();
		void ReserveUniformsDataMemory();
		void SetUniformsDataPointer(std::uint8_t* dataPointer);

		/** @brief Wrapper around `RHI::ShaderUniforms::HasUniform()` */
		inline bool HasUniform(const char* name) const {
			return _shaderUniforms.HasUniform(name);
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::HasUniformBlock()` */
		inline bool HasUniformBlock(const char* name) const {
			return _shaderUniformBlocks.HasUniformBlock(name);
		}

		/** @brief Wrapper around `RHI::ShaderUniforms::GetUniform()` */
		inline RHI::UniformCache* Uniform(const char* name) {
			return _shaderUniforms.GetUniform(name);
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::GetUniformBlock()` */
		inline RHI::UniformBlockCache* UniformBlock(const char* name) {
			return _shaderUniformBlocks.GetUniformBlock(name);
		}

		/** @brief Wrapper around `RHI::ShaderUniforms::GetAllUniforms()` */
		inline const RHI::ShaderUniforms::UniformHashMapType& GetAllUniforms() const {
			return _shaderUniforms.GetAllUniforms();
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::GetAllUniformBlocks()` */
		inline const RHI::ShaderUniformBlocks::UniformHashMapType& GetAllUniformBlocks() const {
			return _shaderUniformBlocks.GetAllUniformBlocks();
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
		bool _isBlendingEnabled;
		// Whether every drawn pixel was hinted fully opaque (see SetOpaqueContentHint())
		bool _hasOpaqueContentHint;
		// Whether the cached sort key has to be recomputed
		bool _sortKeyDirty;
		// Number of texture units in use, i.e. the highest unit with a texture plus one
		std::uint8_t _usedTextureUnits;
		BlendingFactor _srcBlendingFactor;
		BlendingFactor _destBlendingFactor;
		BlendingFactor _srcAlphaBlendingFactor;
		BlendingFactor _destAlphaBlendingFactor;
		// Cached result of GetSortKey(), recomputed only when the hashed state changes
		std::uint32_t _sortKey;
		// Incremented every time SetShaderProgram() rebuilds the uniform caches, so
		// that pointers into them can be cached and safely invalidated by observers
		std::uint32_t _shaderChangeCounter;
		ShaderProgramType _shaderProgramType;
		RHI::ShaderProgram* _shaderProgram;
		RHI::ShaderUniforms _shaderUniforms;
		RHI::ShaderUniformBlocks _shaderUniformBlocks;
		const RHI::Texture* _textures[RHI::Texture::MaxTextureUnits];

		/** @brief The size of the memory buffer containing uniform values */
		std::uint32_t _uniformsHostBufferSize;
		/** @brief Memory buffer with uniform values to be sent to the GPU */
		std::unique_ptr<std::uint8_t[]> _uniformsHostBuffer;

		void Bind();
		// Maintains the used texture unit count after a texture change on the specified unit
		void UpdateUsedTextureUnits(std::uint32_t unit, bool textureSet);
		/** @brief Wrapper around `RHI::ShaderUniforms::CommitUniforms()` */
		inline void CommitUniforms() {
			_shaderUniforms.CommitUniforms();
		}
		/** @brief Wrapper around `RHI::ShaderUniformBlocks::CommitUniformBlocks()` */
		inline void CommitUniformBlocks() {
			_shaderUniformBlocks.CommitUniformBlocks();
		}
		/** @brief Wrapper around `RHI::ShaderProgram::DefineVertexFormat()` */
		void DefineVertexFormat(const RHI::Buffer* vbo, const RHI::Buffer* ibo, std::uint32_t vboOffset);
		std::uint32_t GetSortKey();
	};

}

