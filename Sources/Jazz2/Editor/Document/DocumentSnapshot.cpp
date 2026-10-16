#include "DocumentSnapshot.h"

#if defined(WITH_EDITOR)

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <cstring>
#include <type_traits>

using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Editor
{
	namespace
	{
		// Snapshots live only in memory, so values are kept in the native byte order
		class SnapshotWriter
		{
		public:
			explicit SnapshotWriter(SmallVectorImpl<std::uint8_t>& data)
				: _data(data) {}

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
		};

		class SnapshotReader
		{
		public:
			explicit SnapshotReader(ArrayView<const std::uint8_t> data)
				: _pos(data.data()), _end(data.data() + data.size()), _failed(false) {}

			bool IsValid() const {
				return !_failed;
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
			const std::uint8_t* _pos;
			const std::uint8_t* _end;
			bool _failed;
		};

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
				w.WriteArray(tileSet.Data);
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
				r.ReadArray(tileSet.Data);
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

		void WriteLayers(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue<std::uint32_t>(std::uint32_t(document.Layers.size()));
			for (const DocumentLayer& layer : document.Layers) {
				w.WriteValue(layer.Record);
				w.WriteString(layer.Name);
				w.WriteValue(layer.EditorFlags);
				w.WriteArray(layer.Tiles);
			}
			w.WriteArray(document.Events);
			w.WriteArray(document.OffGridEvents);
		}

		void ReadLayers(SnapshotReader& r, LevelDocument& document)
		{
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.Layers.clear();
			document.Layers.reserve(count);
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				DocumentLayer& layer = document.Layers.emplace_back();
				layer.Record = r.ReadValue<Formats::LayerRecord>();
				layer.Name = r.ReadString();
				layer.EditorFlags = r.ReadValue<std::uint8_t>();
				r.ReadArray(layer.Tiles);
			}
			r.ReadArray(document.Events);
			r.ReadArray(document.OffGridEvents);
		}

		void WriteChunks(SnapshotWriter& w, const LevelDocument& document)
		{
			w.WriteValue<std::uint32_t>(std::uint32_t(document.Chunks.size()));
			for (const Formats::ChunkRecord& chunk : document.Chunks) {
				w.WriteValue(chunk.Tag);
				w.WriteArray(chunk.Data);
			}
		}

		void ReadChunks(SnapshotReader& r, LevelDocument& document)
		{
			std::uint32_t count = r.ReadValue<std::uint32_t>();
			document.Chunks.clear();
			for (std::uint32_t i = 0; i < count && r.IsValid(); i++) {
				Formats::ChunkRecord& chunk = document.Chunks.emplace_back();
				chunk.Tag = r.ReadValue<std::uint32_t>();
				r.ReadArray(chunk.Data);
			}
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
		: _parts(SnapshotParts::None)
	{
	}

	DocumentSnapshot DocumentSnapshot::Capture(const LevelDocument& document, SnapshotParts parts)
	{
		SmallVector<std::uint8_t, 0> data;
		SnapshotWriter w(data);
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
			WriteLayers(w, document);
		}
		if ((parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			WriteChunks(w, document);
		}

		DocumentSnapshot snapshot;
		snapshot._parts = parts;
		snapshot._data.Compress(data);
		return snapshot;
	}

	void DocumentSnapshot::Restore(LevelDocument& document) const
	{
		SmallVector<std::uint8_t, 0> data;
		if (!_data.Decompress(data)) {
			DEATH_ASSERT_UNREACHABLE("Snapshot cannot be decompressed", );
		}

		SnapshotReader r(data);
		if ((_parts & SnapshotParts::Properties) != SnapshotParts::None) {
			ReadProperties(r, document);
		}
		if ((_parts & SnapshotParts::TileSets) != SnapshotParts::None) {
			ReadTileSets(r, document);
		}
		if ((_parts & SnapshotParts::Texts) != SnapshotParts::None) {
			ReadTexts(r, document);
		}
		if ((_parts & SnapshotParts::Animations) != SnapshotParts::None) {
			ReadAnimations(r, document);
		}
		if ((_parts & SnapshotParts::Layers) != SnapshotParts::None) {
			ReadLayers(r, document);
		}
		if ((_parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			ReadChunks(r, document);
		}

		DEATH_DEBUG_ASSERT(r.IsValid(), "Snapshot is damaged", );
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
				DocumentChangeKind::Tiles | DocumentChangeKind::Events | DocumentChangeKind::EditorState;
		}
		if ((parts & SnapshotParts::Chunks) != SnapshotParts::None) {
			kinds |= DocumentChangeKind::Chunks;
		}
		return kinds;
	}
}

#endif
