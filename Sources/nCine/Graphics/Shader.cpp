#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "Shader.h"
#include "GL/GLShaderProgram.h"
#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "../Application.h"
#include "../tracy.h"
#include "../../Common.h"

#if defined(WITH_EMBEDDED_SHADERS)
#	include "shader_strings.h"
#else
#	include <IO/FileSystem.h>
using namespace Death::IO;
#endif

using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		static const char BatchSizeFormatString[] = "#define BATCH_SIZE (%d)\n#line 0\n";

		GLShaderProgram::Introspection shaderToShaderProgramIntrospection(Shader::Introspection introspection)
		{
			switch (introspection) {
				default:
				case Shader::Introspection::Enabled:
					return GLShaderProgram::Introspection::Enabled;
				case Shader::Introspection::NoUniformsInBlocks:
					return GLShaderProgram::Introspection::NoUniformsInBlocks;
				case Shader::Introspection::Disabled:
					return GLShaderProgram::Introspection::Disabled;
			}
		}

		bool isBatchedVertex(Shader::DefaultVertex vertex)
		{
			switch (vertex) {
				case Shader::DefaultVertex::BATCHED_SPRITES:
				case Shader::DefaultVertex::BATCHED_SPRITES_NOTEXTURE:
				case Shader::DefaultVertex::BATCHED_MESHSPRITES:
				case Shader::DefaultVertex::BATCHED_MESHSPRITES_NOTEXTURE:
				//case Shader::DefaultVertex::BATCHED_TEXTNODES:
					return true;
				default:
					return false;
			}
		}
	}

	Shader::Shader()
		: Object(ObjectType::Shader), glShaderProgram_(std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate))
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: loadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, vertex, fragment, batchSize)
			: loadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, const char* vertex, const char* fragment, int batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: loadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, DefaultVertex vertex, const char* fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, vertex, fragment, batchSize)
			: loadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, DefaultVertex vertex, const char* fragment, int batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: loadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, const char* vertex, DefaultFragment fragment, int batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? loadFromMemory(shaderName, vertex, fragment, batchSize)
			: loadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"%s\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, const char* vertex, DefaultFragment fragment, int batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::~Shader()
	{
		RenderResources::unregisterBatchedShader(glShaderProgram_.get());
	}

	bool Shader::loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[3] = { sourceString, vertex, nullptr };
			glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, { });
		} else {
			glShaderProgram_->attachShaderFromString(GL_VERTEX_SHADER, vertex);
		}

		glShaderProgram_->attachShaderFromString(GL_FRAGMENT_SHADER, fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromMemory(const char* shaderName, const char* vertex, const char* fragment, int batchSize)
	{
		return loadFromMemory(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::loadFromMemory(const char* vertex, const char* fragment, int batchSize)
	{
		return loadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);
		loadDefaultShader(vertex, batchSize);
		glShaderProgram_->attachShaderFromString(GL_FRAGMENT_SHADER, fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, int batchSize)
	{
		const Introspection introspection = (isBatchedVertex(vertex) ? Introspection::NoUniformsInBlocks : Introspection::Enabled);
		return loadFromMemory(shaderName, introspection, vertex, fragment, batchSize);
	}

	bool Shader::loadFromMemory(DefaultVertex vertex, const char* fragment, int batchSize)
	{
		return loadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[3] = { sourceString, vertex, nullptr };
			glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, { });
		} else {
			glShaderProgram_->attachShaderFromString(GL_VERTEX_SHADER, vertex);
		}

		loadDefaultShader(fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, int batchSize)
	{
		return loadFromMemory(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::loadFromMemory(const char* vertex, DefaultFragment fragment, int batchSize)
	{
		return loadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[2] = { sourceString, nullptr };
			glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, vertex);
		} else {
			glShaderProgram_->attachShaderFromFile(GL_VERTEX_SHADER, vertex);
		}

		glShaderProgram_->attachShaderFromFile(GL_FRAGMENT_SHADER, fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromFile(const char* shaderName, const char* vertex, const char* fragment, int batchSize)
	{
		return loadFromFile(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(const char* vertex, const char* fragment, int batchSize)
	{
		return loadFromFile(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);
		loadDefaultShader(vertex, batchSize);
		glShaderProgram_->attachShaderFromFile(GL_FRAGMENT_SHADER, fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromFile(const char* shaderName, DefaultVertex vertex, const char* fragment, int batchSize)
	{
		const Introspection introspection = (isBatchedVertex(vertex) ? Introspection::NoUniformsInBlocks : Introspection::Enabled);
		return loadFromFile(shaderName, introspection, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(DefaultVertex vertex, const char* fragment, int batchSize)
	{
		return loadFromFile(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset(); // reset before attaching new shaders
		glShaderProgram_->setBatchSize(batchSize);
		glShaderProgram_->setObjectLabel(shaderName);

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[2] = { sourceString, nullptr };
			glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, vertex);
		} else {
			glShaderProgram_->attachShaderFromFile(GL_VERTEX_SHADER, vertex);
		}

		loadDefaultShader(fragment);
		glShaderProgram_->link(shaderToShaderProgramIntrospection(introspection));

		return isLinked();
	}

	bool Shader::loadFromFile(const char* shaderName, const char* vertex, DefaultFragment fragment, int batchSize)
	{
		return loadFromFile(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::loadFromFile(const char* vertex, DefaultFragment fragment, int batchSize)
	{
		return loadFromFile(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::loadFromCache(const char* shaderName, uint64_t shaderVersion, Introspection introspection)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, strlen(shaderName));
		}

		glShaderProgram_->reset();
		glShaderProgram_->setObjectLabel(shaderName);
		return RenderResources::binaryShaderCache().loadFromCache(shaderName, shaderVersion, glShaderProgram_.get(), shaderToShaderProgramIntrospection(introspection));
	}

	bool Shader::saveToCache(const char* shaderName, uint64_t shaderVersion) const
	{
		return RenderResources::binaryShaderCache().saveToCache(shaderName, shaderVersion, glShaderProgram_.get());
	}

	bool Shader::setAttribute(const char* name, int stride, void* pointer)
	{
		GLVertexFormat::Attribute* attribute = glShaderProgram_->attribute(name);
		if (attribute != nullptr) {
			attribute->setVboParameters(stride, pointer);
		}
		return (attribute != nullptr);
	}

	bool Shader::isLinked() const
	{
		return glShaderProgram_->isLinked();
	}

	unsigned int Shader::retrieveInfoLogLength() const
	{
		return glShaderProgram_->retrieveInfoLogLength();
	}

	void Shader::retrieveInfoLog(std::string& infoLog) const
	{
		glShaderProgram_->retrieveInfoLog(infoLog);
	}

	bool Shader::logOnErrors() const
	{
		return glShaderProgram_->logOnErrors();
	}

	void Shader::setLogOnErrors(bool shouldLogOnErrors)
	{
		glShaderProgram_->setLogOnErrors(shouldLogOnErrors);
	}

	void Shader::setGLShaderProgramLabel(const char* label)
	{
		glShaderProgram_->setObjectLabel(label);
	}

	void Shader::registerBatchedShader(Shader& batchedShader)
	{
		RenderResources::registerBatchedShader(glShaderProgram_.get(), batchedShader.glShaderProgram_.get());
	}

	bool Shader::loadDefaultShader(DefaultVertex vertex, int batchSize)
	{
#if !defined(WITH_EMBEDDED_SHADERS)
		StringView vertexShader;
		switch (vertex) {
			case DefaultVertex::SPRITE:
				vertexShader = "sprite_vs.glsl"_s;
				break;
			case DefaultVertex::SPRITE_NOTEXTURE:
				vertexShader = "sprite_notexture_vs.glsl"_s;
				break;
			case DefaultVertex::MESHSPRITE:
				vertexShader = "meshsprite_vs.glsl"_s;
				break;
			case DefaultVertex::MESHSPRITE_NOTEXTURE:
				vertexShader = "meshsprite_notexture_vs.glsl"_s;
				break;
			//case DefaultVertex::TEXTNODE:
			//	vertexShader = "textnode_vs.glsl"_s;
			//	break;
			case DefaultVertex::BATCHED_SPRITES:
				vertexShader = "batched_sprites_vs.glsl"_s;
				break;
			case DefaultVertex::BATCHED_SPRITES_NOTEXTURE:
				vertexShader = "batched_sprites_notexture_vs.glsl"_s;
				break;
			case DefaultVertex::BATCHED_MESHSPRITES:
				vertexShader = "batched_meshsprites_vs.glsl"_s;
				break;
			case DefaultVertex::BATCHED_MESHSPRITES_NOTEXTURE:
				vertexShader = "batched_meshsprites_notexture_vs.glsl"_s;
				break;
			//case DefaultVertex::BATCHED_TEXTNODES:
			//	vertexShader = "batched_textnodes_vs.glsl";
			//	break;
		}

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[2] = { sourceString, nullptr };
			return glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, vertexShader }));
		} else {
			return glShaderProgram_->attachShaderFromFile(GL_VERTEX_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, vertexShader }));
		}
#else
		const char* vertexShader = nullptr;
		// Skipping the initial new line character of the raw string literal
		switch (vertex) {
			case DefaultVertex::SPRITE:
				vertexShader = ShaderStrings::sprite_vs + 1;
				break;
			case DefaultVertex::SPRITE_NOTEXTURE:
				vertexShader = ShaderStrings::sprite_notexture_vs + 1;
				break;
			case DefaultVertex::MESHSPRITE:
				vertexShader = ShaderStrings::meshsprite_vs + 1;
				break;
			case DefaultVertex::MESHSPRITE_NOTEXTURE:
				vertexShader = ShaderStrings::meshsprite_notexture_vs + 1;
				break;
			//case DefaultVertex::TEXTNODE:
			//	vertexShader = ShaderStrings::textnode_vs + 1;
			//	break;
			case DefaultVertex::BATCHED_SPRITES:
				vertexShader = ShaderStrings::batched_sprites_vs + 1;
				break;
			case DefaultVertex::BATCHED_SPRITES_NOTEXTURE:
				vertexShader = ShaderStrings::batched_sprites_notexture_vs + 1;
				break;
			case DefaultVertex::BATCHED_MESHSPRITES:
				vertexShader = ShaderStrings::batched_meshsprites_vs + 1;
				break;
			case DefaultVertex::BATCHED_MESHSPRITES_NOTEXTURE:
				vertexShader = ShaderStrings::batched_meshsprites_notexture_vs + 1;
				break;
			//case DefaultVertex::BATCHED_TEXTNODES:
			//	vertexShader = ShaderStrings::batched_textnodes_vs + 1;
			//	break;
		}

		if (batchSize > 0) {
			char sourceString[48];
			formatString(sourceString, sizeof(sourceString), BatchSizeFormatString, batchSize);
			const char* vertexStrings[3] = { sourceString, vertexShader, nullptr };
			return glShaderProgram_->attachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, { });
		} else {
			return glShaderProgram_->attachShaderFromString(GL_VERTEX_SHADER, vertexShader);
		}
#endif
	}

	bool Shader::loadDefaultShader(DefaultFragment fragment)
	{
#if !defined(WITH_EMBEDDED_SHADERS)
		StringView fragmentShader;
		switch (fragment) {
			case DefaultFragment::SPRITE:
				fragmentShader = "sprite_fs.glsl"_s;
				break;
			//case DefaultFragment::SPRITE_GRAY:
			//	fragmentShader = "sprite_gray_fs.glsl"_s;
			//	break;
			case DefaultFragment::SPRITE_NOTEXTURE:
				fragmentShader = "sprite_notexture_fs.glsl"_s;
				break;
			//case DefaultFragment::TEXTNODE_ALPHA:
			//	fragmentShader = "textnode_alpha_fs.glsl"_s;
			//	break;
			//case DefaultFragment::TEXTNODE_RED:
			//	fragmentShader = "textnode_red_fs.glsl"_s;
			//	break;
		}
		return glShaderProgram_->attachShaderFromFile(GL_FRAGMENT_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, fragmentShader }));
#else
		const char* fragmentShader = nullptr;
		// Skipping the initial new line character of the raw string literal
		switch (fragment) {
			case DefaultFragment::SPRITE:
				fragmentShader = ShaderStrings::sprite_fs + 1;
				break;
			//case DefaultFragment::SPRITE_GRAY:
			//	fragmentShader = ShaderStrings::sprite_gray_fs + 1;
			//	break;
			case DefaultFragment::SPRITE_NOTEXTURE:
				fragmentShader = ShaderStrings::sprite_notexture_fs + 1;
				break;
			//case DefaultFragment::TEXTNODE_ALPHA:
			//	fragmentShader = ShaderStrings::textnode_alpha_fs + 1;
			//	break;
			//case DefaultFragment::TEXTNODE_RED:
			//	fragmentShader = ShaderStrings::textnode_red_fs + 1;
			//	break;
		}
		return glShaderProgram_->attachShaderFromString(GL_FRAGMENT_SHADER, fragmentShader);
#endif
	}
}