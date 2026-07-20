#pragma once

#include "D3D11ShaderTypes.h"
#include "D3D11VertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#include <Containers/ArrayView.h>
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
		std::vector<GlobalVar> Globals;	// if IsGlobals: the members to gather
	};

	/**
		@brief Shader program of the Direct3D 11 backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes, and compiles
		the reflection's `HlslVsSource`/`HlslFsSource` into real `ID3D11VertexShader`/`ID3D11PixelShader`
		objects (matrix packing forced column-major so the emitter's `mul(M,v)` column-vector algebra matches
		the engine's column-major uniform data verbatim), reflects their constant buffers into @ref
		D3D11CBufferSlot lists the device rebinds each draw, and keeps the VS bytecode for building the input
		layout of attribute-based (mesh/tilemap) shaders. @ref Use() records the program as current on the device.
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

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes and compile HLSL */
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
			return shouldLogOnErrors_;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

		// -- Backend extensions (used by the uniform caches and the device draw path) --

		/** @brief Publishes a committed loose-uniform value pointer for the device to gather into `_Globals` */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

		/** @brief Returns the compiled vertex shader, or `nullptr` if not compiled (e.g. runtime shader with no HLSL) */
		inline ID3D11VertexShader* GetVertexShader() const {
			return vertexShader_;
		}
		/** @brief Returns the compiled pixel shader, or `nullptr` */
		inline ID3D11PixelShader* GetPixelShader() const {
			return pixelShader_;
		}
		/** @brief Returns/creates the input layout for this program's attributes + bound vertex format; `nullptr` for `SV_VertexID` shaders */
		ID3D11InputLayout* GetInputLayout();
		/** @brief Returns `true` if the vertex shader reads vertex attributes (needs an input layout + vertex buffer) */
		inline bool HasVertexAttributes() const {
			return !attributes_.empty();
		}
		/** @brief Returns the vertex buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const D3D11BufferObject* GetBoundVbo() const {
			return boundVbo_;
		}
		/** @brief Returns the index buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const D3D11BufferObject* GetBoundIbo() const {
			return boundIbo_;
		}
		/** @brief Returns the per-vertex byte stride (from the first enabled attribute of the bound vertex format) */
		std::uint32_t GetVertexStride() const;

		/** @brief Constant-buffer slots the vertex stage expects (rebuilt and bound each draw) */
		inline const std::vector<D3D11CBufferSlot>& GetVsCBuffers() const {
			return vsCBuffers_;
		}
		/** @brief Constant-buffer slots the pixel stage expects */
		inline const std::vector<D3D11CBufferSlot>& GetPsCBuffers() const {
			return psCBuffers_;
		}
		/** @brief Bitmask of the texture/sampler registers the vertex stage actually reads (bit N = slot tN/sN) */
		inline std::uint32_t GetVsTextureMask() const {
			return vsTextureMask_;
		}
		/** @brief Bitmask of the texture/sampler registers the pixel stage actually reads */
		inline std::uint32_t GetPsTextureMask() const {
			return psTextureMask_;
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

		std::vector<D3D11Uniform> uniforms_;
		std::vector<D3D11UniformBlock> uniformBlocks_;
		std::vector<D3D11Attribute> attributes_;

		const ShaderCompiler::ProgramVariant* reflection_;

		D3D11VertexFormat vertexFormat_;
		const D3D11BufferObject* boundVbo_;
		const D3D11BufferObject* boundIbo_;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> resolvedUniforms_;

		// Compiled Direct3D 11 objects (owned)
		ID3D11VertexShader* vertexShader_;
		ID3D11PixelShader* pixelShader_;
		ID3D11InputLayout* inputLayout_;
		std::uint64_t inputLayoutFingerprint_;		// vertex-format fingerprint the cached input layout was built for
		std::vector<std::uint8_t> vsByteCode_;		// kept for building input layouts
		std::vector<D3D11CBufferSlot> vsCBuffers_;
		std::vector<D3D11CBufferSlot> psCBuffers_;
		std::uint32_t vsTextureMask_ = 0;			// texture/sampler registers the stage binds (from bytecode reflection)
		std::uint32_t psTextureMask_ = 0;

		void PerformIntrospection();
		void ImportReflection();
		/** @brief Compiles `reflection_`'s HLSL stage sources and reflects their constant buffers (called during introspection) */
		void CompileHlsl();
		/** @brief Reflects the constant buffers and used texture/sampler registers of one compiled stage bytecode */
		void ReflectStageCBuffers(const void* byteCode, std::size_t byteCodeSize, std::vector<D3D11CBufferSlot>& slots, std::uint32_t& textureMask);
	};
}
