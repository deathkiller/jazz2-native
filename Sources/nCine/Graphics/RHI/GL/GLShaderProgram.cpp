#include "GLShaderProgram.h"
#include "GLShader.h"
#include "GLDebug.h"
#include "../../BinaryShaderCache.h"
#include "../../RenderResources.h"
#include "../../RenderVaoPool.h"
#include "../../IGfxCapabilities.h"
#include "../../../ServiceLocator.h"
#include "../../../Base/StaticHashMapIterator.h"
#include "../../../tracy.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstring>
#include <string>

namespace nCine::RHI::GL
{
	namespace
	{
		GLenum UniformTypeToGL(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float: return GL_FLOAT;
				case ShaderCompiler::UniformType::Int: return GL_INT;
				case ShaderCompiler::UniformType::UInt: return GL_UNSIGNED_INT;
#if !defined(DEATH_TARGET_VITA)
				case ShaderCompiler::UniformType::Bool: return GL_BOOL;
#endif
				case ShaderCompiler::UniformType::Vec2: return GL_FLOAT_VEC2;
				case ShaderCompiler::UniformType::Vec3: return GL_FLOAT_VEC3;
				case ShaderCompiler::UniformType::Vec4: return GL_FLOAT_VEC4;
				case ShaderCompiler::UniformType::IVec2: return GL_INT_VEC2;
				case ShaderCompiler::UniformType::IVec3: return GL_INT_VEC3;
				case ShaderCompiler::UniformType::IVec4: return GL_INT_VEC4;
#if !defined(DEATH_TARGET_VITA)	// vitaGL declares none of the bool / unsigned-int vector types
				case ShaderCompiler::UniformType::UVec2: return GL_UNSIGNED_INT_VEC2;
				case ShaderCompiler::UniformType::UVec3: return GL_UNSIGNED_INT_VEC3;
				case ShaderCompiler::UniformType::UVec4: return GL_UNSIGNED_INT_VEC4;
				case ShaderCompiler::UniformType::BVec2: return GL_BOOL_VEC2;
				case ShaderCompiler::UniformType::BVec3: return GL_BOOL_VEC3;
				case ShaderCompiler::UniformType::BVec4: return GL_BOOL_VEC4;
#endif
				case ShaderCompiler::UniformType::Mat2: return GL_FLOAT_MAT2;
				case ShaderCompiler::UniformType::Mat3: return GL_FLOAT_MAT3;
				case ShaderCompiler::UniformType::Mat4: return GL_FLOAT_MAT4;
				case ShaderCompiler::UniformType::Sampler2D: return GL_SAMPLER_2D;
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
				case ShaderCompiler::UniformType::Sampler3D: return GL_SAMPLER_3D;
#endif
				case ShaderCompiler::UniformType::SamplerCube: return GL_SAMPLER_CUBE;
				default:
					LOGW("No available case to handle reflected type: {}", std::uint32_t(type));
					return GL_FLOAT;
			}
		}
	}

	GLuint GLShaderProgram::boundProgram_ = 0;

	GLShaderProgram::GLShaderProgram()
		: GLShaderProgram(QueryPhase::Immediate)
	{
	}

	GLShaderProgram::GLShaderProgram(QueryPhase queryPhase)
		: glHandle_(0), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase), batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0), reflection_(nullptr)
	{
		glHandle_ = glCreateProgram();

		attachedShaders_.reserve(AttachedShadersInitialSize);
		uniforms_.reserve(UniformsInitialSize);
		uniformBlocks_.reserve(UniformBlocksInitialSize);
		attributes_.reserve(AttributesInitialSize);
		
#if defined(RHI_GL_PROFILE_ES2)
		// GL_PROGRAM_BINARY_RETRIEVABLE_HINT (and glProgramParameteri) is ES 3.0; the ES2-era
		// OES_get_program_binary path retrieves binaries without a hint
#else
		if (RenderResources::GetBinaryShaderCache().IsAvailable()) {
			glProgramParameteri(glHandle_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		}
#endif
	}

	GLShaderProgram::GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GLShaderProgram(queryPhase)
	{
		const bool hasCompiledVS = AttachShaderFromFile(ShaderStage::Vertex, vertexFile);
		const bool hasCompiledFS = AttachShaderFromFile(ShaderStage::Fragment, fragmentFile);

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

#if !defined(WITH_RHI_SOFTWARE)
		// The render pipeline is built against a single backend's `RHI::` aliases; when the software
		// backend is selected these GL-typed pipeline calls do not apply (this file is dead code there)
		RenderResources::RemoveCameraUniformData(this);
#endif
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

	bool GLShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		return AttachShaderFromStringsAndFile(stage, nullptr, filename);
	}

	bool GLShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		return AttachShaderFromStringsAndFile(stage, arrayView({ string }), {});
	}

	bool GLShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		return AttachShaderFromStringsAndFile(stage, strings, {});
	}

	bool GLShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		std::unique_ptr<GLShader> shader = std::make_unique<GLShader>(stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
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
#if defined(DEATH_TARGET_VITA)
		// vitaGL provides no glValidateProgram(), program validation is a debug-only aid, so assume success
		return true;
#else
		glValidateProgram(glHandle_);
		GLint status;
		glGetProgramiv(glHandle_, GL_VALIDATE_STATUS, &status);
		return (status == GL_TRUE);
#endif
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
#if !defined(DEATH_TARGET_VITA)
			// vitaGL has no glDetachShader(), the shader objects are released with the program instead
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}
#endif

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
		DEATH_ASSERT(name);
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
#if defined(RHI_GL_PROFILE_ES2)
		// ES2: the gl_VertexID-free sprite/full-screen shaders read their quad corner from the shared static
		// corner VBO (bound to aQuadCorner); any real geometry attributes (mesh sprites) come from the geometry
		// VBO. A VAO must be bound even when the sprite has no geometry VBO, so drive it off attribute presence.
		std::int32_t cornerLocation = -1;
		attributeLocations_.contains(Material::QuadCornerAttributeName, cornerLocation);
		const GLBufferObject* cornerVbo = static_cast<const GLBufferObject*>(RenderResources::GetQuadCornerVbo());
		bool hasBoundAttribute = false;
		for (std::int32_t location : attributeLocations_) {
			if (location == cornerLocation) {
				if (cornerVbo != nullptr) {
					vertexFormat_[location].setVbo(cornerVbo);
					vertexFormat_[location].SetBaseOffset(0);
					hasBoundAttribute = true;
				}
			} else if (vbo != nullptr) {
				vertexFormat_[location].setVbo(vbo);
				vertexFormat_[location].SetBaseOffset(vboOffset);
				hasBoundAttribute = true;
			}
		}
		vertexFormat_.SetIbo(ibo);
		if (hasBoundAttribute) {
			RenderResources::GetVaoPool().BindVao(vertexFormat_);
		}
