#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "LevelDocument.h"
#include "DocumentChanges.h"

#include <Containers/ArrayView.h>

#include <memory>
#include <utility>

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
		Layers = 0x10,					/**< Layers with their tiles, all events, which depend on the sprite layer, and the ground lines, which belong to layers */
		Chunks = 0x20,					/**< Other chunks, except those the editor writes on its own when the level is saved */

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
		friend class CommandArchive;

		std::unique_ptr<std::uint8_t[]> _data;
		std::size_t _size;
		std::size_t _originalSize;
	};

	/**
		@brief Identities of this run of the editor for the layers kept by another one

		The identity of a layer (see @ref DocumentLayer::Id) is unique only while the editor runs, so the layers of
		snapshots kept in a recovery copy (see @ref CommandArchive) get new ones when they're read, the same one for
		the same layer in all of them.
	*/
	class LayerIdRemap
	{
	public:
		/** @brief Returns the identity of this run for an identity of the other one, the same one every time */
		std::uint32_t Map(std::uint32_t id);

	private:
		SmallVector<std::pair<std::uint32_t, std::uint32_t>, 0> _ids;
	};

	/**
		@brief Parts of a level being edited, compressed in memory

		Keeps what an edit command changes in a large way --- resized layers, replaced tile sets --- so it can be put
		back. What isn't a change of the level stays as it is when a snapshot is restored: the editor state of layers
		that are still there (hidden, locked), and the chunks the editor writes on its own when the level is saved,
		such as the view or the preview (see @ref IsVolatileChunk()).

		The data is kept in the native byte order and layout of the records, and a recovery copy keeps it as it is
		(see @ref CommandArchive), so any change of what is written changes @ref EncodingVersion too.
	*/
	class DocumentSnapshot
	{
	public:
		/**
		 * @brief Version of how the parts are written, a part of the fingerprint of a recovery copy
		 *
		 * Has to change whenever what any part writes changes (the properties, the tile sets, a layer as
		 * @ref CompressLayer() writes it too, the events, the chunks), a recovery copy of another version would be
		 * misread otherwise.
		 */
		static constexpr std::uint32_t EncodingVersion = 1;

		DocumentSnapshot();

		/**
		 * @brief Takes a snapshot of parts of a document
		 *
		 * With @p layer, @ref SnapshotParts::Layers keeps only that layer, for a change that doesn't add or remove any.
		 * Data kept aside as it is (the tile sets the level stores in itself) is shared with @p previous where it's the
		 * same, as with the snapshot before a change.
		 */
		static DocumentSnapshot Capture(const LevelDocument& document, SnapshotParts parts, std::int32_t layer = -1, const DocumentSnapshot* previous = nullptr);

		/**
		 * @brief Puts the parts kept by the snapshot back into a document
		 *
		 * All of them are read before any is put back, so a damaged snapshot leaves the document as it is and returns `false`.
		 */
		bool Restore(LevelDocument& document) const;

		/** @brief Returns the parts kept by the snapshot */
		SnapshotParts GetParts() const {
			return _parts;
		}
		/** @brief Returns the only layer kept, or -1 if all of them are */
		std::int32_t GetLayer() const {
			return _layer;
		}
		/** @brief Returns the memory taken by the snapshot */
		std::size_t GetMemoryUsage() const;

		/** @brief Returns the changes of a document caused by restoring the given parts */
		static DocumentChangeKind GetChangeKinds(SnapshotParts parts);

		/**
		 * @brief Returns `true` for a chunk the editor writes on its own when the level is saved
		 *
		 * Such as the view (`edoc`), the preview (`PRVW`), the paths of the play-tests (`ppth`) or the path of a
		 * recovery copy (`rcov`). They aren't changes of the level, so restoring @ref SnapshotParts::Chunks leaves
		 * them as they are.
		 */
		static bool IsVolatileChunk(std::uint32_t tag);
		/** @brief Returns `true` for a chunk that belongs to layers, kept by @ref SnapshotParts::Layers */
		static bool IsLayerChunk(std::uint32_t tag);

		/** @brief Compresses a single layer, with its tiles and its editor state */
		static void CompressLayer(const DocumentLayer& layer, CompressedBuffer& buffer);
		/** @brief Decompresses a layer compressed by @ref CompressLayer(), returns `false` if it's damaged */
		static bool DecompressLayer(const CompressedBuffer& buffer, DocumentLayer& layer);

		/** @brief Gives the layers kept by the snapshot the identities of this run of the editor, returns `false` if it's damaged */
		bool RemapLayerIds(LayerIdRemap& remap);
		/** @brief Gives a layer compressed by @ref CompressLayer() the identity of this run of the editor, returns `false` if it's damaged */
		static bool RemapLayerId(CompressedBuffer& buffer, LayerIdRemap& remap);
		/** @brief Shares the data kept aside as it is with another snapshot where it's the same, so it's kept only once */
		void ShareBlobsWith(const DocumentSnapshot& other);

	private:
		friend class CommandArchive;

		using Blob = std::shared_ptr<const SmallVector<std::uint8_t, 0>>;

		SnapshotParts _parts;
		std::int32_t _layer;
		CompressedBuffer _data;
		// Data that is compressed already, the tile sets the level stores in itself, kept as it is and never changed,
		// so the snapshots before and after a change share it
		SmallVector<Blob, 0> _blobs;
	};
}

#endif
