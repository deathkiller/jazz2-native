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

	/**
		@brief The hand-written C++ effect a software program dispatches to at draw time

		The GX backend does not run GLSL — instead each program is tagged with the effect that
		reproduces its shader, derived from the program's object label. New effects (palette remap, tinted,
		outline, ...) are added here as the backend grows; @ref Unknown programs log a skipped draw.
	*/
	enum class GxEffect
	{
		Unknown,				/**< No matching C++ effect — draws are skipped with a log message */
		DefaultSprite,			/**< `texture(uTexture, vTexCoords) * vColor` over a single instance */
		DefaultBatchedSprites,	/**< The same effect over an array of batched instances */
		DefaultSpriteNoTexture,	/**< Solid-colour sprite (`vColor`, no texture) over a single instance */
		DefaultBatchedSpritesNoTexture,	/**< The solid-colour sprite over an array of batched instances */
		TexturedBackground,		/**< The animated, per-pixel-warped menu/level background (planar tunnel) */
		TexturedBackgroundCircle,	/**< The circular ("tube") variant of the textured background */
		Colorized,				/**< Grayscale + dye tint, approximated by modulating with the amplified dye colour */
		BatchedColorized,		/**< The colorized effect over an array of batched instances */
		PaletteRemap,			/**< An R8/RG8 index sprite recolored through the shared palette texture */
		BatchedPaletteRemap,	/**< The palette-remap effect over an array of batched instances */
		Combine					/**< The viewport compositor (scene + lighting + blur + ambient) */
	};

	/**
		@brief Shader program of the GX backend (aliased as `RHI::ShaderProgram`)

		Does not compile or link GLSL. Instead it carries the offline ShaderCompiler reflection (set with
		@ref SetReflection() exactly like the OpenGL backend) from which it imports uniforms, uniform
		blocks and attributes, and a @ref GxEffect identity derived from its object label that tells the
		device which hand-written C++ effect to run. @ref Use() records the program as current on the
		device; committed loose-uniform values are published back here so the effect can read them.
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

		/** @brief Returns the C++ effect this program dispatches to */
		inline GxEffect GetEffect() const {
			return effect_;
		}
		/** @brief Returns the object label the program was tagged with (the shader name), or an empty string */
		inline const char* GetObjectLabel() const {
			return label_.data();
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
		const GxUniformBlock* FindBlock(const char* name) const;
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

		std::vector<GxUniform> uniforms_;
		std::vector<GxUniformBlock> uniformBlocks_;
		std::vector<GxAttribute> attributes_;

		const ShaderCompiler::ProgramVariant* reflection_;
		// Kept after introspection so the effects can read member offsets/texture bindings at draw time
		const ShaderCompiler::ProgramVariant* effectReflection_;
		GxEffect effect_;
		bool unsupportedWarned_ = false;
		bool ditherVariant_;
		// The shader name the program was tagged with (used to look up an offline-transpiled generated fragment)
		String label_;

		GxVertexFormat vertexFormat_;
		const GxBuffer* boundVbo_;
		const GxBuffer* boundIbo_;

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
