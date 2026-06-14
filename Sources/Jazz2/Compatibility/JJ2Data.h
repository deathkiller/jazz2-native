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
	/**
		@brief Parses original `.j2d` data files
		
		Opens an original game data archive (`.j2d`), exposing its embedded items, and either extracts
		them verbatim or converts the recognized ones (such as the SFX list and menu images) into the
		engine's `.pak` format.
	*/
	class JJ2Data
	{
	public:
		/** @brief Item from a `.j2d` data file */
		struct Item {
			/** @brief File name of the item */
			String Filename;
			/** @brief Raw item data */
			std::unique_ptr<uint8_t[]> Blob;
			/** @brief Item type */
			std::uint32_t Type;
			/** @brief Size of the item data in bytes */
			std::int32_t Size;
		};

		/** @brief List of parsed items */
		SmallVector<Item, 0> Items;

		/** @brief Creates a new instance */
		JJ2Data() {}

		/** @brief Opens and parses the specified `.j2d` data file */
		bool Open(StringView path, bool strictParser);

		/** @brief Extracts all items to the specified target path */
		void Extract(StringView targetPath);
		/** @brief Converts the data file and writes the result to a `.pak` file */
		void Convert(PakWriter& pakWriter, JJ2Version version);

	private:
		void ConvertSfxList(const Item& item, PakWriter& pakWriter, StringView targetPath, AnimSetMapping& animMapping);
		void ConvertMenuImage(const Item& item, PakWriter& pakWriter, StringView targetPath, std::int32_t width, std::int32_t height);
	};
}