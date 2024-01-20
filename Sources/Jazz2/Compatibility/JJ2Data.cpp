#include "JJ2Data.h"
#include "JJ2Anims.h"
#include "JJ2Anims.Palettes.h"
#include "JJ2Block.h"
#include "../ContentResolver.h"

#include "../../nCine/Base/Algorithms.h"

#include <IO/FileSystem.h>
#include <IO/DeflateStream.h>

using namespace Death;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Compatibility
{
	bool JJ2Data::Open(const StringView path, bool strictParser)
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

	void JJ2Data::Extract(const StringView targetPath)
	{
		fs::CreateDirectories(targetPath);

		for (auto& item : Items) {
			auto so = fs::Open(fs::CombinePath(targetPath, item.Filename), FileAccessMode::Write);
			ASSERT_MSG(so->IsValid(), "Cannot open file \"%s\" for writing", item.Filename.data());

			so->Write(item.Blob.get(), item.Size);
		}
	}

	void JJ2Data::Convert(const StringView targetPath, JJ2Version version)
	{
		AnimSetMapping animMapping = AnimSetMapping::GetSampleMapping(version);
		auto animationsUiPath = fs::CombinePath({ targetPath, "Animations"_s, "UI"_s });
		auto cinematicsPath = fs::CombinePath(targetPath, "Cinematics"_s);

		fs::CreateDirectories(animationsUiPath);
		fs::CreateDirectories(cinematicsPath);

		for (auto& item : Items) {
			if (item.Filename == "SoundFXList.Intro"_s) {
				ConvertSfxList(item, fs::CombinePath(cinematicsPath, "intro.j2sfx"), animMapping);
			} else if (item.Filename == "SoundFXList.Ending"_s) {
				ConvertSfxList(item, fs::CombinePath(cinematicsPath, "ending.j2sfx"), animMapping);
			} else if (item.Filename == "Menu.Texture.16x16"_s) {
				ConvertMenuImage(item, fs::CombinePath(animationsUiPath, "menu16.aura"), 16, 16);
			} else if (item.Filename == "Menu.Texture.32x32"_s) {
				ConvertMenuImage(item, fs::CombinePath(animationsUiPath, "menu32.aura"), 32, 32);
			} else if (item.Filename == "Menu.Texture.128x128"_s) {
				ConvertMenuImage(item, fs::CombinePath(animationsUiPath, "menu128.aura"), 128, 128);
			}
		}
	}

	void JJ2Data::ConvertSfxList(const Item& item, const StringView targetPath, AnimSetMapping& animMapping)
	{
#pragma pack(push, 1)
		struct SoundFXList {
			std::uint32_t Frame;
			std::uint32_t Sample;
			std::uint32_t Volume;
			std::uint32_t Panning;
		};
#pragma pack(pop)

		auto s = fs::Open(targetPath, FileAccessMode::Write);
		s->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
		s->WriteValue<std::uint8_t>(ContentResolver::SfxListFile);
		s->WriteValue<std::uint16_t>(1);

		DeflateWriter d(*s);

		HashMap<std::uint32_t, std::uint32_t> sampleToIndex;
		SmallVector<std::uint32_t> indexToSample;

		std::int32_t itemCount = item.Size / sizeof(SoundFXList);
		std::int32_t itemRealCount = 0;
		std::int32_t sampleCount = 0;
		for (std::int32_t i = 0; i < itemCount; i++) {
			SoundFXList sfx;
			std::memcpy(&sfx, &item.Blob[i * sizeof(SoundFXList)], sizeof(SoundFXList));
			if (sfx.Frame == UINT32_MAX) {
				break;
			}

			auto it = sampleToIndex.find(sfx.Sample);
			if (it == sampleToIndex.end()) {
				sampleToIndex.emplace(sfx.Sample, sampleCount);
				indexToSample.emplace_back(sfx.Sample);
				sampleCount++;
			}

			itemRealCount++;
		}

		d.WriteValue<std::uint16_t>((std::uint16_t)sampleCount);
		for (std::int32_t i = 0; i < sampleCount; i++) {
			auto sample = animMapping.GetByOrdinal(indexToSample[i]);
			if (sample == nullptr) {
				d.WriteValue<std::uint8_t>(0);
			} else {
				String samplePath = "/"_s.join({ sample->Category, sample->Name + ".wav"_s });
				d.WriteValue<std::uint8_t>((std::uint8_t)samplePath.size());
				d.Write(samplePath.data(), (std::uint32_t)samplePath.size());
			}
		}

		d.WriteValue<std::uint16_t>((std::uint16_t)itemRealCount);
		for (std::int32_t i = 0; i < itemRealCount; i++) {
			SoundFXList sfx;
			std::memcpy(&sfx, &item.Blob[i * sizeof(SoundFXList)], sizeof(SoundFXList));
					
			d.WriteVariableUint32(sfx.Frame);

			auto it = sampleToIndex.find(sfx.Sample);
			if (it != sampleToIndex.end()) {
				d.WriteValue<std::uint16_t>((std::uint16_t)it->second);
			} else {
				d.WriteValue<std::uint16_t>(0);
			}

			d.WriteValue<std::uint8_t>((std::uint8_t)std::max(sfx.Volume * 255 / 0x40, (std::uint32_t)UINT8_MAX));
			d.WriteValue<std::int8_t>((std::int8_t)std::clamp(((std::int32_t)sfx.Panning - 0x20) * INT8_MAX / /*0x20*/0x40, -(std::int32_t)INT8_MAX, (std::int32_t)INT8_MAX));
		}
	}

	void JJ2Data::ConvertMenuImage(const Item& item, const StringView targetPath, std::int32_t width, std::int32_t height)
	{
		std::int32_t pixelCount = width * height;
		RETURN_ASSERT_MSG(item.Size == pixelCount, "Image has unexpected size");

		std::unique_ptr<uint8_t[]> pixels = std::make_unique<uint8_t[]>(pixelCount * 4);
		for (std::int32_t i = 0; i < pixelCount; i++) {
			std::uint8_t colorIdx = item.Blob[i];
			const Color& src = MenuPalette[colorIdx];
			std::uint8_t a = (colorIdx == 0 ? 0 : src.A);
			
			pixels[(i * 4)] = src.R;
			pixels[(i * 4) + 1] = src.G;
			pixels[(i * 4) + 2] = src.B;
			pixels[(i * 4) + 3] = a;
		}

		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		std::uint8_t flags = 0x80 | 0x01 | 0x02;

		so->WriteValue<std::uint64_t>(0xB8EF8498E2BFBBEF);
		so->WriteValue<std::uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

		so->WriteValue<std::uint8_t>(4);
		so->WriteValue<std::uint32_t>(width);
		so->WriteValue<std::uint32_t>(height);

		// Include Sprite extension
		so->WriteValue<std::uint8_t>(1); // FrameConfigurationX
		so->WriteValue<std::uint8_t>(1); // FrameConfigurationY
		so->WriteValue<std::uint16_t>(1); // FrameCount
		so->WriteValue<std::uint16_t>(0); // FrameRate
		so->WriteValue<std::uint16_t>(UINT16_MAX); // NormalizedHotspotX
		so->WriteValue<std::uint16_t>(UINT16_MAX); // NormalizedHotspotY
		so->WriteValue<std::uint16_t>(UINT16_MAX); // ColdspotX
		so->WriteValue<std::uint16_t>(UINT16_MAX); // ColdspotY
		so->WriteValue<std::uint16_t>(UINT16_MAX); // GunspotX
		so->WriteValue<std::uint16_t>(UINT16_MAX); // GunspotY

		JJ2Anims::WriteImageToFileInternal(so, pixels.get(), width, height, 4);
	}
}