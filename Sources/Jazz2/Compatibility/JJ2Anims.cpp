#include "JJ2Anims.h"
#include "JJ2Anims.Palettes.h"
#include "JJ2Block.h"
#include "AnimSetMapping.h"

#include <IO/FileSystem.h>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	JJ2Version JJ2Anims::Convert(const StringView path, const StringView targetPath, bool isPlus)
	{
		JJ2Version version;
		SmallVector<AnimSection, 0> anims;
		SmallVector<SampleSection, 0> samples;

		auto s = fs::Open(path, FileAccessMode::Read);
		ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		bool seemsLikeCC = false;

		uint32_t magic = s->ReadValue<uint32_t>();
		ASSERT(magic == 0x42494C41);

		uint32_t signature = s->ReadValue<uint32_t>();
		ASSERT(signature == 0x00BEBA00);

		uint32_t headerLen = s->ReadValue<uint32_t>();

		uint32_t magicUnknown = s->ReadValue<uint32_t>();	// Probably `uint16_t version` and `uint16_t unknown`
		ASSERT(magicUnknown == 0x18080200);

		/*uint32_t fileLen =*/ s->ReadValue<uint32_t>();
		/*uint32_t crc =*/ s->ReadValue<uint32_t>();
		int32_t setCount = s->ReadValue<int32_t>();
		SmallVector<uint32_t, 0> setAddresses(setCount);

		for (int32_t i = 0; i < setCount; i++) {
			setAddresses[i] = s->ReadValue<uint32_t>();
		}

		ASSERT(headerLen == s->GetPosition());

		// Read content
		bool isStreamComplete = true;

		for (int32_t i = 0; i < setCount; i++) {
			if (s->GetPosition() >= s->GetSize()) {
				isStreamComplete = false;
				LOGW("Stream should contain %i sets, but found %i sets instead!", setCount, i);
				break;
			}

			uint32_t magicANIM = s->ReadValue<uint32_t>();
			uint8_t animCount = s->ReadValue<uint8_t>();
			uint8_t sndCount = s->ReadValue<uint8_t>();
			/*uint16_t frameCount =*/ s->ReadValue<uint16_t>();
			/*uint32_t cumulativeSndIndex =*/ s->ReadValue<uint32_t>();
			int32_t infoBlockLenC = s->ReadValue<int32_t>();
			int32_t infoBlockLenU = s->ReadValue<int32_t>();
			int32_t frameDataBlockLenC = s->ReadValue<int32_t>();
			int32_t frameDataBlockLenU = s->ReadValue<int32_t>();
			int32_t imageDataBlockLenC = s->ReadValue<int32_t>();
			int32_t imageDataBlockLenU = s->ReadValue<int32_t>();
			int32_t sampleDataBlockLenC = s->ReadValue<int32_t>();
			int32_t sampleDataBlockLenU = s->ReadValue<int32_t>();

			JJ2Block infoBlock(s, infoBlockLenC, infoBlockLenU);
			JJ2Block frameDataBlock(s, frameDataBlockLenC, frameDataBlockLenU);
			JJ2Block imageDataBlock(s, imageDataBlockLenC, imageDataBlockLenU);
			JJ2Block sampleDataBlock(s, sampleDataBlockLenC, sampleDataBlockLenU);

			if (magicANIM != 0x4D494E41) {
				LOGD("Header for set %i is incorrect (bad magic value), skipping", i);
				continue;
			}

			for (uint16_t j = 0; j < animCount; j++) {
				AnimSection& anim = anims.emplace_back();
				anim.Set = i;
				anim.Anim = j;
				anim.FrameCount = infoBlock.ReadUInt16();
				anim.FrameRate = infoBlock.ReadUInt16();
				anim.Frames.resize(anim.FrameCount);

				// Skip the rest, seems to be 0x00000000 for all headers
				infoBlock.DiscardBytes(4);

				if (anim.FrameCount > 0) {
					for (uint16_t k = 0; k < anim.FrameCount; k++) {
						AnimFrameSection& frame = anim.Frames[k];

						frame.SizeX = frameDataBlock.ReadInt16();
						frame.SizeY = frameDataBlock.ReadInt16();
						frame.ColdspotX = frameDataBlock.ReadInt16();
						frame.ColdspotY = frameDataBlock.ReadInt16();
						frame.HotspotX = frameDataBlock.ReadInt16();
						frame.HotspotY = frameDataBlock.ReadInt16();
						frame.GunspotX = frameDataBlock.ReadInt16();
						frame.GunspotY = frameDataBlock.ReadInt16();

						frame.ImageAddr = frameDataBlock.ReadInt32();
						frame.MaskAddr = frameDataBlock.ReadInt32();

						// Adjust normalized position
						// In the output images, we want to make the hotspot and image size constant.
						anim.NormalizedHotspotX = std::max((int16_t)-frame.HotspotX, anim.NormalizedHotspotX);
						anim.NormalizedHotspotY = std::max((int16_t)-frame.HotspotY, anim.NormalizedHotspotY);

						anim.LargestOffsetX = std::max((int16_t)(frame.SizeX + frame.HotspotX), anim.LargestOffsetX);
						anim.LargestOffsetY = std::max((int16_t)(frame.SizeY + frame.HotspotY), anim.LargestOffsetY);

						anim.AdjustedSizeX = std::max(
							(int16_t)(anim.NormalizedHotspotX + anim.LargestOffsetX),
							anim.AdjustedSizeX
						);
						anim.AdjustedSizeY = std::max(
							(int16_t)(anim.NormalizedHotspotY + anim.LargestOffsetY),
							anim.AdjustedSizeY
						);

						int32_t dpos = (frame.ImageAddr + 4);

						imageDataBlock.SeekTo(dpos - 4);
						uint16_t width2 = imageDataBlock.ReadUInt16();
						imageDataBlock.SeekTo(dpos - 2);
						/*uint16_t height2 =*/ imageDataBlock.ReadUInt16();

						frame.DrawTransparent = (width2 & 0x8000) > 0;

						int32_t pxRead = 0;
						int32_t pxTotal = (frame.SizeX * frame.SizeY);
						bool lastOpEmpty = true;

						frame.ImageData = std::make_unique<uint8_t[]>(pxTotal);

						imageDataBlock.SeekTo(dpos);

						while (pxRead < pxTotal) {
							uint8_t op = imageDataBlock.ReadByte();
							if (op < 0x80) {
								// Skip the given number of pixels, writing them with the transparent color 0, array should be already zeroed
								pxRead += op;
							} else if (op == 0x80) {
								// Skip until the end of the line, array should be already zeroed
								uint16_t linePxLeft = (uint16_t)(frame.SizeX - pxRead % frame.SizeX);
								if (pxRead % frame.SizeX == 0 && !lastOpEmpty) {
									linePxLeft = 0;
								}

								pxRead += linePxLeft;
							} else {
								// Copy specified amount of pixels (ignoring the high bit)
								uint16_t bytesToRead = (uint16_t)(op & 0x7F);
								imageDataBlock.ReadRawBytes(frame.ImageData.get() + pxRead, bytesToRead);
								pxRead += bytesToRead;
							}

							lastOpEmpty = (op == 0x80);
						}

						// TODO: Sprite mask
						/*frame.MaskData = std::make_unique<uint8_t[]>(pxTotal);

						if (frame.MaskAddr != 0xFFFFFFFF) {
							imageDataBlock.SeekTo(frame.MaskAddr);
							pxRead = 0;
							while (pxRead < pxTotal) {
								uint8_t b = imageDataBlock.ReadByte();
								for (uint8_t bit = 0; bit < 8 && (pxRead + bit) < pxTotal; ++bit) {
									frame.MaskData[pxRead + bit] = ((b & (1 << (7 - bit))) != 0);
								}
								pxRead += 8;
							}
						}*/
					}
				}
			}

			if (i == 65 && animCount > 5) {
				seemsLikeCC = true;
			}

			for (uint16_t j = 0; j < sndCount; j++) {
				SampleSection& sample = samples.emplace_back();
				sample.IdInSet = j;
				sample.Set = i;

				int32_t totalSize = sampleDataBlock.ReadInt32();
				uint32_t magicRIFF = sampleDataBlock.ReadUInt32();
				int32_t chunkSize = sampleDataBlock.ReadInt32();
				// "ASFF" for 1.20, "AS  " for 1.24
				uint32_t format = sampleDataBlock.ReadUInt32();
				ASSERT(format == 0x46465341 || format == 0x20205341);
				bool isASFF = (format == 0x46465341);

				uint32_t magicSAMP = sampleDataBlock.ReadUInt32();
				/*uint32_t sampSize =*/ sampleDataBlock.ReadUInt32();
				ASSERT_MSG(magicRIFF == 0x46464952 && magicSAMP == 0x504D4153, "Sample has invalid header");

				// Padding/unknown data #1
				// For set 0 sample 0:
				//       1.20                           1.24
				//  +00  00 00 00 00 00 00 00 00   +00  40 00 00 00 00 00 00 00
				//  +08  00 00 00 00 00 00 00 00   +08  00 00 00 00 00 00 00 00
				//  +10  00 00 00 00 00 00 00 00   +10  00 00 00 00 00 00 00 00
				//  +18  00 00 00 00               +18  00 00 00 00 00 00 00 00
				//                                 +20  00 00 00 00 00 40 FF 7F
				sampleDataBlock.DiscardBytes(40 - (isASFF ? 12 : 0));
				if (isASFF) {
					// All 1.20 samples seem to be 8-bit. Some of them are among those
					// for which 1.24 reads as 24-bit but that might just be a mistake.
					sampleDataBlock.DiscardBytes(2);
					sample.Multiplier = 0;
				} else {
					// for 1.24. 1.20 has "20 40" instead in s0s0 which makes no sense
					sample.Multiplier = sampleDataBlock.ReadUInt16();
				}
				// Unknown. s0s0 1.20: 00 80, 1.24: 80 00
				sampleDataBlock.DiscardBytes(2);

				/*uint32_t payloadSize =*/ sampleDataBlock.ReadUInt32();
				// Padding #2, all zeroes in both
				sampleDataBlock.DiscardBytes(8);

				sample.SampleRate = sampleDataBlock.ReadUInt32();
				sample.DataSize = chunkSize - 76 + (isASFF ? 12 : 0);

				sample.Data = std::make_unique<uint8_t[]>(sample.DataSize);
				sampleDataBlock.ReadRawBytes(sample.Data.get(), sample.DataSize);
				// Padding #3
				sampleDataBlock.DiscardBytes(4);

				/*if (sample.Data.Length < actualDataSize) {
					Log.Write(LogType.Warning, "Sample " + j + " in set " + i + " was shorter than expected! Expected "
						+ actualDataSize + " bytes, but read " + sample.Data.Length + " instead.");
				}*/

				if (totalSize > chunkSize + 12) {
					// Sample data is probably aligned to X bytes since the next sample doesn't always appear right after the first ends.
					LOGW("Adjusting read offset of sample %i in set %i by %i bytes.", j, i, (totalSize - chunkSize - 12));

					sampleDataBlock.DiscardBytes(totalSize - chunkSize - 12);
				}
			}
		}

		// Detect version to import
		if (headerLen == 464) {
			if (isStreamComplete) {
				version = JJ2Version::BaseGame;
				LOGI("Detected Jazz Jackrabbit 2 (v1.20/1.23)");
			} else {
				version = JJ2Version::BaseGame | JJ2Version::SharewareDemo;
				LOGI("Detected Jazz Jackrabbit 2 (v1.20/1.23): Shareware Demo");
			}
		} else if (headerLen == 500) {
			if (!isStreamComplete) {
				version = JJ2Version::TSF | JJ2Version::SharewareDemo;
				// TODO: This version is not supported (yet)
				LOGE("Detected Jazz Jackrabbit 2: The Secret Files Demo - This version is not supported!");
				return JJ2Version::Unknown;
			} else if (seemsLikeCC) {
				version = JJ2Version::CC;
				LOGI("Detected Jazz Jackrabbit 2: Christmas Chronicles");
			} else {
				version = JJ2Version::TSF;
				LOGI("Detected Jazz Jackrabbit 2: The Secret Files");
			}
		} else if (headerLen == 476) {
			version = JJ2Version::HH;
			LOGI("Detected Jazz Jackrabbit 2: Holiday Hare '98");
		} else if (headerLen == 64) {
			version = JJ2Version::PlusExtension;
			if (!isPlus) {
				LOGE("Detected Jazz Jackrabbit 2 Plus extension - This version is not supported!");
				return JJ2Version::Unknown;
			}
		} else {
			version = JJ2Version::Unknown;
			LOGE("Could not determine the version, header size: %u bytes", headerLen);
		}

		ImportAnimations(targetPath, version, anims);
		ImportAudioSamples(targetPath, version, samples);

		return version;
	}

	void JJ2Anims::ImportAnimations(const StringView targetPath, JJ2Version version, SmallVectorImpl<AnimSection>& anims)
	{
		if (anims.empty()) {
			return;
		}

		LOGI("Importing animations...");

		AnimSetMapping animMapping = AnimSetMapping::GetAnimMapping(version);

		for (auto& anim : anims) {
			if (anim.FrameCount == 0) {
				continue;
			}

			AnimSetMapping::Entry* entry = animMapping.Get(anim.Set, anim.Anim);
			if (entry == nullptr || entry->Category == AnimSetMapping::Discard) {
				continue;
			}

			int32_t sizeX = (anim.AdjustedSizeX + AddBorder * 2);
			int32_t sizeY = (anim.AdjustedSizeY + AddBorder * 2);
			// Determine the frame configuration to use.
			// Each asset must fit into a 4096 by 4096 texture,
			// as that is the smallest texture size we have decided to support.
			if (anim.FrameCount > 1) {
				int32_t rows = std::max(1, (int32_t)std::ceil(sqrt(anim.FrameCount * sizeX / sizeY)));
				int32_t columns = std::max(1, (int32_t)std::ceil(anim.FrameCount * 1.0 / rows));

				// Do a bit of optimization, as the above algorithm ends occasionally with some extra space
				// (it is careful with not underestimating the required space)
				while (columns * (rows - 1) >= anim.FrameCount) {
					rows--;
				}

				anim.FrameConfigurationX = (uint8_t)columns;
				anim.FrameConfigurationY = (uint8_t)rows;
			} else {
				anim.FrameConfigurationX = (uint8_t)anim.FrameCount;
				anim.FrameConfigurationY = 1;
			}

			// TODO: Hardcoded name
			bool applyToasterPowerUpFix = (entry->Category == "Object"_s && entry->Name == "powerup_upgrade_toaster"_s);
			if (applyToasterPowerUpFix) {
				LOGI("Applying \"Toaster PowerUp\" palette fix to %i:%u", anim.Set, anim.Anim);
			}

			bool applyVineFix = (entry->Category == "Object"_s && entry->Name == "vine"_s);
			if (applyVineFix) {
				LOGI("Applying \"Vine\" palette fix to %i:%u", anim.Set, anim.Anim);
			}

			bool applyFlyCarrotFix = (entry->Category == "Pickup"_s && entry->Name == "carrot_fly"_s);
			if (applyFlyCarrotFix) {
				// This image has 4 wrong pixels that should be transparent
				LOGI("Applying \"Fly Carrot\" image fix to %i:%u", anim.Set, anim.Anim);
			}

			bool playerFlareFix = ((entry->Category == "Jazz"_s || entry->Category == "Spaz"_s) && (entry->Name == "shoot_ver"_s || entry->Name == "vine_shoot_up"_s));
			if (playerFlareFix) {
				// This image has already applied weapon flare, remove it
				LOGI("Applying \"Player Flare\" image fix to %i:%u", anim.Set, anim.Anim);
			}

			String filename;
			if (entry->Name.empty()) {
				/*filename = "s" + sample.Set + "_s" + sample.IdInSet + ".jri";
				if (version == JJ2Version::PlusExtension) {
					filename = "plus_" + filename;
				}*/
				ASSERT(!entry->Name.empty());
				continue;
			} else {
				fs::CreateDirectories(fs::CombinePath(targetPath, entry->Category));
				filename = fs::CombinePath(entry->Category, entry->Name + ".aura"_s);
			}

			int32_t stride = sizeX * anim.FrameConfigurationX;
			std::unique_ptr<uint8_t[]> pixels = std::make_unique<uint8_t[]>(stride * sizeY * anim.FrameConfigurationY * 4);

			for (int32_t j = 0; j < anim.Frames.size(); j++) {
				auto& frame = anim.Frames[j];

				int32_t offsetX = anim.NormalizedHotspotX + frame.HotspotX;
				int32_t offsetY = anim.NormalizedHotspotY + frame.HotspotY;

				for (int32_t y = 0; y < frame.SizeY; y++) {
					for (int32_t x = 0; x < frame.SizeX; x++) {
						int32_t targetX = (j % anim.FrameConfigurationX) * sizeX + offsetX + x + AddBorder;
						int32_t targetY = (j / anim.FrameConfigurationX) * sizeY + offsetY + y + AddBorder;
						uint8_t colorIdx = frame.ImageData[frame.SizeX * y + x];

						// Apply palette fixes
						if (applyToasterPowerUpFix) {
							if ((x >= 3 && y >= 4 && x <= 15 && y <= 20) || (x >= 2 && y >= 7 && x <= 15 && y <= 19)) {
								colorIdx = ToasterPowerUpFix[colorIdx];
							}
						} else if (applyVineFix) {
							if (colorIdx == 128) {
								colorIdx = 0;
							}
						} else if (applyFlyCarrotFix) {
							if (colorIdx >= 68 && colorIdx <= 70) {
								colorIdx = 0;
							}
						} else if (playerFlareFix) {
							if (j == 0 && y < 14 && (colorIdx == 15 || (colorIdx >= 40 && colorIdx <= 42))) {
								colorIdx = 0;
							}
						}

						if (entry->Palette == JJ2DefaultPalette::Menu) {
							const Color& src = MenuPalette[colorIdx];
							uint8_t a;
							if (colorIdx == 0) {
								a = 0;
							} else if (frame.DrawTransparent) {
								a = 140 * src.A / 255;
							} else {
								a = src.A;
							}

							pixels[(stride * targetY + targetX) * 4] = src.R;
							pixels[(stride * targetY + targetX) * 4 + 1] = src.G;
							pixels[(stride * targetY + targetX) * 4 + 2] = src.B;
							pixels[(stride * targetY + targetX) * 4 + 3] = a;
						} else {
							uint8_t a;
							if (colorIdx == 0) {
								a = 0;
							} else if (frame.DrawTransparent) {
								a = 140;
							} else {
								a = 255;
							}

							pixels[(stride * targetY + targetX) * 4] = colorIdx;
							pixels[(stride * targetY + targetX) * 4 + 1] = colorIdx;
							pixels[(stride * targetY + targetX) * 4 + 2] = colorIdx;
							pixels[(stride * targetY + targetX) * 4 + 3] = a;
						}
					}
				}
			}

			bool applyLoriLiftFix = (entry->Category == "Lori"_s && (entry->Name == "lift"_s || entry->Name == "lift_start"_s || entry->Name == "lift_end"_s));
			if (applyLoriLiftFix) {
				LOGI("Applying \"Lori\" hotspot fix to %i:%u", anim.Set, anim.Anim);
				anim.NormalizedHotspotX = 20;
				anim.NormalizedHotspotY = 4;
			}

			// TODO: Use single channel instead
			String fullPath = fs::CombinePath(targetPath, filename);
			WriteImageToFile(fullPath, pixels.get(), sizeX, sizeY, 4, anim, entry);

			/*if (!string.IsNullOrEmpty(data.Name) && !data.SkipNormalMap) {
				PngWriter normalMap = NormalMapGenerator.FromSprite(img,
						new Point(currentAnim.FrameConfigurationX, currentAnim.FrameConfigurationY),
						!data.AllowRealtimePalette && data.Palette == JJ2DefaultPalette.ByIndex ? JJ2DefaultPalette.Sprite : null);

				normalMap.Save(filename.Replace(".png", ".n.png"));
			}*/
		}
	}

	void JJ2Anims::ImportAudioSamples(const StringView targetPath, JJ2Version version, SmallVectorImpl<SampleSection>& samples)
	{
		if (samples.empty()) {
			return;
		}

		LOGI("Importing audio samples...");

		AnimSetMapping mapping = AnimSetMapping::GetSampleMapping(version);

		for (auto& sample : samples) {
			AnimSetMapping::Entry* entry = mapping.Get(sample.Set, sample.IdInSet);
			if (entry == nullptr || entry->Category == AnimSetMapping::Discard) {
				continue;
			}

			String filename;
			if (entry->Name.empty()) {
				/*filename = "s" + sample.Set + "_s" + sample.IdInSet + ".wav";
				if (version == JJ2Version::PlusExtension) {
					filename = "plus_" + filename;
				}*/
				ASSERT(!entry->Name.empty());
				continue;
			} else {
				fs::CreateDirectories(fs::CombinePath(targetPath, entry->Category));

				filename = fs::CombinePath(entry->Category, entry->Name + ".wav"_s);
			}

			auto so = fs::Open(fs::CombinePath(targetPath, filename), FileAccessMode::Write);
			ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

			// TODO: The modulo here essentially clips the sample to 8- or 16-bit.
			// There are some samples (at least the Rapier random noise) that at least get reported as 24-bit
			// by the read header data. It is not clear if they actually are or if the header data is just
			// read incorrectly, though - one would think the data would need to be reshaped between 24 and 8
			// but it works just fine as is.
			int bytesPerSample = (sample.Multiplier / 4) % 2 + 1;
			int dataOffset = 0;
			if (sample.Data[0] == 0x00 && sample.Data[1] == 0x00 && sample.Data[2] == 0x00 && sample.Data[3] == 0x00 &&
				(sample.Data[4] != 0x00 || sample.Data[5] != 0x00 || sample.Data[6] != 0x00 || sample.Data[7] != 0x00) &&
				(sample.Data[7] == 0x00 || sample.Data[8] == 0x00)) {
				// Trim first 8 samples (bytes) to prevent popping
				dataOffset = 8;
			}

			// Create PCM wave file
			// Main header
			so->Write("RIFF", 4);
			so->WriteValue<uint32_t>(36 + sample.DataSize - dataOffset); // File size
			so->Write("WAVE", 4);

			// Format header
			so->Write("fmt ", 4);
			so->WriteValue<uint32_t>(16); // Header remainder length
			so->WriteValue<uint16_t>(1); // Format = PCM
			so->WriteValue<uint16_t>(1); // Channels
			so->WriteValue<uint32_t>(sample.SampleRate); // Sample rate
			so->WriteValue<uint32_t>(sample.SampleRate * bytesPerSample); // Bytes per second
			so->WriteValue<uint32_t>(bytesPerSample * 0x00080001);

			// Payload
			so->Write("data", 4);
			so->WriteValue<uint32_t>(sample.DataSize - dataOffset); // Payload size
			for (uint32_t k = dataOffset; k < sample.DataSize; k++) {
				so->WriteValue<uint8_t>((bytesPerSample << 7) ^ sample.Data[k]);
			}
		}
	}

	void JJ2Anims::WriteImageToFile(const StringView targetPath, const uint8_t* data, int32_t width, int32_t height, int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry)
	{
		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		uint8_t flags = 0x00;
		if (entry != nullptr) {
			flags |= 0x80;
			/*if (!entry->AllowRealtimePalette && entry->Palette == JJ2DefaultPalette::Sprite) {
				flags |= 0x01;
			}
			if (!entry->AllowRealtimePalette) { // Use Linear Sampling, only if the palette is applied in pre-processing stage
				flags |= 0x02;
			}*/

			if (entry->Palette != JJ2DefaultPalette::Sprite) {
				flags |= 0x01;
			}
			if (entry->SkipNormalMap) {
				flags |= 0x02;
			}
		}

		so->WriteValue<uint64_t>(0xB8EF8498E2BFBBEF);
		so->WriteValue<uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

		so->WriteValue<uint8_t>(channelCount);
		so->WriteValue<uint32_t>(width);
		so->WriteValue<uint32_t>(height);

		// Include Sprite extension
		if (entry != nullptr) {
			so->WriteValue<uint8_t>(anim.FrameConfigurationX);
			so->WriteValue<uint8_t>(anim.FrameConfigurationY);
			so->WriteValue<uint16_t>(anim.FrameCount);
			so->WriteValue<uint16_t>((uint16_t)(anim.FrameRate == 0 ? 0 : 256 * 5 / anim.FrameRate));

			if (anim.NormalizedHotspotX != 0 || anim.NormalizedHotspotY != 0) {
				so->WriteValue<uint16_t>(anim.NormalizedHotspotX + AddBorder);
				so->WriteValue<uint16_t>(anim.NormalizedHotspotY + AddBorder);
			} else {
				so->WriteValue<uint16_t>(UINT16_MAX);
				so->WriteValue<uint16_t>(UINT16_MAX);
			}
			if (anim.Frames[0].ColdspotX != 0 || anim.Frames[0].ColdspotY != 0) {
				so->WriteValue<uint16_t>((anim.NormalizedHotspotX + anim.Frames[0].HotspotX) - anim.Frames[0].ColdspotX + AddBorder);
				so->WriteValue<uint16_t>((anim.NormalizedHotspotY + anim.Frames[0].HotspotY) - anim.Frames[0].ColdspotY + AddBorder);
			} else {
				so->WriteValue<uint16_t>(UINT16_MAX);
				so->WriteValue<uint16_t>(UINT16_MAX);
			}
			if (anim.Frames[0].GunspotX != 0 || anim.Frames[0].GunspotY != 0) {
				so->WriteValue<uint16_t>((anim.NormalizedHotspotX + anim.Frames[0].HotspotX) - anim.Frames[0].GunspotX + AddBorder);
				so->WriteValue<uint16_t>((anim.NormalizedHotspotY + anim.Frames[0].HotspotY) - anim.Frames[0].GunspotY + AddBorder);
			} else {
				so->WriteValue<uint16_t>(UINT16_MAX);
				so->WriteValue<uint16_t>(UINT16_MAX);
			}

			width *= anim.FrameConfigurationX;
			height *= anim.FrameConfigurationY;
		}

		WriteImageToFileInternal(so, data, width, height, channelCount);
	}

	void JJ2Anims::WriteImageToFileInternal(std::unique_ptr<Stream>& so, const uint8_t* data, int32_t width, int32_t height, int32_t channelCount)
	{
		typedef union {
			struct {
				unsigned char r, g, b, a;
			} rgba;
			unsigned int v;
		} rgba_t;

		#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
		#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
		#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
		#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
		#define QOI_OP_RGB    0xfe /* 11111110 */
		#define QOI_OP_RGBA   0xff /* 11111111 */

		#define QOI_MASK_2    0xc0 /* 11000000 */

		#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)

		auto pixels = (const uint8_t*)data;

		rgba_t index[64] { };
		rgba_t px, px_prev;

		int run = 0;
		px_prev.rgba.r = 0;
		px_prev.rgba.g = 0;
		px_prev.rgba.b = 0;
		px_prev.rgba.a = 255;
		px = px_prev;

		int px_len = width * height * channelCount;
		int px_end = px_len - channelCount;

		for (int px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (channelCount == 4) {
				px = *(rgba_t*)(pixels + px_pos);
			} else {
				px.rgba.r = pixels[px_pos + 0];
				px.rgba.g = pixels[px_pos + 1];
				px.rgba.b = pixels[px_pos + 2];
			}

			if (px.v == px_prev.v) {
				run++;
				if (run == 62 || px_pos == px_end) {
					so->WriteValue<uint8_t>(QOI_OP_RUN | (run - 1));
					run = 0;
				}
			} else {
				int index_pos;

				if (run > 0) {
					so->WriteValue<uint8_t>(QOI_OP_RUN | (run - 1));
					run = 0;
				}

				index_pos = QOI_COLOR_HASH(px) % 64;

				if (index[index_pos].v == px.v) {
					so->WriteValue<uint8_t>(QOI_OP_INDEX | index_pos);
				} else {
					index[index_pos] = px;

					if (px.rgba.a == px_prev.rgba.a) {
						signed char vr = px.rgba.r - px_prev.rgba.r;
						signed char vg = px.rgba.g - px_prev.rgba.g;
						signed char vb = px.rgba.b - px_prev.rgba.b;

						signed char vg_r = vr - vg;
						signed char vg_b = vb - vg;

						if (
							vr > -3 && vr < 2 &&
							vg > -3 && vg < 2 &&
							vb > -3 && vb < 2
						) {
							so->WriteValue<uint8_t>(QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
						} else if (
							vg_r >  -9 && vg_r < 8 &&
							vg   > -33 && vg   < 32 &&
							vg_b >  -9 && vg_b < 8
						) {
							so->WriteValue<uint8_t>(QOI_OP_LUMA | (vg + 32));
							so->WriteValue<uint8_t>((vg_r + 8) << 4 | (vg_b + 8));
						} else {
							so->WriteValue<uint8_t>(QOI_OP_RGB);
							so->WriteValue<uint8_t>(px.rgba.r);
							so->WriteValue<uint8_t>(px.rgba.g);
							so->WriteValue<uint8_t>(px.rgba.b);
						}
					} else {
						so->WriteValue<uint8_t>(QOI_OP_RGBA);
						so->WriteValue<uint8_t>(px.rgba.r);
						so->WriteValue<uint8_t>(px.rgba.g);
						so->WriteValue<uint8_t>(px.rgba.b);
						so->WriteValue<uint8_t>(px.rgba.a);
					}
				}
			}
			px_prev = px;
		}
	}
}