#else
		if (vbo != nullptr) {
			for (std::int32_t location : attributeLocations_) {
				vertexFormat_[location].setVbo(vbo);
				vertexFormat_[location].SetBaseOffset(vboOffset);
			}
			vertexFormat_.SetIbo(ibo);

#if !defined(WITH_RHI_SOFTWARE)
			RenderResources::GetVaoPool().BindVao(vertexFormat_);
#endif
		}
#endif
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

#if !defined(DEATH_TARGET_VITA)
			// vitaGL has no glDetachShader(), the shader objects are released with the program instead
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}
#endif

			attachedShaders_.clear();
			glDeleteProgram(glHandle_);

#if !defined(WITH_RHI_SOFTWARE)
			RenderResources::RemoveCameraUniformData(this);
			RenderResources::UnregisterBatchedShader(this);
#endif

			glHandle_ = glCreateProgram();
		}

		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
		reflection_ = nullptr;
	}

	void GLShaderProgram::SetObjectLabel(StringView label)
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
#if !defined(DEATH_TARGET_VITA)
			// vitaGL has no glDetachShader(), the shader objects are released with the program instead
			for (auto& shader : attachedShaders_) {
				glDetachShader(glHandle_, shader->GetGLHandle());
			}
#endif

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
					static char buffer[2048];
					glGetProgramInfoLog(glHandle_, sizeof(buffer), &length, buffer);
					LOGW("{}", buffer);
				}
			}
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
			uniformsSize_ = 0;
			uniformBlocksSize_ = 0;

			if (reflection_ != nullptr) {
				ImportReflection();
			} else {
				const GLUniformBlock::DiscoverUniforms discover = (introspection_ == Introspection::NoUniformsInBlocks)
					? GLUniformBlock::DiscoverUniforms::DISABLED
					: GLUniformBlock::DiscoverUniforms::ENABLED;

				DiscoverUniforms();
				DiscoverUniformBlocks(discover);
				DiscoverAttributes();
			}
			InitVertexFormat();
			status_ = Status::LinkedWithIntrospection;
		}
		// The reflection data is consumed by introspection and may not outlive the caller, so it is never kept
		reflection_ = nullptr;
	}

	void GLShaderProgram::ImportReflection()
	{
		ZoneScopedC(0x81A861);
		const ShaderCompiler::ProgramVariant& reflection = *reflection_;

		// Loose uniforms - the reflection keeps samplers in a separate list, but GL treats them as uniforms
		// (all samplers are 2D across the shader set; the reflected texture bindings carry no dimension)
		for (std::size_t i = 0; i < reflection.UniformCount; i++) {
			const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
			GLUniform& uniform = uniforms_.emplace_back(glHandle_, u.Name, UniformTypeToGL(u.Type), GLint(u.ArraySize));
			uniformsSize_ += uniform.GetMemorySize();

			LOGD("Shader program {} - uniform {} : \"{}\" (reflected)", glHandle_, uniform.GetLocation(), uniform.GetName());
		}
		for (std::size_t i = 0; i < reflection.TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = reflection.Textures[i];
			GLUniform& uniform = uniforms_.emplace_back(glHandle_, t.Name, GL_SAMPLER_2D, 1);
			uniformsSize_ += uniform.GetMemorySize();

			LOGD("Shader program {} - sampler uniform {} : \"{}\" (reflected)", glHandle_, uniform.GetLocation(), uniform.GetName());
		}

		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const ShaderCompiler::UniformBlock& b = reflection.Blocks[i];
#if defined(RHI_GL_PROFILE_ES2)
			// ES2 (ESSL 100) has no uniform buffer objects: the block's members are plain loose uniforms in
			// the linked program. Keep the block in the reflection so BaseSprite/Material still address the
			// per-instance data through it, but resolve each member's real loose location below so that
			// GLShaderUniformBlocks::Bind() can push them with glUniform* instead of a UBO bind. There is no
			// glGetUniformBlockIndex() (ES 3.0) to call; the stored index stays GL_INVALID_INDEX and unused
			// (the ES2 path never binds a buffer range).
			const GLuint blockIndex = GL_INVALID_INDEX;
#else
			const GLuint blockIndex = glGetUniformBlockIndex(glHandle_, b.Name);
			if (blockIndex == GL_INVALID_INDEX) {
				// The whole block was optimized out by the driver - GL introspection would not have listed it either
				LOGD("Shader program {} - uniform block \"{}\" is inactive and was skipped", glHandle_, b.Name);
				continue;
			}
#endif

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same 64 KB-based
			// fallback the in-shader "#ifndef BATCH_SIZE" defaults assume when no size is injected
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (batchSize_ != std::uint32_t(DefaultBatchSize) && batchSize_ > 0)
					? batchSize_ : (64 * 1024) / b.InstanceStride;
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			GLUniformBlock& uniformBlock = uniformBlocks_.emplace_back(glHandle_, b.Name, blockIndex, GLint(dataSize));
			uniformBlocksSize_ += uniformBlock.GetSize();

			if (introspection_ != Introspection::NoUniformsInBlocks) {
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// GL introspection reports flattened leaves of struct members instead - no engine code
						// accesses struct aggregates by name, so they are skipped here
						continue;
					}

					GLUniform blockUniform;
					std::size_t nameLength = strnlen(m.Name, GLUniform::MaxNameLength);
					DEATH_ASSERT(nameLength < GLUniform::MaxNameLength);
					std::memcpy(blockUniform.name_, m.Name, nameLength);
					blockUniform.name_[nameLength] = '\0';
					blockUniform.blockIndex_ = GLint(blockIndex);
					blockUniform.type_ = UniformTypeToGL(m.Type);
					blockUniform.size_ = (m.ArraySize == ShaderCompiler::SymbolicArraySize) ? GLint(effectiveBatchSize)
						: (m.ArraySize > 0 ? GLint(m.ArraySize) : 1);
					blockUniform.offset_ = GLint(m.Offset);
#if defined(RHI_GL_PROFILE_ES2)
					// The member is a real loose uniform on ES2 - resolve its location for the glUniform upload
					blockUniform.location_ = glGetUniformLocation(glHandle_, m.Name);
#endif
					uniformBlock.blockUniforms_[blockUniform.name_] = blockUniform;
				}
			}

			LOGD("Shader program {} - uniform block {} : \"{}\" ({} bytes with {} align, reflected)", glHandle_, uniformBlock.GetIndex(), uniformBlock.GetName(), uniformBlock.GetSize(), uniformBlock.GetAlignAmount());
		}

