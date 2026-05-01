#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "Uniform.h"
#include "UniformBlock.h"
#include "Attribute.h"
#include "VertexFormat.h"
#include "../../../Base/StaticHashMap.h"
#include "../../../../Main.h"

#include <string>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	class Shader;

	/// Handles OpenGL shader programs
	class ShaderProgram
	{
		friend class ShaderUniforms;
		friend class ShaderUniformBlocks;

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

		static constexpr std::int32_t DefaultBatchSize = -1;

		ShaderProgram();
		explicit ShaderProgram(QueryPhase queryPhase);
		ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		ShaderProgram(StringView vertexFile, StringView fragmentFile);
		~ShaderProgram();

		ShaderProgram(const ShaderProgram&) = delete;
		ShaderProgram& operator=(const ShaderProgram&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
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

		/// Returns the length of the information log including the null termination character
		std::uint32_t RetrieveInfoLogLength() const;
		/// Retrieves the information log and copies it in the provided string object
		void RetrieveInfoLog(std::string& infoLog) const;

		/// Returns the total memory needed for all uniforms outside of blocks
		inline std::uint32_t GetUniformsSize() const {
			return uniformsSize_;
		}
		/// Returns the total memory needed for all uniforms inside of blocks
		inline std::uint32_t GetUniformBlocksSize() const {
			return uniformBlocksSize_;
		}

		bool AttachShaderFromFile(GLenum type, StringView filename);
		bool AttachShaderFromString(GLenum type, StringView string);
		bool AttachShaderFromStrings(GLenum type, ArrayView<const StringView> strings);
		bool AttachShaderFromStringsAndFile(GLenum type, ArrayView<const StringView> strings, StringView filename);
		bool Link(Introspection introspection);
		void Use();
		bool Validate();

		bool FinalizeAfterLinking(Introspection introspection);

		inline std::uint32_t GetAttributeCount() const {
			return attributeLocations_.size();
		}
		inline bool HasAttribute(const char* name) const {
			return (attributeLocations_.find(String::nullTerminatedView(name)) != nullptr);
		}
		VertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const Buffer* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const Buffer* vbo, const Buffer* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const Buffer* vbo, const Buffer* ibo, std::uint32_t vboOffset);

		/// Deletes the current OpenGL shader program so that new shaders can be attached
		void Reset();

		void SetObjectLabel(StringView label);

		/// Returns the automatic log on errors flag
		inline bool GetLogOnErrors() const {
			return shouldLogOnErrors_;
		}
		/// Sets the automatic log on errors flag
		/*! If the flag is true the shader program will automatically log compilation and linking errors. */
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

	private:
		/// Max number of discoverable uniforms
		static constexpr std::uint32_t MaxNumUniforms = 32;

		static constexpr std::int32_t AttachedShadersInitialSize = 2;
		static constexpr std::int32_t UniformsInitialSize = 8;
		static constexpr std::int32_t UniformBlocksInitialSize = 4;
		static constexpr std::int32_t AttributesInitialSize = 4;

		static GLuint boundProgram_;

		GLuint glHandle_;

		SmallVector<std::unique_ptr<Shader>, AttachedShadersInitialSize> attachedShaders_;
		Status status_;
		Introspection introspection_;
		QueryPhase queryPhase_;
		std::uint32_t batchSize_;

		/// A flag indicating whether the shader program should automatically log errors (the information log)
		bool shouldLogOnErrors_;

		std::uint32_t uniformsSize_;
		std::uint32_t uniformBlocksSize_;

		SmallVector<Uniform, 0> uniforms_;
		SmallVector<UniformBlock, 0> uniformBlocks_;
		SmallVector<Attribute, 0> attributes_;

		StaticHashMap<String, std::int32_t, VertexFormat::MaxAttributes> attributeLocations_;
		VertexFormat vertexFormat_;

		bool ProcessDeferredQueries();
		bool CheckLinking();
		void PerformIntrospection();

		void DiscoverUniforms();
		void DiscoverUniformBlocks(UniformBlock::DiscoverUniforms discover);
		void DiscoverAttributes();
		void InitVertexFormat();
	};
}

#endif // defined(WITH_RHI_GL)
