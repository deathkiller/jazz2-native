#pragma once

#include "../../Main.h"

#include <memory>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/** @brief Size of one ISO 9660 logical sector, which is the user data of one CD-ROM sector */
	constexpr std::uint32_t IsoSectorSize = 2048;

	/** @brief Size of the system area an ISO 9660 volume begins with, which on a Dreamcast disc holds `IP.BIN` */
	constexpr std::uint32_t IsoSystemAreaSize = 16 * IsoSectorSize;

	/**
		@brief Reads the logical sectors of a data track, addressed by their absolute LBA

		The file system stores absolute addresses --- the ones the drive is asked for --- so everything here
		works in them as well, and the track a volume happens to live in only shows up when the addresses are
		turned into positions in a file.
	*/
	class ISectorReader
	{
	public:
		virtual ~ISectorReader() {}

		/** @brief Reads one logical sector into a buffer of @ref IsoSectorSize bytes */
		virtual bool ReadSector(std::uint32_t lba, std::uint8_t* destination) = 0;
	};

	/** @brief Receives the logical sectors of a generated volume, in order from its first one */
	class ISectorSink
	{
	public:
		virtual ~ISectorSink() {}

		/** @brief Writes one logical sector from a buffer of @ref IsoSectorSize bytes */
		virtual bool WriteSector(const std::uint8_t* data) = 0;
	};

	/**
		@brief Reads an ISO 9660 file system

		Only as much of the format as taking a volume apart again requires: the descriptors, so a generated one
		can keep the identity of the one it replaces, and the directory hierarchy, so the files that are not
		being replaced can be carried over.
	*/
	class Iso9660Reader
	{
	public:
		/** @brief One entry of a directory */
		struct Entry {
			/** @brief Name as it is stored, with the `;1` version suffix already removed */
			String Name;
			/** @brief Absolute address of the first sector of the entry */
			std::uint32_t Lba = 0;
			/** @brief Size in bytes, or the size of the extent of a directory */
			std::uint32_t Size = 0;
			bool IsDirectory = false;
			/** @brief Recording date and time, in the 7-byte form a directory record stores */
			std::uint8_t Recorded[7] {};
		};

		Iso9660Reader() : _reader(nullptr), _rootLba(0), _rootSize(0), _volumeSpaceSize(0) {}

		/**
			@brief Reads the volume descriptors of the file system in the specified track

			@param reader			Provides the sectors of the track
			@param trackStartLba	Absolute address the track begins at, which its system area is relative to
		*/
		bool Open(ISectorReader& reader, std::uint32_t trackStartLba);

		/** @brief Reads the entries of a directory, leaving out the `.` and `..` records */
		bool ReadDirectory(std::uint32_t lba, std::uint32_t size, SmallVectorImpl<Entry>& entries);

		/** @brief The primary volume descriptor, exactly as it is stored */
		ArrayView<const std::uint8_t> GetPrimaryDescriptor() const {
			return _primaryDescriptor;
		}

		/** @brief The Joliet supplementary volume descriptor, or an empty view if the volume has none */
		ArrayView<const std::uint8_t> GetSupplementaryDescriptor() const {
			return _supplementaryDescriptor;
		}

		/** @brief Absolute address of the root directory, of the Joliet hierarchy if the volume has one */
		std::uint32_t GetRootLba() const {
			return _rootLba;
		}

		/** @brief Size of the extent holding the root directory */
		std::uint32_t GetRootSize() const {
			return _rootSize;
		}

		/** @brief Number of logical sectors the volume declares, which is not always the whole track */
		std::uint32_t GetVolumeSpaceSize() const {
			return _volumeSpaceSize;
		}

	private:
		ISectorReader* _reader;
		std::uint32_t _rootLba;
		std::uint32_t _rootSize;
		std::uint32_t _volumeSpaceSize;
		Array<std::uint8_t> _primaryDescriptor;
		Array<std::uint8_t> _supplementaryDescriptor;
	};

	/**
		@brief Generates an ISO 9660 file system

		Writes the plain hierarchy and a Joliet one beside it. Both are needed: the plain one is what the
		bootstrap of the Dreamcast looks `1ST_READ.BIN` up in, and it can only spell names in upper case out of
		a very small alphabet, which mangles most of the names the game's content uses; the Joliet hierarchy
		carries them as they really are, and that is the one KallistiOS reads whenever a disc has it.

		Files are placed at the end of the volume, where a CD passes the most data per rotation under the head.
		A disc that is mostly empty therefore reads its content roughly twice as fast as one filled from the
		inside out, which is what @cpp mkdcdisc @ce does by default and what this preserves.
	*/
	class Iso9660Builder
	{
	public:
		Iso9660Builder();
		~Iso9660Builder();

		/** @brief Sets the 32 KB system area the volume begins with */
		void SetSystemArea(ArrayView<const std::uint8_t> data);

		/**
			@brief Sets the volume descriptors the generated ones are derived from

			Everything that is not a size or an address --- the volume label, the publisher, the dates --- is
			taken from these, so a rewritten disc keeps the identity of the one it was made from. The
			supplementary descriptor may be empty, in which case a Joliet one is derived from the primary.
		*/
		void SetDescriptorTemplates(ArrayView<const std::uint8_t> primary, ArrayView<const std::uint8_t> supplementary);

		/** @brief Sets the absolute address the track carrying the volume begins at */
		void SetTrackStartLba(std::uint32_t lba) {
			_trackStartLba = lba;
		}

		/** @brief Adds an empty directory, which files added afterwards may be put in */
		bool AddDirectory(StringView path, const std::uint8_t recorded[7]);

		/** @brief Adds a file whose contents are copied from the volume being read */
		bool AddFileFromImage(StringView path, std::uint32_t lba, std::uint32_t size, const std::uint8_t recorded[7]);

		/** @brief Adds a directory and everything below it from the file system */
		bool AddDirectoryFromDisk(StringView path, StringView hostPath);

		/** @brief Number of logical sectors the volume needs to hold everything that was added */
		std::uint32_t GetRequiredSectorCount();

		/**
			@brief Lays the volume out over the specified number of sectors and writes it

			@param sink					Receives every sector of the volume, from the first one
			@param volumeSectorCount	How long the volume is, at least @ref GetRequiredSectorCount() sectors
			@param imageReader			Provides the sectors of files added by @ref AddFileFromImage()
		*/
		bool Write(ISectorSink& sink, std::uint32_t volumeSectorCount, ISectorReader& imageReader);

	private:
		/** @brief One file or directory of the hierarchy being built */
		struct Node {
			/** @brief Name as it was given */
			String Name;
			/** @brief Identifier in the plain hierarchy --- upper case, sanitized and, for a file, versioned */
			String IsoName;
			/** @brief Identifier in the Joliet hierarchy --- the name as it is, in UCS-2, likewise versioned */
			Array<std::uint8_t> JolietName;
			bool IsDirectory = false;
			/** @brief Where the contents come from; empty for a file copied out of the volume being read */
			String HostPath;
			/** @brief Where the contents are in the volume being read, when they are not read from a file */
			std::uint32_t SourceLba = 0;
			std::uint32_t Size = 0;
			std::uint8_t Recorded[7] {};
			Node* Parent = nullptr;
			SmallVector<std::unique_ptr<Node>, 0> Children;
			/** @brief The same children in the order the Joliet hierarchy has to list them */
			SmallVector<Node*, 0> JolietChildren;

			/** @brief Address assigned to the contents, or to the extent of a directory */
			std::uint32_t Lba = 0;
			/** @brief Address of the extent of a directory in the Joliet hierarchy */
			std::uint32_t JolietLba = 0;
			/** @brief Sizes of the two extents of a directory */
			std::uint32_t ExtentSize = 0, JolietExtentSize = 0;
			/** @brief One-based position in the path tables, and that of the parent directory */
			std::uint32_t PathTableIndex = 0, ParentPathTableIndex = 0;
		};

		/** @brief Where everything ended up, once the volume has been laid out */
		struct Layout {
			/** @brief Addresses relative to the start of the volume, unlike the ones the records store */
			std::uint32_t PathTableLba = 0, JolietPathTableLba = 0;
			std::uint32_t PathTableSectors = 0, JolietPathTableSectors = 0;
			std::uint32_t PathTableSize = 0, JolietPathTableSize = 0;
			/** @brief Where the sector the contents of the first file are in, this one absolute */
			std::uint32_t FirstFileLba = 0;
			std::uint32_t VolumeSectorCount = 0;
			/** @brief Directories in the order the path tables list them, root first */
			SmallVector<Node*, 0> Directories;
			/** @brief Files in the order their contents are stored */
			SmallVector<Node*, 0> Files;
		};

		Node _root;
		std::uint32_t _trackStartLba;
		Array<std::uint8_t> _systemArea;
		Array<std::uint8_t> _primaryTemplate;
		Array<std::uint8_t> _supplementaryTemplate;

		Node* FindOrCreateDirectory(StringView path);
		Node* AddChild(Node& parent, StringView name, bool isDirectory);
		bool AddDirectoryContents(Node& parent, StringView hostPath);
		void AssignNames(Node& node);
		bool BuildLayout(std::uint32_t volumeSectorCount, Layout& layout);
		void WriteDescriptors(ArrayView<std::uint8_t> target, const Layout& layout);
		void WritePathTables(ArrayView<std::uint8_t> target, const Layout& layout);
		void WriteDirectoryExtents(ArrayView<std::uint8_t> target, const Layout& layout, bool joliet);
	};
}