#if defined(RHI_GL_PROFILE_ES2)
		// The reflected attributes describe the modern (gl_VertexID) source; the linked ES2 program has
		// different vertex inputs (aQuadCorner / aInstanceIndex replacing gl_VertexID), so query the real
		// active attributes from GL instead of the offline reflection.
		DiscoverAttributes();
#else
		for (std::size_t i = 0; i < reflection.AttributeCount; i++) {
			const ShaderCompiler::Attribute& a = reflection.Attributes[i];
			DEATH_UNUSED GLAttribute& attribute = attributes_.emplace_back(glHandle_, a.Name, UniformTypeToGL(a.Type));
			LOGD("Shader program {} - attribute {} : \"{}\" (reflected)", glHandle_, attribute.GetLocation(), attribute.GetName());
		}
#endif
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::DiscoverUniforms()
	{
		ZoneScopedC(0x81A861);
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no uniform blocks, so every active uniform is a loose one - enumerate them directly
		// (glGetActiveUniformsiv() used below to filter out block members is ES 3.0)
		GLint uniformCount = 0;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORMS, &uniformCount);

		for (GLint i = 0; i < uniformCount; i++) {
			GLUniform& uniform = uniforms_.emplace_back(glHandle_, static_cast<GLuint>(i));
			uniformsSize_ += uniform.GetMemorySize();

			LOGD("Shader program {} - uniform {} : \"{}\"", glHandle_, uniform.GetLocation(), uniform.GetName());
		}
