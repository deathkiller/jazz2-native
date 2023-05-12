#pragma once

#include "../../Common.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	class JJ2Data // .j2d
	{
	public:
		struct Item {
			String Filename;
			uint32_t Type;
			std::unique_ptr<uint8_t[]> Blob;
			int32_t Size;
		};

		JJ2Data() { }

		bool Open(const StringView& path, bool strictParser);

		void Extract(const String& targetPath);

		SmallVector<Item, 0> Items;
	};
}