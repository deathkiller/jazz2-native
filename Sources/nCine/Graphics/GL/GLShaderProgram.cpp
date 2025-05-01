#include "GLShaderProgram.h"
#include "GLShader.h"
#include "GLDebug.h"
#include "../BinaryShaderCache.h"
#include "../RenderResources.h"
#include "../RenderVaoPool.h"
#include "../IGfxCapabilities.h"
#include "../../ServiceLocator.h"
#include "../../Base/StaticHashMapIterator.h"
#include "../../tracy.h"

#include <string>

namespace nCine
{
	GLuint GLShaderProgram::boundProgram_ = 0;
#if defined(DEATH_TRACE)
	char GLShaderProgram::infoLogString_[MaxInfoLogLength];
#endif

	GLShaderProgram::GLShaderProgram()
		: GLShaderProgram(QueryPhase::Immediate)
	{
	}

	GLShaderProgram::GLShaderProgram(QueryPhase queryPhase)
		: glHandle_(0), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase), batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0)
	{
		glHandle_ = glCreateProgram();

		attachedShaders_.reserve(AttachedShadersInitialSize);
		uniforms_.reserve(UniformsInitialSize);
		uniformBlocks_.reserve(UniformBlocksInitialSize);
		attributes_.reserve(AttributesInitialSize);
		
		if (RenderResources::binaryShaderCache().IsAvailable()) {
			glProgramParameteri(glHandle_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		}
	}

	GLShaderProgram::GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GLShaderProgram(queryPhase)
	{
		const bool hasCompiledVS = AttachShaderFromFile(GL_VERTEX_SHADER, vertexFile);
		const bool hasCompiledFS = AttachShaderFromFile(GL_FRAGMENT_SHADER, fragmentFile);

		if (hasCompiledVS && hasCompiledFS) {
			Link(introspection);
		}
	}

	GLShaderProgram::GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: GLShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	GLShaderProgram::GLShaderProgram(StringView vertexFile, StringView fragmentFile)
		: GLShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	GLShaderProgram::~GLShaderProgram()
	{
		if (boundProgram_ == glHandle_) {
			glUseProgram(0);
		}

		glDeleteProgram(glHandle_);

		RenderResources::removeCameraUniformData(this);
	}

	bool GLShaderProgram::IsLinked() const
	{
		return (status_ == Status::Linked ||
				status_ == Status::LinkedWithDeferredQueries ||
				status_ == Status::LinkedWithIntrospection);
	}

	std::uint32_t GLShaderProgram::RetrieveInfoLogLength() const
	{
		GLint length = 0;
		glGetProgramiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
		return static_cast<std::uint32_t>(length);
	}

	void GLShaderProgram::RetrieveInfoLog(std::string& infoLog) const
	{
		GLint length = 0;
		glGetProgramiv(glHandle_, GL_INFO_LOG_LENGTH, &length);

		if (length > 0 && infoLog.capacity() > 0) {
			const std::uint32_t capacity = (std::uint32_t)infoLog.capacity();
			glGetProgramInfoLog(glHandle_, capacity, &length, infoLog.data());
			infoLog.resize(static_cast<std::uint32_t>(length) < capacity - 1 ? static_cast<std::uint32_t>(length) : capacity - 1);
		}
	}

	bool GLShaderProgram::AttachShaderFromFile(GLenum type, StringView filename)
	{
		return AttachShaderFromStringsAndFile(type, nullptr, filename);
	}
	
	bool GLShaderProgram::AttachShaderFromString(GLenum type, StringView string)
	{
		return AttachShaderFromStringsAndFile(type, arrayView({ string }), {});
	}
	
	bool GLShaderProgram::AttachShaderFromStrings(GLenum type, ArrayView<const StringView> strings)
	{
		return AttachShaderFromStringsAndFile(type, strings, {});
	}
	
	bool GLShaderProgram::AttachShaderFromStringsAndFile(GLenum type, ArrayView<const StringView> strings, StringView filename)
	{
		std::unique_ptr<GLShader> shader = std::make_unique<GLShader>(type);
		shader->LoadFromStringsAndFile(strings, filename);
		glAttachShader(glHandle_, shader->GetGLHandle());

		const GLShader::ErrorChecking errorChecking = (queryPhase_ == GLShaderProgram::QueryPhase::Immediate)
			? GLShader::ErrorChecking::Immediate
			: GLShader::ErrorChecking::Deferred;
		const bool hasCompiled = shader->Compile(errorChecking, shouldLogOnErrors_);

		if (hasCompiled) {
			attachedShaders_.push_back(std::move(shader));
		} else {
			status_ = Status::CompilationFailed;
		}
		return hasCompiled;
	}

	bool GLShaderProgram::Link(Introspection introspection)
	{
		glLinkProgram(glHandle_);
		return FinalizeAfterLinking(introspection);
	}

	void GLShaderProgram::Use()
	{
		if (boundProgram_ != glHandle_) {
			ProcessDeferredQueries();

			glUseProgram(glHandle_);
			boundProgram_ = glHandle_;
		}
	}

	bool GLShaderProgram::Validate()
	{
		glValidateProgram(glHandle_);
		GLint status;
		glGetProgramiv(glHandle_, GL_VALIDATE_STATUS, &status);
		return (status == GL_TRUE);
	}

	bool GLShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;

		if (queryPhase_ == QueryPhase::Immediate) {
			status_ = Status::NotLinked;
			const bool linkCheck = CheckLinking();
			if (!linkCheck) {
				return false;
			}

			// After linking, shader objects are not needed anymore
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}

			attachedShaders_.clear();

			PerformIntrospection();
			return linkCheck;
		} else {
			status_ = GLShaderProgram::Status::LinkedWithDeferredQueries;
			return true;
		}
	}

	GLVertexFormat::Attribute* GLShaderProgram::GetAttribute(const char* name)
	{
		ASSERT(name);
		GLVertexFormat::Attribute* vertexAttribute = nullptr;

		std::int32_t location = -1;
		const bool attributeFound = attributeLocations_.contains(name, location);
		if (attributeFound) {
			vertexAttribute = &vertexFormat_[location];
		}
		return vertexAttribute;
	}

	void GLShaderProgram::DefineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, std::uint32_t vboOffset)
	{
		if (vbo != nullptr) {
			for (std::int32_t location : attributeLocations_) {
				vertexFormat_[location].setVbo(vbo);
				vertexFormat_[location].SetBaseOffset(vboOffset);
			}
			vertexFormat_.SetIbo(ibo);

			RenderResources::vaoPool().bindVao(vertexFormat_);
		}
	}

	void GLShaderProgram::Reset()
	{
		if (status_ != Status::NotLinked && status_ != Status::CompilationFailed) {
			uniforms_.clear();
			uniformBlocks_.clear();
			attributes_.clear();

			attributeLocations_.clear();
			vertexFormat_.Reset();

			if (boundProgram_ == glHandle_) {
				glUseProgram(0);
			}

			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}

			attachedShaders_.clear();
			glDeleteProgram(glHandle_);

			RenderResources::removeCameraUniformData(this);
			RenderResources::unregisterBatchedShader(this);

			glHandle_ = glCreateProgram();
		}

		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
	}

	void GLShaderProgram::SetObjectLabel(const char* label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Program, glHandle_, label);
	}

	bool GLShaderProgram::ProcessDeferredQueries()
	{
		if (status_ == GLShaderProgram::Status::LinkedWithDeferredQueries) {
			for (std::unique_ptr<GLShader>& attachedShader : attachedShaders_) {
				const bool compileCheck = attachedShader->CheckCompilation(shouldLogOnErrors_);
				if (!compileCheck) {
					return false;
				}
			}

			const bool linkCheck = CheckLinking();
			if (!linkCheck) {
				return false;
			}

			// After linking, shader objects are not needed anymore
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}

			attachedShaders_.clear();

			PerformIntrospection();
		}
		return true;
	}

	bool GLShaderProgram::CheckLinking()
	{
		if (status_ == Status::Linked || status_ == Status::LinkedWithIntrospection) {
			return true;
		}

		GLint status;
		glGetProgramiv(glHandle_, GL_LINK_STATUS, &status);
		if (status == GL_FALSE) {
#if defined(DEATH_TRACE)
			if (shouldLogOnErrors_) {
				GLint length = 0;
				glGetProgramiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
				if (length > 0) {
					glGetProgramInfoLog(glHandle_, MaxInfoLogLength, &length, infoLogString_);
					LOGW("%s", infoLogString_);
				}
			}
			DEATH_ASSERT_BREAK();
#endif
			status_ = Status::LinkingFailed;
			return false;
		}

		status_ = Status::Linked;
		return true;
	}

	void GLShaderProgram::PerformIntrospection()
	{
		if (introspection_ != Introspection::Disabled && status_ != Status::LinkedWithIntrospection) {
			const GLUniformBlock::DiscoverUniforms discover = (introspection_ == Introspection::NoUniformsInBlocks)
				? GLUniformBlock::DiscoverUniforms::DISABLED
				: GLUniformBlock::DiscoverUniforms::ENABLED;

			DiscoverUniforms();
			DiscoverUniformBlocks(discover);
			DiscoverAttributes();
			InitVertexFormat();
			status_ = Status::LinkedWithIntrospection;
		}
	}

	void GLShaderProgram::DiscoverUniforms()
	{
		static const std::uint32_t NumIndices = 512;
		static GLuint uniformIndices[NumIndices];
		static GLint blockIndices[NumIndices];

		ZoneScopedC(0x81A861);
		GLint uniformCount = 0;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORMS, &uniformCount);

		if (uniformCount > 0) {
			std::uint32_t uniformsOutsideBlocks = 0;
			GLuint indices[MaxNumUniforms];
			std::uint32_t remainingIndices = static_cast<std::uint32_t>(uniformCount);

			while (remainingIndices > 0) {
				const std::uint32_t uniformCountStep = (remainingIndices > NumIndices) ? NumIndices : remainingIndices;
				const std::uint32_t startIndex = static_cast<std::uint32_t>(uniformCount) - remainingIndices;

				for (std::uint32_t i = 0; i < uniformCountStep; i++)
					uniformIndices[i] = startIndex + i;

				glGetActiveUniformsiv(glHandle_, uniformCountStep, uniformIndices, GL_UNIFORM_BLOCK_INDEX, blockIndices);

				for (std::uint32_t i = 0; i < uniformCountStep; i++) {
					if (blockIndices[i] == -1) {
						indices[uniformsOutsideBlocks] = startIndex + i;
						uniformsOutsideBlocks++;
					}
					if (uniformsOutsideBlocks > MaxNumUniforms - 1)
						break;
				}

				remainingIndices -= uniformCountStep;
			}

			for (std::uint32_t i = 0; i < uniformsOutsideBlocks; i++) {
				GLUniform& uniform = uniforms_.emplace_back(glHandle_, indices[i]);
				uniformsSize_ += uniform.GetMemorySize();

				LOGD("Shader program %u - uniform %d : \"%s\"", glHandle_, uniform.GetLocation(), uniform.GetName());
			}
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::DiscoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover)
	{
		ZoneScopedC(0x81A861);
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORM_BLOCKS, &count);

		for (std::int32_t i = 0; i < count; i++) {
			GLUniformBlock& uniformBlock = uniformBlocks_.emplace_back(glHandle_, i, discover);
			uniformBlocksSize_ += uniformBlock.GetSize();

			LOGD("Shader program %u - uniform block %u : \"%s\" (%d bytes with %u align)", glHandle_, uniformBlock.GetIndex(), uniformBlock.GetName(), uniformBlock.GetSize(), uniformBlock.GetAlignAmount());
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::DiscoverAttributes()
	{
		ZoneScopedC(0x81A861);
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_ATTRIBUTES, &count);

		for (std::int32_t i = 0; i < count; i++) {
			DEATH_UNUSED GLAttribute& attribute = attributes_.emplace_back(glHandle_, i);
			LOGD("Shader program %u - attribute %d : \"%s\"", glHandle_, attribute.GetLocation(), attribute.GetName());
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::InitVertexFormat()
	{
		ZoneScopedC(0x81A861);
		const std::uint32_t count = (std::uint32_t)attributes_.size();
		if (count > GLVertexFormat::MaxAttributes) {
			LOGW("More active attributes (%d) than supported by the vertex format class (%d)", count, GLVertexFormat::MaxAttributes);
		}
		for (std::uint32_t i = 0; i < attributes_.size(); i++) {
			const GLAttribute& attribute = attributes_[i];
			const std::int32_t location = attribute.GetLocation();
			if (location < 0)
				continue;

			attributeLocations_[attribute.GetName()] = location;
			vertexFormat_[location].Init(attribute.GetLocation(), attribute.GetComponentCount(), attribute.GetBasicType());
		}
	}
}
