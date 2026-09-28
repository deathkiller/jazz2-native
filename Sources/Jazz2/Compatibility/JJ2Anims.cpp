#include "JJ2Anims.h"
#include "JJ2Anims.Palettes.h"
#include "JJ2Block.h"
#include "AnimSetMapping.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/FileStream.h>
#include <IO/MemoryStream.h>

#if defined(WITH_LZ4)
#	include <lz4.h>
#	include <lz4hc.h>
#endif

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	namespace
	{
		std::int32_t NextPowerOfTwo(std::int32_t value)
		{
			std::int32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}

		// A sheet is split into at most this many pages along each axis
		constexpr std::int32_t MaxPagesPerAxis = (JJ2Anims::MaxSheetSize + JJ2Anims::SheetPageSize - 1) / JJ2Anims::SheetPageSize;

		/** @brief One row of the shelf packer, no taller than the rectangle that opened it */
		struct Shelf
		{
			std::int32_t Y, Height;
			/** @brief Where the next rectangle goes in each page column the shelf crosses */
			std::int32_t Cursor[MaxPagesPerAxis];
		};

		/**
			@brief Tries to lay the rectangles out in a sheet of the given width, see JJ2Anims::PackRectangles()

			First fit: every rectangle, tallest first, goes into the first shelf that still has room for it,
			and only when none has is a new shelf opened below the others - which, unlike filling one shelf
			after another, puts the small rectangles at the end into the gaps the large ones left.

			A rectangle never lies across a multiple of `pageSize`. Along the width, each shelf keeps a separate
			cursor for every page column, so a rectangle that would run across the line starts the next column
			instead. Along the height, a new shelf opens in the first band of pages that still has room for all
			of it - so the small shelves at the end fill up the bottom of the bands the large ones left - but
			that is only done when `banded` is set: without it everything has to fit into a single page's
			height, which is tried first. Every column and band except the last also keeps `spacing` free
			before its page line, because a backend that samples the sheet whole draws the rectangles on both
			sides of it as the neighbours they are. A `pageSize` of at least `maxSize` disables all of that.

			`usedWidth` and `usedHeight` receive the extent of what was placed, also when not everything was.
		*/
		bool TryPackIntoWidth(SmallVectorImpl<JJ2Anims::PackedFrame>& rects, const SmallVectorImpl<std::int32_t>& order,
			std::int32_t width, std::int32_t spacing, std::int32_t pageSize, std::int32_t maxSize, bool banded,
			SmallVectorImpl<Shelf>& shelves, std::int32_t& usedWidth, std::int32_t& usedHeight)
		{
			usedWidth = 0;
			usedHeight = 0;

			const std::int32_t columnCount = (width + pageSize - 1) / pageSize;
			const std::int32_t bandCount = (banded ? (maxSize + pageSize - 1) / pageSize : 1);
			if (columnCount > MaxPagesPerAxis || bandCount > MaxPagesPerAxis) {
				return false;
			}

			std::int32_t columnEnd[MaxPagesPerAxis];
			for (std::int32_t c = 0; c < columnCount; c++) {
				columnEnd[c] = (c < columnCount - 1 ? (c + 1) * pageSize - spacing : width);
			}
			std::int32_t bandEnd[MaxPagesPerAxis], bandFill[MaxPagesPerAxis];
			for (std::int32_t b = 0; b < bandCount; b++) {
				bandEnd[b] = (b < bandCount - 1 ? (b + 1) * pageSize - spacing : std::min((b + 1) * pageSize, maxSize));
				bandFill[b] = b * pageSize;
			}

			shelves.clear();
			for (std::int32_t i : order) {
				JJ2Anims::PackedFrame& rect = rects[i];
				bool placed = false;
				for (Shelf& shelf : shelves) {
					if (rect.H > shelf.Height) {
						continue;
					}
					for (std::int32_t c = 0; c < columnCount; c++) {
						if (shelf.Cursor[c] + rect.W <= columnEnd[c]) {
							rect.X = shelf.Cursor[c];
							rect.Y = shelf.Y;
							shelf.Cursor[c] += rect.W + spacing;
							placed = true;
							break;
						}
					}
					if (placed) {
						break;
					}
				}

				if (!placed) {
					std::int32_t band = 0;
					while (band < bandCount && bandFill[band] + rect.H > bandEnd[band]) {
						band++;
					}
					if (band >= bandCount) {
						return false;
					}

					Shelf& shelf = shelves.emplace_back();
					shelf.Y = bandFill[band];
					shelf.Height = rect.H;
					bandFill[band] += rect.H + spacing;
					for (std::int32_t c = 0; c < columnCount; c++) {
						shelf.Cursor[c] = c * pageSize;
					}
					for (std::int32_t c = 0; c < columnCount && !placed; c++) {
						if (shelf.Cursor[c] + rect.W <= columnEnd[c]) {
							rect.X = shelf.Cursor[c];
							rect.Y = shelf.Y;
							shelf.Cursor[c] += rect.W + spacing;
							placed = true;
						}
					}
					if (!placed) {
						return false;		// Wider than any column
					}
				}

				usedWidth = std::max(usedWidth, rect.X + rect.W);
				usedHeight = std::max(usedHeight, rect.Y + rect.H);
			}
			return true;
		}
	}

	bool JJ2Anims::PackRectangles(SmallVectorImpl<PackedFrame>& rects, std::int32_t spacing, std::int32_t& sheetWidth, std::int32_t& sheetHeight)
	{
		const std::int32_t count = std::int32_t(rects.size());
		if (count <= 0) {
			return false;
		}

		std::int32_t widest = 1;
		for (const PackedFrame& rect : rects) {
			widest = std::max(widest, rect.W);
		}

		// Tallest first, so each shelf is filled by rectangles of similar height. The index decides between
		// equal sizes, which keeps the layout the same whatever std::sort does with them.
		SmallVector<std::int32_t, 0> order(count);
		for (std::int32_t i = 0; i < count; i++) {
			order[i] = i;
		}
		std::sort(order.begin(), order.end(), [&rects](std::int32_t a, std::int32_t b) {
			if (rects[a].H != rects[b].H) {
				return rects[a].H > rects[b].H;
			}
			if (rects[a].W != rects[b].W) {
				return rects[a].W > rects[b].W;
			}
			return a < b;
		});

		// Try the sheet widths that could hold the widest rectangle and keep the best result - by the padded
		// area first, then whether the sheet fits into one page, then the exact area, then the more square one.
		// A layout depends on the width only through which rectangles fit where, so one that ends up narrower
		// than the width it was tried at comes out the same at every width down to its own - the widths are
		// tried from the widest down, and all of those are skipped.
		SmallVector<Shelf, 0> shelves;
		SmallVector<std::int32_t, 0> bestPositions(count * 2);
		std::int32_t bestWidth = 0, bestHeight = 0;
		std::int64_t bestPaddedArea = INT64_MAX, bestArea = INT64_MAX;
		bool bestExceedsPage = true;
		std::int32_t bestSkew = INT32_MAX;
		// Nothing across a page line if at all possible - the second pass, which ignores the pages, is only
		// reached when the rectangles cannot be laid out around them within the size limit at all
		for (std::int32_t pageSize : { SheetPageSize, MaxSheetSize }) {
			std::int32_t width = MaxSheetSize;
			while (width >= widest) {
				std::int32_t usedWidth, usedHeight;
				bool fits = TryPackIntoWidth(rects, order, width, spacing, pageSize, MaxSheetSize, false, shelves, usedWidth, usedHeight);
				std::int32_t reachedWidth = usedWidth;
				if (!fits && pageSize < MaxSheetSize) {
					// Taller than one page, so more than one band of pages it is
					fits = TryPackIntoWidth(rects, order, width, spacing, pageSize, MaxSheetSize, true, shelves, usedWidth, usedHeight);
					reachedWidth = std::max(reachedWidth, usedWidth);
				}

				if (fits) {
					const std::int64_t paddedArea = std::int64_t(NextPowerOfTwo(usedWidth)) * NextPowerOfTwo(usedHeight);
					const bool exceedsPage = (usedWidth > SheetPageSize || usedHeight > SheetPageSize);
					const std::int64_t area = std::int64_t(usedWidth) * usedHeight;
					const std::int32_t skew = std::abs(usedWidth - usedHeight);
					// The widths only get narrower from here, and the narrower one wins a complete tie
					if (paddedArea != bestPaddedArea ? paddedArea < bestPaddedArea
						: exceedsPage != bestExceedsPage ? !exceedsPage
						: area != bestArea ? area < bestArea
						: skew <= bestSkew) {
						bestPaddedArea = paddedArea;
						bestExceedsPage = exceedsPage;
						bestArea = area;
						bestSkew = skew;
						bestWidth = usedWidth;
						bestHeight = usedHeight;
						for (std::int32_t i = 0; i < count; i++) {
							bestPositions[i * 2] = rects[i].X;
							bestPositions[i * 2 + 1] = rects[i].Y;
						}
					}
				}

				// The same holds whether the attempt fitted or not - every decision up to where it ended would be
				// made the same way - but only as long as the width keeps its number of page columns
				const std::int32_t narrowestWithSameColumns = ((width - 1) / pageSize) * pageSize + 1;
				width = std::max(reachedWidth, narrowestWithSameColumns) - 1;
			}
			if (bestWidth > 0) {
				break;
			}
		}
		if (bestWidth <= 0) {
			return false;
		}

		for (std::int32_t i = 0; i < count; i++) {
			rects[i].X = bestPositions[i * 2];
			rects[i].Y = bestPositions[i * 2 + 1];
		}
		// Never empty, even when every rectangle is
		sheetWidth = std::max<std::int32_t>(bestWidth, 1);
		sheetHeight = std::max<std::int32_t>(bestHeight, 1);
		return true;
	}

	/**
		@brief Packs the frames of one animation so each keeps only the space it needs

		A frame's own extent is usually much smaller than the largest frame of its animation, and a grid
		of equal cells pays for the difference in every single frame. The frames are laid out by
		@ref PackRectangles(), which for sprite sheets - many similar heights, few outliers - comes within
		a few percent of the theoretical minimum while staying simple enough to reason about.
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
		}

		// Does not fit when it fails, the caller falls back to the regular grid
		return PackRectangles(packed, border, sheetWidth, sheetHeight);
	}

	JJ2Version JJ2Anims::Convert(StringView path, PakWriter& pakWriter, bool isPlus, ConversionProgress progress)
	{
		// Reading the sets decompresses the whole file, but writing the sprite sheets out has to compress
		// everything again, which is the slower direction by far - so that step is given most of the range
		ConversionProgress readProgress = progress.Narrow(0.0f, 0.2f);

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

			readProgress.ReportStep(i + 1, setCount);
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

		ImportAnimations(pakWriter, version, anims, progress.Narrow(0.2f, 0.9f));
		ImportAudioSamples(pakWriter, version, samples, progress.Narrow(0.9f, 1.0f));

		return version;
	}

	void JJ2Anims::ImportAnimations(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<AnimSection>& anims, ConversionProgress progress)
	{
		if (anims.empty()) {
			return;
		}

		LOGI("Importing animations...");

		AnimSetMapping animMapping = AnimSetMapping::GetAnimMapping(version);

		std::int32_t animsDone = 0;
		for (auto& anim : anims) {
			// Reported before the animation is written rather than after, because the loop skips the rest of
			// the body in a few places
			progress.ReportStep(++animsDone, (std::int32_t)anims.size());

			if (anim.FrameCount == 0) {
				continue;
			}

			AnimSetMapping::Entry* entry = animMapping.Get(anim.Set, anim.Anim);
			if (entry == nullptr || entry->Category == AnimSetMapping::Discard) {
				continue;
			}

			std::int32_t sizeX = (anim.AdjustedSizeX + AddBorder * 2);
			std::int32_t sizeY = (anim.AdjustedSizeY + AddBorder * 2);
			// Only reported if the grid is really used, most animations are packed tightly instead (see below)
			bool gridFits = true;
			// Determine the frame configuration to use. Each asset should fit into a texture of
			// MaxSheetSize², the smallest limit among the supported platforms.
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
					if (width > MaxSheetSize || height > MaxSheetSize) {
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
					gridFits = false;
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
				LOGI("Applying \"Toaster PowerUp\" palette fix to {}:{} \"{}/{}\"", anim.Set, anim.Anim, entry->Category, entry->Name);
			}

			bool applyVineFix = (entry->Category == "Object"_s && entry->Name == "vine"_s);
			if (applyVineFix) {
				LOGI("Applying \"Vine\" palette fix to {}:{} \"{}/{}\"", anim.Set, anim.Anim, entry->Category, entry->Name);
			}

			bool applyFlyCarrotFix = (entry->Category == "Pickup"_s && entry->Name == "carrot_fly"_s);
			if (applyFlyCarrotFix) {
				// This image has 4 wrong pixels that should be transparent
				LOGI("Applying \"Fly Carrot\" image fix to {}:{} \"{}/{}\"", anim.Set, anim.Anim, entry->Category, entry->Name);
			}

			bool playerFlareFix = ((entry->Category == "Jazz"_s || entry->Category == "Spaz"_s) && (entry->Name == "shoot_ver"_s || entry->Name == "vine_shoot_up"_s));
			if (playerFlareFix) {
				// This image has already applied weapon flare, remove it
				LOGI("Applying \"Player Flare\" image fix to {}:{} \"{}/{}\"", anim.Set, anim.Anim, entry->Category, entry->Name);
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
			if (tightlyPacked && std::any_of(packedFrames.begin(), packedFrames.end(), LiesAcrossPageLine)) {
				LOGW("Frames of {}:{} \"{}/{}\" lie across a {}-pixel page line of its {}x{} sheet, a platform that splits the texture into pages draws them cut off",
					anim.Set, anim.Anim, entry->Category, entry->Name, SheetPageSize, sheetWidth, sheetHeight);
			}
			if (!tightlyPacked) {
				sheetWidth = sizeX * anim.FrameConfigurationX;
				sheetHeight = sizeY * anim.FrameConfigurationY;
				if (!gridFits) {
					LOGW("No frame configuration of {}:{} \"{}/{}\" fits into a {}x{} texture ({} frames of {}x{})",
						anim.Set, anim.Anim, entry->Category, entry->Name, MaxSheetSize, MaxSheetSize, anim.FrameCount, sizeX, sizeY);
				}
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
				LOGI("Applying \"Lori\" hotspot fix to {}:{} \"{}/{}\"", anim.Set, anim.Anim, entry->Category, entry->Name);
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
			// LZ4 content is stored as it is: compressing it again would only put an inflate in front of the
			// decoder it was chosen for
			bool success = pakWriter.AddFile(so, filename, PreferredImageCompression == ImageCompression::Lz4
				? PakPreferredCompression::None : PakPreferredCompression::Deflate);
			DEATH_ASSERT(success, "Failed to add file to .pak container", );

			/*if (!string.IsNullOrEmpty(data.Name) && !data.SkipNormalMap) {
				PngWriter normalMap = NormalMapGenerator.FromSprite(img,
						new Point(currentAnim.FrameConfigurationX, currentAnim.FrameConfigurationY),
						!data.AllowRealtimePalette && data.Palette == JJ2DefaultPalette.ByIndex ? JJ2DefaultPalette.Sprite : null);

				normalMap.Save(filename.Replace(".png", ".n.png"));
			}*/
		}
	}

	void JJ2Anims::ImportAudioSamples(PakWriter& pakWriter, JJ2Version version, SmallVectorImpl<SampleSection>& samples, ConversionProgress progress)
	{
		if (samples.empty()) {
			return;
		}

		LOGI("Importing audio samples...");

		AnimSetMapping mapping = AnimSetMapping::GetSampleMapping(version);

		std::int32_t samplesDone = 0;
		for (auto& sample : samples) {
			// Reported before the sample is written rather than after, because the loop skips the rest of the
			// body in a few places
			progress.ReportStep(++samplesDone, (std::int32_t)samples.size());

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
#if defined(WITH_LZ4)
		if (PreferredImageCompression == ImageCompression::Lz4) {
			flags |= ImageContentLz4Flag;
		}
#endif

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

#if defined(WITH_LZ4)
		if (PreferredImageCompression == ImageCompression::Lz4) {
			// A sheet is read whole, so it is a single block
			WriteImageContentLz4(targetStream, data, width, height, channelCount, height);
			return;
		}
#endif
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
		ImageContentDecoder decoder;
		decoder.Decode(s, data, width * height, channelCount);
	}

	JJ2Anims::ImageContentDecoder::ImageContentDecoder()
		: _index{}, _px{0, 0, 0, 255}, _run(0)
	{
	}

	void JJ2Anims::ImageContentDecoder::Decode(Stream& s, std::uint8_t* data, std::int32_t pixelCount, std::int32_t channelCount)
	{
		typedef union {
			struct {
				std::uint8_t r, g, b, a;
			} rgba;
			std::uint32_t v;
		} rgba_t;

		// The state lives in the object between calls; the loop works on local copies
		rgba_t index[64];
		rgba_t px;
		std::int32_t run = _run;
		std::memcpy(index, _index, sizeof(index));
		std::memcpy(&px, _px, sizeof(px));

		const std::int32_t px_len = pixelCount * channelCount;
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

			// Copy exactly the pixel's channels: storing the whole union regardless of the channel count
			// would land on positions that are not 4-aligned for 1- and 2-channel images - a trap on
			// strict-alignment targets (MIPS) - and would also write past the last pixel of the buffer
			std::memcpy(data + px_pos, &px, channelCount);
		}

		_run = run;
		std::memcpy(_index, index, sizeof(index));
		std::memcpy(_px, &px, sizeof(px));
	}

	namespace
	{
		typedef union {
			struct {
				std::uint8_t r, g, b, a;
			} rgba;
			std::uint32_t v;
		} DecoderPixel;

		/**
			@brief The in-memory decode loop, specialized per channel count

			The pixel store is the only thing that depends on the channel count, and with it a template
			parameter it is a fixed one- to four-byte store instead of a `memcpy` call per pixel - which on
			the consoles' in-order CPUs was the largest single cost of decoding a sheet. The decoder state
			(index table, previous pixel, run) is maintained exactly as the generic loop does, whatever the
			channel count, because the DIFF/LUMA/INDEX operations and the hash read every component.

			Two things the generic loop does per pixel are hoisted out, which took a third off decoding a
			tileset on the Nintendo 64 (where it is most of what loading a level costs):
			- An INDEX operation does not store its pixel back into the table. The encoder emits one only
			  when the table already holds that pixel at its own hash, so the store (and the hash computed
			  for it) always rewrote the same entry. Tilesets are more than 40% INDEX pixels.
			- The end of the input is checked once per operation rather than once per byte while at least
			  one whole operation (5 bytes at most) is left; past that point every read is checked as before.
			A run fills its pixels in a loop of its own.
		*/
		template<std::int32_t Channels>
		DEATH_ALWAYS_INLINE void StoreDecodedPixel(std::uint8_t* DEATH_RESTRICT data, DecoderPixel px)
		{
			if (Channels == 1) {
				data[0] = px.rgba.r;
			} else if (Channels == 2) {
				data[0] = px.rgba.r; data[1] = px.rgba.g;
			} else if (Channels == 3) {
				data[0] = px.rgba.r; data[1] = px.rgba.g; data[2] = px.rgba.b;
			} else {
				data[0] = px.rgba.r; data[1] = px.rgba.g; data[2] = px.rgba.b; data[3] = px.rgba.a;
			}
		}

		template<std::int32_t Channels>
		void DecodeFromMemory(const std::uint8_t*& src, const std::uint8_t* end, DecoderPixel* index, DecoderPixel& pxRef, std::int32_t& runRef,
			std::uint8_t* DEATH_RESTRICT data, std::int32_t pixelCount)
		{
			// The largest operation (RGBA) is 5 bytes; with that much left no single read can pass the end
			constexpr std::ptrdiff_t MaxOpSize = 5;

			const std::uint8_t* DEATH_RESTRICT s = src;
			DecoderPixel px = pxRef;
			std::int32_t run = runRef;
			std::uint8_t* const dataEnd = data + std::ptrdiff_t(pixelCount) * Channels;

			while (data < dataEnd) {
				if (run > 0) {
					// The pixel is repeated; the run may continue past the end of this call (a band boundary)
					std::int32_t count = std::int32_t((dataEnd - data) / Channels);
					if (count > run) {
						count = run;
					}
					run -= count;
					for (std::int32_t i = 0; i < count; i++, data += Channels) {
						StoreDecodedPixel<Channels>(data, px);
					}
					continue;
				}

				std::int32_t b1;
				if DEATH_LIKELY(end - s >= MaxOpSize) {
					b1 = *s++;
					if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
						px = index[b1];
						StoreDecodedPixel<Channels>(data, px);
						data += Channels;
						continue;
					} else if (b1 == QOI_OP_RGB) {
						px.rgba.r = *s++;
						if (Channels >= 2) { px.rgba.g = *s++; } else { px.rgba.g = 0; }
						if (Channels >= 3) { px.rgba.b = *s++; } else { px.rgba.b = 0; }
					} else if (b1 == QOI_OP_RGBA) {
						px.rgba.r = s[0];
						px.rgba.g = s[1];
						px.rgba.b = s[2];
						px.rgba.a = s[3];
						s += 4;
					} else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
						px.rgba.r += ((b1 >> 4) & 0x03) - 2;
						px.rgba.g += ((b1 >> 2) & 0x03) - 2;
						px.rgba.b += (b1 & 0x03) - 2;
					} else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
						const std::int32_t b2 = *s++;
						const std::int32_t vg = (b1 & 0x3f) - 32;
						px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
						px.rgba.g += vg;
						px.rgba.b += vg - 8 + (b2 & 0x0f);
					} else {
						// QOI_OP_RUN - this pixel is the first of the run, the rest are filled above
						run = (b1 & 0x3f);
					}
				} else {
					// The tail of the input (or a truncated one), where every read is checked
					b1 = (s < end ? *s++ : 0);
					if (b1 == QOI_OP_RGB) {
						px.rgba.r = (s < end ? *s++ : 0);
						if (Channels >= 2) { px.rgba.g = (s < end ? *s++ : 0); } else { px.rgba.g = 0; }
						if (Channels >= 3) { px.rgba.b = (s < end ? *s++ : 0); } else { px.rgba.b = 0; }
					} else if (b1 == QOI_OP_RGBA) {
						px.rgba.r = (s < end ? *s++ : 0);
						px.rgba.g = (s < end ? *s++ : 0);
						px.rgba.b = (s < end ? *s++ : 0);
						px.rgba.a = (s < end ? *s++ : 0);
					} else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
						px = index[b1];
					} else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
						px.rgba.r += ((b1 >> 4) & 0x03) - 2;
						px.rgba.g += ((b1 >> 2) & 0x03) - 2;
						px.rgba.b += (b1 & 0x03) - 2;
					} else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
						const std::int32_t b2 = (s < end ? *s++ : 0);
						const std::int32_t vg = (b1 & 0x3f) - 32;
						px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
						px.rgba.g += vg;
						px.rgba.b += vg - 8 + (b2 & 0x0f);
					} else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
						run = (b1 & 0x3f);
					}
				}

				index[QOI_COLOR_HASH(px) & (64 - 1)] = px;
				StoreDecodedPixel<Channels>(data, px);
				data += Channels;
			}

			src = s;
			pxRef = px;
			runRef = run;
		}
	}

	void JJ2Anims::ImageContentDecoder::Decode(const std::uint8_t*& src, const std::uint8_t* end, std::uint8_t* data, std::int32_t pixelCount, std::int32_t channelCount)
	{
		DecoderPixel index[64];
		DecoderPixel px;
		std::int32_t run = _run;
		std::memcpy(index, _index, sizeof(index));
		std::memcpy(&px, _px, sizeof(px));

		switch (channelCount) {
			case 1: DecodeFromMemory<1>(src, end, index, px, run, data, pixelCount); break;
			case 2: DecodeFromMemory<2>(src, end, index, px, run, data, pixelCount); break;
			case 3: DecodeFromMemory<3>(src, end, index, px, run, data, pixelCount); break;
			default: DecodeFromMemory<4>(src, end, index, px, run, data, pixelCount); break;
		}

		_run = run;
		std::memcpy(_index, index, sizeof(index));
		std::memcpy(_px, &px, sizeof(px));
	}

	JJ2Anims::ImageCompression JJ2Anims::PreferredImageCompression = JJ2Anims::ImageCompression::Default;

