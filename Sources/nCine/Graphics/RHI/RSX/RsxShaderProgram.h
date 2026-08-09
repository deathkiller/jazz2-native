#pragma once

#include "RsxShaderTypes.h"
#include "RsxVertexFormat.h"
#include "RsxVram.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <rsx/rsx_program.h>

using namespace Death::Containers;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::RSX
{
	class RsxShaderUniforms;
	class RsxShaderUniformBlocks;
	class RsxBufferObject;

	/**
		@brief One reflected RSX program constant the draw path writes a uniform into

		Resolving the constants once at link time keeps `rsxVertexProgramGetConst()` - a linear scan over the
		program's name table, with a `strcmp` per entry - off the per-draw path.

		`Param` is the microcode's own descriptor, which the `rsxSet*ProgramParameter()` commands take
		directly; it points into the embedded program blob and so outlives this program. `ByteSize` comes
		from the engine-side reflection rather than from the shader, because it is the size of the value the
		pipeline will hand over, which is what bounds the copy.
	*/
	struct RsxUniformSlot
	{
		const char* Name;					// the reflection's own string, which outlives the program
		const rsxProgramConst* Param;		// the microcode's descriptor for this constant
		std::uint32_t ByteSize;				// bytes the engine-side reflection declares for the value
	};

	/**
		@brief One vertex attribute the compiled stage declares, as the shader itself reports it

		The offline reflection cannot supply this list. It describes the *modern GLSL* source, where the
		sprite shaders take no vertex input at all and synthesize the quad corner from `gl_VertexID` - while
		the Cg lowering has no vertex-ID input and reads `aQuadCorner` / `aInstanceIndex` instead, attributes
		that @ref ShaderCompiler::VertexIdRewrite invents and the reflection has never heard of. Asking the
		compiled program is therefore the only way to know a sprite shader has vertex inputs at all.

		`Name` points into the compiled program's own string table, which lives in the executable's rodata.
	*/
	struct RsxStageAttribute
	{
		const char* Name;
		std::uint8_t RegIndex;			// the attribute register `rsxBindVertexArrayAttrib()` takes
	};

	/**
		@brief Shader program of the RSX backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes, and gets its two stage *binaries* from
		the identity @ref SetProgramIdentity() plumbs in - the generated `RsxGeneratedShaders.h` table.

		**Everything about this class follows from there being no shader compiler on the console.** The Vita
		backend this otherwise mirrors ships Cg source and compiles it at link time through SceShaccCg; the
		PS3 has no such thing, so the same Cg is compiled to NV40 microcode offline by cgcomp and embedded as
		a `rsxVertexProgram` / `rsxFragmentProgram` blob. Linking is therefore not a compile at all - it
		looks the pair up, copies the fragment half where the hardware can fetch it, and resolves names.

		A consequence worth stating plainly: a shader whose Cg exceeded what the `vp40`/`fp40` profiles can
		express has no entry in that table, and @ref Link() fails for it at load time with a message naming
		the variant. That is a **build-time** fact rather than a runtime one - it cannot change on the
		console - so it is reported once and the program stays unlinked rather than being retried.

		There is also far less caching here than the sceGxm backend needs. sceGxm bakes the vertex layout
		into a patched vertex program and the blend state into a patched fragment program, so it keeps a
		cache of each; the RSX takes both as ordinary state - attributes through
		`rsxBindVertexArrayAttrib()` at draw time, blending through `rsxSetBlendFunc()` - so one program
		object serves every layout and every blend configuration.

		**Fragment constants are the one genuinely awkward part.** The RSX patches a fragment program's
		constants *into its microcode* rather than into a register file, which is why
		`rsxSetFragmentProgramParameter()` needs the microcode's address and location, and why each program
		owns a private copy of its fragment microcode in local memory (@ref _fragmentUcode) rather than
		pointing at the shared blob in the executable. Two programs sharing one blob would overwrite each
		other's constants.
	*/
	class RsxShaderProgram
	{
		friend class RsxShaderUniforms;
		friend class RsxShaderUniformBlocks;

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

		RsxShaderProgram();
		explicit RsxShaderProgram(QueryPhase queryPhase);
		RsxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		RsxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		RsxShaderProgram(StringView vertexFile, StringView fragmentFile);
		~RsxShaderProgram();

		RsxShaderProgram(const RsxShaderProgram&) = delete;
		RsxShaderProgram& operator=(const RsxShaderProgram&) = delete;

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

			This backend resolves its generated microcode from this identity, the same way the fixed-function
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
		RsxVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const RsxBufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const RsxBufferObject* vbo, const RsxBufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const RsxBufferObject* vbo, const RsxBufferObject* ibo, std::uint32_t vboOffset);

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

		/** @brief Returns the embedded vertex microcode descriptor, or `nullptr` if the program did not link */
		inline const rsxVertexProgram* GetVertexProgram() const {
			return _vertexProgram;
		}
		/** @brief Returns the embedded fragment microcode descriptor, or `nullptr` */
		inline const rsxFragmentProgram* GetFragmentProgram() const {
			return _fragmentProgram;
		}
		/** @brief Returns the vertex microcode the device hands to `rsxLoadVertexProgram()` */
		inline const void* GetVertexUcode() const {
			return _vertexUcode;
		}
		/** @brief Returns this program's private copy of the fragment microcode in local memory */
		inline const RsxVram::Block& GetFragmentUcodeBlock() const {
			return _fragmentUcode;
		}

		/** @brief Loose-uniform slots of the vertex stage (resolved once at link time) */
		inline const SmallVector<RsxUniformSlot, 0>& GetVertexUniformSlots() const {
			return _vertexUniformSlots;
		}
		/** @brief Loose-uniform slots of the fragment stage */
		inline const SmallVector<RsxUniformSlot, 0>& GetFragmentUniformSlots() const {
			return _fragmentUniformSlots;
		}

		/**
			@brief Where one uniform block's bytes go among a stage's constants

			The Cg dialect has no counterpart to a std140 block - its emitter hoists each block member to a
			top-level `uniform` - so a block is uploaded member by member, exactly as on the Vita.
		*/
		struct RsxBlockUpload
		{
			const rsxProgramConst* Param;	// the microcode descriptor of the member's constant
			std::uint32_t BindingIndex;		// uniform binding point the pipeline bound the range to
			std::uint32_t SourceOffset;		// byte offset of the member within that range
			std::uint32_t MaxByteSize;		// bytes the engine's block layout declares (an upper bound on a copy)
			const char* Name;				// the reflection's member name, for diagnostics
		};

		/**
			@brief One field of a batched instance element, and where it sits on each side

			A batched instance array cannot go through @ref RsxBlockUpload, because the two sides disagree
			about the element layout. The engine writes std140, which packs a trailing `vec2` and `float`
			into one 16-byte slot; cgcomp gives every field its own constant register. For the sprite
			element that is 7 vec4 on the engine's side and 8 registers on the shader's - so the element
			cannot be handed over as one block, and its 5 fields cannot be handed over one register write
			each either without costing 5 writes per sprite.

			Instead the draw path repacks: it gathers an element's fields out of the std140 range into a
			scratch buffer laid out the way the registers are, and issues **one**
			`rsxSetVertexProgramConstants()` per element. This structure is what that repacking is driven by.
		*/
		struct RsxInstanceField
		{
			std::uint32_t SourceOffset;		// byte offset of the field within one std140 element
			std::uint32_t RegisterOffset;	// register offset of the field within one compiled element
			std::uint32_t RegisterCount;	// registers the field occupies (4 for a mat4, 1 otherwise)
			std::uint32_t ByteSize;			// bytes to copy out of the std140 element
			const char* Name;				// for diagnostics
		};

		/**
			@brief The batched instance array of a stage, if it has one

			Derived at link time from the compiled microcode's own constant table - the offline reflection
			describes the array only as one opaque `Struct` member, so the field layout is recovered from the
			`instances[0].<field>` constants cgcomp emitted and their declared types (see
			@ref RsxShaderProgram::ResolveInstanceArray()).
		*/
		struct RsxInstanceArray
		{
			bool Valid = false;
			std::uint32_t BindingIndex = 0;		// uniform binding point the pipeline bound the range to
			std::uint32_t BaseRegister = 0;		// register of element 0's first field
			std::uint32_t RegistersPerElement = 0;
			std::uint32_t SourceStride = 0;		// std140 element stride the pipeline writes
			std::uint32_t ElementCount = 0;
			SmallVector<RsxInstanceField, 0> Fields;
		};

		/** @brief The vertex stage's batched instance array (never present on the fragment stage) */
		inline const RsxInstanceArray& GetInstanceArray() const {
			return _instanceArray;
		}

		/** @brief Uniform-block member uploads of the vertex stage */
		inline const SmallVector<RsxBlockUpload, 0>& GetVertexBlockUploads() const {
			return _vertexBlockUploads;
		}
		/** @brief Uniform-block member uploads of the fragment stage */
		inline const SmallVector<RsxBlockUpload, 0>& GetFragmentBlockUploads() const {
			return _fragmentBlockUploads;
		}

		/**
			@brief Where one of a stage's samplers takes its texture from

			The two indices are not the same thing: `EngineUnit` is the texture unit the pipeline binds a
			texture to, `HardwareUnit` is the RSX texture unit the compiled shader samples. The Cg emitter
			tags each sampler with a `TEXUNIT<n>` semantic taken from the offline reflection, so the two
			normally agree - but they are resolved separately so a shader that renames or reorders its
			samplers cannot silently sample the wrong one.
		*/
		struct RsxSamplerBinding
		{
			const char* Name;
			std::uint32_t EngineUnit;
			std::uint32_t HardwareUnit;
		};

		/** @brief Samplers the fragment stage declares (the vertex stage never samples in this engine) */
		inline const SmallVector<RsxSamplerBinding, 0>& GetFragmentSamplers() const {
			return _fragmentSamplers;
		}

		/** @brief Vertex attributes the compiled stage declares (see @ref RsxStageAttribute) */
		inline const SmallVector<RsxStageAttribute, 0>& GetStageAttributes() const {
			return _stageAttributes;
		}
		/** @brief Returns the attribute register of the named compiled attribute, or -1 */
		std::int32_t GetStageAttributeRegister(const char* name) const;

		/** @brief Returns the vertex format the pipeline last defined on this program */
		inline RsxVertexFormat& GetVertexFormat() {
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

		// The embedded microcode of the resolved (program, variant). Both point into the executable's
		// rodata, so neither is owned; only the fragment half is copied, into _fragmentUcode below.
		const rsxVertexProgram* _vertexProgram;
		const rsxFragmentProgram* _fragmentProgram;
		const void* _vertexUcode;
		/** @brief This program's private copy of the fragment microcode (see the class documentation) */
		RsxVram::Block _fragmentUcode;

		SmallVector<RsxUniform, 0> _uniforms;
		SmallVector<RsxUniformBlock, 0> _uniformBlocks;
		SmallVector<RsxAttribute, 0> _attributes;
		std::uint32_t _uniformsSize;
		std::uint32_t _uniformBlocksSize;

		SmallVector<RsxUniformSlot, 0> _vertexUniformSlots;
		SmallVector<RsxUniformSlot, 0> _fragmentUniformSlots;
		SmallVector<RsxBlockUpload, 0> _vertexBlockUploads;
		SmallVector<RsxBlockUpload, 0> _fragmentBlockUploads;
		RsxInstanceArray _instanceArray;
		SmallVector<RsxSamplerBinding, 0> _fragmentSamplers;
		SmallVector<RsxStageAttribute, 0> _stageAttributes;

		/** @brief Committed loose-uniform value pointers, published by the uniform caches */
		SmallVector<std::pair<const char*, const std::uint8_t*>, 0> _resolvedUniforms;

		RsxVertexFormat _vertexFormat;

		/** @brief Looks the embedded microcode pair up from the program identity; `false` if there is none */
		bool ResolveGeneratedProgram();
		/** @brief Copies the fragment microcode into local memory, where the fragment engine can fetch it */
		bool UploadFragmentUcode();
		/** @brief Imports uniforms, blocks and attributes from the offline reflection */
		void ImportReflection(Introspection introspection);
		/** @brief Resolves every imported uniform and block member against the microcode's name tables */
		void ResolveConstants();
		/**
			@brief Recovers a batched instance array's field layout from the compiled microcode

			@param blockIndex   Binding point the pipeline bound the block's range to
			@param memberName   Name of the array member in the shader (e.g. "instances")
			@param sourceStride std140 element stride the engine writes
			@param elementCount Elements the batch covers
		*/
		void ResolveInstanceArray(std::uint32_t blockIndex, const char* memberName,
			std::uint32_t sourceStride, std::uint32_t elementCount);
		/** @brief Records the attributes and samplers the compiled stages declare */
		void ResolveStageInterface();
	};
}
