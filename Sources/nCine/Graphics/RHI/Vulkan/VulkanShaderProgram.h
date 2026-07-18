#pragma once

#include "VulkanShaderTypes.h"
#include "VulkanVertexFormat.h"
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

namespace nCine::RhiVulkan
{
	class VulkanBufferObject;
	class VulkanShaderUniforms;
	class VulkanShaderUniformBlocks;

	/**
		@brief Shader program of the Vulkan backend (aliased as `Rhi::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes so the pipeline's uniform machinery runs.
		It also builds the `VkShaderModule` pair from the reflection's embedded `VkVsSpirv`/`VkFsSpirv`, a
		`VkDescriptorSetLayout` from the same reflection (set 0: optional `_Globals` UBO, then the std140 blocks,
		then the combined-image-samplers, in reflection order) and a `VkPipeline` per vertex format. @ref Use()
		records the program as current on the device.
	*/
	class VulkanShaderProgram
	{
		friend class VulkanShaderUniforms;
		friend class VulkanShaderUniformBlocks;

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

		VulkanShaderProgram();
		explicit VulkanShaderProgram(QueryPhase queryPhase);
		VulkanShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		VulkanShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		VulkanShaderProgram(StringView vertexFile, StringView fragmentFile);
		~VulkanShaderProgram();

		VulkanShaderProgram(const VulkanShaderProgram&) = delete;
		VulkanShaderProgram& operator=(const VulkanShaderProgram&) = delete;

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
		VulkanVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const VulkanBufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const VulkanBufferObject* vbo, const VulkanBufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const VulkanBufferObject* vbo, const VulkanBufferObject* ibo, std::uint32_t vboOffset);

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

		/** @brief Returns `true` if the vertex shader reads vertex attributes (needs vertex input state + a vertex buffer) */
		inline bool HasVertexAttributes() const {
			return !attributes_.empty();
		}
		/** @brief Returns the vertex buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const VulkanBufferObject* GetBoundVbo() const {
			return boundVbo_;
		}
		/** @brief Returns the index buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const VulkanBufferObject* GetBoundIbo() const {
			return boundIbo_;
		}

		// -- SPIR-V modules, descriptor-set layout and the reflection metadata the device uses to
		// build pipelines + descriptor sets. Vulkan handles are opaque integers so this contract header stays
		// free of <vulkan/vulkan.h>; the .cpp reinterprets them. --

		/** @brief One entry of the descriptor set (set 0), matching the offline emitter's binding scheme */
		struct DescriptorBinding
		{
			enum class Kind { Globals, Block, Sampler };
			Kind kind;
			std::uint32_t binding;		// the layout(binding=) number in the SPIR-V
			std::int32_t slot;			// Block: reflection block index (device uniform-range slot); Sampler: texture unit
		};

		/** @brief A loose (default-block) uniform gathered into the "_Globals" UBO, with its std140 placement */
		struct LooseUniform
		{
			String Name;
			std::uint32_t Offset;		// std140 byte offset within _Globals
			std::uint32_t Size;			// byte size of the value
		};

		/** @brief Returns `true` if the SPIR-V modules and layouts were built successfully (draws are skipped otherwise) */
		inline bool HasPipelineState() const {
			return (vsModule_ != 0 && fsModule_ != 0 && pipelineLayout_ != 0);
		}
		/** @brief The vertex-stage `VkShaderModule` (as `std::uint64_t`) */
		inline std::uint64_t GetVsModule() const { return vsModule_; }
		/** @brief The fragment-stage `VkShaderModule` */
		inline std::uint64_t GetFsModule() const { return fsModule_; }
		/** @brief The `VkPipelineLayout` (one descriptor set, set 0) */
		inline std::uint64_t GetPipelineLayout() const { return pipelineLayout_; }
		/** @brief The `VkDescriptorSetLayout` for set 0 */
		inline std::uint64_t GetDescriptorSetLayout() const { return descriptorSetLayout_; }
		/** @brief The descriptor bindings (in reflection order) the device writes each draw */
		inline const std::vector<DescriptorBinding>& GetDescriptorBindings() const { return descriptorBindings_; }
		/** @brief The loose uniforms gathered into "_Globals" (empty if the program has none) */
		inline const std::vector<LooseUniform>& GetLooseUniforms() const { return looseUniforms_; }
		/** @brief std140 byte size of the "_Globals" UBO (0 if the program has no loose uniforms) */
		inline std::uint32_t GetGlobalsSize() const { return globalsSize_; }
		/** @brief The binding number of the "_Globals" UBO (always 0 when present) */
		inline bool HasGlobals() const { return globalsSize_ > 0; }

		/** @brief A vertex attribute for building the pipeline's vertex input state */
		struct VertexAttrib
		{
			std::uint32_t Location;
			std::int32_t ComponentCount;
			std::uint32_t Offset;		// byte offset within the vertex
		};
		/**
		 * @brief Fills the current vertex input layout (attributes + per-vertex stride) from the bound vertex format
		 *
		 * Writes at most @p maxAttribs entries into @p outAttribs (a caller-provided array, so the per-draw
		 * pipeline-key computation never touches the heap) and returns the number written.
		 */
		std::uint32_t GetVertexInput(VertexAttrib* outAttribs, std::uint32_t maxAttribs, std::uint32_t& outStride) const;

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

		std::vector<VulkanUniform> uniforms_;
		std::vector<VulkanUniformBlock> uniformBlocks_;
		std::vector<VulkanAttribute> attributes_;

		const ShaderCompiler::ProgramVariant* reflection_;

		VulkanVertexFormat vertexFormat_;
		const VulkanBufferObject* boundVbo_;
		const VulkanBufferObject* boundIbo_;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> resolvedUniforms_;

		// GPU objects (opaque integers; see the getters above)
		std::uint64_t vsModule_;
		std::uint64_t fsModule_;
		std::uint64_t descriptorSetLayout_;
		std::uint64_t pipelineLayout_;
		std::vector<DescriptorBinding> descriptorBindings_;
		std::vector<LooseUniform> looseUniforms_;
		std::uint32_t globalsSize_;

		void PerformIntrospection();
		void ImportReflection();
		/** @brief Builds the SPIR-V modules, descriptor-set layout, pipeline layout and reflection metadata (during introspection, while the reflection is still available) */
		void BuildPipelineState();
	};
}
