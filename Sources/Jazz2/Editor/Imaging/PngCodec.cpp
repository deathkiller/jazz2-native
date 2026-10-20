#include "PngCodec.h"

#if defined(WITH_EDITOR)

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Editor
{
	bool PngCodec::Decode(Stream& s, PngImage& image, String* error)
	{
		PngDecodeOptions options;
		// Images of the editor come from anywhere, a damaged one is told by the checksums before its data is used
		options.VerifyCrc = true;
		options.MaxPixels = MaxPixels;
		PngError result = nCine::PngCodec::Decode(s, image, options);
		if (result == PngError::None) {
			return true;
		}
		if (error != nullptr) {
			*error = GetErrorMessage(result);
		}
		return false;
	}

	bool PngCodec::Load(StringView path, PngImage& image, String* error)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			if (error != nullptr) {
				*error = _("The file can't be read");
			}
			return false;
		}
		return Decode(*s, image, error);
	}

	bool PngCodec::LoadWithMasks(StringView path, PngImage& image, PngImage& masks, bool& hasMasks, String* error)
	{
		hasMasks = false;
		if (!Load(path, image, error)) {
			return false;
		}
		String masksPath = GetMasksPath(path);
		if (fs::IsReadableFile(masksPath)) {
			auto s = fs::Open(masksPath, FileAccess::Read);
			hasMasks = (s->IsValid() && Decode(*s, masks));
		}
		return true;
	}

	String PngCodec::GetMasksPath(StringView path)
	{
		return String(path.exceptSuffix(std::size_t(4))) + ".mask.png"_s;
	}

	bool PngCodec::Encode(Stream& s, const PngImage& image)
	{
		return nCine::PngCodec::Encode(s, image);
	}

	String PngCodec::GetErrorMessage(PngError error)
	{
		switch (error) {
			case PngError::None: return {};
			case PngError::NotPng: return _("It isn't a PNG image");
			case PngError::EndsTooSoon: return _("The image ends too soon");
			case PngError::TooLarge: return _("The image is too large");
			case PngError::UnknownKind: return _("The image is of an unknown kind");
			case PngError::Unsupported: return _("The image uses a feature that isn't supported");
			case PngError::NoPalette: return _("The image has no palette");
			default: return _("The image is damaged");
		}
	}
}

#endif
