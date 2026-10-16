#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "LevelDocument.h"
#include "DocumentChanges.h"

#include <Containers/ArrayView.h>

#include <memory>

namespace Jazz2::Editor
{
	/** @brief Parts of a level kept by a @ref DocumentSnapshot, supports a bitwise combination of its member values */
	enum class SnapshotParts : std::uint32_t
	{
		None = 0,						/**< Nothing */
		Properties = 0x01,				/**< Level flags, properties and palettes */
		TileSets = 0x02,				/**< Tile sets and overridden tiles */
		Texts = 0x04,					/**< Level texts */
		Animations = 0x08,				/**< Animated tiles */
		Layers = 0x10,					/**< Layers with their tiles, and all events, which depend on the sprite layer */
		Chunks = 0x20,					/**< Other chunks */

		All = 0x3F						/**< Everything */
	};

	DEATH_ENUM_FLAGS(SnapshotParts);

	/**
		@brief Data compressed by Deflate in memory
	*/
	class CompressedBuffer
	{
	public:
		CompressedBuffer();

		CompressedBuffer(const CompressedBuffer&) = delete;
		CompressedBuffer(CompressedBuffer&&) noexcept = default;
		CompressedBuffer& operator=(const CompressedBuffer&) = delete;
		CompressedBuffer& operator=(CompressedBuffer&&) noexcept = default;

		/** @brief Compresses data, replacing what was there */
		void Compress(ArrayView<const std::uint8_t> data, std::int32_t compressionLevel = 1);
		/** @brief Decompresses the data, returns `false` if it can't be */
		bool Decompress(SmallVectorImpl<std::uint8_t>& data) const;

		/** @brief Returns `true` if there are no data */
		bool IsEmpty() const {
			return (_originalSize == 0);
		}
		/** @brief Returns the size of the data before compression */
		std::size_t GetOriginalSize() const {
			return _originalSize;
		}
		/** @brief Returns the memory taken by the compressed data */
		std::size_t GetMemoryUsage() const {
			return _size;
		}

	private:
		std::unique_ptr<std::uint8_t[]> _data;
		std::size_t _size;
		std::size_t _originalSize;
	};

	/**
		@brief Parts of a level being edited, compressed in memory

		Keeps what an edit command changes in a large way --- added or resized layers, replaced tile sets --- so
		it can be put back.
	*/
	class DocumentSnapshot
	{
	public:
		DocumentSnapshot();

		/** @brief Takes a snapshot of parts of a document */
		static DocumentSnapshot Capture(const LevelDocument& document, SnapshotParts parts);

		/** @brief Puts the parts kept by the snapshot back into a document */
		void Restore(LevelDocument& document) const;

		/** @brief Returns the parts kept by the snapshot */
		SnapshotParts GetParts() const {
			return _parts;
		}
		/** @brief Returns the memory taken by the snapshot */
		std::size_t GetMemoryUsage() const {
			return _data.GetMemoryUsage();
		}

		/** @brief Returns the changes of a document caused by restoring the given parts */
		static DocumentChangeKind GetChangeKinds(SnapshotParts parts);

	private:
		SnapshotParts _parts;
		CompressedBuffer _data;
	};
}

#endif