#else
		static const std::uint32_t NumIndices = 512;
		static GLuint uniformIndices[NumIndices];
		static GLint blockIndices[NumIndices];

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

				LOGD("Shader program {} - uniform {} : \"{}\"", glHandle_, uniform.GetLocation(), uniform.GetName());
			}
		}
#endif
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::DiscoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no uniform blocks (GL_ACTIVE_UNIFORM_BLOCKS is ES 3.0); raw-string programs on this profile
		// (e.g. ImGui) only carry loose uniforms, which DiscoverUniforms() has already enumerated
		static_cast<void>(discover);
#else
		ZoneScopedC(0x81A861);
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_UNIFORM_BLOCKS, &count);

		for (std::int32_t i = 0; i < count; i++) {
			GLUniformBlock& uniformBlock = uniformBlocks_.emplace_back(glHandle_, i, discover);
			uniformBlocksSize_ += uniformBlock.GetSize();

			LOGD("Shader program {} - uniform block {} : \"{}\" ({} bytes with {} align)", glHandle_, uniformBlock.GetIndex(), uniformBlock.GetName(), uniformBlock.GetSize(), uniformBlock.GetAlignAmount());
		}
		GL_LOG_ERRORS();
#endif
	}

	void GLShaderProgram::DiscoverAttributes()
	{
		ZoneScopedC(0x81A861);
		GLint count;
		glGetProgramiv(glHandle_, GL_ACTIVE_ATTRIBUTES, &count);

		for (std::int32_t i = 0; i < count; i++) {
			DEATH_UNUSED GLAttribute& attribute = attributes_.emplace_back(glHandle_, i);
			LOGD("Shader program {} - attribute {} : \"{}\"", glHandle_, attribute.GetLocation(), attribute.GetName());
		}
		GL_LOG_ERRORS();
	}

	void GLShaderProgram::InitVertexFormat()
	{
		ZoneScopedC(0x81A861);
		const std::uint32_t count = (std::uint32_t)attributes_.size();
		if (count > GLVertexFormat::MaxAttributes) {
			LOGW("More active attributes ({}) than supported by the vertex format class ({})", count, GLVertexFormat::MaxAttributes);
		}
		for (std::uint32_t i = 0; i < attributes_.size(); i++) {
			const GLAttribute& attribute = attributes_[i];
			const std::int32_t location = attribute.GetLocation();
			if (location < 0)
				continue;

			attributeLocations_[attribute.GetName()] = location;
			vertexFormat_[location].Init(attribute.GetLocation(), attribute.GetComponentCount(), attribute.GetBasicType());
#if defined(RHI_GL_PROFILE_ES2)
			// The ES2 quad-corner attribute is fed from a tightly-packed static VBO (a plain vec2 stream),
			// not the geometry VBO; set its stride/offset once here (the VBO is bound in DefineVertexFormat).
			if (std::strcmp(attribute.GetName(), Material::QuadCornerAttributeName) == 0) {
				vertexFormat_[location].SetVboParameters(2 * sizeof(GLfloat), nullptr);
			}
#endif
		}
	}
}
