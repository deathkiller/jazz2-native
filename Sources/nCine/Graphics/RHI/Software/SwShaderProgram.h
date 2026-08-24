#pragma once

#include "SwShaderTypes.h"
#include "SwVertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#include <Containers/ArrayView.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::Software
{
	class SwShaderUniforms;
	class SwShaderUniformBlocks;

	/**
		@brief The hand-written C++ effect a software program dispatches to at draw time

		The software backend does not run GLSL — instead each program is tagged with the effect that
		reproduces its shader, derived from the program's object label. New effects (palette remap, tinted,
		outline, ...) are added here as the backend grows; @ref Unknown programs log a skipped draw.
	*/
	enum class SwEffect
	{
		Unknown,				/**< No matching C++ effect — draws are skipped with a log message */
		DefaultSprite,			/**< `texture(uTexture, vTexCoords) * vColor` over a single instance */
		DefaultBatchedSprites,	/**< The same effect over an array of batched instances */
		DefaultSpriteNoTexture,	/**< Solid-colour sprite (`vColor`, no texture) over a single instance */
		DefaultBatchedSpritesNoTexture,	/**< The solid-colour sprite over an array of batched instances */
		TexturedBackground,		/**< The animated, per-pixel-warped menu/level background (planar tunnel) */
		TexturedBackgroundCircle,	/**< The circular ("tube") variant of the textured background */
		PaletteRemap,			/**< An R8/RG8 index sprite recolored through the shared palette texture */
		BatchedPaletteRemap,	/**< The palette-remap effect over an array of batched instances */
		Combine					/**< The viewport compositor (scene + lighting + blur + ambient) */
	};

	/**
		@brief Shader program of the software backend (aliased as `RHI::ShaderProgram`)

		Does not compile or link GLSL. Instead it carries the offline ShaderCompiler reflection (set with
		@ref SetReflection() exactly like the OpenGL backend) from which it imports uniforms, uniform
		blocks and attributes, and a @ref SwEffect identity derived from its object label that tells the
		device which hand-written C++ effect to run. @ref Use() records the program as current on the
		device; committed loose-uniform values are published back here so the effect can read them.
	*/
	class SwShaderProgram
	{
		friend class SwShaderUniforms;
		friend class SwShaderUniformBlocks;

	public:
		enum class Introspection
		{
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

		enum class Status
		{
			NotLinked,
			CompilationFailed,
			LinkingFailed,
			Linked,
			LinkedWithDeferredQueries,
			LinkedWithIntrospection
		};

		enum class QueryPhase
		{
			Immediate,
			Deferred
		};

		/** @brief Default batch size, indicating the shader is not batched */
		static constexpr std::int32_t DefaultBatchSize = -1;

		SwShaderProgram();
		explicit SwShaderProgram(QueryPhase queryPhase);
		SwShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		SwShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		SwShaderProgram(StringView vertexFile, StringView fragmentFile);
		~SwShaderProgram();

		SwShaderProgram(const SwShaderProgram&) = delete;
		SwShaderProgram& operator=(const SwShaderProgram&) = delete;

		/** @brief Returns a backend-neutral identifier uniquely identifying the program (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
			return _handle;
		}
		inline Status GetStatus() const {
			return _status;
		}
		inline Introspection GetIntrospection() const {
			return _introspection;
		}
		inline QueryPhase GetQueryPhase() const {
			return _queryPhase;
		}
		inline std::uint32_t GetBatchSize() const {
			return _batchSize;
		}
		inline void SetBatchSize(std::uint32_t value) {
			_batchSize = value;
		}

		bool IsLinked() const;

		std::uint32_t RetrieveInfoLogLength() const {
			return 0;
		}
		void RetrieveInfoLog(std::string& infoLog) const {
			static_cast<void>(infoLog);
		}

		inline std::uint32_t GetUniformsSize() const {
			return _uniformsSize;
		}
		inline std::uint32_t GetUniformBlocksSize() const {
			return _uniformBlocksSize;
		}

		bool AttachShaderFromFile(ShaderStage stage, StringView filename);
		bool AttachShaderFromString(ShaderStage stage, StringView string);
		bool AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings);
		bool AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename);

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes */
		inline void SetReflection(const ShaderCompiler::ProgramVariant* reflection) {
			_reflection = reflection;
		}
		/**
			@brief Records the true (program, variant) identity of the loaded shader

			Only the fixed-function console backends consume it (they resolve their generated
			effect tables from this identity instead of the object label); the software backend
			still keys its fast paths and generated fragments on the object label and ignores it.
		*/
		inline void SetProgramIdentity(const char* programName, const char* variantName) {
			static_cast<void>(programName);
			static_cast<void>(variantName);
		}


		bool Link(Introspection introspection);
		void Use();
		bool Validate() {
			return true;
		}
		bool FinalizeAfterLinking(Introspection introspection);

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(_attributes.size());
		}
		bool HasAttribute(const char* name) const;
		SwVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const SwBuffer* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const SwBuffer* vbo, const SwBuffer* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const SwBuffer* vbo, const SwBuffer* ibo, std::uint32_t vboOffset);

		void Reset();
		void SetObjectLabel(StringView label);

		inline bool GetLogOnErrors() const {
			return _shouldLogOnErrors;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			_shouldLogOnErrors = shouldLogOnErrors;
		}

		// -- Software backend extensions (used by the device and the effects) --

		/** @brief Returns the C++ effect this program dispatches to */
		inline SwEffect GetEffect() const {
			return _effect;
		}
		/** @brief Returns the object label the program was tagged with (the shader name), or an empty string */
		inline const char* GetObjectLabel() const {
			return _label.data();
		}
		/** @brief Returns `true` if the program is the dithering variant of its effect (derived from the label) */
		inline bool IsDitherVariant() const {
			return _ditherVariant;
		}
		/** @brief Returns the offline reflection last set on the program (kept for the effects to read) */
		inline const ShaderCompiler::ProgramVariant* GetReflection() const {
			return _effectReflection;
		}
		/** @brief Returns the imported metadata of the uniform block with the given name, or `nullptr` */
		const SwUniformBlock* FindBlock(const char* name) const;
		/** @brief Publishes a committed loose-uniform value pointer for the effects to read */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;
		/** @brief Returns the published `uProjectionMatrix` value without the by-name scan (it runs per draw) */
		inline const std::uint8_t* GetResolvedProjection() const {
			return _resolvedProjection;
		}
		/** @brief Returns the published `uViewMatrix` value without the by-name scan (it runs per draw) */
		inline const std::uint8_t* GetResolvedView() const {
			return _resolvedView;
		}

		/**
			@brief Facts of the program the draw dispatch reads on every command, resolved once at introspection

			All of them are constants of the linked program (they only depend on its reflection), but the
			dispatch used to re-derive each by scanning reflection name strings per RenderCommand - dozens
			of dependent loads per draw. Only the instance block's BINDING stays a per-draw read, because
			the material assigns bindings after linking.
		*/
		struct DispatchFacts
		{
			/** @brief The "InstanceBlock" (or the batched "InstancesBlock"), or `nullptr` */
			const SwUniformBlock* InstanceBlock = nullptr;
			/** @brief Reflected per-instance stride; positive exactly for batched programs */
			std::uint32_t InstanceStride = 0;
			/** @brief Unit `uTexture` binds (meaningful when @ref HasTexture) */
			std::int32_t TextureUnit = 0;
			/** @brief Unit `uTexturePalette` binds (meaningful when the program remaps) */
			std::int32_t PaletteUnit = 1;
			/** @brief Whether the reflection binds `uTexture` at all */
			bool HasTexture = false;
			/** @brief Whether the instance block declares `texRect` (the textured member layout) */
			bool TexturedLayout = false;
		};
		inline const DispatchFacts& GetDispatchFacts() const {
			return _dispatchFacts;
		}

	private:
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		Status _status;
		Introspection _introspection;
		QueryPhase _queryPhase;
		std::uint32_t _batchSize;
		bool _shouldLogOnErrors;
		std::uint32_t _uniformsSize;
		std::uint32_t _uniformBlocksSize;

		std::vector<SwUniform> _uniforms;
		std::vector<SwUniformBlock> _uniformBlocks;
		std::vector<SwAttribute> _attributes;

		const ShaderCompiler::ProgramVariant* _reflection;
		// Kept after introspection so the effects can read member offsets/texture bindings at draw time
		const ShaderCompiler::ProgramVariant* _effectReflection;
		SwEffect _effect;
		bool _ditherVariant;
		DispatchFacts _dispatchFacts;
		// The shader name the program was tagged with (used to look up an offline-transpiled generated fragment)
		String _label;

		SwVertexFormat _vertexFormat;
		const SwBuffer* _boundVbo;
		const SwBuffer* _boundIbo;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> _resolvedUniforms;
		// The two names every dispatch reads are kept as direct pointers, bypassing the by-name scan
		const std::uint8_t* _resolvedProjection = nullptr;
		const std::uint8_t* _resolvedView = nullptr;

		void PerformIntrospection();
		void ImportReflection();
	};
}
