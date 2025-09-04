#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "Shader.h"
#include "GL/GLShaderProgram.h"
#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "../Application.h"
#include "../tracy.h"
#include "../../Main.h"

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
		static const char BatchSizeDefine[] = "BATCH_SIZE";
		static const char DefineFormatString[] = "#define {} ({})\n";
		static const char ResetLineString[] = "#line 0\n";
		static const std::int32_t MaxShaderStrings = 8;

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

		std::size_t populateShaderStrings(ArrayView<StringView> strings, ArrayView<char> backingStore, const char* content, std::int32_t batchSize, ArrayView<const StringView> defines)
		{
			std::size_t lastOffset = 0, lastIndex = 0;
			if (batchSize > 0 || !strings.empty()) {
				if (batchSize > 0) {
					std::size_t length = formatInto({ &backingStore[lastOffset], backingStore.size() }, DefineFormatString, BatchSizeDefine, batchSize);
					strings[lastIndex++] = { &backingStore[lastOffset], length };
					lastOffset += length + 1;
				}
				for (auto define : defines) {
					std::size_t charsLeft = backingStore.size() - lastOffset;
					if (lastIndex >= arraySize(strings) - 3 && arraySize(DefineFormatString) + define.size() >= charsLeft) {
						break;
					}
					std::size_t length = formatInto({ &backingStore[lastOffset], charsLeft }, DefineFormatString, define, 1);
					strings[lastIndex++] = { &backingStore[lastOffset], length };
					lastOffset += length + 1;
				}
				strings[lastIndex++] = ResetLineString;
			}
			if (content != nullptr) {
				strings[lastIndex++] = content;
			}
			return lastIndex;
		}
	}

	Shader::Shader()
		: Object(ObjectType::Shader), glShaderProgram_(std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate))
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, DefaultVertex vertex, const char* fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, DefaultVertex vertex, const char* fragment, int batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, introspection, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(const char* shaderName, LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize)
		: Shader()
	{
		const bool hasLoaded = loadMode == LoadMode::String
			? LoadFromMemory(shaderName, vertex, fragment, batchSize)
			: LoadFromFile(shaderName, vertex, fragment, batchSize);

		if (!hasLoaded) {
			LOGE("Shader \"{}\" cannot be loaded", shaderName);
		}
	}

	Shader::Shader(LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize)
		: Shader(nullptr, loadMode, vertex, fragment, batchSize)
	{
	}

	Shader::~Shader()
	{
		RenderResources::UnregisterBatchedShader(glShaderProgram_.get());
	}

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, vertex, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, arrayView(strings, stringsCount), {});

		stringsCount = populateShaderStrings(strings, backingStore, fragment, -1, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_FRAGMENT_SHADER, arrayView(strings, stringsCount), {});

		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromMemory(const char* shaderName, const char* vertex, const char* fragment, std::int32_t batchSize)
	{
		return LoadFromMemory(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromMemory(const char* vertex, const char* fragment, std::int32_t batchSize)
	{
		return LoadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);
		LoadDefaultShader(vertex, batchSize);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, fragment, -1, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_FRAGMENT_SHADER, arrayView(strings, stringsCount), {});

		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, std::int32_t batchSize)
	{
		const Introspection introspection = (isBatchedVertex(vertex) ? Introspection::NoUniformsInBlocks : Introspection::Enabled);
		return LoadFromMemory(shaderName, introspection, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromMemory(DefaultVertex vertex, const char* fragment, std::int32_t batchSize)
	{
		return LoadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, vertex, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, arrayView(strings, stringsCount), {});

		LoadDefaultShader(fragment);
		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, std::int32_t batchSize)
	{
		return LoadFromMemory(shaderName, Introspection::Enabled, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromMemory(const char* vertex, DefaultFragment fragment, std::int32_t batchSize)
	{
		return LoadFromMemory(nullptr, vertex, fragment, batchSize);
	}

	bool Shader::LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, {}, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, arrayView(strings, stringsCount), vertexPath);

		stringsCount = populateShaderStrings(strings, backingStore, {}, -1, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_FRAGMENT_SHADER, arrayView(strings, stringsCount), fragmentPath);

		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromFile(const char* shaderName, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize)
	{
		return LoadFromFile(shaderName, Introspection::Enabled, vertexPath, fragmentPath, batchSize);
	}

	bool Shader::LoadFromFile(StringView vertexPath, StringView fragmentPath, std::int32_t batchSize)
	{
		return LoadFromFile(nullptr, vertexPath, fragmentPath, batchSize);
	}

	bool Shader::LoadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);
		LoadDefaultShader(vertex, batchSize);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, {}, -1, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_FRAGMENT_SHADER, arrayView(strings, stringsCount), fragmentPath);

		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromFile(const char* shaderName, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize)
	{
		const Introspection introspection = (isBatchedVertex(vertex) ? Introspection::NoUniformsInBlocks : Introspection::Enabled);
		return LoadFromFile(shaderName, introspection, vertex, fragmentPath, batchSize);
	}

	bool Shader::LoadFromFile(DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize)
	{
		return LoadFromFile(nullptr, vertex, fragmentPath, batchSize);
	}

	bool Shader::LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, {}, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, arrayView(strings, stringsCount), vertexPath);

		LoadDefaultShader(fragment);
		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromFile(const char* shaderName, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize)
	{
		return LoadFromFile(shaderName, Introspection::Enabled, vertexPath, fragment, batchSize);
	}

	bool Shader::LoadFromFile(StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize)
	{
		return LoadFromFile(nullptr, vertexPath, fragment, batchSize);
	}

	bool Shader::LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset();
		glShaderProgram_->SetObjectLabel(shaderName);
		return RenderResources::GetBinaryShaderCache().LoadFromCache(shaderName, shaderVersion, glShaderProgram_.get(), shaderToShaderProgramIntrospection(introspection));
	}

	bool Shader::SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const
	{
		return RenderResources::GetBinaryShaderCache().SaveToCache(shaderName, shaderVersion, glShaderProgram_.get());
	}

	bool Shader::SetAttribute(const char* name, std::int32_t stride, void* pointer)
	{
		GLVertexFormat::Attribute* attribute = glShaderProgram_->GetAttribute(name);
		if (attribute != nullptr) {
			attribute->SetVboParameters(stride, pointer);
		}
		return (attribute != nullptr);
	}

	bool Shader::IsLinked() const
	{
		return glShaderProgram_->IsLinked();
	}

	unsigned int Shader::RetrieveInfoLogLength() const
	{
		return glShaderProgram_->RetrieveInfoLogLength();
	}

	void Shader::RetrieveInfoLog(std::string& infoLog) const
	{
		glShaderProgram_->RetrieveInfoLog(infoLog);
	}

	bool Shader::GetLogOnErrors() const
	{
		return glShaderProgram_->GetLogOnErrors();
	}

	void Shader::SetLogOnErrors(bool shouldLogOnErrors)
	{
		glShaderProgram_->SetLogOnErrors(shouldLogOnErrors);
	}

	void Shader::SetGLShaderProgramLabel(const char* label)
	{
		glShaderProgram_->SetObjectLabel(label);
	}

	void Shader::RegisterBatchedShader(Shader& batchedShader)
	{
		RenderResources::RegisterBatchedShader(glShaderProgram_.get(), batchedShader.glShaderProgram_.get());
	}

	bool Shader::LoadDefaultShader(DefaultVertex vertex, int batchSize)
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
			std::size_t length = formatInto(sourceString, DefineFormatString, BatchSizeDefine, batchSize);
			StringView vertexStrings[2] = { { sourceString, length }, ResetLineString };
			return glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, vertexShader }));
		} else {
			return glShaderProgram_->AttachShaderFromFile(GL_VERTEX_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, vertexShader }));
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
			std::size_t length = formatInto(sourceString, DefineFormatString, BatchSizeDefine, batchSize);
			StringView vertexStrings[3] = { { sourceString, length }, ResetLineString, vertexShader };
			return glShaderProgram_->AttachShaderFromStringsAndFile(GL_VERTEX_SHADER, vertexStrings, {});
		} else {
			return glShaderProgram_->AttachShaderFromString(GL_VERTEX_SHADER, vertexShader);
		}
#endif
	}

	bool Shader::LoadDefaultShader(DefaultFragment fragment)
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
		return glShaderProgram_->AttachShaderFromFile(GL_FRAGMENT_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, fragmentShader }));
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
		return glShaderProgram_->AttachShaderFromString(GL_FRAGMENT_SHADER, fragmentShader);
#endif
	}
}