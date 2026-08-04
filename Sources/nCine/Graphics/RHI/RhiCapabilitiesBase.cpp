#include "RhiCapabilitiesBase.h"
#include "RhiFwd.h"		// for the RHI_CAP_* flags of the selected backend

namespace nCine::RHI
{
#if defined(DEATH_TRACE)
	namespace
	{
		/** @brief Whether an information string carries nothing worth logging (a driver query can also fail and return `nullptr`) */
		inline bool IsInfoStringEmpty(const char* value)
		{
			return (value == nullptr || value[0] == '\0');
		}
	}
#endif

	RhiCapabilitiesBase::RhiCapabilitiesBase()
		: majorVersion_(0), minorVersion_(0), releaseVersion_(0)
	{
		for (std::int32_t i = 0; i < (std::int32_t)IntValues::Count; i++) {
			intValues_[i] = 0;
		}
		for (std::int32_t i = 0; i < (std::int32_t)Extensions::Count; i++) {
			extensions_[i] = false;
		}
		for (std::int32_t i = 0; i < MaxProgramBinaryFormats; i++) {
			programBinaryFormats_[i] = -1;
		}
	}

	std::int32_t RhiCapabilitiesBase::GetApiVersion(IRhiCapabilities::ApiVersion version) const
	{
		switch (version) {
			case ApiVersion::Major: return majorVersion_;
			case ApiVersion::Minor: return minorVersion_;
			case ApiVersion::Release: return releaseVersion_;
			default: return 0;
		}
	}

	std::int32_t RhiCapabilitiesBase::GetValue(IntValues valueName) const
	{
		std::int32_t value = 0;
		if (valueName >= (IntValues)0 && valueName < IntValues::Count) {
			value = intValues_[(std::int32_t)valueName];
		}
		return value;
	}

	std::int32_t RhiCapabilitiesBase::GetArrayValue(ArrayIntValues valueName, std::uint32_t index) const
	{
		std::int32_t value = 0;
		if (valueName == ArrayIntValues::PROGRAM_BINARY_FORMATS && index < std::uint32_t(intValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS])) {
			value = programBinaryFormats_[index];
		}
		return value;
	}

	bool RhiCapabilitiesBase::HasExtension(Extensions extensionName) const
	{
		bool extensionAvailable = false;
		if (extensionName >= (Extensions)0 && extensionName < Extensions::Count) {
			extensionAvailable = extensions_[(std::int32_t)extensionName];
		}
		return extensionAvailable;
	}

	void RhiCapabilitiesBase::SetDeviceCapabilities(const char* renderer, std::int32_t maxTextureSize, std::int32_t maxTextureImageUnits,
		std::int32_t maxUniformBlockSize, std::int32_t uniformBufferOffsetAlignment, std::int32_t maxColorAttachments)
	{
		majorVersion_ = 3;
		minorVersion_ = 3;
		releaseVersion_ = 0;
		infoStrings_.vendor = nullptr;
		infoStrings_.renderer = renderer;
		infoStrings_.apiVersion = "3.3";
		infoStrings_.shadingLanguageVersion = nullptr;

		intValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE] = maxTextureSize;
		intValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS] = maxTextureImageUnits;
		intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = maxUniformBlockSize;
		intValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = uniformBufferOffsetAlignment;
		intValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = maxColorAttachments;

		intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS] = 8;
		intValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS] = 8;
		intValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS] = 8;
		intValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE] = 2048;
		intValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] = 0;

		NormalizeUniformBlockSize();
	}

	void RhiCapabilitiesBase::NormalizeUniformBlockSize()
	{
		std::int32_t normalized = intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE];
		if (normalized > 64 * 1024) {
			normalized = 64 * 1024;
		} else if (normalized <= 0) {
			normalized = 16 * 1024;
		}
		intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = normalized;
	}

	void RhiCapabilitiesBase::LogCapabilities() const
	{
#if defined(DEATH_TRACE)
		LOGI("--- Rendering device info ---");
		if (!IsInfoStringEmpty(infoStrings_.vendor)) {
			LOGI("Vendor: {}", infoStrings_.vendor);
		}
		if (!IsInfoStringEmpty(infoStrings_.renderer)) {
			LOGI("Renderer: {}", infoStrings_.renderer);
		}
		if (!IsInfoStringEmpty(infoStrings_.apiVersion)) {
			LOGI("API version: {}", infoStrings_.apiVersion);
		}
		if (!IsInfoStringEmpty(infoStrings_.shadingLanguageVersion)) {
			LOGI("Shading language version: {}", infoStrings_.shadingLanguageVersion);
		}

		// The tier the pipeline runs on this backend, which is what most of the guarded rendering code keys on
		LOGI("Rendering tier: {}{}{}{}",
#	if defined(RHI_CAP_POSTPROCESSING)
			"post-processing (shaders, render targets)",
#	elif defined(RHI_CAP_SHADERS)
			"direct (shaders, no render targets)",
#	elif defined(RHI_CAP_FRAMEBUFFERS)
			"direct (render targets, no shaders)",
#	else
			"direct (no shaders, no render targets)",
#	endif
#	if defined(RHI_CAP_PALETTED_TEXTURES)
			", paletted textures",
#	else
			"",
#	endif
#	if defined(RHI_CAP_STREAMING_TEXTURES)
			", streaming textures",
#	else
			"",
#	endif
#	if defined(RHI_USE_FB16)
			", 16-bit framebuffer");
#	else
			"");
#	endif

		LOGI("--- Rendering device capabilities ---");
		LOGI("Max texture size: {}", intValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE]);
		LOGI("Max texture image units: {}", intValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS]);
		LOGI("Max color attachments: {}", intValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);
		// Not queryable on every target (ES 3.0 and below, WebGL, Apple ARM), where it stays 0
		if (intValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE] > 0) {
			LOGI("Max vertex attribute stride: {}", intValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
		}

		const std::int32_t blockSize = intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE];
		const std::int32_t blockSizeUsed = intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED];
		if (blockSize != blockSizeUsed) {
			LOGI("Max uniform block size: {} ({} used)", blockSize, blockSizeUsed);
		} else {
			LOGI("Max uniform block size: {}", blockSize);
		}
		if (intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS] > 0) {
			LOGI("Max uniform buffer bindings: {} (vertex {}, fragment {}), offset alignment: {}",
				intValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS],
				intValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS],
				intValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS],
				intValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
		} else {
			LOGI("Uniform buffer objects: not available, uniforms are set individually");
		}
		LOGI("---");
#endif
	}
}
