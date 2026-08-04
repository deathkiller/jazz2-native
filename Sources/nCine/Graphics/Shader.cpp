#include "Shader.h"
#include "RHI/Rhi.h"
#include "RenderResources.h"
#include "BinaryShaderCache.h"
#include "../Application.h"
#include "../tracy.h"
#include "../../Main.h"
#include "../../Utilities/ShaderCompiler/RuntimeShader.h"
#if defined(RHI_GL_PROFILE_ES2)
#	include "../../Utilities/ShaderCompiler/Essl100.h"
#endif

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
		: Object(ObjectType::Shader), _glShaderProgram(std::make_unique<RHI::ShaderProgram>(RHI::ShaderProgram::QueryPhase::Immediate)), _renderModes(0)
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
		RenderResources::UnregisterBatchedShader(_glShaderProgram.get());
	}

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		_glShaderProgram->Reset(); // reset before attaching new shaders
		_glShaderProgram->SetBatchSize(batchSize);
		_glShaderProgram->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, vertex, batchSize, defines);
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), {});

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, fragment, batchSize, defines);
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), {});

		_glShaderProgram->Link(shaderToShaderProgramIntrospection(introspection));

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

	bool Shader::LoadFromMemory(const char* shaderName, Introspection introspection, const ShaderCompiler::ProgramVariant& variant, std::int32_t batchSize, const char* programName)
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
		// Storage for the on-the-fly lowering of runtime-compiled shaders - kept alive for the whole call
		// because the C-string views below point into it.
		String vsSource100, fsSource100;
		if (variant.VsSource100 != nullptr) {
			vsSource = variant.VsSource100;
		} else if (vsSource != nullptr) {
			// Runtime-compiled (".shader") programs carry no baked ESSL 100 source, so lower it here with the
			// same Essl100Emitter the offline tool runs on the fully-assembled stage source. On decline keep the
			// modern source (the strict ES2 compile then fails non-fatally) and log which shader and why.
			ShaderCompiler::Diagnostic diag;
			if (ShaderCompiler::Essl100Emitter::Transform(vsSource, true, vsSource100, diag)) {
				vsSource = vsSource100.data();
			} else {
				LOGW("ESSL 100 lowering declined for shader \"{}\" vertex stage (line {}): {} - falling back to the modern source", shaderName != nullptr ? shaderName : "(unnamed)", diag.Line, diag.Message.data());
			}
		}
		if (variant.FsSource100 != nullptr) {
			fsSource = variant.FsSource100;
		} else if (fsSource != nullptr) {
			ShaderCompiler::Diagnostic diag;
			if (ShaderCompiler::Essl100Emitter::Transform(fsSource, false, fsSource100, diag)) {
				fsSource = fsSource100.data();
			} else {
				LOGW("ESSL 100 lowering declined for shader \"{}\" fragment stage (line {}): {} - falling back to the modern source", shaderName != nullptr ? shaderName : "(unnamed)", diag.Line, diag.Message.data());
			}
		}
		if (batchSize > 12) {
			batchSize = 8;
		}
#endif

		_glShaderProgram->Reset(); // reset before attaching new shaders
		_glShaderProgram->SetBatchSize(batchSize);
		_glShaderProgram->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, vsSource, batchSize, {});
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), {});

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, fsSource, batchSize, {});
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), {});

		// Set after Reset(), which clears any previous reflection
		_glShaderProgram->SetReflection(&variant);
		// The fixed-function console backends resolve their generated effects from this true
		// (program, variant) identity at link time - never from the object label
		if (programName != nullptr) {
			_glShaderProgram->SetProgramIdentity(programName, variant.Name);
		}
		_glShaderProgram->Link(shaderToShaderProgramIntrospection(introspection));

		return IsLinked();
	}

	bool Shader::LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize, ArrayView<const StringView> defines)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		_glShaderProgram->Reset(); // reset before attaching new shaders
		_glShaderProgram->SetBatchSize(batchSize);
		_glShaderProgram->SetObjectLabel(shaderName);

		StringView strings[MaxShaderStrings]; std::size_t stringsCount; char backingStore[256];

		stringsCount = populateShaderStrings(strings, backingStore, {}, batchSize, defines);
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Vertex, arrayView(strings, stringsCount), vertexPath);

		// The BATCH_SIZE define is baked into both stages - a batched InstancesBlock is declared
		// in the fragment stage too (shared globals), and mismatched block sizes would fail to link
		stringsCount = populateShaderStrings(strings, backingStore, {}, batchSize, defines);
		_glShaderProgram->AttachShaderFromStringsAndFile(ShaderStage::Fragment, arrayView(strings, stringsCount), fragmentPath);

		_glShaderProgram->Link(shaderToShaderProgramIntrospection(introspection));

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
		_renderModes = view.RenderModes;
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

		_glShaderProgram->Reset();
		_glShaderProgram->SetObjectLabel(shaderName);
		return RenderResources::GetBinaryShaderCache().LoadFromCache(shaderName, shaderVersion, _glShaderProgram.get(), shaderToShaderProgramIntrospection(introspection));
	}

	bool Shader::LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection, const ShaderCompiler::ProgramVariant& variant, const char* programName)
	{
		ZoneScopedC(0x81A861);
		if (shaderName != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(shaderName, std::strlen(shaderName));
		}

		_glShaderProgram->Reset();
		_glShaderProgram->SetObjectLabel(shaderName);
		_glShaderProgram->SetReflection(&variant);
		// Identity travels with the reflection so a cache hit resolves console effects the same way
		// a fresh compile does (the binary cache is disabled on the fixed-function tiers today, but
		// the invariant is kept regardless)
		if (programName != nullptr) {
			_glShaderProgram->SetProgramIdentity(programName, variant.Name);
		}
		return RenderResources::GetBinaryShaderCache().LoadFromCache(shaderName, shaderVersion, _glShaderProgram.get(), shaderToShaderProgramIntrospection(introspection));
	}

	bool Shader::SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const
	{
		return RenderResources::GetBinaryShaderCache().SaveToCache(shaderName, shaderVersion, _glShaderProgram.get());
	}

	bool Shader::SetAttribute(const char* name, std::int32_t stride, void* pointer)
	{
		RHI::VertexFormat::Attribute* attribute = _glShaderProgram->GetAttribute(name);
		if (attribute != nullptr) {
			attribute->SetVboParameters(stride, pointer);
		}
		return (attribute != nullptr);
	}

	bool Shader::IsLinked() const
	{
		return _glShaderProgram->IsLinked();
	}

	unsigned int Shader::RetrieveInfoLogLength() const
	{
		return _glShaderProgram->RetrieveInfoLogLength();
	}

	void Shader::RetrieveInfoLog(std::string& infoLog) const
	{
		_glShaderProgram->RetrieveInfoLog(infoLog);
	}

	bool Shader::GetLogOnErrors() const
	{
		return _glShaderProgram->GetLogOnErrors();
	}

	void Shader::SetLogOnErrors(bool shouldLogOnErrors)
	{
		_glShaderProgram->SetLogOnErrors(shouldLogOnErrors);
	}

	void Shader::SetGLShaderProgramLabel(const char* label)
	{
		_glShaderProgram->SetObjectLabel(label);
	}

	void Shader::RegisterBatchedShader(Shader& batchedShader)
	{
		RenderResources::RegisterBatchedShader(_glShaderProgram.get(), batchedShader._glShaderProgram.get());
	}
}
