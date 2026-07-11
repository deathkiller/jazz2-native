#pragma once

#include "IGfxCapabilities.h"

namespace nCine
{
	/**
		@brief Stores and retrieves runtime OpenGL device capabilities
		
		Concrete @ref IGfxCapabilities implementation that queries the active OpenGL context once at startup and
		caches the version numbers, information strings, integer limits and extension availability flags so that
		the rest of the renderer can look them up without further driver calls.
	*/
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		std::int32_t GetApiVersion(ApiVersion version) const override;
		inline const InfoStrings& GetInfoStrings() const override {
			return glInfoStrings_;
		}
		std::int32_t GetValue(IntValues valueName) const override;
		std::int32_t GetArrayValue(ArrayIntValues valueName, std::uint32_t index) const override;
		bool HasExtension(Extensions extensionName) const override;

	private:
		std::int32_t glMajorVersion_;
		std::int32_t glMinorVersion_;
		/** @brief OpenGL release version number (not available in OpenGL ES) */
		std::int32_t glReleaseVersion_;

		InfoStrings glInfoStrings_;

		/** @brief Cached values of the queryable OpenGL integer limits */
		std::int32_t glIntValues_[std::int32_t(IGfxCapabilities::IntValues::Count)];
		/** @brief Cached availability flags of the queryable OpenGL extensions */
		bool glExtensions_[std::int32_t(IGfxCapabilities::Extensions::Count)];

		static constexpr std::int32_t MaxProgramBinaryFormats = 4;
		std::int32_t programBinaryFormats_[MaxProgramBinaryFormats];

		/** @brief Queries the device about its runtime graphics capabilities and caches the results */
		void Init();

		/** @brief Checks availability of the specified OpenGL extensions */
		void CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const;
	};
}
