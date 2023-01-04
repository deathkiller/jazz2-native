#include "JJ2Episode.h"
#include "../ContentResolver.h"

#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/IO/FileSystem.h"

namespace Jazz2::Compatibility
{
	bool JJ2Episode::Open(const StringView& path)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsOpened(), "Cannot open file for reading");

		Name = fs::GetFileNameWithoutExtension(path);
		lowercaseInPlace(Name);

		// TODO: Implement JJ2+ extended data, but I haven't seen it anywhere yet
		// the condition of unlocking (currently only defined for 0 meaning "always unlocked"
		// and 1 meaning "requires the previous episode to be finished", stored as a 4-byte-long
		// integer starting at byte 0x4), binary flags of various purpose (currently supported
		// flags are 1 and 2 used to reset respectively player ammo and lives when the episode
		// begins; stored as a 4-byte-long integer starting at byte 0x8)

		// Header (208 bytes)
		/*int32_t headerSize =*/ s->ReadValue<int32_t>();
		Position = s->ReadValue<int32_t>();
		/*uint32_t flags =*/ s->ReadValue<uint32_t>();	// 0x01 = Not Shareware
		/*uint32_t unknown1 =*/ s->ReadValue<uint32_t>();

		char tmpBuffer[64];

		// Episode name
		s->Read(tmpBuffer, 64);
		int length = 0;
		while (tmpBuffer[length] != '\0' && length < 64) {
			length++;
		}
		DisplayName = String(tmpBuffer, length);

		// Previous Episode
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		PreviousEpisode = String(tmpBuffer, length);
		lowercaseInPlace(PreviousEpisode);

		// Next Episode
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		NextEpisode = String(tmpBuffer, length);
		lowercaseInPlace(NextEpisode);

		// First level
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		FirstLevel = String(tmpBuffer, length);
		lowercaseInPlace(FirstLevel);

		// TODO: Episode images are not supported yet
		/*int32_t width =*/ s->ReadValue<int32_t>();
		/*int32_t height =*/ s->ReadValue<int32_t>();
		/*int32_t unknown2 =*/ s->ReadValue<int32_t>();
		/*int32_t unknown3 =*/ s->ReadValue<int32_t>();

		/*int32_t titleWidth =*/ s->ReadValue<int32_t>();
		/*int32_t titleHeight =*/ s->ReadValue<int32_t>();
		/*int32_t unknown4 =*/ s->ReadValue<int32_t>();
		/*int32_t unknown5 =*/ s->ReadValue<int32_t>();

		// TODO
		/*{
			int imagePackedSize = s->ReadValue<int32_t>();
			int imageUnpackedSize = width * height;
			JJ2Block imageBlock(s, imagePackedSize, imageUnpackedSize);
			//episode.image = ConvertIndicesToRgbaBitmap(width, height, imageBlock, false);
		}
		{
			int titleLightPackedSize = s->ReadValue<int32_t>();
			int titleLightUnpackedSize = titleWidth * titleHeight;
			JJ2Block titleLightBlock(s, titleLightPackedSize, titleLightUnpackedSize);
			episode.titleLight = ConvertIndicesToRgbaBitmap(titleWidth, titleHeight, titleLightBlock, true);
		}*/
		//{
		//    int titleDarkPackedSize = r.ReadInt32();
		//    int titleDarkUnpackedSize = titleWidth * titleHeight;
		//    JJ2Block titleDarkBlock = new JJ2Block(s, titleDarkPackedSize, titleDarkUnpackedSize);
		//    episode.titleDark = ConvertIndicesToRgbaBitmap(titleWidth, titleHeight, titleDarkBlock, true);
		//}

		return true;
	}

	void JJ2Episode::Convert(const String& targetPath, std::function<JJ2Level::LevelToken(const StringView&)> levelTokenConversion, std::function<String(JJ2Episode*)> episodeNameConversion, std::function<Pair<String, String>(JJ2Episode*)> episodePrevNext)
	{
		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsOpened(), "Cannot open file for writing");

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::EpisodeFile);

		uint16_t flags = 0x00;
		so->WriteValue<uint16_t>(flags);

		String displayName = (episodeNameConversion != nullptr ? episodeNameConversion(this) : DisplayName);
		so->WriteValue<uint8_t>((uint8_t)displayName.size());
		so->Write(displayName.data(), displayName.size());

		so->WriteValue<uint16_t>((uint16_t)Position);

		MutableStringView firstLevel = FirstLevel;
		if (JJ2Level::StringHasSuffixIgnoreCase(firstLevel, ".j2l"_s) ||
			JJ2Level::StringHasSuffixIgnoreCase(firstLevel, ".lev"_s)) {
			firstLevel = firstLevel.exceptSuffix(4);
		}

		if (levelTokenConversion != nullptr) {
			auto token = levelTokenConversion(firstLevel);
			so->WriteValue<uint8_t>((uint8_t)token.Level.size());
			so->Write(token.Level.data(), token.Level.size());
		} else {
			so->WriteValue<uint8_t>((uint8_t)firstLevel.size());
			so->Write(firstLevel.data(), firstLevel.size());
		}

		if (!PreviousEpisode.empty() || !NextEpisode.empty()) {
			MutableStringView previousEpisode = PreviousEpisode;
			if (JJ2Level::StringHasSuffixIgnoreCase(previousEpisode, ".j2e"_s)) {
				previousEpisode = previousEpisode.exceptSuffix(4);
			} else if (JJ2Level::StringHasSuffixIgnoreCase(previousEpisode, ".j2pe"_s)) {
				previousEpisode = previousEpisode.exceptSuffix(5);
			}
			MutableStringView nextEpisode = NextEpisode;
			if (JJ2Level::StringHasSuffixIgnoreCase(nextEpisode, ".j2e"_s)) {
				nextEpisode = nextEpisode.exceptSuffix(4);
			} else if(JJ2Level::StringHasSuffixIgnoreCase(nextEpisode, ".j2pe"_s)) {
				nextEpisode = nextEpisode.exceptSuffix(5);
			}

			so->WriteValue<uint8_t>((uint8_t)previousEpisode.size());
			so->Write(previousEpisode.data(), previousEpisode.size());
			so->WriteValue<uint8_t>((uint8_t)nextEpisode.size());
			so->Write(nextEpisode.data(), nextEpisode.size());
		} else if (episodePrevNext != nullptr) {
			auto prevNext = episodePrevNext(this);
			so->WriteValue<uint8_t>((uint8_t)prevNext.first().size());
			so->Write(prevNext.first().data(), prevNext.first().size());
			so->WriteValue<uint8_t>((uint8_t)prevNext.second().size());
			so->Write(prevNext.second().data(), prevNext.second().size());
		} else {
			so->WriteValue<uint8_t>(0);
			so->WriteValue<uint8_t>(0);
		}
	}
}