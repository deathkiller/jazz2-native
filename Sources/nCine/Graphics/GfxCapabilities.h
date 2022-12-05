#pragma once

#include "IGfxCapabilities.h"

namespace nCine
{
	/// A class that stores and retrieves runtime OpenGL device capabilities
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		int glVersion(GLVersion version) const override;
		inline const GlInfoStrings& glInfoStrings() const override {
			return glInfoStrings_;
		}
		int value(GLIntValues valueName) const override;
		bool hasExtension(GLExtensions extensionName) const override;

	private:
		int glMajorVersion_;
		int glMinorVersion_;
		/// The OpenGL release version number (not available in OpenGL ES)
		int glReleaseVersion_;

		GlInfoStrings glInfoStrings_;

		/// Array of OpenGL integer values
		int glIntValues_[(int)IGfxCapabilities::GLIntValues::Count];
		/// Array of OpenGL extension availability flags
		bool glExtensions_[(int)IGfxCapabilities::GLExtensions::Count];

		/// Queries the device about its runtime graphics capabilities
		void init();

		/// Logs OpenGL device info
		void logGLInfo() const;
		/// Logs OpenGL extensions
		void logGLExtensions() const;
		/// Logs OpenGL device capabilites
		void logGLCaps() const;

		/// Checks for OpenGL extensions availability
		void checkGLExtensions(const char* extensionNames[], bool results[], unsigned int numExtensionsToCheck) const;
	};

}
