#include "TerrainSplines.h"

#if defined(WITH_EDITOR)

#include "DocumentChanges.h"
#include "LevelDocument.h"
#include "../../Formats/VarUint.h"

#include <algorithm>
#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		// The layout of the chunk is described with TerrainSpline
		constexpr std::uint8_t FlagSmooth = 0x01;
		using Formats::ReadVarUint;
		using Formats::WriteVarUint;

		void Write32(SmallVectorImpl<std::uint8_t>& data, std::uint32_t value)
		{
			for (std::int32_t i = 0; i < 4; i++) {
				data.push_back(std::uint8_t(value >> (i * 8)));
			}
		}

		bool Read32(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& value)
		{
			if (end - p < 4) {
				return false;
			}
			value = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
			p += 4;
			return true;
		}

		void WriteFloat(SmallVectorImpl<std::uint8_t>& data, float value)
		{
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			Write32(data, bits);
		}

		bool ReadFloat(const std::uint8_t*& p, const std::uint8_t* end, float& value)
		{
			std::uint32_t bits;
			if (!Read32(p, end, bits)) {
				return false;
			}
			std::memcpy(&value, &bits, sizeof(value));
			return true;
		}

		void WriteTile(SmallVectorImpl<std::uint8_t>& data, const TileRef& tile)
		{
			Write32(data, std::uint32_t(tile.Index) | (std::uint32_t(tile.Source) << 16) | (std::uint32_t(tile.Flags) << 24));
		}

		bool ReadTile(const std::uint8_t*& p, const std::uint8_t* end, TileRef& tile)
		{
			std::uint32_t value;
			if (!Read32(p, end, value)) {
				return false;
			}
			tile = TileRef{ std::uint16_t(value & 0xFFFF), std::uint8_t((value >> 16) & 0xFF), std::uint8_t(value >> 24) };
			return true;
		}

		constexpr std::uint8_t Version = 1;
		// Points farther than this are damaged, no layer is this large, and walking along them would take forever
		constexpr float MaxCoordinate = float(1 << 22);

		// Fields of a line, as both versions have them, returns false if they don't fit, @p valid is false if they make no sense
		bool ReadFields(const std::uint8_t*& p, const std::uint8_t* end, TerrainSpline& spline, bool& valid)
		{
			valid = false;
			std::uint32_t layer, pointCount, ownedCount;
			if (!Read32(p, end, layer) || p >= end) {
				return false;
			}
			spline.Layer = std::int32_t(layer);
			spline.Depth = *p++;
			if (end - p < 2) {
				return false;
			}
			spline.Kind = SplineKind(std::min<std::uint8_t>(*p++, std::uint8_t(SplineKind::Closed)));
			spline.Smooth = ((*p++ & FlagSmooth) != 0);
			if (!ReadVarUint(p, end, pointCount) || std::size_t(end - p) / 8 < std::size_t(pointCount)) {
				return false;
			}
			bool pointsValid = true;
			spline.Points.reserve(pointCount);
			for (std::uint32_t j = 0; j < pointCount; j++) {
				Vector2f point;
				ReadFloat(p, end, point.X);
				ReadFloat(p, end, point.Y);
				// Comparisons are false for NaN, so it's left out too
				if (!(point.X >= -MaxCoordinate && point.X <= MaxCoordinate && point.Y >= -MaxCoordinate && point.Y <= MaxCoordinate)) {
					pointsValid = false;
				}
				spline.Points.push_back(point);
			}
			if (!ReadVarUint(p, end, ownedCount) || std::size_t(end - p) / 16 < std::size_t(ownedCount)) {
				return false;
			}
			spline.Owned.reserve(ownedCount);
			for (std::uint32_t j = 0; j < ownedCount; j++) {
				TerrainSpline::OwnedTile owned;
				std::uint32_t x, y;
				Read32(p, end, x);
				Read32(p, end, y);
				owned.X = std::int32_t(x);
				owned.Y = std::int32_t(y);
				ReadTile(p, end, owned.Written);
				ReadTile(p, end, owned.Previous);
				spline.Owned.push_back(owned);
			}
			if (!pointsValid) {
				LOGW("Ground line of layer {} has damaged points, it's left out", spline.Layer);
			}
			valid = pointsValid;
			return true;
		}

		void WriteFields(SmallVectorImpl<std::uint8_t>& data, const TerrainSpline& spline)
		{
			Write32(data, std::uint32_t(spline.Layer));
			data.push_back(std::uint8_t(spline.Depth));
			data.push_back(std::uint8_t(spline.Kind));
			data.push_back(spline.Smooth ? FlagSmooth : 0);
			WriteVarUint(data, std::uint32_t(spline.Points.size()));
			for (const Vector2f& point : spline.Points) {
				WriteFloat(data, point.X);
				WriteFloat(data, point.Y);
			}
			WriteVarUint(data, std::uint32_t(spline.Owned.size()));
			for (const TerrainSpline::OwnedTile& owned : spline.Owned) {
				Write32(data, std::uint32_t(owned.X));
				Write32(data, std::uint32_t(owned.Y));
				WriteTile(data, owned.Written);
				WriteTile(data, owned.Previous);
			}
		}

		// varuint count, then the fields of each line one after another
		void ReadLegacy(const Formats::ChunkRecord& chunk, SmallVectorImpl<TerrainSpline>& splines)
		{
			const std::uint8_t* p = chunk.Data.data();
			const std::uint8_t* end = p + chunk.Data.size();
			std::uint32_t count;
			if (!ReadVarUint(p, end, count)) {
				return;
			}
			for (std::uint32_t i = 0; i < count; i++) {
				TerrainSpline spline;
				bool valid;
				// Without the size of a record, nothing after a truncated one can be found
				if (!ReadFields(p, end, spline, valid)) {
					return;
				}
				if (valid) {
					splines.push_back(Death::move(spline));
				}
			}
		}
	}

	void TerrainSpline::Read(const LevelDocument& document, SmallVectorImpl<TerrainSpline>& splines)
	{
		splines.clear();
		// This version never writes the legacy chunk and drops it once it's read, so a level that has both was saved
		// by 3.9.0 last, which wrote the legacy one anew and only kept the other one as it was
		if (const Formats::ChunkRecord* legacy = document.FindChunk(LegacyChunk)) {
			ReadLegacy(*legacy, splines);
			return;
		}
		const Formats::ChunkRecord* chunk = document.FindChunk(Chunk);
		if (chunk == nullptr) {
			return;
		}
		const std::uint8_t* p = chunk->Data.data();
		const std::uint8_t* end = p + chunk->Data.size();
		std::uint32_t count;
		// A newer version only adds fields to the records, so any version but 0 is read
		if (p >= end || *p++ == 0 || !ReadVarUint(p, end, count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			std::uint32_t size;
			if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
				return;
			}
			const std::uint8_t* recordEnd = p + size;
			TerrainSpline spline;
			bool valid;
			if (ReadFields(p, recordEnd, spline, valid) && valid) {
				splines.push_back(Death::move(spline));
			}
			// What a newer version added is skipped
			p = recordEnd;
		}
	}

	void TerrainSpline::Write(const SmallVectorImpl<TerrainSpline>& splines, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (splines.empty()) {
			// No lines, no chunk
			return;
		}
		data.push_back(Version);
		WriteVarUint(data, std::uint32_t(splines.size()));
		SmallVector<std::uint8_t, 0> record;
		for (const TerrainSpline& spline : splines) {
			record.clear();
			WriteFields(record, spline);
			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}
	}

	void TerrainSpline::GetChunkData(const LevelDocument& document, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		// The legacy chunk is the newer one if there are both, see Read()
		if (document.FindChunk(LegacyChunk) != nullptr) {
			SmallVector<TerrainSpline, 0> splines;
			Read(document, splines);
			Write(splines, data);
		} else if (const Formats::ChunkRecord* chunk = document.FindChunk(Chunk)) {
			data.append(chunk->Data.begin(), chunk->Data.end());
		}
	}

	void TerrainSpline::SetChunkData(LevelDocument& document, ArrayView<const std::uint8_t> data)
	{
		document.SetChunk(Chunk, data);
		document.SetChunk(LegacyChunk, {});
	}

	void TerrainSpline::UpgradeLegacyChunk(LevelDocument& document)
	{
		if (document.FindChunk(LegacyChunk) == nullptr) {
			return;
		}
		SmallVector<std::uint8_t, 0> data;
		GetChunkData(document, data);
		SetChunkData(document, data);
	}

	void TerrainSpline::RemapTiles(LevelDocument& document, Function<bool(TileRef&)>& remap)
	{
		SmallVector<TerrainSpline, 0> splines;
		Read(document, splines);
		if (splines.empty()) {
			return;
		}
		bool changed = false;
		for (TerrainSpline& spline : splines) {
			for (OwnedTile& owned : spline.Owned) {
				for (TileRef* tile : { &owned.Written, &owned.Previous }) {
					TileRef before = *tile;
					if (!remap(*tile)) {
						*tile = TileRef{};
					}
					changed |= (*tile != before);
				}
			}
		}
		if (changed) {
			SmallVector<std::uint8_t, 0> data;
			Write(splines, data);
			SetChunkData(document, data);
		}
	}

	void TerrainSpline::RemapLayers(LevelDocument& document, ArrayView<const std::int32_t> newIndices)
	{
		SmallVector<TerrainSpline, 0> splines;
		Read(document, splines);
		if (splines.empty()) {
			return;
		}
		SmallVector<TerrainSpline, 0> kept;
		for (TerrainSpline& spline : splines) {
			if (spline.Layer >= 0 && spline.Layer < std::int32_t(newIndices.size()) && newIndices[spline.Layer] >= 0) {
				spline.Layer = newIndices[spline.Layer];
				kept.push_back(Death::move(spline));
			}
		}
		SmallVector<std::uint8_t, 0> data;
		Write(kept, data);
		SetChunkData(document, data);
	}

	void TerrainSpline::ResizeLayer(LevelDocument& document, std::int32_t layer, Vector2i size, Vector2i offset)
	{
		SmallVector<TerrainSpline, 0> splines;
		Read(document, splines);
		bool changed = false;
		for (TerrainSpline& spline : splines) {
			if (spline.Layer != layer) {
				continue;
			}
			changed = true;
			for (Vector2f& point : spline.Points) {
				point += Vector2f(offset.X * 32.0f, offset.Y * 32.0f);
			}
			SmallVector<OwnedTile, 0> owned;
			for (OwnedTile& tile : spline.Owned) {
				tile.X += offset.X;
				tile.Y += offset.Y;
				if (tile.X >= 0 && tile.Y >= 0 && tile.X < size.X && tile.Y < size.Y) {
					owned.push_back(tile);
				}
			}
			spline.Owned = Death::move(owned);
		}
		if (changed) {
			SmallVector<std::uint8_t, 0> data;
			Write(splines, data);
			SetChunkData(document, data);
		}
	}

	SetTerrainSplinesCommand::SetTerrainSplinesCommand(const LevelDocument& document, const SmallVectorImpl<TerrainSpline>& splines,
		std::unique_ptr<SetTilesCommand> tiles, String name)
		: _name(Death::move(name)), _tiles(Death::move(tiles))
	{
		TerrainSpline::GetChunkData(document, _oldData);
		TerrainSpline::Write(splines, _newData);
		if (_tiles != nullptr) {
			_tiles->Seal();
		}
	}

	void SetTerrainSplinesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		if (_tiles != nullptr) {
			_tiles->Do(document, changes);
		}
		TerrainSpline::SetChunkData(document, _newData);
		changes.Add(DocumentChangeKind::Chunks);
	}

	void SetTerrainSplinesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_tiles != nullptr) {
			_tiles->Undo(document, changes);
		}
		TerrainSpline::SetChunkData(document, _oldData);
		changes.Add(DocumentChangeKind::Chunks);
	}

	String SetTerrainSplinesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetTerrainSplinesCommand::GetMemoryUsage() const
	{
		return sizeof(SetTerrainSplinesCommand) + _name.size() + _oldData.size() + _newData.size() + (_tiles != nullptr ? _tiles->GetMemoryUsage() : 0);
	}
}

#endif
