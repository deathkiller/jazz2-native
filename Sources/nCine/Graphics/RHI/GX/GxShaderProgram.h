#pragma once

#include "GxShaderTypes.h"
#include "GxVertexFormat.h"
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

namespace nCine::RHI::GX
{
	class GxShaderUniforms;
	class GxShaderUniformBlocks;
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Shader program of the GX backend (aliased as `RHI::ShaderProgram`)

		Does not compile or link GLSL. Instead it carries the offline ShaderCompiler reflection (set with
		@ref SetReflection() exactly like the OpenGL backend) from which it imports uniforms, uniform
		blocks and attributes, and the true (program, variant) identity plumbed by the loaders with
		@ref SetProgramIdentity(), from which the generated fixed-function effect of the program is
		resolved (see @ref GetGeneratedEffect()) - the object label is kept for logging only. @ref Use()
		records the program as current on the device; committed loose-uniform values are published back
		here so the effect can read them.
	*/
	class GxShaderProgram
	{
		friend class GxShaderUniforms;
		friend class GxShaderUniformBlocks;

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

		GxShaderProgram();
		explicit GxShaderProgram(QueryPhase queryPhase);
		GxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		GxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		GxShaderProgram(StringView vertexFile, StringView fragmentFile);
		~GxShaderProgram();

		GxShaderProgram(const GxShaderProgram&) = delete;
		GxShaderProgram& operator=(const GxShaderProgram&) = delete;

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

			@p programName is the `.shader` program name and @p variantName the variant define it was
			compiled with (empty/null for the base variant) - the exact key of the generated
			fixed-function tables, plumbed by ContentResolver and RenderResources. Set it together
			with @ref SetReflection(), before @ref Link(); the table lookup runs at link time.
		*/
		void SetProgramIdentity(const char* programName, const char* variantName);

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
		GxVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const GxBuffer* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const GxBuffer* vbo, const GxBuffer* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const GxBuffer* vbo, const GxBuffer* ibo, std::uint32_t vboOffset);
		/** @brief Returns the vertex buffer last bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const GxBuffer* GetBoundVbo() const {
			return _boundVbo;
		}
		/** @brief Returns the byte offset into @ref GetBoundVbo() the vertex data starts at */
		inline std::uint32_t GetBoundVboOffset() const {
			return _boundVboOffset;
		}

		void Reset();
		void SetObjectLabel(StringView label);

		/** @brief Returns whether the "unsupported effect" warning was already emitted for this program, and marks it emitted */
		bool FetchUnsupportedWarned() {
			bool value = _unsupportedWarned;
			_unsupportedWarned = true;
			return value;
		}

		inline bool GetLogOnErrors() const {
			return _shouldLogOnErrors;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			_shouldLogOnErrors = shouldLogOnErrors;
		}

		// -- GX backend extensions (used by the device and the effects) --

		/**
			@brief Returns the generated fixed-function effect of this program variant, or `nullptr`

			Resolved once at link time from the (program, variant) identity set with
			@ref SetProgramIdentity() against the table the ShaderCompiler transpiled from the
			shaders' `fixed_function` blocks. The dispatch runs its function (or the backend
			pipeline stage its `pipeline` intrinsic names); a null entry is skipped with a
			one-time warning.
		*/
		inline const FixedFunctionGeneratedEffect* GetGeneratedEffect() const {
			return _generatedEffect;
		}
		/** @brief Returns the object label the program was tagged with (the shader name), or an empty string */
		inline const char* GetObjectLabel() const {
			return _label.data();
		}
		/** @brief Returns `true` if the program was compiled as the DITHER variant of its shader */
		inline bool IsDitherVariant() const {
			return _ditherVariant;
		}
		/** @brief Returns `true` if the program samples indexed textures through the palette texture (its reflection binds `uTexturePalette`) */
		inline bool UsesPalette() const {
			return _usesPalette;
		}

		/**
			@brief Facts of the program the draw dispatch reads on every command, resolved once at introspection

			All of them are constants of the linked program (they only depend on its reflection), but the
			dispatch used to re-derive each by scanning reflection name strings per RenderCommand - dozens
			of dependent loads per draw on a CPU where those are the cost. Only the instance block's BINDING
			stays a per-draw read, because the material assigns bindings after linking.
		*/
		struct DispatchFacts
		{
			/** @brief The "InstanceBlock" (or the batched "InstancesBlock"), or `nullptr` */
			const GxUniformBlock* InstanceBlock = nullptr;
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
		/** @brief Returns the offline reflection last set on the program (kept for the effects to read) */
		inline const ShaderCompiler::ProgramVariant* GetReflection() const {
			return _effectReflection;
		}
		/** @brief Returns the imported metadata of the uniform block with the given name, or `nullptr` */
		const GxUniformBlock* FindBlock(const char* name) const;
		/** @brief Publishes a committed loose-uniform value pointer for the effects to read */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;
		/** @brief Returns the last published `uProjectionMatrix` value pointer without the by-name scan (read per draw) */
		inline const std::uint8_t* GetResolvedProjectionMatrix() const {
			return _resolvedProjectionMatrix;
		}
		/** @brief Returns the last published `uViewMatrix` value pointer without the by-name scan (read per draw) */
		inline const std::uint8_t* GetResolvedViewMatrix() const {
			return _resolvedViewMatrix;
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

		std::vector<GxUniform> _uniforms;
		std::vector<GxUniformBlock> _uniformBlocks;
		std::vector<GxAttribute> _attributes;

		const ShaderCompiler::ProgramVariant* _reflection;
		// Kept after introspection so the effects can read member offsets/texture bindings at draw time
		const ShaderCompiler::ProgramVariant* _effectReflection;
		// The generated-table entry of this (program, variant) (see GetGeneratedEffect()); null when
		// the shader has no fixed_function block (the draw is then skipped with a one-time warning)
		const FixedFunctionGeneratedEffect* _generatedEffect;
		bool _unsupportedWarned = false;
		bool _ditherVariant;
		bool _usesPalette;
		DispatchFacts _dispatchFacts;
		// The true identity set by SetProgramIdentity(): the .shader program name and the variant
		// define - the generated-table key. Copies, because runtime loaders hand in transient strings.
		String _programName;
		String _variantName;
		// The shader name the program was tagged with (kept for log messages only)
		String _label;

		GxVertexFormat _vertexFormat;
		const GxBuffer* _boundVbo;
		std::uint32_t _boundVboOffset;
		const GxBuffer* _boundIbo;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> _resolvedUniforms;
		// The camera matrices are read by the device on every draw, so they bypass the by-name list scan
		// above; recognized once in SetResolvedUniform() (see there)
		const std::uint8_t* _resolvedProjectionMatrix = nullptr;
		const std::uint8_t* _resolvedViewMatrix = nullptr;

		void PerformIntrospection();
		void ImportReflection();
	};
}
