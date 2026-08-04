#pragma once

#include "D3D11ShaderTypes.h"
#include "D3D11VertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

// Direct3D 11 interfaces referenced only as opaque pointers here (definitions pulled in by the .cpp)
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::D3D11
{
	class D3D11ShaderUniforms;
	class D3D11ShaderUniformBlocks;

	/**
		@brief Describes one constant buffer slot a shader stage expects, precomputed from bytecode reflection

		At draw time the device rebuilds each slot's bytes into a dynamic `ID3D11Buffer` and binds it at
		@ref Register. A `_Globals` slot (loose uniforms) is gathered member-by-member from the program's
		resolved loose-uniform values; a uniform-block slot is copied verbatim from the std140 range the
		pipeline bound (the HLSL emitter laid the cbuffer out to match std140, so no repacking is needed).
	*/
	struct D3D11CBufferSlot
	{
		struct GlobalVar
		{
			std::string Name;			// loose-uniform name to resolve
			std::uint32_t Offset;		// byte offset within the cbuffer
			std::uint32_t Size;			// byte size of the value
		};

		std::uint32_t Register = 0;		// cbuffer register (bN)
		std::uint32_t ByteSize = 0;		// full cbuffer size (16-aligned), as declared in the bytecode
		bool IsGlobals = false;			// true = built from loose uniforms; false = a uniform block
		std::int32_t BlockIndex = -1;	// if a block: index into the device's bound-uniform-range table
		// If IsGlobals: the members to gather. Heap-only (no inline storage) because the slot itself lives
		// inside the per-stage slot lists below, where inline elements would bloat every entry.
		SmallVector<GlobalVar, 0> Globals;
	};

	/**
		@brief Constant-buffer slots one shader stage expects

		A stage binds one or two cbuffers in practice (the gathered `_Globals` plus at most one uniform
		block), so the inline capacity covers the usual case without a heap allocation, and the draw path
		walks the slots inline.
	*/
	using CBufferSlotList = SmallVector<D3D11CBufferSlot, 2>;

	/** @brief Compiled DXBC bytecode of one shader stage (kilobytes — always heap-allocated) */
	using ShaderByteCode = SmallVector<std::uint8_t, 0>;

	/**
		@brief Shader program of the Direct3D 11 backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes, and creates real
		`ID3D11VertexShader`/`ID3D11PixelShader` objects from the reflection's precompiled DXBC bytecode
		(`HlslVsDxbc`/`HlslFsDxbc`, the normal case) — or, when only HLSL text is embedded, by runtime-compiling
		`HlslVsSource`/`HlslFsSource` (matrix packing forced column-major so the emitter's `mul(M,v)`
		column-vector algebra matches the engine's column-major uniform data verbatim; both paths use the same
		contract). It then reflects the constant buffers into @ref D3D11CBufferSlot lists the device rebinds
		each draw, and keeps the VS bytecode for building the input layout of attribute-based (mesh/tilemap)
		shaders. @ref Use() records the program as current on the device.
	*/
	class D3D11ShaderProgram
	{
		friend class D3D11ShaderUniforms;
		friend class D3D11ShaderUniformBlocks;

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

		D3D11ShaderProgram();
		explicit D3D11ShaderProgram(QueryPhase queryPhase);
		D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile);
		~D3D11ShaderProgram();

		D3D11ShaderProgram(const D3D11ShaderProgram&) = delete;
		D3D11ShaderProgram& operator=(const D3D11ShaderProgram&) = delete;

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

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes and compile HLSL */
		inline void SetReflection(const ShaderCompiler::ProgramVariant* reflection) {
			_reflection = reflection;
		}
		/**
			@brief Records the true (program, variant) identity of the loaded shader

			Only the fixed-function console backends consume it (they resolve their generated
			effect tables from this identity instead of the object label); this backend runs
			real shaders and ignores it.
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
		D3D11VertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const D3D11BufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const D3D11BufferObject* vbo, const D3D11BufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const D3D11BufferObject* vbo, const D3D11BufferObject* ibo, std::uint32_t vboOffset);

		void Reset();
		void SetObjectLabel(StringView label);

		inline bool GetLogOnErrors() const {
			return _shouldLogOnErrors;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			_shouldLogOnErrors = shouldLogOnErrors;
		}

		// -- Backend extensions (used by the uniform caches and the device draw path) --

		/** @brief Publishes a committed loose-uniform value pointer for the device to gather into `_Globals` */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

		/** @brief Returns the compiled vertex shader, or `nullptr` if not compiled (e.g. runtime shader with no HLSL) */
		inline ID3D11VertexShader* GetVertexShader() const {
			return _vertexShader;
		}
		/** @brief Returns the compiled pixel shader, or `nullptr` */
		inline ID3D11PixelShader* GetPixelShader() const {
			return _pixelShader;
		}
		/** @brief Returns/creates the input layout for this program's attributes + bound vertex format; `nullptr` for `SV_VertexID` shaders */
		ID3D11InputLayout* GetInputLayout();
		/** @brief Returns `true` if the vertex shader reads vertex attributes (needs an input layout + vertex buffer) */
		inline bool HasVertexAttributes() const {
			return !_attributes.empty();
		}
		/** @brief Returns the vertex buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const D3D11BufferObject* GetBoundVbo() const {
			return _boundVbo;
		}
		/** @brief Returns the index buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const D3D11BufferObject* GetBoundIbo() const {
			return _boundIbo;
		}
		/** @brief Returns the per-vertex byte stride (from the first enabled attribute of the bound vertex format) */
		std::uint32_t GetVertexStride() const;

		/** @brief Constant-buffer slots the vertex stage expects (rebuilt and bound each draw) */
		inline const CBufferSlotList& GetVsCBuffers() const {
			return _vsCBuffers;
		}
		/** @brief Constant-buffer slots the pixel stage expects */
		inline const CBufferSlotList& GetPsCBuffers() const {
			return _psCBuffers;
		}
		/** @brief Bitmask of the texture/sampler registers the vertex stage actually reads (bit N = slot tN/sN) */
		inline std::uint32_t GetVsTextureMask() const {
			return _vsTextureMask;
		}
		/** @brief Bitmask of the texture/sampler registers the pixel stage actually reads */
		inline std::uint32_t GetPsTextureMask() const {
			return _psTextureMask;
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

		SmallVector<D3D11Uniform, 0> _uniforms;
		SmallVector<D3D11UniformBlock, 0> _uniformBlocks;
		SmallVector<D3D11Attribute, 0> _attributes;

		const ShaderCompiler::ProgramVariant* _reflection;

		D3D11VertexFormat _vertexFormat;
		const D3D11BufferObject* _boundVbo;
		const D3D11BufferObject* _boundIbo;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		SmallVector<ResolvedUniform, 0> _resolvedUniforms;

		// Compiled Direct3D 11 objects (owned)
		ID3D11VertexShader* _vertexShader;
		ID3D11PixelShader* _pixelShader;
		ID3D11InputLayout* _inputLayout;
		std::uint64_t _inputLayoutFingerprint;		// vertex-format fingerprint the cached input layout was built for
		ShaderByteCode _vsByteCode;					// kept for building input layouts
		CBufferSlotList _vsCBuffers;
		CBufferSlotList _psCBuffers;
		std::uint32_t _vsTextureMask = 0;			// texture/sampler registers the stage binds (from bytecode reflection)
		std::uint32_t _psTextureMask = 0;

		void PerformIntrospection();
		void ImportReflection();
		/** @brief Compiles `_reflection`'s HLSL stage sources and reflects their constant buffers (called during introspection) */
		void CompileHlsl();
		/** @brief Reflects the constant buffers and used texture/sampler registers of one compiled stage bytecode */
		void ReflectStageCBuffers(const void* byteCode, std::size_t byteCodeSize, CBufferSlotList& slots, std::uint32_t& textureMask);
	};
}