#if defined(WITH_LZ4)
	void JJ2Anims::WriteImageContentLz4(Stream& so, const std::uint8_t* data, std::int32_t width, std::int32_t height,
		std::int32_t channelCount, std::int32_t bandRows)
	{
		const std::int32_t rowBytes = width * channelCount;
		const std::int32_t maxBandBytes = rowBytes * std::min(bandRows, height);
		const std::int32_t capacity = LZ4_compressBound(maxBandBytes);
		std::unique_ptr<char[]> compressed = std::make_unique<char[]>(std::size_t(capacity));
		for (std::int32_t y = 0; y < height; y += bandRows) {
			const std::int32_t bandBytes = std::min(bandRows, height - y) * rowBytes;
			const std::int32_t size = LZ4_compress_HC(reinterpret_cast<const char*>(data + std::size_t(y) * rowBytes),
				compressed.get(), bandBytes, capacity, LZ4HC_CLEVEL_MAX);
			so.WriteValueAsLE<std::uint32_t>(std::uint32_t(size > 0 ? size : 0));
			if (size > 0) {
				so.Write(compressed.get(), size);
			}
		}
	}

	bool JJ2Anims::DecodeImageContentLz4(const std::uint8_t*& src, const std::uint8_t* end, std::uint8_t* data, std::int32_t byteCount)
	{
		if (end - src < 4) {
			return false;
		}
		const std::uint32_t size = std::uint32_t(src[0]) | (std::uint32_t(src[1]) << 8) |
			(std::uint32_t(src[2]) << 16) | (std::uint32_t(src[3]) << 24);
		src += 4;
		if (size > std::uint32_t(end - src)) {
			src = end;
			return false;
		}
		const std::int32_t decoded = LZ4_decompress_safe(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(data),
			std::int32_t(size), byteCount);
		src += size;
		return (decoded == byteCount);
	}

	bool JJ2Anims::ReadImageContentLz4(Stream& s, std::uint8_t* data, std::int32_t byteCount)
	{
		const std::uint32_t size = s.ReadValueAsLE<std::uint32_t>();
		if (size == 0 || size > std::uint32_t(LZ4_compressBound(byteCount))) {
			return false;
		}
		std::unique_ptr<std::uint8_t[]> compressed(new (std::nothrow) std::uint8_t[size]);
		if (compressed == nullptr || s.Read(compressed.get(), size) != std::int64_t(size)) {
			return false;
		}
		return (LZ4_decompress_safe(reinterpret_cast<const char*>(compressed.get()), reinterpret_cast<char*>(data),
			std::int32_t(size), byteCount) == byteCount);
	}
#endif
}