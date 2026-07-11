#pragma once

#include "../../Main.h"

namespace nCine
{
	/**
		@brief Interface to query runtime OpenGL device capabilities
		
		Abstracts access to the version numbers, information strings, integer limits and extension availability
		flags of the active OpenGL context, so renderer code can adapt to the device without issuing driver
		queries directly. @ref GfxCapabilities is the concrete implementation.
	*/
	class IGfxCapabilities
	{
	public:
		/** @brief OpenGL version component */
		enum class ApiVersion
		{
			Major,
			Minor,
			Release
		};

		/** @brief OpenGL information strings */
		struct InfoStrings
		{
			const char* vendor = nullptr;
			const char* renderer = nullptr;
			const char* apiVersion = nullptr;
			const char* shadingLanguageVersion = nullptr;
		};

		/** @brief OpenGL queryable runtime integer value */
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

		/** @brief OpenGL queryable runtime integer array value */
		enum class ArrayIntValues
		{
			PROGRAM_BINARY_FORMATS = 0,

			Count
		};

		/** @brief OpenGL queryable extension */
		enum class Extensions
		{
			KHR_DEBUG = 0,
			ARB_TEXTURE_STORAGE,
			ARB_BUFFER_STORAGE,
			ARB_GET_PROGRAM_BINARY,
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			OES_GET_PROGRAM_BINARY,
#endif
			EXT_TEXTURE_COMPRESSION_S3TC,
			AMD_COMPRESSED_ATC_TEXTURE,
			IMG_TEXTURE_COMPRESSION_PVRTC,
			KHR_TEXTURE_COMPRESSION_ASTC_LDR,
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
			OES_COMPRESSED_ETC1_RGB8_TEXTURE,
#endif
			Count
		};

		virtual ~IGfxCapabilities() = 0;

		/** @brief Returns the specified OpenGL version component */
		virtual std::int32_t GetApiVersion(ApiVersion version) const = 0;
		/** @brief Returns the OpenGL information strings */
		virtual const InfoStrings& GetInfoStrings() const = 0;
		/** @brief Returns a runtime OpenGL integer value */
		virtual std::int32_t GetValue(IntValues valueName) const = 0;
		/** @brief Returns an element of a runtime OpenGL integer array value */
		virtual std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const = 0;
		/** @brief Returns `true` if the specified OpenGL extension is available */
		virtual bool HasExtension(Extensions extensionName) const = 0;
	};

	inline IGfxCapabilities::~IGfxCapabilities() {}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@brief Fake graphics capabilities that reports no available capabilities
		
		Null implementation of @ref IGfxCapabilities used when no OpenGL context is present (for example in
		headless or server builds); every query returns zero, an empty set of strings or no extension support.
	*/
	class NullGfxCapabilities : public IGfxCapabilities
	{
	public:
		inline std::int32_t GetApiVersion(ApiVersion version) const override {
			return 0;
		}
		inline const InfoStrings& GetInfoStrings() const override {
			return glInfoStrings_;
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
		InfoStrings glInfoStrings_;
	};
#endif
}
