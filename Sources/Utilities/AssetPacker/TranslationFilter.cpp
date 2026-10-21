#include "TranslationFilter.h"

#include <algorithm>
#include <cstring>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Signature of a `.mo` file, as it reads in the byte order the file was written in */
		constexpr std::uint32_t MoSignature = 0x950412de;
		/** @brief The same signature in the other byte order */
		constexpr std::uint32_t MoSignatureSwapped = 0xde120495;
		/** @brief Size of the header, seven 32-bit words */
		constexpr std::uint32_t MoHeaderSize = 28;
		/** @brief Character between the context and the text of an original, see @ref I18n::ContextSeparator */
		constexpr char ContextSeparator = 0x04;

		/** @brief One original text and its translation, both as they are in the file */
		struct MoEntry
		{
			StringView Original;
			StringView Translation;
		};

		std::uint32_t ReadU32(const std::uint8_t* data, bool bigEndian)
		{
			return (bigEndian
				? (std::uint32_t(data[0]) << 24) | (std::uint32_t(data[1]) << 16) | (std::uint32_t(data[2]) << 8) | std::uint32_t(data[3])
				: (std::uint32_t(data[3]) << 24) | (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[1]) << 8) | std::uint32_t(data[0]));
		}

		void WriteU32(std::uint8_t* data, std::uint32_t value)
		{
			data[0] = std::uint8_t(value);
			data[1] = std::uint8_t(value >> 8);
			data[2] = std::uint8_t(value >> 16);
			data[3] = std::uint8_t(value >> 24);
		}

		/** @brief Hash of an original text up to its first null, the one the game looks it up by (see @ref I18n::LookupTranslation()) */
		std::uint32_t HashString(StringView text)
		{
			std::uint32_t hash = 0;
			for (char c : text) {
				if (c == '\0') {
					break;
				}
				hash = (hash << 4) + std::uint8_t(c);
				std::uint32_t g = hash & 0xF0000000u;
				if (g != 0) {
					hash ^= g >> 24;
					hash ^= g;
				}
			}
			return hash;
		}

		bool IsPrime(std::uint32_t value)
		{
			if (value < 2) {
				return false;
			}
			for (std::uint32_t divisor = 2; divisor * divisor <= value; divisor++) {
				if (value % divisor == 0) {
					return false;
				}
			}
			return true;
		}

		/** @brief Size of the hash table for the specified number of texts, the one `msgfmt` picks */
		std::uint32_t GetHashTableSize(std::uint32_t count)
		{
			std::uint32_t size = std::max<std::uint32_t>((count * 4) / 3, 3);
			while (!IsPrime(size)) {
				size++;
			}
			return size;
		}
	}

	std::int32_t TranslationFilter::RemoveContext(StringView path, StringView context)
	{
		Array<std::uint8_t> file;
		{
			auto s = fs::Open(path, FileAccess::Read);
			const std::int64_t size = (s->IsValid() ? s->GetSize() : -1);
			if (size < MoHeaderSize || size > 16 * 1024 * 1024) {
				return -1;
			}
			file = Array<std::uint8_t>{NoInit, std::size_t(size)};
			if (s->Read(file.data(), size) != size) {
				return -1;
			}
		}

		const std::uint32_t signature = ReadU32(file.data(), false);
		if (signature != MoSignature && signature != MoSignatureSwapped) {
			return -1;
		}
		const bool bigEndian = (signature == MoSignatureSwapped);
		const std::uint32_t count = ReadU32(&file[8], bigEndian);
		const std::uint32_t origTableOffset = ReadU32(&file[12], bigEndian);
		const std::uint32_t transTableOffset = ReadU32(&file[16], bigEndian);
		const std::size_t fileSize = file.size();
		if (count == 0 || origTableOffset > fileSize || transTableOffset > fileSize ||
			(fileSize - origTableOffset) / 8 < count || (fileSize - transTableOffset) / 8 < count) {
			return -1;
		}

		// An original of the context starts with it and the separator, a text with no context never has the separator
		String prefix = context + StringView(&ContextSeparator, 1);

		SmallVector<MoEntry, 0> kept;
		kept.reserve(count);
		std::int32_t removed = 0;
		for (std::uint32_t i = 0; i < count; i++) {
			const std::uint32_t origLength = ReadU32(&file[origTableOffset + i * 8], bigEndian);
			const std::uint32_t origOffset = ReadU32(&file[origTableOffset + i * 8 + 4], bigEndian);
			const std::uint32_t transLength = ReadU32(&file[transTableOffset + i * 8], bigEndian);
			const std::uint32_t transOffset = ReadU32(&file[transTableOffset + i * 8 + 4], bigEndian);
			if (origOffset > fileSize || fileSize - origOffset < origLength ||
				transOffset > fileSize || fileSize - transOffset < transLength) {
				return -1;
			}

			StringView original(reinterpret_cast<const char*>(&file[origOffset]), origLength);
			if (original.hasPrefix(prefix)) {
				removed++;
				continue;
			}
			kept.push_back({ original, StringView(reinterpret_cast<const char*>(&file[transOffset]), transLength) });
		}

		if (removed == 0) {
			return 0;
		}

		// Laid out the way `msgfmt` does it - header, both tables, the hash table, then the texts themselves,
		// each followed by a null that the lengths don't count
		const std::uint32_t keptCount = std::uint32_t(kept.size());
		const std::uint32_t hashSize = GetHashTableSize(keptCount);
		const std::uint32_t newOrigTableOffset = MoHeaderSize;
		const std::uint32_t newTransTableOffset = newOrigTableOffset + keptCount * 8;
		const std::uint32_t hashTableOffset = newTransTableOffset + keptCount * 8;
		std::uint32_t textOffset = hashTableOffset + hashSize * 4;
		std::size_t totalSize = textOffset;
		for (const MoEntry& entry : kept) {
			totalSize += entry.Original.size() + 1 + entry.Translation.size() + 1;
		}

		Array<std::uint8_t> output{ValueInit, totalSize};
		WriteU32(&output[0], MoSignature);
		WriteU32(&output[4], 0);	// Revision
		WriteU32(&output[8], keptCount);
		WriteU32(&output[12], newOrigTableOffset);
		WriteU32(&output[16], newTransTableOffset);
		WriteU32(&output[20], hashSize);
		WriteU32(&output[24], hashTableOffset);

		for (std::uint32_t i = 0; i < keptCount; i++) {
			const StringView original = kept[i].Original;
			WriteU32(&output[newOrigTableOffset + i * 8], std::uint32_t(original.size()));
			WriteU32(&output[newOrigTableOffset + i * 8 + 4], textOffset);
			std::memcpy(&output[textOffset], original.data(), original.size());
			textOffset += std::uint32_t(original.size()) + 1;
		}
		for (std::uint32_t i = 0; i < keptCount; i++) {
			const StringView translation = kept[i].Translation;
			WriteU32(&output[newTransTableOffset + i * 8], std::uint32_t(translation.size()));
			WriteU32(&output[newTransTableOffset + i * 8 + 4], textOffset);
			std::memcpy(&output[textOffset], translation.data(), translation.size());
			textOffset += std::uint32_t(translation.size()) + 1;
		}

		// Open addressing with the same double hashing the game probes with, an empty slot is 0 and a text is its index + 1
		std::uint8_t* hashTable = &output[hashTableOffset];
		for (std::uint32_t i = 0; i < keptCount; i++) {
			const std::uint32_t hash = HashString(kept[i].Original);
			std::uint32_t idx = hash % hashSize;
			const std::uint32_t incr = 1 + (hash % (hashSize - 2));
			while (ReadU32(&hashTable[idx * 4], false) != 0) {
				if (idx >= hashSize - incr) {
					idx -= hashSize - incr;
				} else {
					idx += incr;
				}
			}
			WriteU32(&hashTable[idx * 4], i + 1);
		}

		// The source is read whole above, so the same file can be written over now
		auto s = fs::Open(path, FileAccess::Write);
		if (!s->IsValid() || s->Write(output.data(), std::int64_t(output.size())) != std::int64_t(output.size())) {
			return -1;
		}
		return removed;
	}
}
