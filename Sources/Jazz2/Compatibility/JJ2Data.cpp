#include "JJ2Data.h"
#include "JJ2Block.h"

#include "../../nCine/Base/Algorithms.h"

#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Compatibility
{
	bool JJ2Data::Open(const StringView& path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		uint32_t magic = s->ReadValue<uint32_t>();
		RETURNF_ASSERT_MSG(magic == 0x42494C50 /*PLIB*/, "Invalid magic string");

		uint32_t signature = s->ReadValue<uint32_t>();
		RETURNF_ASSERT_MSG(signature == 0xBEBAADDE, "Invalid signature");

		/*uint32_t version =*/ s->ReadValue<uint32_t>();

		uint32_t recordedSize = s->ReadValue<uint32_t>();
		RETURNF_ASSERT_MSG(!strictParser || s->GetSize() == recordedSize, "Unexpected file size");

		/*uint32_t recordedCRC =*/ s->ReadValue<uint32_t>();
		int32_t headerBlockPackedSize = s->ReadValue<int32_t>();
		int32_t headerBlockUnpackedSize = s->ReadValue<int32_t>();

		JJ2Block headerBlock(s, headerBlockPackedSize, headerBlockUnpackedSize);

		int32_t baseOffset = s->GetPosition();

		while (s->GetPosition() < s->GetSize()) {
			StringView name = headerBlock.ReadString(32, true);

			uint32_t type = headerBlock.ReadUInt32();
			uint32_t offset = headerBlock.ReadUInt32();
			/*uint32_t fileCRC =*/ headerBlock.ReadUInt32();
			int32_t filePackedSize = headerBlock.ReadInt32();
			int32_t fileUnpackedSize = headerBlock.ReadInt32();

			s->Seek(baseOffset + offset, SeekOrigin::Begin);

			JJ2Block fileBlock(s, filePackedSize, fileUnpackedSize);
			RETURNF_ASSERT_MSG(fileBlock.GetLength() == fileUnpackedSize, "Unexpected item size");

			Item& item = Items.emplace_back();
			item.Filename = name;
			item.Type = type;
			item.Blob = std::make_unique<uint8_t[]>(fileUnpackedSize);
			item.Size = fileUnpackedSize;
			fileBlock.ReadRawBytes(item.Blob.get(), fileUnpackedSize);
		}

		return true;
	}

	void JJ2Data::Extract(const String& targetPath)
	{
		fs::CreateDirectories(targetPath);

		for (auto& item : Items) {
			if (item.Filename == "Shield.Plasma"_s) {
				// TODO
			}

			auto so = fs::Open(fs::CombinePath(targetPath, item.Filename), FileAccessMode::Write);
			ASSERT_MSG(so->IsValid(), "Cannot open file \"%s\" for writing", item.Filename.data());

			so->Write(item.Blob.get(), item.Size);
		}
	}
}