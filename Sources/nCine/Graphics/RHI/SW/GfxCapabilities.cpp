#if defined(WITH_RHI_SW)

#include "GfxCapabilities.h"
#include "../../../../Main.h"

#include <Containers/StringView.h>
#include <Cpu.h>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine::RHI
{
	GfxCapabilities::GfxCapabilities()
	{
		infoStrings_.renderer = "Software Renderer";

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH)
		auto features = Cpu::runtimeFeatures();
#else
		auto features = Cpu::compiledFeatures();
#endif

		StringView featureName;
#if defined(DEATH_TARGET_X86)
		if (features & Cpu::Avx2) {
			featureName = "AVX2"_s;
		} else if (features & Cpu::Sse2) {
			featureName = "SSE2"_s;
		} else {
			featureName = "Scalar"_s;
		}
#elif defined(DEATH_TARGET_ARM)
		if (features & Cpu::Neon) {
			featureName = "Neon"_s;
		} else {
			featureName = "Scalar"_s;
		}
#elif defined(DEATH_TARGET_WASM)
		if (features & Cpu::Simd128) {
			featureName = "SIMD128"_s;
		} else {
			featureName = "Scalar"_s;
		}
#else
		featureName = "Scalar"_s;
#endif

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH)
		LOGI("The application is using software renderer ({}+)", featureName);
#else
		LOGI("The application is using software renderer ({})", featureName);
#endif
	}

	std::int32_t GfxCapabilities::GetVersion(Version version) const
	{
		return 0;
	}

	const IGfxCapabilities::InfoStrings& GfxCapabilities::GetInfoStrings() const
	{
		return infoStrings_;
	}

	std::int32_t GfxCapabilities::GetValue(IntValues valueName) const
	{
		switch (valueName) {
			case IntValues::MAX_TEXTURE_SIZE:					return 4096;
			case IntValues::MAX_TEXTURE_IMAGE_UNITS:			return 8;
			case IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT:	return 1;
			case IntValues::MAX_VERTEX_ATTRIB_STRIDE:			return 1024;
			case IntValues::MAX_COLOR_ATTACHMENTS:				return 4;
			default:											return 0;
		}
	}

	std::int32_t GfxCapabilities::GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const
	{
		return 0;
	}

	bool GfxCapabilities::HasExtension(Extensions extensionName) const
	{
		return false;
	}
}

#endif
