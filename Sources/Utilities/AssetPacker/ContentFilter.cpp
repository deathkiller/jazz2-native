#include "ContentFilter.h"

#include <Containers/String.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	bool ContentFilter::IsLeftOut(StringView name, bool isDirectory, bool isTopLevel, bool hasEditor)
	{
		if (isDirectory) {
			return (isTopLevel && (name == StringView(N64TempDirectory) || (!hasEditor && name == "Editor"_s)));
		}
		// The ".po" files beside the translations are the sources the ".mo" the game reads are compiled from, and
		// nothing loads one at run time - both the language list and the About section's translator credits require
		// the ".mo" extension. They are larger than the ".mo" they produce, so a tree that is deployed as it is, onto
		// a memory card or a disc that cannot be rewritten, is better off without them.
		return (StringUtils::lowercase(fs::GetExtension(name)) == "po"_s);
	}
}
