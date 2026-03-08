#pragma once

#include "../../Main.h"

namespace nCine
{
	/// Interface to query runtime graphics device capabilities
	class IGfxCapabilities
	{
	public:
		/// Graphics API version components
		enum class Version
		{
			Major,
			Minor,
			Release
		};

		/// Graphics device information strings
		struct InfoStrings
		{
			const char* vendor = nullptr;
			const char* renderer = nullptr;
			const char* glVersion = nullptr;
			const char* glslVersion = nullptr;
		};

		/// Queryable runtime integer capability values
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

		/// Queryable runtime integer array capability values
		enum class ArrayIntValues
		{
			PROGRAM_BINARY_FORMATS = 0,

			Count
		};

		/// Queryable device extensions
		enum class Extensions
		{
			KHR_DEBUG = 0,
			ARB_TEXTURE_STORAGE,
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

		/// Returns the graphics API version numbers
		virtual std::int32_t GetVersion(Version version) const = 0;
		/// Returns the device information strings structure
		virtual const InfoStrings& GetInfoStrings() const = 0;
		/// Returns the value of a runtime integer capability
		virtual std::int32_t GetValue(IntValues valueName) const = 0;
		/// Returns the value of a runtime integer capability from an array
		virtual std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const = 0;
		/// Returns true if the specified device extension is available
		virtual bool HasExtension(Extensions extensionName) const = 0;
	};

	inline IGfxCapabilities::~IGfxCapabilities() {}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/// A fake graphics capabilities class that reports no available capabilities
	class NullGfxCapabilities : public IGfxCapabilities
	{
	public:
		inline std::int32_t GetVersion(Version version) const override {
			return 0;
		}
		inline const InfoStrings& GetInfoStrings() const override {
			return infoStrings_;
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
		InfoStrings infoStrings_;
	};

	/// Software-renderer graphics capabilities — returns conservative safe defaults
	class SWGfxCapabilities : public IGfxCapabilities
	{
	public:
		SWGfxCapabilities()
		{
			infoStrings_.vendor = "Software";
			infoStrings_.renderer = "Software Renderer";
			infoStrings_.glVersion = "SW 1.0";
			infoStrings_.glslVersion = nullptr;
		}

		inline std::int32_t GetVersion(Version version) const override {
			return 0;
		}
		inline const InfoStrings& GetInfoStrings() const override {
			return infoStrings_;
		}
		inline std::int32_t GetValue(IntValues valueName) const override {
			switch (valueName) {
				case IntValues::MAX_TEXTURE_SIZE:                return 4096;
				case IntValues::MAX_TEXTURE_IMAGE_UNITS:         return 8;
				case IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT: return 1;
				case IntValues::MAX_VERTEX_ATTRIB_STRIDE:        return 1024;
				case IntValues::MAX_COLOR_ATTACHMENTS:           return 4;
				default:                                         return 0;
			}
		}
		inline std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const override {
			return 0;
		}
		inline bool HasExtension(Extensions extensionName) const override {
			return false;
		}

	private:
		InfoStrings infoStrings_;
	};
#endif
}
