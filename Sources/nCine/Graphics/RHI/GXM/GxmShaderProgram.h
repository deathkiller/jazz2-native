#pragma once

#include "GxmShaderTypes.h"
#include "GxmVertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <psp2/gxm.h>

using namespace Death::Containers;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::GXM
{
	class GxmShaderUniforms;
	class GxmShaderUniformBlocks;
	class GxmBufferObject;

	/**
		@brief One reflected sceGxm program parameter the draw path writes a uniform into

		`ResourceIndex` is where the value goes: sceGxm addresses a default uniform buffer in 32-bit
		components, so the bytes land at `ResourceIndex * 4`. That this is the whole story was confirmed on
		the console - a sprite's block members come back at component 32, 48, 52, 56 and 58, which times four
		are byte offsets 128, 192, 208, 224 and 232, i.e. exactly the engine's std140 offsets 0, 64, 80, 96
		and 104 relative to the first. Resolving the parameters once at link time keeps
		`sceGxmProgramFindParameterByName()` - a linear scan over the program's parameter table - off the
		per-draw path.
	*/
	struct GxmUniformSlot
	{
		const char* Name;				// the reflection's own string, which outlives the program
		std::uint32_t ResourceIndex;	// component offset within the stage's default uniform buffer
		std::uint32_t ByteSize;			// bytes to copy (from the engine-side reflection, not the shader)
	};

	/**
		@brief One vertex attribute the compiled stage declares, as the shader itself reports it

		The offline reflection cannot supply this list. It describes the *modern GLSL* source, where the
		sprite shaders take no vertex input at all and synthesize the quad corner from `gl_VertexID` - while
		the Cg lowering has no vertex-ID input and reads `aQuadCorner` / `aInstanceIndex` instead, attributes
		that @ref ShaderCompiler::VertexIdRewrite invents and the reflection has never heard of. Asking the
		compiled program is therefore the only way to know a sprite shader has vertex inputs at all; the
		OpenGL|ES 2.0 profile does the same thing with `glGetAttribLocation()` for the same reason.

		`Name` points into the compiled program's own string table (past any `<struct>.` qualifier), which
		lives as long as the GXP binary this program owns.
	*/
	struct GxmStageAttribute
	{
		const char* Name;
		std::uint16_t RegIndex;			// what SceGxmVertexAttribute::regIndex takes
		std::uint8_t ComponentCount;	// as the shader declares it, before the stream layout narrows it
	};

	/**
		@brief Shader program of the sceGxm backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes, and gets its two stage *sources* from
		the identity @ref SetProgramIdentity() plumbs in - the generated `CgGeneratedShaders.h` table, which
		the Cg emitter fills offline. The sources are Cg, so they are compiled **on the console** by
		SceShaccCg (through vitaShaRK's `shark_compile_shader()`) when the program links: sceGxm consumes
		GXP binaries and the SDK ships no offline compiler for them, so `libshacccg.suprx` from the console's
		own firmware is a hard requirement of this backend - @ref GxmDevice::CreateSwapchain() says so
		explicitly if it is missing.

		Linking then registers both stages with the shader patcher and reflects their parameters into the
		@ref GxmUniformSlot tables the draw path uploads through. The two *patched* programs sceGxm actually
		binds are created on demand and cached, because neither is a property of the shader alone:

		- a vertex program bakes in the vertex layout, so there is one per @ref GxmVertexFormat the pipeline
		  binds (usually exactly one),
		- a fragment program bakes in the blend state, so there is one per blend configuration the material
		  sorting produces (a handful: opaque, alpha-blended, additive, ...).
	*/
	class GxmShaderProgram
	{
		friend class GxmShaderUniforms;
		friend class GxmShaderUniformBlocks;

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

		GxmShaderProgram();
		explicit GxmShaderProgram(QueryPhase queryPhase);
		GxmShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		GxmShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		GxmShaderProgram(StringView vertexFile, StringView fragmentFile);
		~GxmShaderProgram();

		GxmShaderProgram(const GxmShaderProgram&) = delete;
		GxmShaderProgram& operator=(const GxmShaderProgram&) = delete;

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

			This backend resolves its generated Cg stage sources from this identity, the same way the
			fixed-function console backends resolve their generated effect tables from it.
		*/
		inline void SetProgramIdentity(const char* programName, const char* variantName) {
			programName_ = programName;
			variantName_ = variantName;
		}
		/** @brief Returns the generated program's name, or `nullptr` for one that carries no identity */
		inline const char* GetProgramName() const {
			return programName_;
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
		/** @brief Returns `true` if the reflection declares @p name with an integer type (a batch element index) */
		bool IsIntegerAttribute(const char* name) const;
		GxmVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const GxmBufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const GxmBufferObject* vbo, const GxmBufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const GxmBufferObject* vbo, const GxmBufferObject* ibo, std::uint32_t vboOffset);

		void Reset();
		void SetObjectLabel(StringView label);

		inline bool GetLogOnErrors() const {
			return shouldLogOnErrors_;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

		// -- Backend extensions (used by the uniform caches and the device draw path) --

		/** @brief Publishes a committed loose-uniform value pointer for the device to upload */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

		/** @brief Returns the compiled vertex stage, or `nullptr` if the program did not link */
		inline const SceGxmProgram* GetVertexStage() const {
			return vertexStage_;
		}
		/** @brief Returns the compiled fragment stage, or `nullptr` */
		inline const SceGxmProgram* GetFragmentStage() const {
			return fragmentStage_;
		}
		/** @brief Returns/creates the patched vertex program for the currently bound vertex format, or `nullptr` */
		SceGxmVertexProgram* GetVertexProgram();
		/**
			@brief Returns/creates the patched fragment program for a blend configuration, or `nullptr`

			@param blendKey Packed blend state from @ref PackBlendKey(), the cache's key
			@param blendInfo The blend state to bake in, or `nullptr` for opaque (blending disabled)
		*/
		SceGxmFragmentProgram* GetFragmentProgram(std::uint32_t blendKey, const SceGxmBlendInfo* blendInfo);

		/** @brief Returns the byte size of the vertex stage's default uniform buffer */
		inline std::uint32_t GetVertexUniformBufferSize() const {
			return vertexUniformBufferSize_;
		}
		/** @brief Returns the byte size of the fragment stage's default uniform buffer */
		inline std::uint32_t GetFragmentUniformBufferSize() const {
			return fragmentUniformBufferSize_;
		}

		/** @brief Loose-uniform slots of the vertex stage (resolved once at link time) */
		inline const SmallVector<GxmUniformSlot, 0>& GetVertexUniformSlots() const {
			return vertexUniformSlots_;
		}
		/** @brief Loose-uniform slots of the fragment stage */
		inline const SmallVector<GxmUniformSlot, 0>& GetFragmentUniformSlots() const {
			return fragmentUniformSlots_;
		}

		/**
			@brief Where one uniform block's bytes go in a stage's default uniform buffer

			The Cg dialect has no counterpart to a std140 block - its emitter hoists each block member to a
			top-level `uniform` - so a block is uploaded member by member. Both layouts place an
			array-of-structs element on a 16-byte boundary and pack its fields the same way, so the member's
			bytes go across verbatim out of the range the pipeline bound, without being repacked.

			A **batched** block does not go through the default uniform buffer at all. Its instance array is
			tens of kilobytes, far past what the SGX's uniform registers hold, so the compiler places it in a
			*uniform buffer container* - reported as `SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER` - which is
			bound by address rather than written into. That is why the pipeline's uniform buffers are
			GPU-visible memory (see @ref GxmBufferObject): the range the batcher filled is handed to sceGxm as
			a pointer, with no copy at all. Mistaking this case for the copied one is invisible at load time
			and costs every batched draw its instance data - sprites, text and tile layers all at once.
		*/
		struct GxmBlockUpload
		{
			/** @brief How the member reaches the shader */
			enum class Destination
			{
				DefaultUniformBuffer,	//!< copied into the draw's reserved default uniform buffer
				UniformBufferContainer	//!< bound by address with sceGxmSet{Vertex,Fragment}UniformBuffer()
			};

			Destination Where;
			std::uint32_t BindingIndex;		// uniform binding point the pipeline bound the range to
			std::uint32_t SourceOffset;		// byte offset of the member within that range
			std::uint32_t Index;			// component offset in the default buffer, or the container's buffer index
			std::uint32_t MaxByteSize;		// bytes the engine's block layout declares (an upper bound on a copy)
			const char* Name;				// the reflection's member name, for diagnostics
			// Element strides of a batched instance array, on the engine's side and in the compiled shader; both
			// zero for anything else. They differ whenever the shader's Instance struct is not the widest one -
			// BatchedLighting's has no palOffset, so the compiler packs its element into less than the std140
			// stride the pipeline writes - and then only element zero of a copied-as-one array lands where the
			// shader reads it, with every later element progressively further out
			std::uint32_t SourceStride;
			std::uint32_t CompiledStride;
		};

		/** @brief Uniform-block member uploads of the vertex stage */
		inline const SmallVector<GxmBlockUpload, 0>& GetVertexBlockUploads() const {
			return vertexBlockUploads_;
		}
		/** @brief Uniform-block member uploads of the fragment stage */
		inline const SmallVector<GxmBlockUpload, 0>& GetFragmentBlockUploads() const {
			return fragmentBlockUploads_;
		}

		/**
			@brief Where one of a stage's samplers takes its texture from

			The two indices are not the same thing: `EngineUnit` is the texture unit the pipeline binds a
			texture to (the reflection's assignment, which the material sets), while `TextureIndex` is the
			sceGxm texture slot the compiled shader reads it from.
		*/
		struct GxmSamplerSlot
		{
			const char* Name;
			std::uint32_t EngineUnit;
			std::uint32_t TextureIndex;
		};

		/** @brief Sampler slots of the vertex stage (rare, but sceGxm allows vertex texturing) */
		inline const SmallVector<GxmSamplerSlot, 0>& GetVertexSamplerSlots() const {
			return vertexSamplerSlots_;
		}
		/** @brief Sampler slots of the fragment stage */
		inline const SmallVector<GxmSamplerSlot, 0>& GetFragmentSamplerSlots() const {
			return fragmentSamplerSlots_;
		}

		/** @brief Returns `true` if the vertex stage reads any vertex attribute */
		inline bool HasVertexAttributes() const {
			return !stageAttributes_.empty();
		}
		/** @brief Returns the vertex attributes the compiled stage declares (see @ref GxmStageAttribute) */
		inline const SmallVector<GxmStageAttribute, 4>& GetStageAttributes() const {
			return stageAttributes_;
		}
		/** @brief Returns the vertex buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const GxmBufferObject* GetBoundVbo() const {
			return boundVbo_;
		}
		/** @brief Returns the index buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const GxmBufferObject* GetBoundIbo() const {
			return boundIbo_;
		}
		/** @brief Returns the byte offset into the bound vertex buffer the attributes start at */
		inline std::uint32_t GetVboOffset() const {
			return vboOffset_;
		}
		/** @brief Returns `true` if the vertex layout takes its corner (and instance index) from a device static stream */
		inline bool UsesStaticCornerStream() const {
			return usesCornerStream_;
		}
		/** @brief Returns `true` if the static stream is the batched one (six vertices per sprite, with an instance index) */
		inline bool UsesBatchedCornerStream() const {
			return usesBatchedStream_;
		}
		/** @brief Returns `true` if the vertex layout reads any attribute out of the pipeline's vertex buffer */
		inline bool UsesGeometryStream() const {
			return usesGeometryStream_;
		}
		/** @brief Returns the sceGxm stream index the static corner stream is bound to (0 when there is no geometry stream) */
		inline std::uint32_t GetStaticStreamIndex() const {
			return (usesGeometryStream_ ? 1u : 0u);
		}
		/** @brief Returns the per-vertex byte stride of the geometry stream */
		inline std::uint32_t GetGeometryStride() const {
			return geometryStride_;
		}

		/** @brief Packs a blend configuration into the key @ref GetFragmentProgram() caches on */
		static std::uint32_t PackBlendKey(bool enabled, nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb,
			nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha);

		/**
			@brief Resolves a vertex attribute of a compiled stage by the name the engine knows it under

			The reflected name of an attribute the entry point receives through an input struct is not
			guaranteed to be the bare member name - a qualified `<struct>.<member>` form is equally plausible
			and only the compiler decides - so an exact match is tried first and a match on the last
            dot-separated component second. Returns `nullptr` and logs the stage's whole attribute list when
			neither resolves, because a silently dropped attribute renders nothing and explains nothing.
		*/
		static const SceGxmProgramParameter* FindAttribute(const SceGxmProgram* program, const char* name);

		/**
			@brief Resolves where a named block member lives in the compiled stage

			Handles all three shapes a member can take: a uniform in the default buffer, a uniform buffer
			container (the batched instance array), and an array of structs named only through its leaves.
			@returns `false` if the stage has no uniform belonging to that name at all
		*/
		static bool FindUniformBase(const SceGxmProgram* program, const char* name,
			GxmBlockUpload::Destination& destination, std::uint32_t& index);

		/**
			@brief Routes SceShaccCg's diagnostics into the engine log

			Called once before anything is compiled - including the device's own built-in shaders, which
			otherwise fail with no explanation but the caller's own message.
		*/
		static void InstallCompilerLogCallback();

		/**
			@brief Compiles one Cg stage on the console and returns a GXP binary the caller owns

			This exists to contain a trap. `shark_compile_shader()` hands back a pointer *into* SceShaccCg's
			compile output (`SceShaccCgCompileOutput::programData`), not a copy of it, and
			`shark_clear_output()` releases that output through `sceShaccCgDestroyCompileOutput()` - so
			clearing the output, as any tidy caller would, leaves the returned `SceGxmProgram*` dangling. The
			symptom is not a crash at the free: the program reads back with **zero reflected parameters** and
			faults later inside the shader patcher, which points at everything except the real cause. The
			bytes are therefore copied out before the output is cleared, which is also what sceGxm requires -
			a registered program's memory has to stay alive for as long as it is registered.

			@param source        Cg source, null-terminated
			@param vertexStage   `true` for a vertex stage, `false` for a fragment one
			@param sizeInBytes   Receives the size of the returned binary
			@returns A GXP binary to release with `std::free()`, or `nullptr` if the compile failed
		*/
		static SceGxmProgram* CompileCgStage(const char* source, bool vertexStage, std::uint32_t& sizeInBytes);

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

		SmallVector<GxmUniform, 0> uniforms_;
		SmallVector<GxmUniformBlock, 0> uniformBlocks_;
		SmallVector<GxmAttribute, 0> attributes_;
		// What the compiled vertex stage declares, which is not the same set the reflection lists
		SmallVector<GxmStageAttribute, 4> stageAttributes_;

		const ShaderCompiler::ProgramVariant* reflection_;
		const char* programName_;
		const char* variantName_;

		GxmVertexFormat vertexFormat_;
		const GxmBufferObject* boundVbo_;
		const GxmBufferObject* boundIbo_;
		std::uint32_t vboOffset_;
		bool usesCornerStream_;
		bool usesBatchedStream_;
		bool usesGeometryStream_;
		std::uint32_t geometryStride_;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		SmallVector<ResolvedUniform, 0> resolvedUniforms_;

		// Compiled stages (owned: allocated by vitaShaRK, released with free()) and their patcher registrations
		SceGxmProgram* vertexStage_;
		SceGxmProgram* fragmentStage_;
		SceGxmShaderPatcherId vertexStageId_;
		SceGxmShaderPatcherId fragmentStageId_;

		std::uint32_t vertexUniformBufferSize_;
		std::uint32_t fragmentUniformBufferSize_;
		SmallVector<GxmUniformSlot, 0> vertexUniformSlots_;
		SmallVector<GxmUniformSlot, 0> fragmentUniformSlots_;
		SmallVector<GxmBlockUpload, 0> vertexBlockUploads_;
		SmallVector<GxmBlockUpload, 0> fragmentBlockUploads_;
		SmallVector<GxmSamplerSlot, 0> vertexSamplerSlots_;
		SmallVector<GxmSamplerSlot, 0> fragmentSamplerSlots_;

		// Patched programs, created on demand (see the class documentation)
		struct CachedVertexProgram
		{
			std::uint64_t Fingerprint;
			SceGxmVertexProgram* Program;
		};
		SmallVector<CachedVertexProgram, 2> vertexPrograms_;
		struct CachedFragmentProgram
		{
			std::uint32_t BlendKey;
			SceGxmFragmentProgram* Program;
		};
		SmallVector<CachedFragmentProgram, 4> fragmentPrograms_;

		void PerformIntrospection();
		/** @brief Fills @ref stageAttributes_ from the compiled vertex stage's attribute parameters */
		void ReflectStageAttributes();
		void ImportReflection();
		/** @brief Compiles the generated Cg sources of @ref programName_ / @ref variantName_ and registers them */
		bool CompileStages();
		/** @brief Resolves one stage's parameters into the uniform, block and sampler slot tables */
		void ReflectStage(const SceGxmProgram* program, SmallVector<GxmUniformSlot, 0>& uniformSlots,
			SmallVector<GxmBlockUpload, 0>& blockUploads, SmallVector<GxmSamplerSlot, 0>& samplerSlots);
		/** @brief Releases the patched programs and the compiled stages */
		void ReleaseGpu();
	};
}
