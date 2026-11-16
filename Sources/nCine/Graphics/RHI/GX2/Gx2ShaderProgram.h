#pragma once

#include "Gx2ShaderTypes.h"
#include "Gx2VertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>

#include <gx2/shaders.h>
#include "Gx2Memory.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::GX2
{
	class Gx2ShaderUniforms;
	class Gx2ShaderUniformBlocks;
	class Gx2BufferObject;

	/**
		@brief Shader program of the GX2 backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes. Its stage binaries come from the
		identity @ref SetProgramIdentity() plumbs in, looked up in `Gx2GeneratedShaders.h`, the table of
		CafeGLSL output that `ShaderCompiler --emit-gx2` writes - there is no shader compiler on the console
		that the game can rely on.

		Both stages are compiled in uniform-block mode. CafeGLSL gathers each stage's loose uniforms into a
		block of its own (`__cafe_loose_uniforms`); the device fills that one per draw from the values the
		uniform caches publish (@ref SetResolvedUniform()). The explicit std140 blocks are handed over as the
		ranges the pipeline bound to the engine's binding points (`Gx2Device::GetUniformRange()`), matched
		by name. A program without an entry in the table links from its reflection alone and
		@ref HasBinaries() is `false`; the device skips draws with it.
	*/
	class Gx2ShaderProgram
	{
		friend class Gx2ShaderUniforms;
		friend class Gx2ShaderUniformBlocks;

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

		Gx2ShaderProgram();
		explicit Gx2ShaderProgram(QueryPhase queryPhase);
		Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile);
		~Gx2ShaderProgram();

		Gx2ShaderProgram(const Gx2ShaderProgram&) = delete;
		Gx2ShaderProgram& operator=(const Gx2ShaderProgram&) = delete;

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

			This backend will resolve its generated shader binaries from this identity, the same way the fixed-function
			console backends resolve their generated effect tables from it.
		*/
		inline void SetProgramIdentity(const char* programName, const char* variantName) {
			_programName = programName;
			_variantName = variantName;
		}
		/** @brief Returns the generated program's name, or `nullptr` for one that carries no identity */
		inline const char* GetProgramName() const {
			return _programName;
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
		/** @brief Returns `true` if the reflection declares @p name with an integer type (a batch element index) */
		bool IsIntegerAttribute(const char* name) const;
		Gx2VertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const Gx2BufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const Gx2BufferObject* vbo, const Gx2BufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const Gx2BufferObject* vbo, const Gx2BufferObject* ibo, std::uint32_t vboOffset);

		void Reset();
		void SetObjectLabel(StringView label);

		inline bool GetLogOnErrors() const {
			return _shouldLogOnErrors;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			_shouldLogOnErrors = shouldLogOnErrors;
		}

		// -- Backend extensions (used by the uniform caches and the device draw path) --

		/** @brief Publishes a committed loose-uniform value pointer for the device to upload */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

		/** @brief Returns `true` when the program has GX2 vertex and pixel shaders to bind */
		inline bool HasBinaries() const {
			return _hasBinaries;
		}

		/** @brief One loose uniform of a stage, copied into the stage's loose-uniform block per draw */
		struct LooseUniform
		{
			const char* Name;
			/** @brief Byte offset in the block */
			std::uint32_t Offset;
			/** @brief Array elements */
			std::uint32_t Count;
			/** @brief Bytes of one element in the engine's packed value */
			std::uint32_t ElementSize;
			/** @brief Bytes between elements in the block (an array element takes at least a whole register) */
			std::uint32_t Stride;
		};

		/** @brief An explicit uniform block of a stage and the engine binding point that feeds it */
		struct BlockBinding
		{
			std::uint32_t Location;
			std::uint32_t BindingIndex;
			std::uint32_t Size;
		};

		/** @brief What the device uploads for one stage before a draw */
		struct StageBindings
		{
			/** @brief Hardware location of the loose-uniform block, or `-1` when the stage has none */
			std::int32_t LooseLocation = -1;
			/** @brief Size of the loose-uniform block, rounded up to a whole register */
			std::uint32_t LooseSize = 0;
			SmallVector<LooseUniform, 0> LooseUniforms;
			SmallVector<BlockBinding, 0> Blocks;
		};

		/** @brief A pixel-stage sampler: the unit the shader samples and the unit the pipeline binds to */
		struct SamplerBinding
		{
			std::uint32_t HardwareUnit;
			std::uint32_t EngineUnit;
		};

		inline const GX2VertexShader* GetVertexShader() const {
			return &_vertexShader;
		}
		inline const GX2PixelShader* GetPixelShader() const {
			return &_pixelShader;
		}
		inline const StageBindings& GetVertexBindings() const {
			return _vertexBindings;
		}
		inline const StageBindings& GetPixelBindings() const {
			return _pixelBindings;
		}
		inline const SmallVector<SamplerBinding, 0>& GetSamplers() const {
			return _samplers;
		}

		/**
			@brief Returns the fetch shader for the current vertex format, building it on first use

			Every enabled attribute of the format becomes one stream, read from attribute buffer
			`streamBuffers[i]`; the device sets those buffers up with `GX2SetAttribBuffer()`. A program
			without attributes (the sprites build their quad from `gl_VertexID`) gets an empty fetch shader.
		*/
		const GX2FetchShader* GetFetchShader();

		/** @brief Returns the vertex format the pipeline last defined on this program */
		inline Gx2VertexFormat& GetVertexFormat() {
			return _vertexFormat;
		}

	private:
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		Status _status;
		Introspection _introspection;
		QueryPhase _queryPhase;
		std::uint32_t _batchSize;
		bool _shouldLogOnErrors;

		const ShaderCompiler::ProgramVariant* _reflection;
		const char* _programName;
		const char* _variantName;

		SmallVector<Gx2Uniform, 0> _uniforms;
		SmallVector<Gx2UniformBlock, 0> _uniformBlocks;
		SmallVector<Gx2Attribute, 0> _attributes;
		std::uint32_t _uniformsSize;
		std::uint32_t _uniformBlocksSize;

		/** @brief Committed loose-uniform value pointers, published by the uniform caches */
		SmallVector<std::pair<const char*, const std::uint8_t*>, 0> _resolvedUniforms;

		Gx2VertexFormat _vertexFormat;

		bool _hasBinaries;
		GX2VertexShader _vertexShader;
		GX2PixelShader _pixelShader;
		Gx2Memory::Block _vertexProgram;
		Gx2Memory::Block _pixelProgram;
		StageBindings _vertexBindings;
		StageBindings _pixelBindings;
		SmallVector<SamplerBinding, 0> _samplers;

		/** @brief A built fetch shader together with the vertex format fingerprint it was built for */
		struct FetchShaderEntry
		{
			std::uint64_t Fingerprint;
			GX2FetchShader Shader;
			Gx2Memory::Block Program;
		};
		SmallVector<FetchShaderEntry, 0> _fetchShaders;

		/** @brief Imports uniforms, blocks and attributes from the offline reflection */
		void ImportReflection(Introspection introspection);
		/** @brief Loads the generated shaders of this program's identity; `false` if the table has none */
		bool LoadBinaries();
		/** @brief Resolves the loose uniforms, blocks and samplers of the loaded shaders against the reflection */
		void BuildBindings();
		/** @brief Releases the shader programs and fetch shaders (through the device's retire list) */
		void ReleaseBinaries();
	};
}
