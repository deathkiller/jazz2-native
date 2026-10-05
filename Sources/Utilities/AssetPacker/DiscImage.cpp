#include "DiscImage.h"
#include "Iso9660.h"
#include "../../Jazz2/Compatibility/JJ2Anims.h"

#include <cstdio>
#include <cstring>

#include <Containers/Array.h>
#include <Containers/GrowableArray.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Containers/DateTime.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>
#include <Utf8.h>

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
		bool IsDiscJugglerStream(Stream& stream)
		{
			std::int64_t fileSize = stream.GetSize();
			if (fileSize < 16) {
				return false;
			}
			stream.Seek(fileSize - 8, SeekOrigin::Begin);
			std::uint32_t version = stream.ReadValueAsLE<std::uint32_t>();
			return (version == CdiVersion2 || version == CdiVersion3 || version == CdiVersion35);
		}

		/** @brief The track of an image that holds the file system, described the same way for both kinds of image */
		struct DataTrack
		{
			CdiImage Image;
			CdiTrack Plain;
			CdiTrack* Track = nullptr;
			bool DiscJuggler = false;
		};

		bool FindDataTrack(Stream& source, StringView sourcePath, DataTrack& result)
		{
			result.DiscJuggler = IsDiscJugglerStream(source);
			if (result.DiscJuggler) {
				if (!ParseCdi(source, result.Image)) {
					return false;
				}

				// The game is on the last data track, which on a disc that boots on an unmodified console is the
				// one of the second session --- the console reads the last session and nothing else
				for (CdiTrack& track : result.Image.Tracks) {
					if (track.Mode != TrackModeAudio) {
						result.Track = &track;
					}
				}
				if (result.Track == nullptr) {
					LOGE("\"{}\" has no data track", sourcePath);
					return false;
				}
				if (result.Track->SectorSize == 2352) {
					LOGE("\"{}\" stores whole 2352-byte sectors, which cannot be written back", sourcePath);
					return false;
				}
			} else {
				// A plain volume is one track that begins where the file does, so everything works on it unchanged
				// once it is described the same way
				result.Plain.Mode = TrackModeData;
				result.Plain.SectorSize = IsoSectorSize;
				result.Plain.Length = std::uint32_t(source.GetSize() / IsoSectorSize);
				result.Plain.TotalLength = result.Plain.Length;
				result.Track = &result.Plain;
				if (result.Plain.Length < 17) {
					LOGE("\"{}\" is neither a DiscJuggler image nor a disc volume", sourcePath);
					return false;
				}
			}
			return true;
		}

		/**
			@brief Where a GameCube disc keeps what the boot ROM reads, all of it inside the system area of the volume

			The disc header (`boot.bin`) comes first, then `bi2.bin` with the settings the boot ROM hands to the game,
			then the apploader. Everything the header names is a byte offset from the start of the disc.
		*/
		enum : std::uint32_t {
			GameCubeMagicOffset = 0x1C,
			GameCubeTitleOffset = 0x20,
			GameCubeTitleSize = 0x3E0,
			GameCubeDolOffsetField = 0x420,
			GameCubeFstOffsetField = 0x424,
			GameCubeFstSizeField = 0x428,
			GameCubeFstMaxSizeField = 0x42C,
			GameCubeBi2Offset = 0x440,
			GameCubeApploaderOffset = 0x2440
		};

		/** @brief The word at @ref GameCubeMagicOffset that makes a disc a GameCube disc, to the console and to Dolphin */
		constexpr std::uint32_t GameCubeDiscMagic = 0xC2339F3D;

		/** @brief Where the boot ROM loads the apploader, which its header has to point into */
		constexpr std::uint32_t GameCubeApploaderAddress = 0x81200000;

		/** @brief Size of the header in front of the code of an apploader */
		constexpr std::uint32_t GameCubeApploaderHeaderSize = 0x20;

		/**
			@brief Zero sectors a GameCube image ends with, after the volume

			Readers on the console fetch more than they are asked for --- libogc's ISO 9660 driver always reads 16
			sectors at once, and the apploader rounds every read up to a cache line --- and a read that runs past
			the end of the image is a read error to Dolphin, however little of it was wanted.
		*/
		constexpr std::uint32_t GameCubeTailSectors = 16;

		std::uint32_t ReadU32BE(const std::uint8_t* source)
		{
			return (std::uint32_t(source[0]) << 24) | (std::uint32_t(source[1]) << 16) |
				(std::uint32_t(source[2]) << 8) | std::uint32_t(source[3]);
		}

		void WriteU32BE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value >> 24);
			target[1] = std::uint8_t(value >> 16);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		void WriteU16Both(std::uint8_t* target, std::uint16_t value)
		{
			target[0] = std::uint8_t(value);
			target[1] = std::uint8_t(value >> 8);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		bool IsGameCubeSystemArea(ArrayView<const std::uint8_t> systemArea)
		{
			return (systemArea.size() >= GameCubeApploaderOffset && ReadU32BE(systemArea.data() + GameCubeMagicOffset) == GameCubeDiscMagic);
		}

		/** @brief Fills in the disc header and `bi2.bin`, everything of them except where the executable is */
		void WriteGameCubeHeader(ArrayView<std::uint8_t> systemArea, StringView gameId, StringView title)
		{
			std::uint8_t* header = systemArea.data();
			std::memcpy(header, gameId.data(), 6);
			// Disc number, version, audio streaming and the size of its buffer are all zero, and so is the rest
			WriteU32BE(header + GameCubeMagicOffset, GameCubeDiscMagic);

			// The name is shown by Dolphin and by loaders, in Shift-JIS on a Japanese disc and in Windows-1252
			// anywhere else - which is Latin-1 for everything below U+0100, enough for "Jazz²"
			std::size_t length = 0;
			for (std::size_t i = 0; i < title.size() && length < GameCubeTitleSize - 1; ) {
				Pair<char32_t, std::size_t> next = Utf8::NextChar(arrayView(title.data(), title.size()), i);
				i = next.second();
				header[GameCubeTitleOffset + length++] = (next.first() < 0x100 ? std::uint8_t(next.first()) : std::uint8_t('?'));
			}

			// No file system table: the game reads the ISO 9660 hierarchy, and nothing else on the console needs one
			WriteU32BE(header + GameCubeFstOffsetField, 0);
			WriteU32BE(header + GameCubeFstSizeField, 0);
			WriteU32BE(header + GameCubeFstMaxSizeField, 0);

			// `bi2.bin`: the memory the game may assume (all 24 MB) and the region, which the boot ROM of a console
			// compares with its own and Dolphin uses to pick the video mode; the fourth character of the game code
			// says the same thing in letters
			std::uint8_t* bi2 = header + GameCubeBi2Offset;
			WriteU32BE(bi2 + 0x04, 0x01800000);
			std::uint32_t region = (gameId[3] == 'J' ? 0 : (gameId[3] == 'E' ? 1 : 2));
			WriteU32BE(bi2 + 0x18, region);
		}

		/**
			@brief Reads an apploader image and checks that it is one

			The image is what the GameCube target builds (see `GameCubeApploader.c`): a 32-byte header naming the
			date, the entry point and the size, followed by the code BS2 loads to @ref GameCubeApploaderAddress.
			It has to fit between `bi2.bin` and the first volume descriptor.
		*/
		bool ReadGameCubeApploader(StringView path, Array<std::uint8_t>& apploader)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("Cannot open \"{}\"", path);
				return false;
			}
			std::int64_t size = s->GetSize();
			if (size <= GameCubeApploaderHeaderSize || size > std::int64_t(IsoSystemAreaSize - GameCubeApploaderOffset)) {
				LOGE("\"{}\" is {} bytes long, an apploader has to be between {} and {} bytes", path, size,
					GameCubeApploaderHeaderSize + 1, IsoSystemAreaSize - GameCubeApploaderOffset);
				return false;
			}
			apploader = Array<std::uint8_t>{NoInit, std::size_t(size)};
			if (s->Read(apploader.data(), size) != size) {
				LOGE("Cannot read \"{}\"", path);
				return false;
			}

			std::uint32_t entryPoint = ReadU32BE(apploader.data() + 0x10);
			std::uint32_t codeSize = ReadU32BE(apploader.data() + 0x14);
			std::uint32_t trailerSize = ReadU32BE(apploader.data() + 0x18);
			if (codeSize == 0 || std::uint64_t(GameCubeApploaderHeaderSize) + codeSize + trailerSize > std::uint64_t(size) ||
				entryPoint < GameCubeApploaderAddress || entryPoint >= GameCubeApploaderAddress + codeSize) {
				LOGE("\"{}\" is not an apploader, its header does not describe the code behind it", path);
				return false;
			}
			return true;
		}

		/** @brief Checks that a file is a DOL whose sections are all inside it */
		bool CheckDolExecutable(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("Cannot open \"{}\"", path);
				return false;
			}
			std::int64_t size = s->GetSize();
			std::uint8_t header[0x100];
			if (size < std::int64_t(sizeof(header)) || s->Read(header, sizeof(header)) != std::int64_t(sizeof(header))) {
				LOGE("\"{}\" is too small to be an executable", path);
				return false;
			}
			for (std::uint32_t i = 0; i < 18; i++) {
				std::uint32_t offset = ReadU32BE(header + i * 4);
				std::uint32_t sectionSize = ReadU32BE(header + 0x90 + i * 4);
				if (sectionSize != 0 && std::uint64_t(offset) + sectionSize > std::uint64_t(size)) {
					LOGE("\"{}\" is not a DOL executable, section {} ends past the end of the file", path, i);
					return false;
				}
			}
			std::uint32_t entryPoint = ReadU32BE(header + 0xE0);
			if (entryPoint < 0x80000000 || entryPoint >= 0x81800000) {
				LOGE("\"{}\" is not a DOL executable, it starts at 0x{:.8x}", path, entryPoint);
				return false;
			}
			return true;
		}

		/** @brief Fills a text field of a volume descriptor, which is padded with spaces */
		void WriteDescriptorText(std::uint8_t* field, std::size_t size, StringView text)
		{
			std::memset(field, ' ', size);
			std::memcpy(field, text.data(), (text.size() < size ? text.size() : size));
		}

		/** @brief Fills a date of a volume descriptor, which unlike that of a directory record is spelled in digits */
		void WriteDescriptorDate(std::uint8_t* field, const DateTime* value)
		{
			char digits[17];
			if (value != nullptr) {
				std::snprintf(digits, sizeof(digits), "%04d%02d%02d%02d%02d%02d00", std::int32_t(value->GetYear()),
					std::int32_t(value->GetMonth() + 1), std::int32_t(value->GetDay()), std::int32_t(value->GetHour()),
					std::int32_t(value->GetMinute()), std::int32_t(value->GetSecond()));
			} else {
				std::memset(digits, '0', 16);
			}
			std::memcpy(field, digits, 16);
			// Offset from GMT in 15-minute intervals, which the digits above already are in local time anyway
			field[16] = 0;
		}

		/**
			@brief Makes the primary volume descriptor a new volume is described by

			Only what @ref Iso9660Builder does not fill in itself: the identity of the volume, its dates, and the
			fixed fields every volume carries the same. The Joliet descriptor is derived from this one.
		*/
		Array<std::uint8_t> MakePrimaryVolumeDescriptor(StringView label)
		{
			Array<std::uint8_t> descriptor{ValueInit, IsoSectorSize};
			std::uint8_t* d = descriptor.data();
			d[0] = 1;
			std::memcpy(d + 1, "CD001", 5);
			d[6] = 1;
			WriteDescriptorText(d + 8, 32, {});
			WriteDescriptorText(d + 40, 32, label);
			WriteU16Both(d + 120, 1);		// Volume set size
			WriteU16Both(d + 124, 1);		// Volume sequence number
			WriteU16Both(d + 128, std::uint16_t(IsoSectorSize));
			WriteDescriptorText(d + 190, 128, label);
			WriteDescriptorText(d + 318, 128, {});
			WriteDescriptorText(d + 446, 128, "ASSETPACKER"_s);
			WriteDescriptorText(d + 574, 128, {});
			WriteDescriptorText(d + 702, 37, {});
			WriteDescriptorText(d + 739, 37, {});
			WriteDescriptorText(d + 776, 37, {});
			DateTime now = DateTime::UtcNow();
			// The root directory takes its date from the record here (see Iso9660Builder::SetDescriptorTemplates())
			std::uint8_t* rootRecorded = d + 156 + 18;
			rootRecorded[0] = std::uint8_t(now.GetYear() - 1900);
			rootRecorded[1] = std::uint8_t(now.GetMonth() + 1);
			rootRecorded[2] = std::uint8_t(now.GetDay());
			rootRecorded[3] = std::uint8_t(now.GetHour());
			rootRecorded[4] = std::uint8_t(now.GetMinute());
			rootRecorded[5] = std::uint8_t(now.GetSecond());
			WriteDescriptorDate(d + 813, &now);
			WriteDescriptorDate(d + 830, &now);
			WriteDescriptorDate(d + 847, nullptr);
			WriteDescriptorDate(d + 864, nullptr);
			d[881] = 1;		// File structure version
			return descriptor;
		}

		/** @brief Stands in for the volume being read when there is none, a new image copies nothing out of one */
		class NoSectorReader : public ISectorReader
		{
		public:
			bool ReadSector(std::uint32_t lba, std::uint8_t* destination) override
			{
				static_cast<void>(destination);
				LOGE("Sector {} was asked for, but there is no image to read it from", lba);
				return false;
			}
		};

		bool WriteZeroSectors(ISectorSink& sink, std::uint32_t count)
		{
			std::uint8_t sector[IsoSectorSize] {};
			for (std::uint32_t i = 0; i < count; i++) {
				if (!sink.WriteSector(sector)) {
					return false;
				}
			}
			return true;
		}
	}

	bool DiscImage::IsDiscJugglerImage(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		return (s->IsValid() && IsDiscJugglerStream(*s));
	}

	bool DiscImage::IsGameCubeImage(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid() || s->GetSize() < std::int64_t(IsoSystemAreaSize) || IsDiscJugglerStream(*s)) {
			return false;
		}
		std::uint8_t header[GameCubeMagicOffset + 4];
		s->Seek(0, SeekOrigin::Begin);
		return (s->Read(header, sizeof(header)) == std::int64_t(sizeof(header)) &&
			ReadU32BE(header + GameCubeMagicOffset) == GameCubeDiscMagic);
	}

	bool DiscImage::CarriesLz4Images(StringView path)
	{
		auto source = fs::Open(path, FileAccess::Read);
		DataTrack data;
		if (!source->IsValid() || !FindDataTrack(*source, path, data)) {
			return false;
		}
		CdiSectorReader sectorReader(*source, *data.Track);
		Iso9660Reader isoReader;
		if (!isoReader.Open(sectorReader, data.Track->StartLba)) {
			return false;
		}

		std::uint32_t lba = isoReader.GetRootLba();
		std::uint32_t size = isoReader.GetRootSize();
		for (StringView directory : { ContentDirectoryName, "Tilesets"_s }) {
			SmallVector<Iso9660Reader::Entry, 0> entries;
			if (!isoReader.ReadDirectory(lba, size, entries)) {
				return false;
			}
			const Iso9660Reader::Entry* found = nullptr;
			for (const Iso9660Reader::Entry& entry : entries) {
				if (entry.IsDirectory && StringUtils::equalsIgnoreCase(entry.Name, directory)) {
					found = &entry;
					break;
				}
			}
			if (found == nullptr) {
				return false;
			}
			lba = found->Lba;
			size = found->Size;
		}

		SmallVector<Iso9660Reader::Entry, 0> tilesets;
		if (!isoReader.ReadDirectory(lba, size, tilesets)) {
			return false;
		}
		for (const Iso9660Reader::Entry& entry : tilesets) {
			if (entry.IsDirectory || entry.Size < 12) {
				continue;
			}
			// The header of a converted tileset (see JJ2Tileset::Convert()): signature, file type, version, flags
			std::uint8_t sector[IsoSectorSize];
			static const std::uint8_t Signature[] = { 0xEF, 0xBB, 0xBF, 0xE2, 0x98, 0x84, 0xEF, 0xB8, 0x8F, 0x20 };
			if (!sectorReader.ReadSector(entry.Lba, sector) || std::memcmp(sector, Signature, sizeof(Signature)) != 0) {
				return false;
			}
			return ((sector[11] & Compatibility::JJ2Anims::ImageContentLz4Flag) != 0);
		}
		return false;
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

		DataTrack data;
		if (!FindDataTrack(*source, sourcePath, data)) {
			return false;
		}
		CdiImage& image = data.Image;
		CdiTrack* dataTrack = data.Track;
		const bool discJuggler = data.DiscJuggler;

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
		// A Dreamcast disc keeps its bootstrap in the system area, and a DiscJuggler image is always one. A GameCube
		// disc keeps its header and apploader there, and the header points at the executable, which moves.
		if (discJuggler && std::memcmp(systemArea.data(), "SEGA SEGAKATANA", 15) != 0) {
			LOGW("\"{}\" does not begin with a Dreamcast bootstrap, the disc it produces may not boot", sourcePath);
		}
		const bool gameCube = (!discJuggler && IsGameCubeSystemArea(systemArea));

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

		// The header names the executable by where it starts, and that is the file to keep track of
		String executableName;
		if (gameCube) {
			std::uint32_t dolOffset = ReadU32BE(systemArea.data() + GameCubeDolOffsetField);
			for (Iso9660Reader::Entry& entry : rootEntries) {
				if (!entry.IsDirectory && std::uint64_t(entry.Lba) * IsoSectorSize == dolOffset) {
					executableName = entry.Name;
					break;
				}
			}
			if (executableName.empty()) {
				LOGE("The executable of \"{}\" is not one of the files of its volume, so the disc cannot be rewritten "
					"without losing it", sourcePath);
				return false;
			}
			if (ReadU32BE(systemArea.data() + GameCubeFstSizeField) != 0) {
				// Not one this tool wrote, and it would point at files that are about to move
				LOGW("\"{}\" carries a GameCube file system table, which the new image leaves out", sourcePath);
				WriteU32BE(systemArea.data() + GameCubeFstOffsetField, 0);
				WriteU32BE(systemArea.data() + GameCubeFstSizeField, 0);
				WriteU32BE(systemArea.data() + GameCubeFstMaxSizeField, 0);
			}
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
		if (runOut > 150 || gameCube) {
			// What a GameCube image ends with is not a run-out but padding, written again below
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

		if (gameCube) {
			std::uint32_t executableLba = builder.FindFileLba(executableName, volumeSectorCount);
			if (executableLba == 0) {
				LOGE("Cannot find \"{}\" in the new volume", executableName);
				return false;
			}
			WriteU32BE(systemArea.data() + GameCubeDolOffsetField, executableLba * IsoSectorSize);
			builder.SetSystemArea(systemArea);
		}

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
			if (!builder.Write(sink, volumeSectorCount, sectorReader) ||
				(gameCube && !WriteZeroSectors(sink, GameCubeTailSectors)) || !sink.Flush()) {
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

	bool DiscImage::CreateGameCubeImage(const GameCubeImageDescription& description, StringView targetPath)
	{
		if (description.GameId.size() != 6) {
			LOGE("\"{}\" is not a GameCube game ID, which is six characters - a game code like \"GJJE\" and a maker code", description.GameId);
			return false;
		}
		for (char c : description.GameId) {
			if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
				LOGE("\"{}\" is not a GameCube game ID, it can only be made of upper-case letters and digits", description.GameId);
				return false;
			}
		}
		if (!fs::DirectoryExists(description.ContentPath)) {
			LOGE("Content directory \"{}\" does not exist", description.ContentPath);
			return false;
		}
		if (!CheckDolExecutable(description.ExecutablePath)) {
			return false;
		}
		Array<std::uint8_t> apploader;
		if (!ReadGameCubeApploader(description.ApploaderPath, apploader)) {
			return false;
		}

		Array<std::uint8_t> systemArea{ValueInit, IsoSystemAreaSize};
		WriteGameCubeHeader(systemArea, description.GameId, description.Title);
		std::memcpy(systemArea.data() + GameCubeApploaderOffset, apploader.data(), apploader.size());

		Iso9660Builder builder;
		builder.SetDescriptorTemplates(MakePrimaryVolumeDescriptor("JAZZ2"_s), {});

		// The executable sits in the root under its own name, which is also how a rewritten image finds it again
		StringView executableName = fs::GetFileName(description.ExecutablePath);
		if (!builder.AddFileFromDisk(executableName, description.ExecutablePath)) {
			return false;
		}
		LOGI("Reading \"{}\"...", description.ContentPath);
		if (!builder.AddDirectoryFromDisk(ContentDirectoryName, description.ContentPath)) {
			return false;
		}

		std::uint32_t volumeSectorCount = builder.GetRequiredSectorCount();
		std::uint32_t executableLba = builder.FindFileLba(executableName, volumeSectorCount);
		if (volumeSectorCount == 0 || executableLba == 0) {
			return false;
		}
		WriteU32BE(systemArea.data() + GameCubeDolOffsetField, executableLba * IsoSectorSize);
		builder.SetSystemArea(systemArea);

		LOGI("Writing \"{}\"...", targetPath);
		auto target = fs::Open(targetPath, FileAccess::Write);
		if (!target->IsValid()) {
			LOGE("Cannot open \"{}\" for writing", targetPath);
			return false;
		}

		NoSectorReader noReader;
		CdiSectorSink sink(*target, IsoSectorSize);
		if (!builder.Write(sink, volumeSectorCount, noReader) || !WriteZeroSectors(sink, GameCubeTailSectors) || !sink.Flush()) {
			return false;
		}

		LOGI("The disc is {} sectors long, the executable starts at sector {}", volumeSectorCount + GameCubeTailSectors, executableLba);
		return true;
	}
}
