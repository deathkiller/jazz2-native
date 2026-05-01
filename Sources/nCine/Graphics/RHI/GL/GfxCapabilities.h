#pragma once

#include "../../IGfxCapabilities.h"

#if defined(WITH_RHI_GL)

namespace nCine::RHI
{
	/// Stores and retrieves runtime OpenGL device capabilities
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		std::int32_t GetVersion(Version version) const override;
		inline const InfoStrings& GetInfoStrings() const override {
			return infoStrings_;
		}
		std::int32_t GetValue(IntValues valueName) const override;
		std::int32_t GetArrayValue(ArrayIntValues valueName, std::uint32_t index) const override;
		bool HasExtension(Extensions extensionName) const override;

	private:
		std::int32_t glMajorVersion_;
		std::int32_t glMinorVersion_;
		/// The OpenGL release version number (not available in OpenGL ES)
		std::int32_t glReleaseVersion_;

		InfoStrings infoStrings_;

		/// Array of OpenGL integer values
		std::int32_t glIntValues_[std::int32_t(IGfxCapabilities::IntValues::Count)];
		/// Array of OpenGL extension availability flags
		bool glExtensions_[std::int32_t(IGfxCapabilities::Extensions::Count)];

		static constexpr std::int32_t MaxProgramBinaryFormats = 4;
		std::int32_t programBinaryFormats_[MaxProgramBinaryFormats];

		/// Queries the device about its runtime graphics capabilities
		void Init();

		/// Checks for OpenGL extensions availability
		void CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const;
	};
}

#endif
