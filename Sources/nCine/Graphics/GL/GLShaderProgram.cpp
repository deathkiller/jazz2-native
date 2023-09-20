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
		
		if (RenderResources::binaryShaderCache().isAvailable()) {
			glProgramParameteri(glHandle_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		}
	}

	GLShaderProgram::GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GLShaderProgram(queryPhase)
	{
		const bool hasCompiledVS = attachShaderFromFile(GL_VERTEX_SHADER, vertexFile);
		const bool hasCompiledFS = attachShaderFromFile(GL_FRAGMENT_SHADER, fragmentFile);

		if (hasCompiledVS && hasCompiledFS) {
			link(introspection);
		}
	}

	GLShaderProgram::GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile, Introspection introspection)
		: GLShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	GLShaderProgram::GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile)
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

	bool GLShaderProgram::isLinked() const
	{
		return (status_ == Status::Linked ||
				status_ == Status::LinkedWithDeferredQueries ||
				status_ == Status::LinkedWithIntrospection);
	}

	unsigned int GLShaderProgram::retrieveInfoLogLength() const
	{
		GLint length = 0;
		glGetProgramiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
		return static_cast<unsigned int>(length);
	}

	void GLShaderProgram::retrieveInfoLog(std::string& infoLog) const
	{
		GLint length = 0;
		glGetProgramiv(glHandle_, GL_INFO_LOG_LENGTH, &length);

		if (length > 0 && infoLog.capacity() > 0) {
			const unsigned int capacity = (unsigned int)infoLog.capacity();
			glGetProgramInfoLog(glHandle_, capacity, &length, infoLog.data());
			infoLog.resize(static_cast<unsigned int>(length) < capacity - 1 ? static_cast<unsigned int>(length) : capacity - 1);
		}
	}

	bool GLShaderProgram::attachShaderFromFile(GLenum type, const StringView& filename)
	{
		return attachShaderFromStringsAndFile(type, nullptr, filename);
	}
	
	bool GLShaderProgram::attachShaderFromString(GLenum type, const char* string)
	{
		const char* strings[2] = { string, nullptr };
		return attachShaderFromStringsAndFile(type, strings, { });
	}
	
	bool GLShaderProgram::attachShaderFromStrings(GLenum type, const char** strings)
	{
		return attachShaderFromStringsAndFile(type, strings, { });
	}
	
	bool GLShaderProgram::attachShaderFromStringsAndFile(GLenum type, const char** strings, const StringView& filename)
	{
		std::unique_ptr<GLShader> shader = std::make_unique<GLShader>(type);
		shader->loadFromStringsAndFile(strings, filename);
		glAttachShader(glHandle_, shader->glHandle());

		const GLShader::ErrorChecking errorChecking = (queryPhase_ == GLShaderProgram::QueryPhase::Immediate)
			? GLShader::ErrorChecking::Immediate
			: GLShader::ErrorChecking::Deferred;
		const bool hasCompiled = shader->compile(errorChecking, shouldLogOnErrors_);

		if (hasCompiled) {
			attachedShaders_.push_back(std::move(shader));
		} else {
			status_ = Status::CompilationFailed;
		}
		return hasCompiled;
	}

	bool GLShaderProgram::link(Introspection introspection)
	{
		glLinkProgram(glHandle_);
		return finalizeAfterLinking(introspection);
	}

	void GLShaderProgram::use()
	{
		if (boundProgram_ != glHandle_) {
			deferredQueries();

			glUseProgram(glHandle_);
			boundProgram_ = glHandle_;
		}
	}

	bool GLShaderProgram::validate()
	{
		glValidateProgram(glHandle_);
		GLint status;
		glGetProgramiv(glHandle_, GL_VALIDATE_STATUS, &status);
		return (status == GL_TRUE);
	}

	bool GLShaderProgram::finalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;

		if (queryPhase_ == QueryPhase::Immediate) {
			status_ = Status::NotLinked;
			const bool linkCheck = checkLinking();
			if (!linkCheck) {
				return false;
			}

			// After linking, shader objects are not needed anymore
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->glHandle());
			}

			attachedShaders_.clear();

			performIntrospection();
			return linkCheck;
		} else {
			status_ = GLShaderProgram::Status::LinkedWithDeferredQueries;
			return true;
		}
	}

	GLVertexFormat::Attribute* GLShaderProgram::attribute(const char* name)
	{
		ASSERT(name);
		GLVertexFormat::Attribute* vertexAttribute = nullptr;

		int location = -1;
		const bool attributeFound = attributeLocations_.contains(name, location);
		if (attributeFound) {
			vertexAttribute = &vertexFormat_[location];
		}
		return vertexAttribute;
	}

	void GLShaderProgram::defineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, unsigned int vboOffset)
	{
		if (vbo != nullptr) {
			for (int location : attributeLocations_) {
				vertexFormat_[location].setVbo(vbo);
				vertexFormat_[location].setBaseOffset(vboOffset);
			}
			vertexFormat_.setIbo(ibo);

			RenderResources::vaoPool().bindVao(vertexFormat_);
		}
	}

	void GLShaderProgram::reset()
	{
		if (status_ != Status::NotLinked && status_ != Status::CompilationFailed) {
			uniforms_.clear();
			uniformBlocks_.clear();
			attributes_.clear();

			attributeLocations_.clear();
			vertexFormat_.reset();

			if (boundProgram_ == glHandle_) {
				glUseProgram(0);
			}

			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->glHandle());
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

	void GLShaderProgram::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::Program, glHandle_, label);
	}

	bool GLShaderProgram::deferredQueries()
	{
		if (status_ == GLShaderProgram::Status::LinkedWithDeferredQueries) {
			for (std::unique_ptr<GLShader>& attachedShader : attachedShaders_) {
				const bool compileCheck = attachedShader->checkCompilation(shouldLogOnErrors_);
				if (!compileCheck) {
					return false;
				}
			}

			const bool linkCheck = checkLinking();
			if (!linkCheck) {
				return false;
			}

			// After linking, shader objects are not needed anymore
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->glHandle());
			}

			attachedShaders_.clear();

			performIntrospection();
		}
		return true;
	}

	bool GLShaderProgram::checkLinking()
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
#endif
			status_ = Status::LinkingFailed;
			return false;
		}

		status_ = Status::Linked;
		return true;
	}

	void GLShaderProgram::performIntrospection()
	{
		if (introspection_ != Introspection::Disabled && status_ != Status::LinkedWithIntrospection) {
			const GLUniformBlock::DiscoverUniforms discover = (introspection_ == Introspection::NoUniformsInBlocks)
				? GLUniformBlock::DiscoverUniforms::DISABLED
				: GLUniformBlock::DiscoverUniforms::ENABLED;

			discoverUniforms();
			discoverUniformBlocks(discover);
			discoverAttributes();
			initVertexFormat();
			status_ = Status::LinkedWithIntrospection;
		}
	}

	void GLShaderProgram::discoverUniforms()
	{
		static const unsigned int NumIndices = 512;
		static GLuint uniformIndices[NumIndices];
		static GLint blockIndices[NumIndices];

		ZoneScoped;
		GLint uniformCount = 0;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORMS, &uniformCount);

		if (uniformCount > 0) {
			unsigned int uniformsOutsideBlocks = 0;
			GLuint indices[MaxNumUniforms];
			unsigned int remainingIndices = static_cast<unsigned int>(uniformCount);

			while (remainingIndices > 0) {
				const unsigned int uniformCountStep = (remainingIndices > NumIndices) ? NumIndices : remainingIndices;
				const unsigned int startIndex = static_cast<unsigned int>(uniformCount) - remainingIndices;

				for (unsigned int i = 0; i < uniformCountStep; i++)
					uniformIndices[i] = startIndex + i;

				glGetActiveUniformsiv(glHandle_, uniformCountStep, uniformIndices, GL_UNIFORM_BLOCK_INDEX, blockIndices);

				for (unsigned int i = 0; i < uniformCountStep; i++) {
					if (blockIndices[i] == -1) {
						indices[uniformsOutsideBlocks] = startIndex + i;
						uniformsOutsideBlocks++;
					}
					if (uniformsOutsideBlocks > MaxNumUniforms - 1)
						break;
				}

				remainingIndices -= uniformCountStep;
			}

			for (unsigned int i = 0; i < uniformsOutsideBlocks; i++) {
				GLUniform& uniform = uniforms_.emplace_back(glHandle_, indices[i]);
				uniformsSize_ += uniform.memorySize();

				LOGD("Shader program %u - uniform %d : \"%s\"", glHandle_, uniform.location(), uniform.name());
			}
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::discoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover)
	{
		ZoneScoped;
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORM_BLOCKS, &count);

		for (int i = 0; i < count; i++) {
			GLUniformBlock& uniformBlock = uniformBlocks_.emplace_back(glHandle_, i, discover);
			uniformBlocksSize_ += uniformBlock.size();

			LOGD("Shader program %u - uniform block %u : \"%s\" (%d bytes with %u align)", glHandle_, uniformBlock.index(), uniformBlock.name(), uniformBlock.size(), uniformBlock.alignAmount());
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::discoverAttributes()
	{
		ZoneScoped;
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_ATTRIBUTES, &count);

		for (int i = 0; i < count; i++) {
			GLAttribute& attribute = attributes_.emplace_back(glHandle_, i);

			LOGD("Shader program %u - attribute %d : \"%s\"", glHandle_, attribute.location(), attribute.name());
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::initVertexFormat()
	{
		ZoneScoped;
		const unsigned int count = (unsigned int)attributes_.size();
		if (count > GLVertexFormat::MaxAttributes) {
			LOGW("More active attributes (%d) than supported by the vertex format class (%d)", count, GLVertexFormat::MaxAttributes);
		}
		for (unsigned int i = 0; i < attributes_.size(); i++) {
			const GLAttribute& attribute = attributes_[i];
			const int location = attribute.location();
			if (location < 0)
				continue;

			attributeLocations_[attribute.name()] = location;
			vertexFormat_[location].init(attribute.location(), attribute.numComponents(), attribute.basicType());
		}
	}
}
