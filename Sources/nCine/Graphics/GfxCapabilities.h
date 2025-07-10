#pragma once

#include "IGfxCapabilities.h"

namespace nCine
{
	/// Stores and retrieves runtime OpenGL device capabilities
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		std::int32_t GetGLVersion(GLVersion version) const override;
		inline const GLInfoStrings& GetGLInfoStrings() const override {
			return glInfoStrings_;
		}
		std::int32_t GetValue(GLIntValues valueName) const override;
		std::int32_t GetArrayValue(GLArrayIntValues valueName, std::uint32_t index) const override;
		bool HasExtension(GLExtensions extensionName) const override;

	private:
		std::int32_t glMajorVersion_;
		std::int32_t glMinorVersion_;
		/// The OpenGL release version number (not available in OpenGL ES)
		std::int32_t glReleaseVersion_;

		GLInfoStrings glInfoStrings_;

		/// Array of OpenGL integer values
		std::int32_t glIntValues_[std::int32_t(IGfxCapabilities::GLIntValues::Count)];
		/// Array of OpenGL extension availability flags
		bool glExtensions_[std::int32_t(IGfxCapabilities::GLExtensions::Count)];

		static constexpr std::int32_t MaxProgramBinaryFormats = 4;
		std::int32_t programBinaryFormats_[MaxProgramBinaryFormats];

		/// Queries the device about its runtime graphics capabilities
		void Init();

		/// Checks for OpenGL extensions availability
		void CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const;
	};
}
