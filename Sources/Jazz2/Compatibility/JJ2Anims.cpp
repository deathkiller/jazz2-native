#include "JJ2Anims.h"
#include "JJ2Anims.Palettes.h"
#include "JJ2Block.h"
#include "AnimSetMapping.h"

#include <algorithm>
#include <cstdlib>

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/FileStream.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	namespace
	{
		// Largest sprite sheet any supported platform can sample. The smallest limit among them decides it,
		// so a sheet that fits here needs no per-platform variant of the converted assets.
		constexpr std::int32_t MaxTextureSize = 1024;

		std::int32_t NextPowerOfTwo(std::int32_t value)
		{
			std::int32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}
	}

	/**
		@brief Packs the frames of one animation so each keeps only the space it needs

		A frame's own extent is usually much smaller than the largest frame of its animation, and a grid
		of equal cells pays for the difference in every single frame. The frames are placed in rows
		ordered by height (shelf packing), which for sprite sheets - many similar heights, few outliers -
		comes within a few percent of the theoretical minimum while staying simple enough to reason about.
		The sheet is sized to the power-of-two dimensions that waste the least memory, since hardware that
		cannot sample non-power-of-two textures pads it to exactly that.
	*/
	bool JJ2Anims::PackFramesTightly(const AnimSection& anim, std::int32_t border,
		SmallVector<PackedFrame, 0>& packed, std::int32_t& sheetWidth, std::int32_t& sheetHeight)
	{
		const std::int32_t frameCount = std::int32_t(anim.Frames.size());
		if (frameCount <= 0) {
			return false;
		}

		packed.clear();
		packed.reserve(frameCount);
		std::int32_t widest = 0;
		for (std::int32_t i = 0; i < frameCount; i++) {
			const AnimFrameSection& frame = anim.Frames[i];
			PackedFrame& p = packed.emplace_back();
			// The frame covers exactly its own pixels. The gap that stops a neighbour bleeding in under
			// bilinear filtering is placed between frames instead of around each one, so it is paid once
			// per boundary rather than twice.
			p.W = frame.SizeX;
			p.H = frame.SizeY;
			p.X = p.Y = 0;
			// Where this frame's pixels begin inside its logical cell. The cell keeps its border, so that
			// is included here - it is what makes the hotspot and every cell-aligned position come out right
			// (see GenericGraphicResource::GetFrameAnchor / GetFrameOffset).
			p.OffsetX = anim.NormalizedHotspotX + frame.HotspotX + border;
			p.OffsetY = anim.NormalizedHotspotY + frame.HotspotY + border;
			widest = std::max(widest, p.W);
		}

		// Tallest first, so each row is filled by frames of similar height
		SmallVector<std::int32_t, 0> order(frameCount);
		for (std::int32_t i = 0; i < frameCount; i++) {
			order[i] = i;
		}
		std::sort(order.begin(), order.end(), [&packed](std::int32_t a, std::int32_t b) {
			if (packed[a].H != packed[b].H) {
				return packed[a].H > packed[b].H;
			}
			return packed[a].W > packed[b].W;
		});

		// Try every sheet width that could hold the widest frame and keep the best result
		std::int32_t bestWidth = 0, bestHeight = 0;
		std::int64_t bestArea = INT64_MAX;
		for (std::int32_t width = NextPowerOfTwo(widest); width <= MaxTextureSize; width <<= 1) {
			std::int32_t x = 0, rowY = 0, rowHeight = 0;
			for (std::int32_t i = 0; i < frameCount; i++) {
				const PackedFrame& p = packed[order[i]];
				if (x > 0 && x + p.W > width) {
					rowY += rowHeight + border;
					x = 0;
					rowHeight = 0;
				}
				x += p.W + border;
				rowHeight = std::max(rowHeight, p.H);
			}
			const std::int32_t height = NextPowerOfTwo(rowY + rowHeight);
			if (height > MaxTextureSize) {
				continue;
			}

			// Several widths usually pad to the same area; among those prefer the squarest sheet, which keeps
			// the sheet away from the maximum texture size and samples better than a long narrow strip
			const std::int64_t cost = std::int64_t(width) * height * 4096 + std::abs(width - height);
			if (cost < bestArea) {
				bestArea = cost;
				bestWidth = width;
				bestHeight = height;
			}
		}
		if (bestWidth <= 0) {
			return false;		// Does not fit, fall back to the regular grid
		}

		// Lay the frames out for real at the chosen size
		std::int32_t x = 0, rowY = 0, rowHeight = 0;
		for (std::int32_t i = 0; i < frameCount; i++) {
			PackedFrame& p = packed[order[i]];
			if (x > 0 && x + p.W > bestWidth) {
				rowY += rowHeight + border;
				x = 0;
				rowHeight = 0;
			}
			p.X = x;
			p.Y = rowY;
			x += p.W + border;
			rowHeight = std::max(rowHeight, p.H);
		}

		sheetWidth = bestWidth;
		sheetHeight = bestHeight;
		return true;
	}

	JJ2Version JJ2Anims::Convert(StringView path, PakWriter& pakWriter, bool isPlus)
	{
		JJ2Version version;
		SmallVector<AnimSection, 0> anims;
		SmallVector<SampleSection, 0> samples;

		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			LOGE("Cannot open file \"{}\" for reading", path);
			return JJ2Version::Unknown;
		}

		bool seemsLikeCC = false;

		std::uint32_t magic = s->ReadValueAsLE<std::uint32_t>();
		DEATH_ASSERT(magic == 0x42494C41, "Invalid magic number", JJ2Version::Unknown);

		std::uint32_t signature = s->ReadValueAsLE<std::uint32_t>();
		DEATH_ASSERT(signature == 0x00BEBA00, "Invalid signature", JJ2Version::Unknown);

		std::uint32_t headerLen = s->ReadValueAsLE<std::uint32_t>();

		std::uint32_t magicUnknown = s->ReadValueAsLE<std::uint32_t>();	// Probably `uint16_t version` and `uint16_t unknown`
		DEATH_ASSERT(magicUnknown == 0x18080200, "Invalid version", JJ2Version::Unknown);

		/*std::uint32_t fileLen =*/ s->ReadValueAsLE<std::uint32_t>();
		/*std::uint32_t crc =*/ s->ReadValueAsLE<std::uint32_t>();
		std::int32_t setCount = s->ReadValueAsLE<std::int32_t>();
		SmallVector<std::uint32_t, 0> setAddresses(setCount);

		for (std::int32_t i = 0; i < setCount; i++) {
			setAddresses[i] = s->ReadValueAsLE<std::uint32_t>();
		}

		DEATH_ASSERT(headerLen == s->GetPosition(), "Invalid header size", JJ2Version::Unknown);

		// Read content
		bool isStreamComplete = true;

		for (std::int32_t i = 0; i < setCount; i++) {
			if (s->GetPosition() >= s->GetSize()) {
				isStreamComplete = false;
				LOGW("Stream should contain {} sets, but found {} sets instead!", setCount, i);
				break;
			}

			std::uint32_t magicANIM = s->ReadValueAsLE<std::uint32_t>();
			std::uint8_t animCount = s->ReadValue<std::uint8_t>();
			std::uint8_t sndCount = s->ReadValue<std::uint8_t>();
			/*std::uint16_t frameCount =*/ s->ReadValueAsLE<std::uint16_t>();
			/*std::uint32_t cumulativeSndIndex =*/ s->ReadValueAsLE<std::uint32_t>();
			std::int32_t infoBlockLenC = s->ReadValueAsLE<std::int32_t>();
			std::int32_t infoBlockLenU = s->ReadValueAsLE<std::int32_t>();
			std::int32_t frameDataBlockLenC = s->ReadValueAsLE<std::int32_t>();
			std::int32_t frameDataBlockLenU = s->ReadValueAsLE<std::int32_t>();
			std::int32_t imageDataBlockLenC = s->ReadValueAsLE<std::int32_t>();
			std::int32_t imageDataBlockLenU = s->ReadValueAsLE<std::int32_t>();
			std::int32_t sampleDataBlockLenC = s->ReadValueAsLE<std::int32_t>();
			std::int32_t sampleDataBlockLenU = s->ReadValueAsLE<std::int32_t>();

			JJ2Block infoBlock(s, infoBlockLenC, infoBlockLenU);
			JJ2Block frameDataBlock(s, frameDataBlockLenC, frameDataBlockLenU);
			JJ2Block imageDataBlock(s, imageDataBlockLenC, imageDataBlockLenU);
			JJ2Block sampleDataBlock(s, sampleDataBlockLenC, sampleDataBlockLenU);

			if (magicANIM != 0x4D494E41) {
				LOGD("Header for set {} is incorrect (bad magic value), skipping", i);
				continue;
			}

			for (std::uint16_t j = 0; j < animCount; j++) {
				AnimSection& anim = anims.emplace_back();
				anim.Set = i;
				anim.Anim = j;
				anim.FrameCount = infoBlock.ReadUInt16();
				anim.FrameRate = infoBlock.ReadUInt16();
				anim.Frames.resize(anim.FrameCount);

				// Skip the rest, seems to be 0x00000000 for all headers
				infoBlock.DiscardBytes(4);

				if (anim.FrameCount > 0) {
					for (std::uint16_t k = 0; k < anim.FrameCount; k++) {
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
						anim.NormalizedHotspotX = std::max((std::int16_t)-frame.HotspotX, anim.NormalizedHotspotX);
						anim.NormalizedHotspotY = std::max((std::int16_t)-frame.HotspotY, anim.NormalizedHotspotY);

						anim.LargestOffsetX = std::max((std::int16_t)(frame.SizeX + frame.HotspotX), anim.LargestOffsetX);
						anim.LargestOffsetY = std::max((std::int16_t)(frame.SizeY + frame.HotspotY), anim.LargestOffsetY);

						anim.AdjustedSizeX = std::max(
							(std::int16_t)(anim.NormalizedHotspotX + anim.LargestOffsetX),
							anim.AdjustedSizeX
						);
						anim.AdjustedSizeY = std::max(
							(std::int16_t)(anim.NormalizedHotspotY + anim.LargestOffsetY),
							anim.AdjustedSizeY
						);

						std::int32_t dpos = (frame.ImageAddr + 4);

						imageDataBlock.SeekTo(dpos - 4);
						std::uint16_t width2 = imageDataBlock.ReadUInt16();
						imageDataBlock.SeekTo(dpos - 2);
						/*std::uint16_t height2 =*/ imageDataBlock.ReadUInt16();

						frame.DrawTransparent = (width2 & 0x8000) > 0;

						std::int32_t pxRead = 0;
						std::int32_t pxTotal = (frame.SizeX * frame.SizeY);
						bool lastOpEmpty = true;

						frame.ImageData = std::make_unique<std::uint8_t[]>(pxTotal);

						imageDataBlock.SeekTo(dpos);

						while (pxRead < pxTotal) {
							std::uint8_t op = imageDataBlock.ReadByte();
							if (op < 0x80) {
								// Skip the given number of pixels, writing them with the transparent color 0, array should be already zeroed
								pxRead += op;
							} else if (op == 0x80) {
								// Skip until the end of the line, array should be already zeroed
								std::uint16_t linePxLeft = (std::uint16_t)(frame.SizeX - pxRead % frame.SizeX);
								if (pxRead % frame.SizeX == 0 && !lastOpEmpty) {
									linePxLeft = 0;
								}

								pxRead += linePxLeft;
							} else {
								// Copy specified amount of pixels (ignoring the high bit)
								std::uint16_t bytesToRead = (std::uint16_t)(op & 0x7F);
								imageDataBlock.ReadRawBytes(frame.ImageData.get() + pxRead, bytesToRead);
								pxRead += bytesToRead;
							}

							lastOpEmpty = (op == 0x80);
						}

						// TODO: Sprite mask
						/*frame.MaskData = std::make_unique<std::uint8_t[]>(pxTotal);

						if (frame.MaskAddr != 0xFFFFFFFF) {
							imageDataBlock.SeekTo(frame.MaskAddr);
							pxRead = 0;
							while (pxRead < pxTotal) {
								std::uint8_t b = imageDataBlock.ReadByte();
								for (std::uint8_t bit = 0; bit < 8 && (pxRead + bit) < pxTotal; ++bit) {
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

			for (std::uint16_t j = 0; j < sndCount; j++) {
				SampleSection& sample = samples.emplace_back();
				sample.IdInSet = j;
				sample.Set = i;

				std::int32_t totalSize = sampleDataBlock.ReadInt32();
				std::uint32_t magicRIFF = sampleDataBlock.ReadUInt32();
				std::int32_t chunkSize = sampleDataBlock.ReadInt32();
				// "ASFF" for 1.20, "AS  " for 1.24
				std::uint32_t format = sampleDataBlock.ReadUInt32();
				DEATH_ASSERT(format == 0x46465341 || format == 0x20205341, "Invalid sound format", JJ2Version::Unknown);
				bool isASFF = (format == 0x46465341);

				std::uint32_t magicSAMP = sampleDataBlock.ReadUInt32();
				/*std::uint32_t sampSize =*/ sampleDataBlock.ReadUInt32();
				DEATH_ASSERT(magicRIFF == 0x46464952 && magicSAMP == 0x504D4153, "Invalid sound format", JJ2Version::Unknown);

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

				sample.Data = std::make_unique<std::uint8_t[]>(sample.DataSize);
				sampleDataBlock.ReadRawBytes(sample.Data.get(), sample.DataSize);
				// Padding #3
				sampleDataBlock.DiscardBytes(4);

				/*if (sample.Data.Length < actualDataSize) {
					Log.Write(LogType.Warning, "Sample " + j + " in set " + i + " was shorter than expected! Expected "
						+ actualDataSize + " bytes, but read " + sample.Data.Length + " instead.");
				}*/

				if (totalSize > chunkSize + 12) {
					// Sample data is probably aligned to X bytes since the next sample doesn't always appear right after the first ends.
					LOGW("Adjusting read offset of sample {} in set {} by {} bytes.", j, i, (totalSize - chunkSize - 12));

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
			LOGE("Could not determine the version, header size: {} bytes", headerLen);
		}

		ImportAnimations(pakWriter, version, anims);
		ImportAudioSamples(pakWriter, version, samples);

		return version;
	}

	void JJ2Anims::ImportAnimations(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<AnimSection>& anims)
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

			std::int32_t sizeX = (anim.AdjustedSizeX + AddBorder * 2);
			std::int32_t sizeY = (anim.AdjustedSizeY + AddBorder * 2);
			// Determine the frame configuration to use. Each asset should fit into a texture of
			// MaxTextureSize², the smallest limit among the supported platforms.
			if (anim.FrameCount > 1) {
				// Pick the grid whose texture wastes the least memory once it is rounded up to power-of-two
				// dimensions. Graphics hardware that cannot sample non-power-of-two textures has to pad them,
				// and a layout chosen only to be roughly square lands just past a power of two surprisingly
				// often - a 669x552 sheet occupies a 1024x1024 texture, so almost two thirds of it is unused.
				// Choosing by padded area instead usually fills the texture almost completely, at no cost to
				// platforms that sample the sheet at its exact size.
				std::int32_t bestColumns = 0, bestRows = 0;
				std::int64_t bestCost = INT64_MAX;
				// If no layout fits (kept below as a fallback), take the one that pads to the smallest
				// texture anyway - a mildly oversized square sheet still beats a FrameCount x 1 strip
				std::int32_t fallbackColumns = 0, fallbackRows = 0;
				std::int64_t fallbackCost = INT64_MAX;
				// The configuration is stored in a byte per axis, so neither may exceed 255
				const std::int32_t maxColumns = std::min<std::int32_t>(anim.FrameCount, 255);
				for (std::int32_t columns = 1; columns <= maxColumns; columns++) {
					const std::int32_t rows = (anim.FrameCount + columns - 1) / columns;
					if (rows > 255) {
						continue;
					}
					const std::int32_t width = columns * sizeX;
					const std::int32_t height = rows * sizeY;
					const std::int64_t paddedArea = std::int64_t(NextPowerOfTwo(width)) * NextPowerOfTwo(height);
					if (width > MaxTextureSize || height > MaxTextureSize) {
						if (paddedArea < fallbackCost) {
							fallbackCost = paddedArea;
							fallbackColumns = columns;
							fallbackRows = rows;
						}
						continue;
					}

					// Prefer the layout that pads to the smallest texture; among equals prefer the one that
					// wastes fewer cells in the grid itself, then the more square one, so the choice is stable
					const std::int64_t emptyCells = std::int64_t(columns) * rows - anim.FrameCount;
					const std::int64_t cost = (paddedArea * 1024 + emptyCells * 16) * 1024 + std::abs(width - height);
					if (cost < bestCost) {
						bestCost = cost;
						bestColumns = columns;
						bestRows = rows;
					}
				}
				if (bestColumns == 0) {
					LOGW("No frame configuration of {}:{} fits into a {}x{} texture ({} frames of {}x{})",
						anim.Set, anim.Anim, MaxTextureSize, MaxTextureSize, anim.FrameCount, sizeX, sizeY);
					bestColumns = (fallbackColumns > 0 ? fallbackColumns : maxColumns);
					bestRows = (fallbackRows > 0 ? fallbackRows : 255);
				}

				anim.FrameConfigurationX = (std::uint8_t)bestColumns;
				anim.FrameConfigurationY = (std::uint8_t)bestRows;
			} else {
				anim.FrameConfigurationX = (std::uint8_t)anim.FrameCount;
				anim.FrameConfigurationY = 1;
			}

			// TODO: Hardcoded name
			bool applyToasterPowerUpFix = (entry->Category == "Object"_s && entry->Name == "powerup_upgrade_toaster"_s);
			if (applyToasterPowerUpFix) {
				LOGI("Applying \"Toaster PowerUp\" palette fix to {}:{}", anim.Set, anim.Anim);
			}

			bool applyVineFix = (entry->Category == "Object"_s && entry->Name == "vine"_s);
			if (applyVineFix) {
				LOGI("Applying \"Vine\" palette fix to {}:{}", anim.Set, anim.Anim);
			}

			bool applyFlyCarrotFix = (entry->Category == "Pickup"_s && entry->Name == "carrot_fly"_s);
			if (applyFlyCarrotFix) {
				// This image has 4 wrong pixels that should be transparent
				LOGI("Applying \"Fly Carrot\" image fix to {}:{}", anim.Set, anim.Anim);
			}

			bool playerFlareFix = ((entry->Category == "Jazz"_s || entry->Category == "Spaz"_s) && (entry->Name == "shoot_ver"_s || entry->Name == "vine_shoot_up"_s));
			if (playerFlareFix) {
				// This image has already applied weapon flare, remove it
				LOGI("Applying \"Player Flare\" image fix to {}:{}", anim.Set, anim.Anim);
			}

			String filename;
			if (entry->Name.empty()) {
				LOGE("Entry name is empty");
				continue;
			}

			filename = fs::CombinePath({ "Animations"_s, entry->Category, String(entry->Name + ".aura"_s) });

			// Pack the frames tightly when they fit that way, otherwise keep the regular grid
			SmallVector<PackedFrame, 0> packedFrames;
			std::int32_t sheetWidth = 0, sheetHeight = 0;
			const bool tightlyPacked = PackFramesTightly(anim, AddBorder, packedFrames, sheetWidth, sheetHeight);
			if (!tightlyPacked) {
				sheetWidth = sizeX * anim.FrameConfigurationX;
				sheetHeight = sizeY * anim.FrameConfigurationY;
			}

			std::int32_t stride = sheetWidth;
			std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(sheetWidth * sheetHeight * 4);

			for (std::int32_t j = 0; j < (std::int32_t)anim.Frames.size(); j++) {
				auto& frame = anim.Frames[j];

				std::int32_t offsetX = anim.NormalizedHotspotX + frame.HotspotX;
				std::int32_t offsetY = anim.NormalizedHotspotY + frame.HotspotY;

				// Where this frame's top-left corner lands in the sheet
				std::int32_t frameBaseX, frameBaseY;
				if (tightlyPacked) {
					frameBaseX = packedFrames[j].X;
					frameBaseY = packedFrames[j].Y;
				} else {
					frameBaseX = (j % anim.FrameConfigurationX) * sizeX + offsetX;
					frameBaseY = (j / anim.FrameConfigurationX) * sizeY + offsetY;
				}

				for (std::int32_t y = 0; y < frame.SizeY; y++) {
					for (std::int32_t x = 0; x < frame.SizeX; x++) {
						std::int32_t targetX = frameBaseX + x + (tightlyPacked ? 0 : AddBorder);
						std::int32_t targetY = frameBaseY + y + (tightlyPacked ? 0 : AddBorder);
						std::uint8_t colorIdx = frame.ImageData[frame.SizeX * y + x];

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
							std::uint8_t a;
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
							std::uint8_t a;
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
				LOGI("Applying \"Lori\" hotspot fix to {}:{}", anim.Set, anim.Anim);
				anim.NormalizedHotspotX = 20;
				anim.NormalizedHotspotY = 4;
			}

			MemoryStream so(16384);
			std::int32_t totalPixels = sheetWidth * sheetHeight;
			std::int32_t outChannels = 4;
			std::unique_ptr<std::uint8_t[]> packed;
			const std::uint8_t* outData = pixels.get();
			// Indexed sprites (default Sprite palette) keep the palette index in the red channel and are recolored
			// in-game through the palette texture. Save them with the fewest channels so no per-pixel work is
			// needed at load: 1 (index only), or 2 (index + alpha) when any pixel is partially transparent
			// (DrawTransparent). True-color palettes (e.g., Menu) stay RGBA.
			if (entry->Palette == JJ2DefaultPalette::Sprite) {
				bool hasPartialAlpha = false;
				for (std::int32_t i = 0; i < totalPixels; i++) {
					std::uint8_t a = pixels[(i * 4) + 3];
					if (a != 0 && a != 255) {
						hasPartialAlpha = true;
						break;
					}
				}
				outChannels = (hasPartialAlpha ? 2 : 1);
				packed = std::make_unique<std::uint8_t[]>(totalPixels * outChannels);
				if (outChannels == 2) {
					for (std::int32_t i = 0; i < totalPixels; i++) {
						packed[(i * 2) + 0] = pixels[(i * 4) + 0]; // palette index (red channel)
						packed[(i * 2) + 1] = pixels[(i * 4) + 3]; // alpha
					}
				} else {
					for (std::int32_t i = 0; i < totalPixels; i++) {
						packed[i] = pixels[(i * 4) + 0]; // palette index (red channel)
					}
				}
				outData = packed.get();
			}

			PackedSheet packedSheet;
			if (tightlyPacked) {
				packedSheet.Frames = &packedFrames;
				packedSheet.Width = sheetWidth;
				packedSheet.Height = sheetHeight;
			}
			WriteImageToStream(so, outData, sizeX, sizeY, outChannels, anim, entry, packedSheet);
			so.Seek(0, SeekOrigin::Begin);
			bool success = pakWriter.AddFile(so, filename, PakPreferredCompression::Deflate);
			DEATH_ASSERT(success, "Failed to add file to .pak container", );

			/*if (!string.IsNullOrEmpty(data.Name) && !data.SkipNormalMap) {
				PngWriter normalMap = NormalMapGenerator.FromSprite(img,
						new Point(currentAnim.FrameConfigurationX, currentAnim.FrameConfigurationY),
						!data.AllowRealtimePalette && data.Palette == JJ2DefaultPalette.ByIndex ? JJ2DefaultPalette.Sprite : null);

				normalMap.Save(filename.Replace(".png", ".n.png"));
			}*/
		}
	}

	void JJ2Anims::ImportAudioSamples(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<SampleSection>& samples)
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
				LOGE("Entry name is empty");
				continue;
			}

			filename = fs::CombinePath({ "Animations"_s, entry->Category, String(entry->Name + ".wav"_s) });

			MemoryStream so(16384);

			// TODO: The modulo here essentially clips the sample to 8- or 16-bit.
			// There are some samples (at least the Rapier random noise) that at least get reported as 24-bit
			// by the read header data. It is not clear if they actually are or if the header data is just
			// read incorrectly, though - one would think the data would need to be reshaped between 24 and 8
			// but it works just fine as is.
			std::int32_t bytesPerSample = (sample.Multiplier / 4) % 2 + 1;
			std::int32_t dataOffset = 0;
			if (sample.Data[0] == 0x00 && sample.Data[1] == 0x00 && sample.Data[2] == 0x00 && sample.Data[3] == 0x00 &&
				(sample.Data[4] != 0x00 || sample.Data[5] != 0x00 || sample.Data[6] != 0x00 || sample.Data[7] != 0x00) &&
				(sample.Data[7] == 0x00 || sample.Data[8] == 0x00)) {
				// Trim first 8 samples (bytes) to prevent popping
				dataOffset = 8;
			}

			// Create PCM wave file
			// Main header
			so.Write("RIFF", 4);
			so.WriteValueAsLE<std::uint32_t>(36 + sample.DataSize - dataOffset); // File size
			so.Write("WAVE", 4);

			// Format header
			so.Write("fmt ", 4);
			so.WriteValueAsLE<std::uint32_t>(16); // Header remainder length
			so.WriteValueAsLE<std::uint16_t>(1); // Format = PCM
			so.WriteValueAsLE<std::uint16_t>(1); // Channels
			so.WriteValueAsLE<std::uint32_t>(sample.SampleRate); // Sample rate
			so.WriteValueAsLE<std::uint32_t>(sample.SampleRate * bytesPerSample); // Bytes per second
			so.WriteValueAsLE<std::uint32_t>(bytesPerSample * 0x00080001);

			// Payload
			so.Write("data", 4);
			so.WriteValueAsLE<std::uint32_t>(sample.DataSize - dataOffset); // Payload size
			for (std::uint32_t k = dataOffset; k < sample.DataSize; k++) {
				so.WriteValue<std::uint8_t>((bytesPerSample << 7) ^ sample.Data[k]);
			}

			so.Seek(0, SeekOrigin::Begin);
			bool success = pakWriter.AddFile(so, filename, PakPreferredCompression::Deflate);
			DEATH_ASSERT(success, "Failed to add file to .pak container", );
		}
	}

	void JJ2Anims::WriteImageToFile(StringView targetPath, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry)
	{
		FileStream so(targetPath, FileAccess::Write);
		DEATH_ASSERT(so.IsValid(), "Cannot open file for writing", );
		WriteImageToStream(so, data, width, height, channelCount, anim, entry, PackedSheet());
	}

	void JJ2Anims::WriteImageToStream(Stream& targetStream, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount, const AnimSection& anim, AnimSetMapping::Entry* entry, const PackedSheet& packedSheet)
	{
		const bool tightlyPacked = (packedSheet.Frames != nullptr && !packedSheet.Frames->empty());
		std::uint8_t flags = 0x00;
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
			if (tightlyPacked) {
				// The frames don't form a grid, so their positions are listed after the header
				flags |= 0x04;
			}
		}

		targetStream.WriteValueAsLE<std::uint64_t>(0xB8EF8498E2BFBBEF);
		targetStream.WriteValueAsLE<std::uint16_t>(0x208F);
		targetStream.WriteValue<std::uint8_t>(0x02); // Version 2 is reserved for sprites (or bigger images)
		targetStream.WriteValue<std::uint8_t>(flags);

		targetStream.WriteValue<std::uint8_t>(channelCount);
		targetStream.WriteValueAsLE<std::uint32_t>(width);
		targetStream.WriteValueAsLE<std::uint32_t>(height);

		// Include Sprite extension
		if (entry != nullptr) {
			targetStream.WriteValue<std::uint8_t>(anim.FrameConfigurationX);
			targetStream.WriteValue<std::uint8_t>(anim.FrameConfigurationY);
			targetStream.WriteValueAsLE<std::uint16_t>(anim.FrameCount);
			targetStream.WriteValueAsLE<std::uint16_t>(anim.FrameRate == 0 ? 0 : 256 * 5 / anim.FrameRate);

			if (anim.NormalizedHotspotX != 0 || anim.NormalizedHotspotY != 0) {
				targetStream.WriteValueAsLE<std::uint16_t>(anim.NormalizedHotspotX + AddBorder);
				targetStream.WriteValueAsLE<std::uint16_t>(anim.NormalizedHotspotY + AddBorder);
			} else {
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
			}
			if (anim.Frames[0].ColdspotX != 0 || anim.Frames[0].ColdspotY != 0) {
				targetStream.WriteValueAsLE<std::uint16_t>((anim.NormalizedHotspotX + anim.Frames[0].HotspotX) - anim.Frames[0].ColdspotX + AddBorder);
				targetStream.WriteValueAsLE<std::uint16_t>((anim.NormalizedHotspotY + anim.Frames[0].HotspotY) - anim.Frames[0].ColdspotY + AddBorder);
			} else {
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
			}
			if (anim.Frames[0].GunspotX != 0 || anim.Frames[0].GunspotY != 0) {
				targetStream.WriteValueAsLE<std::uint16_t>((anim.NormalizedHotspotX + anim.Frames[0].HotspotX) - anim.Frames[0].GunspotX + AddBorder);
				targetStream.WriteValueAsLE<std::uint16_t>((anim.NormalizedHotspotY + anim.Frames[0].HotspotY) - anim.Frames[0].GunspotY + AddBorder);
			} else {
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
				targetStream.WriteValueAsLE<std::uint16_t>(UINT16_MAX);
			}

			if (tightlyPacked) {
				// The sheet's own size, then where every frame sits in it
				targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)packedSheet.Width);
				targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)packedSheet.Height);

				for (const PackedFrame& frame : *packedSheet.Frames) {
					targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)frame.X);
					targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)frame.Y);
					targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)frame.W);
					targetStream.WriteValueAsLE<std::uint16_t>((std::uint16_t)frame.H);
					targetStream.WriteValueAsLE<std::int16_t>((std::int16_t)frame.OffsetX);
					targetStream.WriteValueAsLE<std::int16_t>((std::int16_t)frame.OffsetY);
				}

				width = packedSheet.Width;
				height = packedSheet.Height;
			} else {
				width *= anim.FrameConfigurationX;
				height *= anim.FrameConfigurationY;
			}
		}

		WriteImageContent(targetStream, data, width, height, channelCount);
	}

	void JJ2Anims::WriteImageContent(Stream& so, const std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount)
	{
		typedef union {
			struct {
				std::uint8_t r, g, b, a;
			} rgba;
			std::uint32_t v;
		} rgba_t;

		#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
		#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
		#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
		#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
		#define QOI_OP_RGB    0xfe /* 11111110 */
		#define QOI_OP_RGBA   0xff /* 11111111 */

		#define QOI_MASK_2    0xc0 /* 11000000 */

		#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)

		auto pixels = (const std::uint8_t*)data;

		rgba_t index[64] {};
		rgba_t px, px_prev;

		std::int32_t run = 0;
		px_prev.rgba.r = 0;
		px_prev.rgba.g = 0;
		px_prev.rgba.b = 0;
		px_prev.rgba.a = 255;
		px = px_prev;

		std::int32_t px_len = width * height * channelCount;
		std::int32_t px_end = px_len - channelCount;

		for (std::int32_t px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (channelCount >= 4) {
				px = *(rgba_t*)(pixels + px_pos);
			} else {
				// Fewer channels (1 = palette index, 2 = index + alpha) are packed into r/g; b stays 0, a stays 255
				px.rgba.r = pixels[px_pos + 0];
				px.rgba.g = (channelCount >= 2 ? pixels[px_pos + 1] : 0);
				px.rgba.b = (channelCount >= 3 ? pixels[px_pos + 2] : 0);
				px.rgba.a = 255;
			}

			if (px.v == px_prev.v) {
				run++;
				if (run == 62 || px_pos == px_end) {
					so.WriteValue<std::uint8_t>(QOI_OP_RUN | (run - 1));
					run = 0;
				}
			} else {
				std::int32_t index_pos;

				if (run > 0) {
					so.WriteValue<std::uint8_t>(QOI_OP_RUN | (run - 1));
					run = 0;
				}

				index_pos = QOI_COLOR_HASH(px) & (64 - 1);

				if (index[index_pos].v == px.v) {
					so.WriteValue<std::uint8_t>(QOI_OP_INDEX | index_pos);
				} else {
					index[index_pos] = px;

					if (px.rgba.a == px_prev.rgba.a) {
						std::int8_t vr = px.rgba.r - px_prev.rgba.r;
						std::int8_t vg = px.rgba.g - px_prev.rgba.g;
						std::int8_t vb = px.rgba.b - px_prev.rgba.b;

						std::int8_t vg_r = vr - vg;
						std::int8_t vg_b = vb - vg;

						if (
							vr > -3 && vr < 2 &&
							vg > -3 && vg < 2 &&
							vb > -3 && vb < 2
						) {
							so.WriteValue<std::uint8_t>(QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
						} else if (
							vg_r >  -9 && vg_r < 8 &&
							vg   > -33 && vg   < 32 &&
							vg_b >  -9 && vg_b < 8
						) {
							so.WriteValue<std::uint8_t>(QOI_OP_LUMA | (vg + 32));
							so.WriteValue<std::uint8_t>((vg_r + 8) << 4 | (vg_b + 8));
						} else {
							so.WriteValue<std::uint8_t>(QOI_OP_RGB);
							so.WriteValue<std::uint8_t>(px.rgba.r);
							if (channelCount >= 2) {
								so.WriteValue<std::uint8_t>(px.rgba.g);
							}
							if (channelCount >= 3) {
								so.WriteValue<std::uint8_t>(px.rgba.b);
							}
						}
					} else {
						so.WriteValue<std::uint8_t>(QOI_OP_RGBA);
						so.WriteValue<std::uint8_t>(px.rgba.r);
						so.WriteValue<std::uint8_t>(px.rgba.g);
						so.WriteValue<std::uint8_t>(px.rgba.b);
						so.WriteValue<std::uint8_t>(px.rgba.a);
					}
				}
			}
			px_prev = px;
		}
	}

	void JJ2Anims::ReadImageContent(Stream& s, std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t channelCount)
	{
		typedef union {
			struct {
				std::uint8_t r, g, b, a;
			} rgba;
			std::uint32_t v;
		} rgba_t;

		rgba_t index[64] {};
		rgba_t px;
		std::int32_t run = 0;
		std::int32_t px_len = width * height * channelCount;

		px.rgba.r = 0;
		px.rgba.g = 0;
		px.rgba.b = 0;
		px.rgba.a = 255;

		for (std::int32_t px_pos = 0; px_pos < px_len; px_pos += channelCount) {
			if (run > 0) {
				run--;
			} else {
				std::int32_t b1 = s.ReadValue<std::uint8_t>();

				if (b1 == QOI_OP_RGB) {
					px.rgba.r = s.ReadValue<std::uint8_t>();
					px.rgba.g = (channelCount >= 2 ? s.ReadValue<std::uint8_t>() : 0);
					px.rgba.b = (channelCount >= 3 ? s.ReadValue<std::uint8_t>() : 0);
				} else if (b1 == QOI_OP_RGBA) {
					px.rgba.r = s.ReadValue<std::uint8_t>();
					px.rgba.g = s.ReadValue<std::uint8_t>();
					px.rgba.b = s.ReadValue<std::uint8_t>();
					px.rgba.a = s.ReadValue<std::uint8_t>();
				} else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
					px = index[b1];
				} else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
					px.rgba.r += ((b1 >> 4) & 0x03) - 2;
					px.rgba.g += ((b1 >> 2) & 0x03) - 2;
					px.rgba.b += (b1 & 0x03) - 2;
				} else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
					std::int32_t b2 = s.ReadValue<std::uint8_t>();
					std::int32_t vg = (b1 & 0x3f) - 32;
					px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
					px.rgba.g += vg;
					px.rgba.b += vg - 8 + (b2 & 0x0f);
				} else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
					run = (b1 & 0x3f);
				}

				index[QOI_COLOR_HASH(px) & (64 - 1)] = px;
			}

			*(rgba_t*)(data + px_pos) = px;
		}
	}
}