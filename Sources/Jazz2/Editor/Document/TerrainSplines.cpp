#include "TerrainSplines.h"

#if defined(WITH_EDITOR)

#include "DocumentChanges.h"
#include "LevelDocument.h"

#include <algorithm>
#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		// varuint count, then for each one: i32 layer, u8 depth, u8 kind, u8 flags (1 smooth), varuint count of points and
		// f32 x, y of each, varuint count of tiles and i32 x, y and the tile written and the one before (u16 index, u8 source,
		// u8 flags) of each
		constexpr std::uint8_t FlagSmooth = 0x01;
		void WriteVarUint(SmallVectorImpl<std::uint8_t>& data, std::uint32_t value)
		{
			do {
				std::uint8_t byte = std::uint8_t(value & 0x7F);
				value >>= 7;
				data.push_back(value != 0 ? std::uint8_t(byte | 0x80) : byte);
			} while (value != 0);
		}

		bool ReadVarUint(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& value)
		{
			value = 0;
			for (std::int32_t shift = 0; shift < 35 && p < end; shift += 7) {
				std::uint8_t byte = *p++;
				value |= std::uint32_t(byte & 0x7F) << shift;
				if ((byte & 0x80) == 0) {
					return true;
				}
			}
			return false;
		}

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
	}

	void TerrainSpline::Read(const LevelDocument& document, SmallVectorImpl<TerrainSpline>& splines)
	{
		splines.clear();
		const Formats::ChunkRecord* chunk = document.FindChunk(Chunk);
		if (chunk == nullptr) {
			return;
		}
		const std::uint8_t* p = chunk->Data.data();
		const std::uint8_t* end = p + chunk->Data.size();
		std::uint32_t count;
		if (!ReadVarUint(p, end, count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			TerrainSpline spline;
			std::uint32_t layer, pointCount, ownedCount;
			if (!Read32(p, end, layer) || p >= end) {
				return;
			}
			spline.Layer = std::int32_t(layer);
			spline.Depth = *p++;
			if (end - p < 2) {
				return;
			}
			spline.Kind = SplineKind(std::min<std::uint8_t>(*p++, std::uint8_t(SplineKind::Closed)));
			spline.Smooth = ((*p++ & FlagSmooth) != 0);
			if (!ReadVarUint(p, end, pointCount) || std::size_t(end - p) < std::size_t(pointCount) * 8) {
				return;
			}
			for (std::uint32_t j = 0; j < pointCount; j++) {
				Vector2f point;
				ReadFloat(p, end, point.X);
				ReadFloat(p, end, point.Y);
				spline.Points.push_back(point);
			}
			if (!ReadVarUint(p, end, ownedCount) || std::size_t(end - p) < std::size_t(ownedCount) * 16) {
				return;
			}
			for (std::uint32_t j = 0; j < ownedCount; j++) {
				OwnedTile owned;
				std::uint32_t x, y;
				Read32(p, end, x);
				Read32(p, end, y);
				owned.X = std::int32_t(x);
				owned.Y = std::int32_t(y);
				ReadTile(p, end, owned.Written);
				ReadTile(p, end, owned.Previous);
				spline.Owned.push_back(owned);
			}
			splines.push_back(std::move(spline));
		}
	}

	void TerrainSpline::Write(const SmallVectorImpl<TerrainSpline>& splines, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (splines.empty()) {
			// No lines, no chunk
			return;
		}
		WriteVarUint(data, std::uint32_t(splines.size()));
		for (const TerrainSpline& spline : splines) {
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
			for (const OwnedTile& owned : spline.Owned) {
				Write32(data, std::uint32_t(owned.X));
				Write32(data, std::uint32_t(owned.Y));
				WriteTile(data, owned.Written);
				WriteTile(data, owned.Previous);
			}
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
				kept.push_back(std::move(spline));
			}
		}
		SmallVector<std::uint8_t, 0> data;
		Write(kept, data);
		document.SetChunk(Chunk, data);
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
			spline.Owned = std::move(owned);
		}
		if (changed) {
			SmallVector<std::uint8_t, 0> data;
			Write(splines, data);
			document.SetChunk(Chunk, data);
		}
	}

	SetTerrainSplinesCommand::SetTerrainSplinesCommand(const LevelDocument& document, const SmallVectorImpl<TerrainSpline>& splines,
		std::unique_ptr<SetTilesCommand> tiles, String name)
		: _name(std::move(name)), _tiles(std::move(tiles))
	{
		if (const Formats::ChunkRecord* chunk = document.FindChunk(TerrainSpline::Chunk)) {
			_oldData = chunk->Data;
		}
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
		document.SetChunk(TerrainSpline::Chunk, _newData);
		changes.Add(DocumentChangeKind::Chunks);
	}

	void SetTerrainSplinesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_tiles != nullptr) {
			_tiles->Undo(document, changes);
		}
		document.SetChunk(TerrainSpline::Chunk, _oldData);
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
