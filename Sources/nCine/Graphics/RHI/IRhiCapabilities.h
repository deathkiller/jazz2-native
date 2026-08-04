#pragma once

#include "../../../Main.h"

namespace nCine::RHI
{
	/**
		@brief Interface to query the runtime capabilities of the selected RHI backend

		Abstracts access to the version numbers, information strings, integer limits and extension availability
		flags of the active rendering device, so renderer code can adapt to it without issuing backend queries
		directly. The value and extension names are the OpenGL ones the render pipeline has always read; a
		backend that has no OpenGL context publishes the equivalent limit of its own API under the same name.
		@ref RhiCapabilitiesBase is the shared implementation every backend derives from, aliased as
		@ref RHI::Capabilities for the backend selected at compile time.
	*/
	class IRhiCapabilities
	{
	public:
		/** @brief API version component */
		enum class ApiVersion
		{
			Major,
			Minor,
			Release
		};

		/** @brief Device information strings */
		struct InfoStrings
		{
			const char* vendor = nullptr;
			const char* renderer = nullptr;
			const char* apiVersion = nullptr;
			const char* shadingLanguageVersion = nullptr;
		};

		/** @brief Queryable runtime integer value */
		enum class IntValues
		{
			MAX_TEXTURE_SIZE = 0,
			MAX_TEXTURE_IMAGE_UNITS,
			MAX_UNIFORM_BLOCK_SIZE,
			MAX_UNIFORM_BLOCK_SIZE_NORMALIZED,
			MAX_UNIFORM_BUFFER_BINDINGS,
			MAX_VERTEX_UNIFORM_BLOCKS,
			MAX_FRAGMENT_UNIFORM_BLOCKS,
			UNIFORM_BUFFER_OFFSET_ALIGNMENT,
			MAX_VERTEX_ATTRIB_STRIDE,
			MAX_COLOR_ATTACHMENTS,
			NUM_PROGRAM_BINARY_FORMATS,

			Count
		};

		/** @brief Queryable runtime integer array value */
		enum class ArrayIntValues
		{
			PROGRAM_BINARY_FORMATS = 0,

			Count
		};

		/** @brief Queryable OpenGL extension, only ever available with the OpenGL family backend */
		enum class Extensions
		{
			KHR_DEBUG = 0,
			ARB_TEXTURE_STORAGE,
			ARB_BUFFER_STORAGE,
			ARB_GET_PROGRAM_BINARY,
#if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			OES_GET_PROGRAM_BINARY,
#endif
			EXT_TEXTURE_COMPRESSION_S3TC,
			AMD_COMPRESSED_ATC_TEXTURE,
			IMG_TEXTURE_COMPRESSION_PVRTC,
			KHR_TEXTURE_COMPRESSION_ASTC_LDR,
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			OES_COMPRESSED_ETC1_RGB8_TEXTURE,
#endif
			Count
		};

		virtual ~IRhiCapabilities() = 0;

		/** @brief Returns the specified API version component */
		virtual std::int32_t GetApiVersion(ApiVersion version) const = 0;
		/** @brief Returns the device information strings */
		virtual const InfoStrings& GetInfoStrings() const = 0;
		/** @brief Returns a runtime integer value */
		virtual std::int32_t GetValue(IntValues valueName) const = 0;
		/** @brief Returns an element of a runtime integer array value */
		virtual std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const = 0;
		/** @brief Returns `true` if the specified OpenGL extension is available */
		virtual bool HasExtension(Extensions extensionName) const = 0;
	};

	inline IRhiCapabilities::~IRhiCapabilities() {}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@brief Fake capabilities that report no available capabilities

		Null implementation of @ref IRhiCapabilities used when no rendering device is present (for example in
		headless or server builds); every query returns zero, an empty set of strings or no extension support.
	*/
	class NullRhiCapabilities : public IRhiCapabilities
	{
	public:
		inline std::int32_t GetApiVersion(ApiVersion version) const override {
			return 0;
		}
		inline const InfoStrings& GetInfoStrings() const override {
			return _infoStrings;
		}
		inline std::int32_t GetValue(IntValues valueName) const override {
			return 0;
		}
		inline std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const override {
			return 0;
		}
		inline bool HasExtension(Extensions extensionName) const override {
			return false;
		}

	private:
		InfoStrings _infoStrings;
	};
#endif
}
