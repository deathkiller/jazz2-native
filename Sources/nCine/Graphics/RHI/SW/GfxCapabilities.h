#pragma once

#if defined(WITH_RHI_SW)

#include "../../IGfxCapabilities.h"

namespace nCine::RHI
{
	/// Software-renderer graphics capabilities — returns conservative safe defaults
	class GfxCapabilities : public IGfxCapabilities
	{
	public:
		GfxCapabilities();

		std::int32_t GetVersion(Version version) const override;
		const InfoStrings& GetInfoStrings() const override;
		std::int32_t GetValue(IntValues valueName) const override;
		std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const override;
		bool HasExtension(Extensions extensionName) const override;

	private:
		InfoStrings infoStrings_;
	};
}

#endif
