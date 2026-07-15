#pragma once

#include "SwShaderTypes.h"
#include "SwVertexFormat.h"

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

namespace nCine::RhiSoftware
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
		TexturedBackground,		/**< The animated, per-pixel-warped menu/level background (planar tunnel) */
		TexturedBackgroundCircle,	/**< The circular ("tube") variant of the textured background */
		PaletteRemap,			/**< An R8/RG8 index sprite recolored through the shared palette texture */
		BatchedPaletteRemap,	/**< The palette-remap effect over an array of batched instances */
		Combine					/**< The viewport compositor (scene + lighting + blur + ambient) */
	};

	/**
		@brief Shader program of the software backend (aliased as `Rhi::ShaderProgram`)

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

		/** @brief Returns a synthetic handle uniquely identifying the program (used by material sort keys) */
		inline std::uint32_t GetGLHandle() const {
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

		bool AttachShaderFromFile(std::uint32_t type, StringView filename);
		bool AttachShaderFromString(std::uint32_t type, StringView string);
		bool AttachShaderFromStrings(std::uint32_t type, ArrayView<const StringView> strings);
		bool AttachShaderFromStringsAndFile(std::uint32_t type, ArrayView<const StringView> strings, StringView filename);

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes */
		inline void SetReflection(const ShaderCompiler::ProgramVariant* reflection) {
			reflection_ = reflection;
		}

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
			return shouldLogOnErrors_;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

		// -- Software backend extensions (used by the device and the effects) --

		/** @brief Returns the C++ effect this program dispatches to */
		inline SwEffect GetEffect() const {
			return effect_;
		}
		/** @brief Returns `true` if the program is the dithering variant of its effect (derived from the label) */
		inline bool IsDitherVariant() const {
			return ditherVariant_;
		}
		/** @brief Returns the offline reflection last set on the program (kept for the effects to read) */
		inline const ShaderCompiler::ProgramVariant* GetReflection() const {
			return effectReflection_;
		}
		/** @brief Returns the imported metadata of the uniform block with the given name, or `nullptr` */
		const SwUniformBlock* FindBlock(const char* name) const;
		/** @brief Publishes a committed loose-uniform value pointer for the effects to read */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

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

		std::vector<SwUniform> uniforms_;
		std::vector<SwUniformBlock> uniformBlocks_;
		std::vector<SwAttribute> attributes_;

		const ShaderCompiler::ProgramVariant* reflection_;
		// Kept after introspection so the effects can read member offsets/texture bindings at draw time
		const ShaderCompiler::ProgramVariant* effectReflection_;
		SwEffect effect_;
		bool ditherVariant_;

		SwVertexFormat vertexFormat_;
		const SwBuffer* boundVbo_;
		const SwBuffer* boundIbo_;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> resolvedUniforms_;

		void PerformIntrospection();
		void ImportReflection();
	};
}
