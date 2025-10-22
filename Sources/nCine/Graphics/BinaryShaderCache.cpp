#include "BinaryShaderCache.h"
#include "IGfxCapabilities.h"
#include "../ServiceLocator.h"
#include "../Base/Algorithms.h"
#include "../Base/HashFunctions.h"
#include "../../Main.h"

#include <IO/FileSystem.h>
#include <IO/Stream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

static constexpr std::uint8_t Signature[] = { 0xEF, 0xBB, 0xBF, 0xF0, 0x9F, 0x8C, 0xAA, 0x20 };
static constexpr std::uint16_t Version = 1 | 0x1000;

namespace nCine
{
	namespace
	{
		std::uint32_t bufferSize = 0;
		std::unique_ptr<std::uint8_t[]> bufferPtr;
	}

	BinaryShaderCache::BinaryShaderCache(StringView path)
		: isAvailable_(false), platformHash_(0)
	{
		if (path.empty()) {
			LOGD("Binary shader cache is disabled");
			return;
		}

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		const bool isSupported = gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY) ||
								 gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY);
#else
		const bool isSupported = gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY);
#endif
		if (!isSupported) {
			LOGW("GL_ARB_get_program_binary extensions not supported, binary shader cache is disabled");
			return;
		}

#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX) && (!defined(DEATH_TARGET_WINDOWS_RT) || defined(WITH_ANGLE))
		if (gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY)) {
			_glGetProgramBinary = glGetProgramBinaryOES;
			_glProgramBinary = glProgramBinaryOES;
			_glProgramBinaryLength = GL_PROGRAM_BINARY_LENGTH_OES;
		} else
#endif
		{
			_glGetProgramBinary = glGetProgramBinary;
			_glProgramBinary = glProgramBinary;
			_glProgramBinaryLength = GL_PROGRAM_BINARY_LENGTH;
		}

		const auto& infoStrings = gfxCaps.GetGLInfoStrings();

		platformHash_ =
			xxHash3(infoStrings.renderer, std::strlen(infoStrings.renderer),
				xxHash3(infoStrings.glVersion, std::strlen(infoStrings.glVersion)));

		char platformHashString[24];
		std::size_t platformHashLength = formatInto(platformHashString, "{:.16x}", platformHash_);
		path_ = fs::CombinePath(path, { platformHashString, platformHashLength });
		fs::CreateDirectories(path_);

		bufferSize = 64 * 1024;
		bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);

		const bool pathExists = fs::DirectoryExists(path_);
		isAvailable_ = (isSupported && pathExists);
	}

	String BinaryShaderCache::GetCachedShaderPath(const char* shaderName)
	{
		if (!isAvailable_ || shaderName == nullptr || shaderName[0] == '\0') {
			return {};
		}

		std::uint64_t shaderNameHash = xxHash3(shaderName, std::strlen(shaderName));

		char filename[32];
		std::size_t filenameLength = formatInto(filename, "{:.16x}.shader", shaderNameHash);
		return fs::CombinePath(path_, { filename, filenameLength });
	}

	bool BinaryShaderCache::LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection)
	{
		String cachePath = GetCachedShaderPath(shaderName);
		if (cachePath.empty()) {
			return false;
		}

		std::unique_ptr<Stream> s = fs::Open(cachePath, FileAccess::Read);
		std::int64_t fileSize = s->GetSize();
		if (fileSize <= 20 || fileSize > 16 * 1024 * 1024) {
			if (s->IsValid()) {
				LOGW("Invalid .shader file");
			}
			return false;
		}

		std::uint8_t signature[sizeof(Signature)];
		s->Read(signature, sizeof(signature));
		if (std::memcmp(signature, Signature, sizeof(Signature)) != 0) {
			LOGW("Invalid .shader file");
			return false;
		}

		std::uint16_t fileVersion = s->ReadValueAsLE<std::uint16_t>();
		if (fileVersion != Version) {
			LOGW("Unsupported .shader file version");
			return false;
		}

		std::uint64_t fileShaderVersion = s->ReadVariableUint64();
		// Shader version must be the same
		if (fileShaderVersion != shaderVersion) {
			return false;
		}

		std::uint32_t fileBinaryFormat = s->ReadVariableUint32();
		std::uint32_t fileBatchSize = s->ReadVariableUint32();
		std::uint32_t fileShaderLength = s->ReadVariableUint32();
		DEATH_ASSERT(fileShaderLength > 0, "Invalid .shader file", false);

		if (bufferSize < fileShaderLength) {
			bufferSize = fileShaderLength;
			bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);
		}

		DeflateStream ds(*s);
		std::int64_t bytesRead = ds.Read(bufferPtr.get(), std::int64_t(fileShaderLength));
		DEATH_ASSERT(bytesRead == fileShaderLength, "Failed to read binary shader data", false);

		_glProgramBinary(program->GetGLHandle(), fileBinaryFormat, bufferPtr.get(), std::int64_t(fileShaderLength));
		program->SetBatchSize(fileBatchSize);
		return program->FinalizeAfterLinking(introspection);
	}

	bool BinaryShaderCache::SaveToCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program)
	{
		String cachePath = GetCachedShaderPath(shaderName);
		if (cachePath.empty()) {
			return false;
		}

		GLint shaderLength = 0;
		glGetProgramiv(program->GetGLHandle(), _glProgramBinaryLength, &shaderLength);
		if (shaderLength <= 0) {
			return false;
		}

		if (bufferSize < std::uint32_t(shaderLength)) {
			bufferSize = std::uint32_t(shaderLength);
			bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);
		} 

		shaderLength = 0;
		unsigned int binaryFormat = 0;
		_glGetProgramBinary(program->GetGLHandle(), bufferSize, &shaderLength, &binaryFormat, bufferPtr.get());
		if (shaderLength <= 0 || std::uint32_t(shaderLength) > bufferSize) {
			return false;
		}

		std::unique_ptr<Stream> s = fs::Open(cachePath, FileAccess::Write);
		if (!s->IsValid()) {
			return false;
		}

		s->Write(Signature, sizeof(Signature));
		s->WriteValueAsLE<std::uint16_t>(Version);
		s->WriteVariableUint64(shaderVersion);
		s->WriteVariableUint32(binaryFormat);
		s->WriteVariableUint32(program->GetBatchSize());
		s->WriteVariableUint32(shaderLength);

		DeflateWriter dw(*s);
		dw.Write(bufferPtr.get(), shaderLength);

		return true;
	}

	std::uint32_t BinaryShaderCache::Prune()
	{
		auto platformHashString = fs::GetFileName(path_);

		std::uint32_t directoriesRemoved = 0;
		for (auto shaderPath : fs::Directory(fs::GetDirectoryName(path_))) {
			if (fs::DirectoryExists(shaderPath)) {
				StringView filename = fs::GetFileName(shaderPath);
				if (filename != platformHashString) {
					fs::RemoveDirectoryRecursive(shaderPath);
					directoriesRemoved++;
				}
			} else if (fs::GetExtension(shaderPath) == "shader"_s) {
				// Remove stray .shader files that are not in a platform directory
				fs::RemoveFile(shaderPath);
			}
		}

		return directoriesRemoved;
	}

	bool BinaryShaderCache::Clear()
	{
		bool success = fs::RemoveDirectoryRecursive(path_);
		fs::CreateDirectories(path_);
		return success;
	}

	bool BinaryShaderCache::SetPath(StringView path)
	{
		if (!fs::DirectoryExists(path) || !fs::IsWritable(path)) {
			return false;
		}

		path_ = path;
		return true;
	}
}