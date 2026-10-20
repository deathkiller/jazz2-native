#include "DocumentSnapshot.h"

#if defined(WITH_EDITOR)

#include "DocumentViewState.h"
#include "TerrainSplines.h"

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Editor
{
	namespace
	{
		using BlobList = SmallVectorImpl<std::shared_ptr<const SmallVector<std::uint8_t, 0>>>;

		// Data compressed already this large is kept aside as it is, compressing it again would only take time
		constexpr std::size_t BlobThreshold = 4096;

		// Chunks the snapshots treat in a way of their own, any other chunk is of SnapshotParts::Chunks
		struct ChunkTraits
		{
			std::uint32_t Tag;
			// Written by the editor on its own when the level is saved, or by the recovery copy, not a change of the level
			bool Volatile;
			// Belongs to layers, kept by SnapshotParts::Layers
			bool OfLayers;
		};

		constexpr ChunkTraits KnownChunks[] = {
			{ DocumentViewState::Chunk, true, false },
			{ Formats::LevelFile::PreviewChunk, true, false },
			// The paths of the play-tests, written whenever a play-test ends, never undone with the level
			{ Formats::LevelFile::MakeChunkTag('p', 'p', 't', 'h'), true, false },
			// The path and the history of a recovery copy, they're only in the copy
			{ Formats::LevelFile::MakeChunkTag('r', 'c', 'o', 'v'), true, false },
			{ Formats::LevelFile::MakeChunkTag('r', 'h', 's', 't'), true, false },
			{ TerrainSpline::Chunk, false, true },
			{ TerrainSpline::LegacyChunk, false, true },
		};

		const ChunkTraits* FindChunkTraits(std::uint32_t tag)
		{
			for (const ChunkTraits& traits : KnownChunks) {
				if (traits.Tag == tag) {
					return &traits;
				}
			}
			return nullptr;
		}

		// Snapshots live only in memory, so values are kept in the native byte order
		class SnapshotWriter
		{
		public:
			explicit SnapshotWriter(SmallVectorImpl<std::uint8_t>& data, BlobList* blobs = nullptr, const BlobList* shareFrom = nullptr)
				: _data(data), _blobs(blobs), _shareFrom(shareFrom) {}

			void WriteBlob(const SmallVectorImpl<std::uint8_t>& values) {
				if (_blobs != nullptr && values.size() >= BlobThreshold) {
					WriteValue<std::uint8_t>(1);
					WriteValue<std::uint32_t>(std::uint32_t(_blobs->size()));
					// The same data as of the snapshot before is kept only once
					if (_shareFrom != nullptr) {
						for (const auto& blob : *_shareFrom) {
							if (blob->size() == values.size() && std::memcmp(blob->data(), values.data(), values.size()) == 0) {
								_blobs->push_back(blob);
								return;
							}
						}
					}
					auto blob = std::make_shared<SmallVector<std::uint8_t, 0>>();
					blob->append(values.begin(), values.end());
					_blobs->push_back(Death::move(blob));
				} else {
					WriteValue<std::uint8_t>(0);
					WriteArray(values);
				}
			}

			void WriteBytes(const void* source, std::size_t size) {
				const std::uint8_t* bytes = static_cast<const std::uint8_t*>(source);
				_data.append(bytes, bytes + size);
			}

			template<typename T>
			void WriteValue(const T& value) {
				static_assert(std::is_trivially_copyable<T>::value, "The value has to be trivially copyable");
				WriteBytes(&value, sizeof(T));
			}

			template<typename T>
			void WriteArray(const SmallVectorImpl<T>& values) {
				static_assert(std::is_trivially_copyable<T>::value, "The values have to be trivially copyable");
				WriteValue<std::uint32_t>(std::uint32_t(values.size()));
				WriteBytes(values.data(), values.size() * sizeof(T));
			}

			void WriteString(StringView value) {
				WriteValue<std::uint32_t>(std::uint32_t(value.size()));
				WriteBytes(value.data(), value.size());
			}

		private:
			SmallVectorImpl<std::uint8_t>& _data;
			BlobList* _blobs;
			const BlobList* _shareFrom;
		};

		class SnapshotReader
		{
		public:
			// Without `keepBlobs` the data kept aside is only checked, not copied, for a reader that only walks through
			explicit SnapshotReader(ArrayView<const std::uint8_t> data, const BlobList* blobs = nullptr, bool keepBlobs = true)
				: _start(data.data()), _pos(data.data()), _end(data.data() + data.size()), _blobs(blobs), _failed(false), _keepBlobs(keepBlobs) {}

			bool IsValid() const {
				return !_failed;
			}

			void Fail() {
				_failed = true;
			}

			std::size_t GetOffset() const {
				return std::size_t(_pos - _start);
			}

			std::size_t GetRemaining() const {
				return std::size_t(_end - _pos);
			}

			void ReadBlob(SmallVectorImpl<std::uint8_t>& values) {
				if (ReadValue<std::uint8_t>() == 0) {
					ReadArray(values);
					return;
				}
				std::uint32_t index = ReadValue<std::uint32_t>();
				if (_blobs == nullptr || index >= _blobs->size() || (*_blobs)[index] == nullptr) {
					_failed = true;
					values.clear();
					return;
				}
				if (!_keepBlobs) {
					values.clear();
					return;
				}
				const SmallVector<std::uint8_t, 0>& blob = *(*_blobs)[index];
				values.assign(blob.begin(), blob.end());
			}

			void ReadBytes(void* destination, std::size_t size) {
				if (std::size_t(_end - _pos) < size) {
					_failed = true;
					std::memset(destination, 0, size);
					_pos = _end;
					return;
				}
				std::memcpy(destination, _pos, size);
				_pos += size;
			}

			template<typename T>
			T ReadValue() {
				T value;
				ReadBytes(&value, sizeof(T));
				return value;
			}

			template<typename T>
			void ReadArray(SmallVectorImpl<T>& values) {
				std::uint32_t count = ReadValue<std::uint32_t>();
				if (std::size_t(_end - _pos) / sizeof(T) < count) {
					_failed = true;
					values.clear();
					return;
				}
				values.resize_for_overwrite(count);
				ReadBytes(values.data(), count * sizeof(T));
			}

			String ReadString() {
				std::uint32_t length = ReadValue<std::uint32_t>();
				if (std::size_t(_end - _pos) < length) {
					_failed = true;
					return {};
				}
				String value(reinterpret_cast<const char*>(_pos), length);
				_pos += length;
				return value;
			}

		private:
			const std::uint8_t* _start;
			const std::uint8_t* _pos;
			const std::uint8_t* _end;
			const BlobList* _blobs;
			bool _failed;
			bool _keepBlobs;
		};

		// A recovery copy keeps what the parts below write as it is, any change of it has to change
		// DocumentSnapshot::EncodingVersion, so a copy of another build isn't misread
		void WriteProperties(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue(document.Flags);
			const Formats::LevelProperties& p = document.Properties;
			w.WriteString(p.DisplayName);
			w.WriteString(p.Author);
			w.WriteString(p.NextLevel);
			w.WriteString(p.SecretLevel);
			w.WriteString(p.BonusLevel);
			w.WriteString(p.DefaultTileSet);
			w.WriteString(p.Music);
			w.WriteValue(p.AmbientColor);
			w.WriteValue(p.Weather);
			w.WriteValue(p.WeatherIntensity);
			w.WriteValue(p.WaterLevel);
			w.WriteValue(p.CaptionTileId);
			w.WriteBytes(document.LevelPalette, sizeof(document.LevelPalette));
			w.WriteValue<std::uint32_t>(std::uint32_t(document.AdditionalPalettes.size()));
			for (const Formats::PaletteRecord& palette : document.AdditionalPalettes) {
				w.WriteString(palette.Name);
				w.WriteBytes(palette.Colors, sizeof(palette.Colors));
			}
		}

		void ReadProperties(SnapshotReader& r, LevelDocument& document)
		{
			document.Flags = r.ReadValue<LevelFlags>();
			Formats::LevelProperties& p = document.Properties;
			p.DisplayName = r.ReadString();
			p.Author = r.ReadString();
			p.NextLevel = r.ReadString();
			p.SecretLevel = r.ReadString();
			p.BonusLevel = r.ReadString();
			p.DefaultTileSet = r.ReadString();
			p.Music = r.ReadString();
			p.AmbientColor = r.ReadValue<std::uint32_t>();
			p.Weather = r.ReadValue<std::uint8_t>();
			p.WeatherIntensity = r.ReadValue<std::uint8_t>();
			p.WaterLevel = r.ReadValue<std::uint16_t>();
			p.CaptionTileId = r.ReadValue<std::uint16_t>();
			r.ReadBytes(document.LevelPalette, sizeof(document.LevelPalette));
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.AdditionalPalettes.clear();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				Formats::PaletteRecord& palette = document.AdditionalPalettes.emplace_back();
				palette.Name = r.ReadString();
				r.ReadBytes(palette.Colors, sizeof(palette.Colors));
			}
		}

		void WriteTileSets(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue(document.DefaultTileCount);
			w.WriteValue<std::uint32_t>(std::uint32_t(document.ExtraTileSets.size()));
			for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
				w.WriteValue(tileSet.Flags);
				w.WriteString(tileSet.Name);
				w.WriteValue(tileSet.Offset);
				w.WriteValue(tileSet.Count);
				w.WriteBytes(tileSet.Remapping, sizeof(tileSet.Remapping));
				w.WriteValue(tileSet.AlternatePalette);
				// A tile set the level stores in itself is the whole file, which is compressed already
				w.WriteBlob(tileSet.Data);
			}
			w.WriteArray(document.DiffuseOverrides);
			w.WriteArray(document.MaskOverrides);
		}

		void ReadTileSets(SnapshotReader& r, LevelDocument& document)
		{
			document.DefaultTileCount = r.ReadValue<std::int32_t>();
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.ExtraTileSets.clear();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets.emplace_back();
				tileSet.Flags = r.ReadValue<std::uint8_t>();
				tileSet.Name = r.ReadString();
				tileSet.Offset = r.ReadValue<std::uint16_t>();
				tileSet.Count = r.ReadValue<std::uint16_t>();
				r.ReadBytes(tileSet.Remapping, sizeof(tileSet.Remapping));
				tileSet.AlternatePalette = r.ReadValue<std::uint8_t>();
				r.ReadBlob(tileSet.Data);
			}
			r.ReadArray(document.DiffuseOverrides);
			r.ReadArray(document.MaskOverrides);
		}

		void WriteTexts(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue<std::uint32_t>(std::uint32_t(document.Texts.size()));
			for (const String& text : document.Texts) {
				w.WriteString(text);
			}
		}

		void ReadTexts(SnapshotReader& r, LevelDocument& document)
		{
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.Texts.clear();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				document.Texts.push_back(r.ReadString());
			}
		}

		void WriteAnimations(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue(document.TileIdSpace);
			w.WriteValue<std::uint32_t>(std::uint32_t(document.Animations.size()));
			for (const DocumentAnimation& animation : document.Animations) {
				w.WriteValue(animation.Timing.FrameDuration16);
				w.WriteValue(animation.Timing.Delay);
				w.WriteValue(animation.Timing.DelayJitter);
				w.WriteValue(animation.Timing.PingPongDelay);
				w.WriteValue(animation.Timing.IsPingPong);
				w.WriteArray(animation.Frames);
			}
		}

		void ReadAnimations(SnapshotReader& r, LevelDocument& document)
		{
			document.TileIdSpace = r.ReadValue<std::int32_t>();
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.Animations.clear();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				DocumentAnimation& animation = document.Animations.emplace_back();
				animation.Timing.FrameDuration16 = r.ReadValue<std::uint16_t>();
				animation.Timing.Delay = r.ReadValue<std::uint16_t>();
				animation.Timing.DelayJitter = r.ReadValue<std::uint16_t>();
				animation.Timing.PingPongDelay = r.ReadValue<std::uint16_t>();
				animation.Timing.IsPingPong = r.ReadValue<std::uint8_t>();
				r.ReadArray(animation.Frames);
			}
		}

		void WriteLayer(SnapshotWriter& w, const DocumentLayer& layer)
		{
			w.WriteValue(layer.Record);
			w.WriteValue(layer.Id);
			w.WriteString(layer.Name);
			w.WriteValue(layer.EditorFlags);
			w.WriteValue(layer.EditorOpacity);
			w.WriteArray(layer.Tiles);
			// Palette indices, which compress well with the rest
			w.WriteArray(layer.Texture);
		}

		void ReadLayer(SnapshotReader& r, DocumentLayer& layer)
		{
			layer.Record = r.ReadValue<Formats::LayerRecord>();
			layer.Id = r.ReadValue<std::uint32_t>();
			layer.Name = r.ReadString();
			layer.EditorFlags = r.ReadValue<std::uint8_t>();
			layer.EditorOpacity = r.ReadValue<std::uint8_t>();
			r.ReadArray(layer.Tiles);
			r.ReadArray(layer.Texture);
			// Tiles that don't fill the layer would be read past their end
			if (layer.Record.Width < 0 || layer.Record.Height < 0 ||
				std::uint64_t(layer.Tiles.size()) != std::uint64_t(layer.Record.Width) * std::uint64_t(layer.Record.Height)) {
				r.Fail();
			}
		}

		void WriteLayers(SnapshotWriter& w, const LevelDocument& document, std::int32_t onlyLayer)
		{
			if (onlyLayer >= 0 && onlyLayer < std::int32_t(document.Layers.size())) {
				w.WriteValue<std::int32_t>(onlyLayer);
				WriteLayer(w, document.Layers[onlyLayer]);
			} else {
				w.WriteValue<std::int32_t>(-1);
				w.WriteValue<std::uint32_t>(std::uint32_t(document.Layers.size()));
				for (const DocumentLayer& layer : document.Layers) {
					WriteLayer(w, layer);
				}
			}
			w.WriteArray(document.Events);
			w.WriteArray(document.OffGridEvents);

			// Chunks that belong to layers, such as the ground lines, which change with them
			std::uint32_t count = 0;
			for (const Formats::ChunkRecord& chunk : document.Chunks) {
				count += (DocumentSnapshot::IsLayerChunk(chunk.Tag) ? 1 : 0);
			}
			w.WriteValue<std::uint32_t>(count);
			for (const Formats::ChunkRecord& chunk : document.Chunks) {
				if (DocumentSnapshot::IsLayerChunk(chunk.Tag)) {
					w.WriteValue(chunk.Tag);
					w.WriteArray(chunk.Data);
				}
			}
		}

		// The layers part as it's read, put into the document only once the whole snapshot was read
		struct StagedLayers
		{
			// The only layer kept, or -1 if all of them are
			std::int32_t OnlyLayer = -1;
			SmallVector<DocumentLayer, 0> Layers;
			SmallVector<DocumentEvent, 0> Events;
			SmallVector<Formats::OffGridEventRecord, 0> OffGridEvents;
			SmallVector<Formats::ChunkRecord, 0> Chunks;
		};

		void ReadLayers(SnapshotReader& r, const LevelDocument& document, StagedLayers& staged)
		{
			staged.OnlyLayer = r.ReadValue<std::int32_t>();
			if (staged.OnlyLayer >= 0) {
				if (staged.OnlyLayer >= std::int32_t(document.Layers.size())) {
					LOGE("Layer {} cannot be put back, the level has only {} layers", staged.OnlyLayer, document.Layers.size());
					r.Fail();
					return;
				}
				ReadLayer(r, staged.Layers.emplace_back());
			} else {
				std::uint32_t count = r.ReadValue<std::uint32_t>();
				// Each layer takes its record at least, a larger count is damaged
				if (count > r.GetRemaining() / sizeof(Formats::LayerRecord)) {
					r.Fail();
					return;
				}
				staged.Layers.reserve(count);
				for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
					ReadLayer(r, staged.Layers.emplace_back());
				}
			}
			r.ReadArray(staged.Events);
			r.ReadArray(staged.OffGridEvents);

			std::uint32_t chunkCount = r.ReadValue<std::uint32_t>();
			for (std::uint32_t i = 0; i < chunkCount && r.IsValid(); i++) {
				Formats::ChunkRecord& chunk = staged.Chunks.emplace_back();
				chunk.Tag = r.ReadValue<std::uint32_t>();
				r.ReadArray(chunk.Data);
			}
		}

		void PutLayersBack(LevelDocument& document, StagedLayers& staged)
		{
			// The editor state of a layer that is still there stays as it is now, hiding or locking a layer isn't
			// a change that is undone
			auto keepEditorFlags = [&document](DocumentLayer& layer) {
				for (const DocumentLayer& current : document.Layers) {
					if (current.Id == layer.Id) {
						layer.EditorFlags = current.EditorFlags;
						layer.EditorOpacity = current.EditorOpacity;
						break;
					}
				}
			};
			for (DocumentLayer& layer : staged.Layers) {
				keepEditorFlags(layer);
			}
			if (staged.OnlyLayer >= 0) {
				document.Layers[staged.OnlyLayer] = Death::move(staged.Layers[0]);
			} else {
				document.Layers = Death::move(staged.Layers);
			}
			document.Events = Death::move(staged.Events);
			document.OffGridEvents = Death::move(staged.OffGridEvents);

			for (std::size_t i = 0; i < document.Chunks.size(); ) {
				if (DocumentSnapshot::IsLayerChunk(document.Chunks[i].Tag)) {
					document.Chunks.erase(document.Chunks.begin() + i);
				} else {
					i++;
				}
			}
			for (Formats::ChunkRecord& chunk : staged.Chunks) {
				document.Chunks.push_back(Death::move(chunk));
			}
		}

		// Chunks of the other parts, and those the editor writes on its own, aren't kept by this part
		bool IsChunkOfChunksPart(std::uint32_t tag)
		{
			return (!DocumentSnapshot::IsVolatileChunk(tag) && !DocumentSnapshot::IsLayerChunk(tag));
		}

		void WriteChunks(SnapshotWriter& w, const LevelDocument& document)
		{
			std::uint32_t count = 0;
			for (const Formats::ChunkRecord& chunk : document.Chunks) {
				count += (IsChunkOfChunksPart(chunk.Tag) ? 1 : 0);
			}
			w.WriteValue<std::uint32_t>(count);
			for (const Formats::ChunkRecord& chunk : document.Chunks) {
				if (IsChunkOfChunksPart(chunk.Tag)) {
					w.WriteValue(chunk.Tag);
					w.WriteArray(chunk.Data);
				}
			}
		}

		void ReadChunks(SnapshotReader& r, SmallVectorImpl<Formats::ChunkRecord>& chunks)
		{
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				Formats::ChunkRecord& chunk = chunks.emplace_back();
				chunk.Tag = r.ReadValue<std::uint32_t>();
				r.ReadArray(chunk.Data);
			}
		}

		void PutChunksBack(LevelDocument& document, SmallVector<Formats::ChunkRecord, 0>& chunks)
		{
			// The others stay as they are now, after those that are put back
			for (Formats::ChunkRecord& chunk : document.Chunks) {
				if (!IsChunkOfChunksPart(chunk.Tag)) {
					chunks.push_back(Death::move(chunk));
				}
			}
			document.Chunks = Death::move(chunks);
		}

		// Size the parts will take, so the buffer doesn't grow many times for a large level
		std::size_t EstimateSize(const LevelDocument& document, SnapshotParts parts, std::int32_t onlyLayer)
		{
			std::size_t size = 256;
			if ((parts & SnapshotParts::Properties) != SnapshotParts::None) {
				size += sizeof(document.LevelPalette) + document.AdditionalPalettes.size() * sizeof(Formats::PaletteRecord);
			}
			if ((parts & SnapshotParts::TileSets) != SnapshotParts::None) {
				size += document.DiffuseOverrides.size() * sizeof(Formats::TileDiffuseOverride) + document.MaskOverrides.size() * sizeof(Formats::TileMaskOverride);
				for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
					size += sizeof(Formats::ExtraTileSetRecord) + (tileSet.Data.size() < BlobThreshold ? tileSet.Data.size() : 0);
				}
			}
			if ((parts & SnapshotParts::Layers) != SnapshotParts::None) {
				for (std::size_t i = 0; i < document.Layers.size(); i++) {
					if (onlyLayer < 0 || std::int32_t(i) == onlyLayer) {
						size += sizeof(DocumentLayer) + document.Layers[i].Name.size() + document.Layers[i].Tiles.size() * sizeof(TileRef) +
							document.Layers[i].Texture.size();
					}
				}
				size += document.Events.size() * sizeof(DocumentEvent) + document.OffGridEvents.size() * sizeof(Formats::OffGridEventRecord);
			}
			if ((parts & SnapshotParts::Chunks) != SnapshotParts::None) {
				for (const Formats::ChunkRecord& chunk : document.Chunks) {
					size += 8 + chunk.Data.size();
				}
			}
			return size;
		}
	}

	CompressedBuffer::CompressedBuffer()
		: _size(0), _originalSize(0)
	{
	}

	void CompressedBuffer::Compress(ArrayView<const std::uint8_t> data, std::int32_t compressionLevel)
	{
		_data = nullptr;
		_size = 0;
		_originalSize = data.size();
		if (data.empty()) {
			return;
		}

		MemoryStream compressed(std::int64_t(data.size() / 4 + 64));
		{
			DeflateWriter writer(compressed, compressionLevel);
			writer.Write(data.data(), std::int64_t(data.size()));
		}

		_size = std::size_t(compressed.GetSize());
		_data = std::make_unique<std::uint8_t[]>(_size);
		std::memcpy(_data.get(), compressed.GetBuffer(), _size);
	}

	bool CompressedBuffer::Decompress(SmallVectorImpl<std::uint8_t>& data) const
	{
		data.resize_for_overwrite(_originalSize);
		if (_originalSize == 0) {
			return true;
		}

		MemoryStream compressed(_data.get(), std::int64_t(_size));
		DeflateStream reader(compressed, std::int32_t(_size));
		std::size_t pos = 0;
		while (pos < _originalSize) {
			std::int64_t bytesRead = reader.Read(data.data() + pos, std::int64_t(_originalSize - pos));
			if (bytesRead <= 0) {
				return false;
			}
			pos += std::size_t(bytesRead);
		}
		return true;
	}

	DocumentSnapshot::DocumentSnapshot()
		: _parts(SnapshotParts::None), _layer(-1)
	{
	}

	std::uint32_t LayerIdRemap::Map(std::uint32_t id)
	{
		for (const auto& entry : _ids) {
			if (entry.first == id) {
				return entry.second;
			}
		}
		std::uint32_t newId = MakeLayerId();
		_ids.emplace_back(id, newId);
		return newId;
	}

	DocumentSnapshot DocumentSnapshot::Capture(const LevelDocument& document, SnapshotParts parts, std::int32_t layer, const DocumentSnapshot* previous)
	{
		if (layer >= std::int32_t(document.Layers.size())) {
			layer = -1;
		}

		DocumentSnapshot snapshot;
		snapshot._parts = parts;
		snapshot._layer = layer;

		SmallVector<std::uint8_t, 0> data;
		data.reserve(EstimateSize(document, parts, layer));
		SnapshotWriter w(data, &snapshot._blobs, previous != nullptr ? &previous->_blobs : nullptr);
		if ((parts & SnapshotParts::Properties) != SnapshotParts::None) {
			WriteProperties(w, document);
		}
		if ((parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			WriteTileSets(w, document);
		}
		if ((parts & SnapshotParts::Texts) != SnapshotParts::None) {
			WriteTexts(w, document);
		}
		if ((parts & SnapshotParts::Animations) != SnapshotParts::None) {
			WriteAnimations(w, document);
		}
		if ((parts & SnapshotParts::Layers) != SnapshotParts::None) {
			WriteLayers(w, document, layer);
		}
		if ((parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			WriteChunks(w, document);
		}

		snapshot._data.Compress(data);
		return snapshot;
	}

	bool DocumentSnapshot::Restore(LevelDocument& document) const
	{
		SmallVector<std::uint8_t, 0> data;
		// Checked for real, the document is better left as it is than filled with garbage
		if (!_data.Decompress(data)) {
			LOGE("Snapshot cannot be decompressed, the document stays as it is");
			return false;
		}

		// Everything is read aside first, a part of a damaged snapshot put back would leave the document of no state at all
		SnapshotReader r(data, &_blobs);
		LevelDocument staged;
		StagedLayers stagedLayers;
		SmallVector<Formats::ChunkRecord, 0> stagedChunks;
		if ((_parts & SnapshotParts::Properties) != SnapshotParts::None) {
			ReadProperties(r, staged);
		}
		if ((_parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			ReadTileSets(r, staged);
		}
		if ((_parts & SnapshotParts::Texts) != SnapshotParts::None) {
			ReadTexts(r, staged);
		}
		if ((_parts & SnapshotParts::Animations) != SnapshotParts::None) {
			ReadAnimations(r, staged);
		}
		if ((_parts & SnapshotParts::Layers) != SnapshotParts::None && r.IsValid()) {
			ReadLayers(r, document, stagedLayers);
		}
		if ((_parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			ReadChunks(r, stagedChunks);
		}
		if (!r.IsValid()) {
			LOGE("Snapshot is damaged, the document stays as it is");
			return false;
		}

		if ((_parts & SnapshotParts::Properties) != SnapshotParts::None) {
			document.Flags = staged.Flags;
			document.Properties = Death::move(staged.Properties);
			std::memcpy(document.LevelPalette, staged.LevelPalette, sizeof(document.LevelPalette));
			document.AdditionalPalettes = Death::move(staged.AdditionalPalettes);
		}
		if ((_parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			document.DefaultTileCount = staged.DefaultTileCount;
			document.ExtraTileSets = Death::move(staged.ExtraTileSets);
			document.DiffuseOverrides = Death::move(staged.DiffuseOverrides);
			document.MaskOverrides = Death::move(staged.MaskOverrides);
		}
		if ((_parts & SnapshotParts::Texts) != SnapshotParts::None) {
			document.Texts = Death::move(staged.Texts);
		}
		if ((_parts & SnapshotParts::Animations) != SnapshotParts::None) {
			document.TileIdSpace = staged.TileIdSpace;
			document.Animations = Death::move(staged.Animations);
		}
		if ((_parts & SnapshotParts::Layers) != SnapshotParts::None) {
			PutLayersBack(document, stagedLayers);
		}
		if ((_parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			PutChunksBack(document, stagedChunks);
		}
		return true;
	}

	std::size_t DocumentSnapshot::GetMemoryUsage() const
	{
		std::size_t usage = _data.GetMemoryUsage();
		for (const Blob& blob : _blobs) {
			// Shared with the snapshot of the other side of a change, each of them counts its part
			if (blob != nullptr) {
				usage += blob->capacity() / std::size_t(std::max<long>(blob.use_count(), 1));
			}
		}
		return usage;
	}

	bool DocumentSnapshot::IsVolatileChunk(std::uint32_t tag)
	{
		const ChunkTraits* traits = FindChunkTraits(tag);
		return (traits != nullptr && traits->Volatile);
	}

	bool DocumentSnapshot::IsLayerChunk(std::uint32_t tag)
	{
		const ChunkTraits* traits = FindChunkTraits(tag);
		return (traits != nullptr && traits->OfLayers);
	}

	bool DocumentSnapshot::RemapLayerIds(LayerIdRemap& remap)
	{
		if ((_parts & SnapshotParts::Layers) == SnapshotParts::None) {
			return true;
		}
		SmallVector<std::uint8_t, 0> data;
		if (!_data.Decompress(data)) {
			return false;
		}

		// The parts before the layers are only walked through
		SnapshotReader r(data, &_blobs, false);
		LevelDocument scratch;
		if ((_parts & SnapshotParts::Properties) != SnapshotParts::None) {
			ReadProperties(r, scratch);
		}
		if ((_parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			ReadTileSets(r, scratch);
		}
		if ((_parts & SnapshotParts::Texts) != SnapshotParts::None) {
			ReadTexts(r, scratch);
		}
		if ((_parts & SnapshotParts::Animations) != SnapshotParts::None) {
			ReadAnimations(r, scratch);
		}
		std::int32_t onlyLayer = r.ReadValue<std::int32_t>();
		std::uint32_t count = (onlyLayer >= 0 ? 1 : r.ReadValue<std::uint32_t>());
		for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
			// The identity follows the record, see WriteLayer()
			std::size_t idOffset = r.GetOffset() + sizeof(Formats::LayerRecord);
			DocumentLayer layer;
			ReadLayer(r, layer);
			if (r.IsValid()) {
				std::uint32_t id = remap.Map(layer.Id);
				std::memcpy(data.data() + idOffset, &id, sizeof(id));
			}
		}
		if (!r.IsValid()) {
			return false;
		}
		_data.Compress(data);
		return true;
	}

	bool DocumentSnapshot::RemapLayerId(CompressedBuffer& buffer, LayerIdRemap& remap)
	{
		if (buffer.IsEmpty()) {
			return true;
		}
		SmallVector<std::uint8_t, 0> data;
		if (!buffer.Decompress(data) || data.size() < sizeof(Formats::LayerRecord) + sizeof(std::uint32_t)) {
			return false;
		}
		// The identity follows the record, see WriteLayer()
		std::uint32_t id;
		std::memcpy(&id, data.data() + sizeof(Formats::LayerRecord), sizeof(id));
		id = remap.Map(id);
		std::memcpy(data.data() + sizeof(Formats::LayerRecord), &id, sizeof(id));
		buffer.Compress(data);
		return true;
	}

	void DocumentSnapshot::ShareBlobsWith(const DocumentSnapshot& other)
	{
		for (Blob& blob : _blobs) {
			if (blob == nullptr) {
				continue;
			}
			for (const Blob& candidate : other._blobs) {
				if (candidate != nullptr && candidate != blob && candidate->size() == blob->size() &&
					std::memcmp(candidate->data(), blob->data(), blob->size()) == 0) {
					blob = candidate;
					break;
				}
			}
		}
	}

	void DocumentSnapshot::CompressLayer(const DocumentLayer& layer, CompressedBuffer& buffer)
	{
		SmallVector<std::uint8_t, 0> data;
		data.reserve(sizeof(DocumentLayer) + layer.Name.size() + layer.Tiles.size() * sizeof(TileRef) + layer.Texture.size() + 16);
		SnapshotWriter w(data);
		WriteLayer(w, layer);
		buffer.Compress(data);
	}

	bool DocumentSnapshot::DecompressLayer(const CompressedBuffer& buffer, DocumentLayer& layer)
	{
		SmallVector<std::uint8_t, 0> data;
		if (!buffer.Decompress(data)) {
			return false;
		}
		SnapshotReader r(data);
		ReadLayer(r, layer);
		return r.IsValid();
	}

	DocumentChangeKind DocumentSnapshot::GetChangeKinds(SnapshotParts parts)
	{
		DocumentChangeKind kinds = DocumentChangeKind::None;
		if ((parts & SnapshotParts::Properties) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::LevelProperties | DocumentChangeKind::Palette;
		}
		if ((parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::TileSets | DocumentChangeKind::TileOverrides;
		}
		if ((parts & SnapshotParts::Texts) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::Texts;
		}
		if ((parts & SnapshotParts::Animations) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::AnimationStructure | DocumentChangeKind::AnimationTiming;
		}
		if ((parts & SnapshotParts::Layers) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::LayerStructure | DocumentChangeKind::LayerProperties |
				DocumentChangeKind::Tiles | DocumentChangeKind::Events | DocumentChangeKind::EditorState | DocumentChangeKind::Chunks;
		}
		if ((parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::Chunks;
		}
		return kinds;
	}
}

#endif
