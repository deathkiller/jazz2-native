#include "Shader.h"
#include "RHI/Rhi.h"
#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "../Application.h"
#include "../tracy.h"
#include "../../Main.h"
#include "../../ShaderCompiler/RuntimeShader.h"

#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	namespace
	{
		static const char BatchSizeDefine[] = "BATCH_SIZE";
		static const char DefineFormatString[] = "#define {} ({})\n";
		static const char ResetLineString[] = "#line 0\n";
		static const std::int32_t MaxShaderStrings = 8;

		RHI::ShaderProgram::Introspection shaderToShaderProgramIntrospection(Shader::Introspection introspection)
		{
			switch (introspection) {
				default:
				case Shader::Introspection::Enabled:
					return RHI::ShaderProgram::Introspection::Enabled;
				case Shader::Introspection::NoUniformsInBlocks:
					return RHI::ShaderProgram::Introspection::NoUniformsInBlocks;
				case Shader::Introspection::Disabled:
					return RHI::ShaderProgram::Introspection::Disabled;
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
		: Object(ObjectType::Shader), glShaderProgram_(std::make_unique<RHI::ShaderProgram>(RHI::ShaderProgram::QueryPhase::Immediate)), renderModes_(0)
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
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), {});

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, fragment, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), {});

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

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, const ShaderCompiler::ProgramVariant& variant, std::int32_t batchSize)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		// OpenGL|ES 2.0 profile consumes the ESSL 100 (Essl100Emitter) stage sources baked alongside the
		// modern ones; a batched InstancesBlock becomes a small "uniform Instance instances[N];" array that
		// must fit in the ES2 vertex uniform space, so cap the batch. The GL 3.3 / ES 3.0 path is unchanged.
		const char* vsSource = variant.VsSource;
		const char* fsSource = variant.FsSource;
#if defined(RHI_GL_PROFILE_ES2)
		if (variant.VsSource100 != nullptr) {
			vsSource = variant.VsSource100;
		}
		if (variant.FsSource100 != nullptr) {
			fsSource = variant.FsSource100;
		}
		if (batchSize > 12) {
			batchSize = 8;
		}
#endif

		glShaderProgram_->Reset(); // reset before attaching new shaders
		glShaderProgram_->SetBatchSize(batchSize);
		glShaderProgram_->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, vsSource, batchSize, {});
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), {});

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, fsSource, batchSize, {});
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), {});

		// Set after Reset(), which clears any previous reflection
		glShaderProgram_->SetReflection(&variant);
		glShaderProgram_->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
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
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), vertexPath);

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, {}, batchSize, defines);
		glShaderProgram_->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), fragmentPath);

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

	bool Shader::LoadFromShaderFile(const char* shaderName, Introspection introspection, StringView path, const char* variantName, std::int32_t batchSize)
	{
		ShaderCompiler::RuntimeProgram program;
		if (!CompileShaderFile(path, program)) {
			return false;
		}

		// nullptr (or an empty name) resolves to the unnamed base variant (Variants[0])
		const char* resolvedVariantName = (variantName != nullptr ? variantName : "");
		const ShaderCompiler::RuntimeVariant* variant = program.FindVariant(resolvedVariantName);
		if (variant == nullptr) {
			LOGE("Shader file \"{}\" has no variant \"{}\"", path, resolvedVariantName[0] != '\0' ? resolvedVariantName : "(base)");
			return false;
		}

		// The artifact view exposes the same reflection the offline tool emits, so runtime-compiled
		// shaders also skip GL introspection (the view only has to live for the duration of this call)
		const ShaderCompiler::Program& view = program.GetView();
		const ShaderCompiler::ProgramVariant& variantView = view.Variants[variant - program.Variants.data()];
		renderModes_ = view.RenderModes;
		return LoadFromMemory(shaderName, introspection, variantView, batchSize);
	}

	bool Shader::CompileShaderFile(StringView path, ShaderCompiler::RuntimeProgram& program)
	{
		ShaderCompiler::FileReader reader = [](StringView filePath, String& content) {
			std::unique_ptr<Stream> fileHandle = fs::Open(filePath, FileAccess::Read);
			if (!fileHandle->IsValid()) {
				return false;
			}
			std::int64_t fileSize = fileHandle->GetSize();
			content = String{NoInit, static_cast<std::size_t>(fileSize)};
			if (fileSize > 0) {
				fileHandle->Read(content.data(), fileSize);
			}
			return true;
		};

		String content;
		if (!reader(path, content)) {
			LOGE("Cannot read shader file \"{}\"", path);
			return false;
		}

		ShaderCompiler::Diagnostic diag;
		if (!ShaderCompiler::CompileRuntimeProgram(content, ShaderCompiler::ShaderParser::DirectoryOf(path), reader, program, diag)) {
			LOGE("Cannot compile shader file \"{}\" (line {}): {}", path, diag.Line, diag.Message.data());
			return false;
		}
		return true;
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

	bool Shader::LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection, const ShaderCompiler::ProgramVariant& variant)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		glShaderProgram_->Reset();
		glShaderProgram_->SetObjectLabel(shaderName);
		glShaderProgram_->SetReflection(&variant);
		return RenderResources::GetBinaryShaderCache().LoadFromCache(shaderName, shaderVersion, glShaderProgram_.get(), shaderToShaderProgramIntrospection(introspection));
	}

	bool Shader::SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const
	{
		return RenderResources::GetBinaryShaderCache().SaveToCache(shaderName, shaderVersion, glShaderProgram_.get());
	}

	bool Shader::SetAttribute(const char* name, std::int32_t stride, void* pointer)
	{
		RHI::VertexFormat::Attribute* attribute = glShaderProgram_->GetAttribute(name);
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
}
