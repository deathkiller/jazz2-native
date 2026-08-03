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
			return handle_;
		}
		inline Status GetStatus() const {
			return status_;
		}
		inline Introspection GetIntrospection() const {
			return introspection_;
		}
		inline QueryPhase GetQueryPhase() const {
			return queryPhase_;
		}
		inline std::uint32_t GetBatchSize() const {
			return batchSize_;
		}
		inline void SetBatchSize(std::uint32_t value) {
			batchSize_ = value;
		}

		bool IsLinked() const;

		std::uint32_t RetrieveInfoLogLength() const {
			return 0;
		}
		void RetrieveInfoLog(std::string& infoLog) const {
			static_cast<void>(infoLog);
		}

		inline std::uint32_t GetUniformsSize() const {
			return uniformsSize_;
		}
		inline std::uint32_t GetUniformBlocksSize() const {
			return uniformBlocksSize_;
		}

		bool AttachShaderFromFile(ShaderStage stage, StringView filename);
		bool AttachShaderFromString(ShaderStage stage, StringView string);
		bool AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings);
		bool AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename);

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes */
		inline void SetReflection(const ShaderCompiler::ProgramVariant* reflection) {
			reflection_ = reflection;
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
			return std::uint32_t(attributes_.size());
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
			return boundVbo_;
		}
		/** @brief Returns the byte offset into @ref GetBoundVbo() the vertex data starts at */
		inline std::uint32_t GetBoundVboOffset() const {
			return boundVboOffset_;
		}

		void Reset();
		void SetObjectLabel(StringView label);

		/** @brief Returns whether the "unsupported effect" warning was already emitted for this program, and marks it emitted */
		bool FetchUnsupportedWarned() {
			bool value = unsupportedWarned_;
			unsupportedWarned_ = true;
			return value;
		}

		inline bool GetLogOnErrors() const {
			return shouldLogOnErrors_;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
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
			return generatedEffect_;
		}
		/** @brief Returns the object label the program was tagged with (the shader name), or an empty string */
		inline const char* GetObjectLabel() const {
			return label_.data();
		}
		/** @brief Returns `true` if the program was compiled as the DITHER variant of its shader */
		inline bool IsDitherVariant() const {
			return ditherVariant_;
		}
		/** @brief Returns `true` if the program samples indexed textures through the palette texture (its reflection binds `uTexturePalette`) */
		inline bool UsesPalette() const {
			return usesPalette_;
		}
		/** @brief Returns the offline reflection last set on the program (kept for the effects to read) */
		inline const ShaderCompiler::ProgramVariant* GetReflection() const {
			return effectReflection_;
		}
		/** @brief Returns the imported metadata of the uniform block with the given name, or `nullptr` */
		const GxUniformBlock* FindBlock(const char* name) const;
		/** @brief Publishes a committed loose-uniform value pointer for the effects to read */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;
		/** @brief Returns the last published `uProjectionMatrix` value pointer without the by-name scan (read per draw) */
		inline const std::uint8_t* GetResolvedProjectionMatrix() const {
			return resolvedProjectionMatrix_;
		}
		/** @brief Returns the last published `uViewMatrix` value pointer without the by-name scan (read per draw) */
		inline const std::uint8_t* GetResolvedViewMatrix() const {
			return resolvedViewMatrix_;
		}

	private:
		static std::uint32_t nextHandle_;

		std::uint32_t handle_;
		Status status_;
		Introspection introspection_;
		QueryPhase queryPhase_;
		std::uint32_t batchSize_;
		bool shouldLogOnErrors_;
		std::uint32_t uniformsSize_;
		std::uint32_t uniformBlocksSize_;

		std::vector<GxUniform> uniforms_;
		std::vector<GxUniformBlock> uniformBlocks_;
		std::vector<GxAttribute> attributes_;

		const ShaderCompiler::ProgramVariant* reflection_;
		// Kept after introspection so the effects can read member offsets/texture bindings at draw time
		const ShaderCompiler::ProgramVariant* effectReflection_;
		// The generated-table entry of this (program, variant) (see GetGeneratedEffect()); null when
		// the shader has no fixed_function block (the draw is then skipped with a one-time warning)
		const FixedFunctionGeneratedEffect* generatedEffect_;
		bool unsupportedWarned_ = false;
		bool ditherVariant_;
		bool usesPalette_;
		// The true identity set by SetProgramIdentity(): the .shader program name and the variant
		// define - the generated-table key. Copies, because runtime loaders hand in transient strings.
		String programName_;
		String variantName_;
		// The shader name the program was tagged with (kept for log messages only)
		String label_;

		GxVertexFormat vertexFormat_;
		const GxBuffer* boundVbo_;
		std::uint32_t boundVboOffset_;
		const GxBuffer* boundIbo_;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> resolvedUniforms_;
		// The camera matrices are read by the device on every draw, so they bypass the by-name list scan
		// above; recognized once in SetResolvedUniform() (see there)
		const std::uint8_t* resolvedProjectionMatrix_ = nullptr;
		const std::uint8_t* resolvedViewMatrix_ = nullptr;

		void PerformIntrospection();
		void ImportReflection();
	};
}
