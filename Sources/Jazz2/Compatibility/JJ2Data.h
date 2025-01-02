#pragma once

#include "../../Main.h"
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
	/** @brief Parses original `.j2d` data files */
	class JJ2Data
	{
	public:
		/** @brief Item from a `.j2d` data file */
		struct Item {
			String Filename;
			std::unique_ptr<uint8_t[]> Blob;
			std::uint32_t Type;
			std::int32_t Size;
		};

		SmallVector<Item, 0> Items;

		JJ2Data() {}

		bool Open(const StringView path, bool strictParser);

		void Extract(const StringView targetPath);
		void Convert(PakWriter& pakWriter, JJ2Version version);

	private:
		void ConvertSfxList(const Item& item, PakWriter& pakWriter, const StringView targetPath, AnimSetMapping& animMapping);
		void ConvertMenuImage(const Item& item, PakWriter& pakWriter, const StringView targetPath, std::int32_t width, std::int32_t height);
	};
}