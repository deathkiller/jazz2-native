#include "DiscImage.h"
#include "Iso9660.h"

#include <cstring>

#include <Containers/Array.h>
#include <Containers/GrowableArray.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Name of the directory on the disc that holds the game content */
		constexpr StringView ContentDirectoryName = "Content"_s;

		/** @brief How a DiscJuggler image ends, and which of the three layouts it is */
		enum : std::uint32_t {
			CdiVersion2 = 0x80000004,
			CdiVersion3 = 0x80000005,
			CdiVersion35 = 0x80000006
		};

		/** @brief Mode of a track, as the descriptor of a DiscJuggler image spells it */
		enum : std::uint32_t {
			TrackModeAudio = 0,
			TrackModeData = 1,
			TrackModeXa = 2
		};

		/** @brief One track of a DiscJuggler image */
		struct CdiTrack {
			/** @brief Where the stored data of the track begins in the file, the pregap included */
			std::uint64_t DataOffset = 0;
			/** @brief Sectors of pregap stored ahead of the first sector of the track */
			std::uint32_t Pregap = 0;
			/** @brief Sectors of the track itself, and both of them together */
			std::uint32_t Length = 0, TotalLength = 0;
			/** @brief Address the track begins at, which every address of its file system is relative to */
			std::uint32_t StartLba = 0;
			std::uint32_t Mode = TrackModeAudio;
			/** @brief How many bytes of every sector the image stores --- 2048, 2336 or 2352 */
			std::uint32_t SectorSize = 0;
			/** @brief Where in the descriptor the two lengths are, so a track that grew can be written back */
			std::size_t LengthField = 0, TotalLengthField = 0, SecondTotalLengthField = 0;
		};

		/** @brief A DiscJuggler image, as much of one as replacing what is on it needs */
		struct CdiImage {
			std::uint32_t Version = 0;
			/** @brief Where the descriptor the tracks were read from begins */
			std::uint64_t DescriptorOffset = 0;
			Array<std::uint8_t> Descriptor;
			SmallVector<CdiTrack, 4> Tracks;
			/** @brief Where in the descriptor the length of the whole disc is */
			std::size_t DiscLengthField = 0;
		};

		/**
			@brief Walks the descriptor a DiscJuggler image ends with

			The format is not documented anywhere, so this follows what the readers that predate it do, and
			every field it picks out has been checked against images `mkdcdisc` wrote. The records are of a
			fixed size except for the paths they carry, which is why the fields are located by walking rather
			than by a table of offsets.
		*/
		class CdiDescriptorParser
		{
		public:
			CdiDescriptorParser(const Array<std::uint8_t>& descriptor, std::uint32_t version)
				: _data(descriptor), _version(version), _offset(0), _valid(true) {}

			bool IsValid() const {
				return _valid;
			}

			std::size_t GetOffset() const {
				return _offset;
			}

			void Skip(std::size_t count) {
				if (_offset + count > _data.size()) {
					_valid = false;
					_offset = _data.size();
				} else {
					_offset += count;
				}
			}

			std::uint8_t ReadU8() {
				if (_offset + 1 > _data.size()) {
					_valid = false;
					return 0;
				}
				return _data[_offset++];
			}

			std::uint16_t ReadU16() {
				if (_offset + 2 > _data.size()) {
					_valid = false;
					return 0;
				}
				std::uint16_t value = std::uint16_t(_data[_offset]) | (std::uint16_t(_data[_offset + 1]) << 8);
				_offset += 2;
				return value;
			}

			std::uint32_t ReadU32() {
				if (_offset + 4 > _data.size()) {
					_valid = false;
					return 0;
				}
				std::uint32_t value = std::uint32_t(_data[_offset]) | (std::uint32_t(_data[_offset + 1]) << 8) |
					(std::uint32_t(_data[_offset + 2]) << 16) | (std::uint32_t(_data[_offset + 3]) << 24);
				_offset += 4;
				return value;
			}

			/** @brief Skips the two marks and the path that the record of a track, and of the disc, begins with */
			void SkipRecordHeader() {
				// A newer writer puts eight more bytes in front of everything else and says so with a flag
				if (ReadU32() != 0) {
					Skip(8);
				}
				Skip(2 * TrackStartMarkSize);
				Skip(4);
				std::uint8_t pathLength = ReadU8();
				Skip(pathLength);
				Skip(11 + 4 + 4);
				// Another flag, of another writer, and another eight bytes to step over
				if (ReadU32() == 0x80000000) {
					Skip(8);
				}
			}

			/** @brief Steps over what separates the tracks of one session from those of the next */
			void SkipSessionEnd() {
				Skip(_version == CdiVersion2 ? 12 : 13);
			}

		private:
			/** @brief Size of the mark `00 00 01 00 00 00 FF FF FF FF` every record begins with, twice over */
			static constexpr std::size_t TrackStartMarkSize = 10;

			const Array<std::uint8_t>& _data;
			std::uint32_t _version;
			std::size_t _offset;
			bool _valid;
		};

		bool ParseCdi(Stream& stream, CdiImage& image)
		{
			std::int64_t fileSize = stream.GetSize();
			if (fileSize < 16) {
				LOGE("The image is too small to be a DiscJuggler image");
				return false;
			}

			stream.Seek(fileSize - 8, SeekOrigin::Begin);
			image.Version = stream.ReadValueAsLE<std::uint32_t>();
			std::uint32_t headerOffset = stream.ReadValueAsLE<std::uint32_t>();
			if (image.Version != CdiVersion2 && image.Version != CdiVersion3 && image.Version != CdiVersion35) {
				LOGE("0x{:.8x} is not a version of the DiscJuggler format this can read", image.Version);
				return false;
			}

			// The newest of the three layouts counts the descriptor back from the end of the file
			image.DescriptorOffset = (image.Version == CdiVersion35
				? std::uint64_t(fileSize) - headerOffset : headerOffset);
			if (image.DescriptorOffset + 8 > std::uint64_t(fileSize)) {
				LOGE("The image ends before the descriptor it points at");
				return false;
			}

			// One record per track and one for the disc, each a few hundred bytes; a file that claims more
			// than this is not a disc image whose descriptor was found, it is a file that is not one at all
			std::size_t descriptorSize = std::size_t(fileSize - 8 - image.DescriptorOffset);
			if (descriptorSize > 1024 * 1024) {
				LOGE("The descriptor of the image is {} bytes long, which is not a descriptor", descriptorSize);
				return false;
			}
			image.Descriptor = Array<std::uint8_t>{NoInit, descriptorSize};
			stream.Seek(std::int64_t(image.DescriptorOffset), SeekOrigin::Begin);
			if (stream.Read(image.Descriptor.data(), std::int64_t(descriptorSize)) != std::int64_t(descriptorSize)) {
				LOGE("Cannot read the descriptor of the image");
				return false;
			}

			CdiDescriptorParser parser(image.Descriptor, image.Version);
			std::uint64_t dataOffset = 0;
			std::uint16_t sessionCount = parser.ReadU16();
			for (std::uint16_t session = 0; session < sessionCount; session++) {
				std::uint16_t trackCount = parser.ReadU16();
				for (std::uint16_t track = 0; track < trackCount; track++) {
					parser.SkipRecordHeader();

					CdiTrack& entry = image.Tracks.emplace_back();
					entry.DataOffset = dataOffset;
					parser.Skip(2);
					entry.Pregap = parser.ReadU32();
					entry.LengthField = parser.GetOffset();
					entry.Length = parser.ReadU32();
					parser.Skip(6);
					entry.Mode = parser.ReadU32();
					parser.Skip(12);
					entry.StartLba = parser.ReadU32();
					entry.TotalLengthField = parser.GetOffset();
					entry.TotalLength = parser.ReadU32();
					parser.Skip(16);
					std::uint32_t sectorSizeId = parser.ReadU32();
					parser.Skip(5);
					entry.SecondTotalLengthField = parser.GetOffset();
					std::uint32_t secondTotalLength = parser.ReadU32();
					parser.Skip(20);
					if (image.Version != CdiVersion2) {
						parser.Skip(5);
						if (parser.ReadU32() == 0xFFFFFFFF) {
							parser.Skip(78);
						}
					}

					static const std::uint32_t sectorSizes[] = { 2048, 2336, 2352 };
					// Every one of these has to hold for the walk to have stayed in step with the records
					if (!parser.IsValid() || sectorSizeId >= 3 || secondTotalLength != entry.TotalLength ||
						entry.TotalLength != entry.Pregap + entry.Length) {
						LOGE("The descriptor of the image cannot be read, track {} of session {} does not make sense", track + 1, session + 1);
						return false;
					}
					entry.SectorSize = sectorSizes[sectorSizeId];

					dataOffset += std::uint64_t(entry.TotalLength) * entry.SectorSize;
				}
				parser.SkipSessionEnd();
			}

			// What follows the sessions describes the disc as a whole, and begins the same way a track does
			parser.Skip(2);
			parser.SkipRecordHeader();
			image.DiscLengthField = parser.GetOffset();
			parser.ReadU32();

			if (!parser.IsValid() || image.Tracks.empty()) {
				LOGE("The descriptor of the image does not make sense, it cannot be read");
				return false;
			}
			if (dataOffset != image.DescriptorOffset) {
				LOGE("The tracks of the image add up to {} bytes, but the descriptor begins at {}", dataOffset, image.DescriptorOffset);
				return false;
			}

			return true;
		}

		/** @brief Where the user data of a sector begins, once the image has stored what comes before it */
		std::uint32_t UserDataOffset(const CdiTrack& track)
		{
			switch (track.SectorSize) {
				case 2336: return 8;								// A subheader, so Mode 2 Form 1
				case 2352: return (track.Mode == TrackModeData ? 16 : 24);	// A sync pattern and a header as well
				default: return 0;
			}
		}

		/**
			@brief The two tables the error detection and correction of a CD-ROM sector is built out of

			Both are the usual ones: a CRC-32 over the reversed polynomial the standard names for the error
			detection code, and the forward and backward logarithm of the Galois field the Reed-Solomon parity
			is computed in.
		*/
		struct EccTables {
			std::uint8_t Forward[256];
			std::uint8_t Backward[256];
			std::uint32_t Edc[256];

			EccTables() {
				for (std::int32_t i = 0; i < 256; i++) {
					std::uint8_t product = std::uint8_t((i << 1) ^ (i & 0x80 ? 0x1D : 0));
					Forward[i] = product;
					Backward[i ^ product] = std::uint8_t(i);

					std::uint32_t edc = std::uint32_t(i);
					for (std::int32_t j = 0; j < 8; j++) {
						edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
					}
					Edc[i] = edc;
				}
			}
		};

		const EccTables& GetEccTables()
		{
			static const EccTables tables;
			return tables;
		}

		std::uint32_t ComputeEdc(const std::uint8_t* data, std::size_t size)
		{
			const EccTables& tables = GetEccTables();
			std::uint32_t edc = 0;
			for (std::size_t i = 0; i < size; i++) {
				edc = (edc >> 8) ^ tables.Edc[(edc ^ data[i]) & 0xFF];
			}
			return edc;
		}

		/** @brief Computes one of the two Reed-Solomon parities over a block that already holds the other */
		void ComputeEccBlock(std::uint8_t* block, std::uint32_t majorCount, std::uint32_t minorCount,
			std::uint32_t majorMultiplier, std::uint32_t minorIncrement, std::uint32_t destination)
		{
			const EccTables& tables = GetEccTables();
			std::uint32_t size = majorCount * minorCount;
			for (std::uint32_t major = 0; major < majorCount; major++) {
				std::uint32_t index = (major >> 1) * majorMultiplier + (major & 1);
				std::uint8_t a = 0, b = 0;
				for (std::uint32_t minor = 0; minor < minorCount; minor++) {
					std::uint8_t value = block[index];
					index += minorIncrement;
					if (index >= size) {
						index -= size;
					}
					a ^= value;
					b ^= value;
					a = tables.Forward[a];
				}
				a = tables.Backward[tables.Forward[a] ^ b];
				block[destination + major] = a;
				block[destination + major + majorCount] = std::uint8_t(a ^ b);
			}
		}

		/**
			@brief Turns one logical sector into the Mode 2 Form 1 sector a disc actually holds

			@param target	The subheader onwards, so 2336 bytes

			The parity is computed as if the address in the header were zero, which is what makes a Form 1
			sector readable no matter where on the disc it ends up.
		*/
		void EncodeMode2Form1(const std::uint8_t* data, std::uint8_t* target)
		{
			target[0] = 0;
			target[1] = 0;
			target[2] = 0x09;	// The sector holds data and is the end of a record
			target[3] = 0;
			target[4] = 0;
			target[5] = 0;
			target[6] = 0x09;
			target[7] = 0;
			std::memcpy(target + 8, data, IsoSectorSize);

			std::uint32_t edc = ComputeEdc(target, 8 + IsoSectorSize);
			target[2056] = std::uint8_t(edc);
			target[2057] = std::uint8_t(edc >> 8);
			target[2058] = std::uint8_t(edc >> 16);
			target[2059] = std::uint8_t(edc >> 24);

			// The parity covers the header as well, which is four zero bytes ahead of what the image stores,
			// and the second half of it covers the first half
			std::uint8_t block[4 + 2060 + 276];
			std::memset(block, 0, 4);
			std::memcpy(block + 4, target, 2060);
			ComputeEccBlock(block, 86, 24, 2, 86, 2064);
			ComputeEccBlock(block, 52, 43, 86, 88, 2236);
			std::memcpy(target + 2060, block + 2064, 276);
		}

		/** @brief Reads the logical sectors of a data track out of the image holding it */
		class CdiSectorReader : public ISectorReader
		{
		public:
			CdiSectorReader(Stream& stream, const CdiTrack& track)
				: _stream(stream), _track(track), _userDataOffset(UserDataOffset(track)) {}

			bool ReadSector(std::uint32_t lba, std::uint8_t* destination) override
			{
				if (lba < _track.StartLba || lba >= _track.StartLba + _track.Length) {
					LOGE("Sector {} is outside the data track", lba);
					return false;
				}

				std::uint64_t offset = _track.DataOffset +
					std::uint64_t(_track.Pregap + (lba - _track.StartLba)) * _track.SectorSize + _userDataOffset;
				_stream.Seek(std::int64_t(offset), SeekOrigin::Begin);
				return (_stream.Read(destination, IsoSectorSize) == IsoSectorSize);
			}

		private:
			Stream& _stream;
			const CdiTrack& _track;
			std::uint32_t _userDataOffset;
		};

		/** @brief Writes the logical sectors of a data track into the image being generated */
		class CdiSectorSink : public ISectorSink
		{
		public:
			CdiSectorSink(Stream& stream, std::uint32_t sectorSize)
				: _stream(stream), _sectorSize(sectorSize), _failed(false)
			{
				arrayReserve(_buffer, BufferedSectors * sectorSize);
			}

			bool WriteSector(const std::uint8_t* data) override
			{
				if (_failed) {
					return false;
				}

				std::size_t at = _buffer.size();
				arrayResize(_buffer, NoInit, at + _sectorSize);
				std::uint8_t* target = _buffer.data() + at;
				if (_sectorSize == IsoSectorSize) {
					std::memcpy(target, data, IsoSectorSize);
				} else {
					EncodeMode2Form1(data, target);
				}

				if (_buffer.size() >= BufferedSectors * _sectorSize) {
					return Flush();
				}
				return true;
			}

			bool Flush()
			{
				if (_buffer.empty()) {
					return !_failed;
				}
				if (_stream.Write(_buffer.data(), std::int64_t(_buffer.size())) != std::int64_t(_buffer.size())) {
					LOGE("Cannot write the data track");
					_failed = true;
				}
				arrayResize(_buffer, 0);
				return !_failed;
			}

		private:
			/** @brief How many sectors are gathered before the file is touched */
			static constexpr std::size_t BufferedSectors = 512;

			Stream& _stream;
			Array<std::uint8_t> _buffer;
			std::uint32_t _sectorSize;
			bool _failed;
		};

		/** @brief Copies a range of an image into another one, byte for byte */
		bool CopyRange(Stream& source, std::uint64_t offset, std::uint64_t size, Stream& target)
		{
			constexpr std::int64_t ChunkSize = 1024 * 1024;
			Array<std::uint8_t> buffer{NoInit, std::size_t(ChunkSize)};
			source.Seek(std::int64_t(offset), SeekOrigin::Begin);
			while (size > 0) {
				std::int64_t chunk = (size < std::uint64_t(ChunkSize) ? std::int64_t(size) : ChunkSize);
				if (source.Read(buffer.data(), chunk) != chunk || target.Write(buffer.data(), chunk) != chunk) {
					return false;
				}
				size -= std::uint64_t(chunk);
			}
			return true;
		}

		/** @brief Carries a directory of the image being read over to the one being written, entry by entry */
		bool CopyDirectoryFromImage(Iso9660Reader& reader, Iso9660Builder& builder, StringView path,
			std::uint32_t lba, std::uint32_t size)
		{
			SmallVector<Iso9660Reader::Entry, 0> entries;
			if (!reader.ReadDirectory(lba, size, entries)) {
				return false;
			}

			for (Iso9660Reader::Entry& entry : entries) {
				String entryPath = (path.empty() ? String(entry.Name) : String(path + "/"_s + entry.Name));
				if (entry.IsDirectory) {
					if (!builder.AddDirectory(entryPath, entry.Recorded) ||
						!CopyDirectoryFromImage(reader, builder, entryPath, entry.Lba, entry.Size)) {
						return false;
					}
				} else if (!builder.AddFileFromImage(entryPath, entry.Lba, entry.Size, entry.Recorded)) {
					return false;
				}
			}
			return true;
		}

		void WriteU32LE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value);
			target[1] = std::uint8_t(value >> 8);
			target[2] = std::uint8_t(value >> 16);
			target[3] = std::uint8_t(value >> 24);
		}

		std::uint32_t ReadU32LE(const std::uint8_t* source)
		{
			return std::uint32_t(source[0]) | (std::uint32_t(source[1]) << 8) |
				(std::uint32_t(source[2]) << 16) | (std::uint32_t(source[3]) << 24);
		}

		/**
			@brief Whether the image is wrapped in a DiscJuggler container rather than being a plain volume

			A `.cdi` names its layout in the last eight bytes of the file. A plain `.iso` --- which is what the
			PlayStation 2 disc is, and what `mkisofs` and everything like it writes --- has no container at
			all: the file *is* the data track, one 2048-byte sector after another from the first.
		*/
		bool IsDiscJugglerImage(Stream& stream)
		{
			std::int64_t fileSize = stream.GetSize();
			if (fileSize < 16) {
				return false;
			}
			stream.Seek(fileSize - 8, SeekOrigin::Begin);
			std::uint32_t version = stream.ReadValueAsLE<std::uint32_t>();
			return (version == CdiVersion2 || version == CdiVersion3 || version == CdiVersion35);
		}
	}

	bool DiscImage::SwapContent(StringView sourcePath, StringView targetPath, StringView contentPath)
	{
		if (!fs::IsReadableFile(sourcePath)) {
			LOGE("Cannot open \"{}\"", sourcePath);
			return false;
		}
		if (!fs::DirectoryExists(contentPath)) {
			LOGE("Content directory \"{}\" does not exist", contentPath);
			return false;
		}

		auto source = fs::Open(sourcePath, FileAccess::Read);
		if (!source->IsValid()) {
			LOGE("Cannot open \"{}\"", sourcePath);
			return false;
		}

		CdiImage image;
		CdiTrack plainTrack;
		CdiTrack* dataTrack = nullptr;
		bool discJuggler = IsDiscJugglerImage(*source);
		if (discJuggler) {
			if (!ParseCdi(*source, image)) {
				return false;
			}

			// The game is on the last data track, which on a disc that boots on an unmodified console is the
			// one of the second session --- the console reads the last session and nothing else
			for (CdiTrack& track : image.Tracks) {
				if (track.Mode != TrackModeAudio) {
					dataTrack = &track;
				}
			}
			if (dataTrack == nullptr) {
				LOGE("\"{}\" has no data track", sourcePath);
				return false;
			}
			if (dataTrack->SectorSize == 2352) {
				LOGE("\"{}\" stores whole 2352-byte sectors, which cannot be written back", sourcePath);
				return false;
			}
		} else {
			// A plain volume is one track that begins where the file does, so everything below works on it
			// unchanged once it is described the same way
			plainTrack.Mode = TrackModeData;
			plainTrack.SectorSize = IsoSectorSize;
			plainTrack.Length = std::uint32_t(source->GetSize() / IsoSectorSize);
			plainTrack.TotalLength = plainTrack.Length;
			dataTrack = &plainTrack;
			if (plainTrack.Length < 17) {
				LOGE("\"{}\" is neither a DiscJuggler image nor a disc volume", sourcePath);
				return false;
			}
		}

		CdiSectorReader sectorReader(*source, *dataTrack);
		Iso9660Reader isoReader;
		if (!isoReader.Open(sectorReader, dataTrack->StartLba)) {
			return false;
		}

		Array<std::uint8_t> systemArea{ValueInit, IsoSystemAreaSize};
		for (std::uint32_t i = 0; i < 16; i++) {
			if (!sectorReader.ReadSector(dataTrack->StartLba + i, systemArea.data() + i * IsoSectorSize)) {
				return false;
			}
		}
		// Only a Dreamcast disc keeps anything in the system area, and a DiscJuggler image is always one
		if (discJuggler && std::memcmp(systemArea.data(), "SEGA SEGAKATANA", 15) != 0) {
			LOGW("\"{}\" does not begin with a Dreamcast bootstrap, the disc it produces may not boot", sourcePath);
		}

		Iso9660Builder builder;
		builder.SetTrackStartLba(dataTrack->StartLba);
		builder.SetSystemArea(systemArea);
		builder.SetDescriptorTemplates(isoReader.GetPrimaryDescriptor(), isoReader.GetSupplementaryDescriptor());

		// Everything the disc carries is kept where it is, except for the directory being replaced --- which
		// includes the executable, still scrambled exactly as it was, so the tool needs neither the toolchain
		// the disc was built with nor the executable it was built from
		SmallVector<Iso9660Reader::Entry, 0> rootEntries;
		if (!isoReader.ReadDirectory(isoReader.GetRootLba(), isoReader.GetRootSize(), rootEntries)) {
			return false;
		}

		bool replaced = false;
		for (Iso9660Reader::Entry& entry : rootEntries) {
			if (entry.IsDirectory && StringUtils::equalsIgnoreCase(entry.Name, ContentDirectoryName)) {
				replaced = true;
				continue;
			}
			if (entry.IsDirectory) {
				if (!builder.AddDirectory(entry.Name, entry.Recorded) ||
					!CopyDirectoryFromImage(isoReader, builder, entry.Name, entry.Lba, entry.Size)) {
					return false;
				}
			} else if (!builder.AddFileFromImage(entry.Name, entry.Lba, entry.Size, entry.Recorded)) {
				return false;
			}
		}
		if (!replaced) {
			LOGW("\"{}\" has no \"{}\" directory, so one is being added", sourcePath, ContentDirectoryName);
		}

		LOGI("Reading \"{}\"...", contentPath);
		if (!builder.AddDirectoryFromDisk(ContentDirectoryName, contentPath)) {
			return false;
		}

		// A data track ends with a couple of sectors that are not part of the volume; the image keeps them,
		// and so does this one, by leaving the file system the same number of sectors short of the track.
		// A volume that is shorter than its track by more than a pregap is not one that ends in a run-out,
		// it is one that was written to a track longer than it needed, and it gets all of it
		std::uint32_t runOut = (dataTrack->Length > isoReader.GetVolumeSpaceSize()
			? dataTrack->Length - isoReader.GetVolumeSpaceSize() : 0);
		if (runOut > 150) {
			runOut = 0;
		}
		std::uint32_t required = builder.GetRequiredSectorCount();
		if (required == 0) {
			return false;
		}

		// A DiscJuggler image is kept exactly as long as it was whenever the new content fits, because its
		// length is the geometry of a disc that was authored once: most of it is the empty run that pushes
		// the files out to the edge, so there is usually a lot of room. A plain volume has no geometry to
		// keep --- it is a file that is as long as what is on it --- so it is sized to what it now holds.
		std::uint32_t trackLength = (discJuggler ? dataTrack->Length : required + runOut);
		if (required + runOut > trackLength) {
			trackLength = required + runOut;
			LOGI("The new content does not fit in the disc as it is, growing it by {} sectors", trackLength - dataTrack->Length);
		}
		std::uint32_t volumeSectorCount = trackLength - runOut;

		bool inPlace = (targetPath.empty() || targetPath == sourcePath);
		String writePath = targetPath;
		if (inPlace) {
			// The image is written next to the one being read and only takes its place once it is complete,
			// so an interrupted run cannot leave a half-written disc behind
			writePath = sourcePath + ".tmp"_s;
		}

		LOGI("Writing \"{}\"...", inPlace ? sourcePath : targetPath);
		{
			auto target = fs::Open(writePath, FileAccess::Write);
			if (!target->IsValid()) {
				LOGE("Cannot open \"{}\" for writing", writePath);
				return false;
			}

			// Everything ahead of the first sector of the data track --- the audio track of the first session
			// and the pregap of this one --- is carried over as it is
			std::uint64_t trackStartOffset = dataTrack->DataOffset + std::uint64_t(dataTrack->Pregap) * dataTrack->SectorSize;
			if (!CopyRange(*source, 0, trackStartOffset, *target)) {
				LOGE("Cannot write \"{}\"", writePath);
				return false;
			}

			CdiSectorSink sink(*target, dataTrack->SectorSize);
			if (!builder.Write(sink, volumeSectorCount, sectorReader) || !sink.Flush()) {
				return false;
			}

			// The sectors the volume does not reach are taken from the image, which is where the form they
			// have to be in --- they are not ordinary data sectors --- is written down
			std::uint64_t runOutOffset = trackStartOffset +
				std::uint64_t(dataTrack->Length - runOut) * dataTrack->SectorSize;
			if (!CopyRange(*source, runOutOffset, std::uint64_t(runOut) * dataTrack->SectorSize, *target)) {
				LOGE("Cannot write \"{}\"", writePath);
				return false;
			}

			if (discJuggler && trackLength != dataTrack->Length) {
				std::uint32_t totalLength = trackLength + dataTrack->Pregap;
				std::uint32_t discLength = ReadU32LE(image.Descriptor.data() + image.DiscLengthField) +
					(totalLength - dataTrack->TotalLength);
				WriteU32LE(image.Descriptor.data() + dataTrack->LengthField, trackLength);
				WriteU32LE(image.Descriptor.data() + dataTrack->TotalLengthField, totalLength);
				WriteU32LE(image.Descriptor.data() + dataTrack->SecondTotalLengthField, totalLength);
				WriteU32LE(image.Descriptor.data() + image.DiscLengthField, discLength);

				// A disc that no longer fits on one is still written, because what it is going to be burned
				// onto, if anything ever is, is not something this can know
				if (discLength > 360000) {
					LOGW("The disc is now {} sectors long, which is more than a 80 minute CD holds", discLength);
				}
			}

			// A plain volume ends where the track does, there is nothing to write after it
			if (discJuggler) {
				std::int64_t descriptorPosition = target->GetPosition();
				if (target->Write(image.Descriptor.data(), std::int64_t(image.Descriptor.size())) != std::int64_t(image.Descriptor.size())) {
					LOGE("Cannot write \"{}\"", writePath);
					return false;
				}
				// The two older layouts point at the descriptor from the beginning of the file and the newest
				// one from its end; the descriptor is the same size as the one that was read either way
				target->WriteValueAsLE<std::uint32_t>(image.Version);
				target->WriteValueAsLE<std::uint32_t>(image.Version == CdiVersion35
					? std::uint32_t(image.Descriptor.size() + 8) : std::uint32_t(descriptorPosition));
			}
		}

		if (inPlace) {
			// Both platforms replace what is already there, so the image that was read is never gone before
			// the one that replaces it is in place
			source->Dispose();
			if (!fs::Move(writePath, sourcePath)) {
				LOGE("Cannot replace \"{}\" with the image that was written to \"{}\"", sourcePath, writePath);
				return false;
			}
		}

		return true;
	}
}
