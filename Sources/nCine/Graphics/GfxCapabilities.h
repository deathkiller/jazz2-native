#pragma once

#include "IGfxCapabilities.h"

namespace nCine
{
	/// Stores and retrieves runtime OpenGL device capabilities
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		std::int32_t glVersion(GLVersion version) const override;
		inline const GlInfoStrings& glInfoStrings() const override {
			return glInfoStrings_;
		}
		std::int32_t value(GLIntValues valueName) const override;
		std::int32_t arrayValue(GLArrayIntValues valueName, std::uint32_t index) const override;
		bool hasExtension(GLExtensions extensionName) const override;

	private:
		std::int32_t glMajorVersion_;
		std::int32_t glMinorVersion_;
		/// The OpenGL release version number (not available in OpenGL ES)
		std::int32_t glReleaseVersion_;

		GlInfoStrings glInfoStrings_;

		/// Array of OpenGL integer values
		std::int32_t glIntValues_[(std::int32_t)IGfxCapabilities::GLIntValues::Count];
		/// Array of OpenGL extension availability flags
		bool glExtensions_[(std::int32_t)IGfxCapabilities::GLExtensions::Count];

		static constexpr std::int32_t MaxProgramBinaryFormats = 4;
		std::int32_t programBinaryFormats_[MaxProgramBinaryFormats];

		/// Queries the device about its runtime graphics capabilities
		void init();

		/// Logs OpenGL device info
		void logGLInfo() const;
		/// Logs OpenGL extensions
		void logGLExtensions() const;
		/// Logs OpenGL device capabilites
		void logGLCaps() const;

		/// Checks for OpenGL extensions availability
		void checkGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const;
	};
}
