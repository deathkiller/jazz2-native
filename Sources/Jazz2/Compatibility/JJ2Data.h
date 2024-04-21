#pragma once

#include "../../Common.h"
#include "AnimSetMapping.h"
#include "JJ2Version.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/PakFile.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	class JJ2Data // .j2d
	{
	public:
		struct Item {
			String Filename;
			std::uint32_t Type;
			std::unique_ptr<uint8_t[]> Blob;
			std::int32_t Size;
		};

		SmallVector<Item, 0> Items;

		JJ2Data() { }

		bool Open(const StringView path, bool strictParser);

		void Extract(const StringView targetPath);
		void Convert(PakWriter& pakWriter, JJ2Version version);

	private:
		void ConvertSfxList(const Item& item, PakWriter& pakWriter, const StringView targetPath, AnimSetMapping& animMapping);
		void ConvertMenuImage(const Item& item, PakWriter& pakWriter, const StringView targetPath, std::int32_t width, std::int32_t height);
	};
}