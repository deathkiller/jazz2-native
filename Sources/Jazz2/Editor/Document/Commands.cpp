#include "Commands.h"

#if defined(WITH_EDITOR)

#include "TerrainSplines.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <type_traits>

namespace Jazz2::Editor
{
	namespace
	{
		Recti MakeArea(std::int32_t minX, std::int32_t minY, std::int32_t maxX, std::int32_t maxY)
		{
			if (maxX < minX || maxY < minY) {
				return {};
			}
			return Recti(minX, minY, maxX - minX + 1, maxY - minY + 1);
		}

		// Merges changes of the same cell, the first one has the original state and the last one the final state
		template<class TChange, class TEqual>
		void MergeChanges(SmallVectorImpl<TChange>& changes, TEqual&& isEqual)
		{
			std::stable_sort(changes.begin(), changes.end(), [](const TChange& a, const TChange& b) {
				return a.Cell < b.Cell;
			});

			std::size_t target = 0;
			for (std::size_t i = 0; i < changes.size(); ) {
				std::size_t last = i;
				while (last + 1 < changes.size() && changes[last + 1].Cell == changes[i].Cell) {
					last++;
				}
				TChange merged = changes[i];
				merged.New = changes[last].New;
				if (!isEqual(merged.Old, merged.New)) {
					changes[target++] = merged;
				}
				i = last + 1;
			}
			changes.truncate(target);
		}
	}

	SetTilesCommand::SetTilesCommand(const LevelDocument& document, std::int32_t layer, String name)
		: _document(document), _layer(layer), _width(0), _name(Death::move(name)),
			_minX(INT32_MAX), _minY(INT32_MAX), _maxX(INT32_MIN), _maxY(INT32_MIN), _sealed(false)
	{
		if (layer >= 0 && layer < std::int32_t(document.Layers.size())) {
			_width = document.Layers[layer].GetWidth();
		}
	}

	void SetTilesCommand::Set(std::int32_t x, std::int32_t y, const TileRef& tile)
	{
		DEATH_DEBUG_ASSERT(!_sealed, "The command was already sealed", );

		if (_layer < 0 || _layer >= std::int32_t(_document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = _document.Layers[_layer];
		if (!layer.Contains(x, y)) {
			return;
		}

		Change& change = _changes.emplace_back();
		change.Cell = std::uint32_t(y) * std::uint32_t(_width) + std::uint32_t(x);
		change.Old = layer.GetTile(x, y);
		change.New = tile;

		_minX = std::min(_minX, x);
		_minY = std::min(_minY, y);
		_maxX = std::max(_maxX, x);
		_maxY = std::max(_maxY, y);
	}

	void SetTilesCommand::Seal()
	{
		if (_sealed) {
			return;
		}
		_sealed = true;

		MergeChanges(_changes, [](const TileRef& a, const TileRef& b) { return a == b; });

		_minX = INT32_MAX; _minY = INT32_MAX; _maxX = INT32_MIN; _maxY = INT32_MIN;
		for (const Change& change : _changes) {
			std::int32_t x = std::int32_t(change.Cell % std::uint32_t(_width));
			std::int32_t y = std::int32_t(change.Cell / std::uint32_t(_width));
			_minX = std::min(_minX, x);
			_minY = std::min(_minY, y);
			_maxX = std::max(_maxX, x);
			_maxY = std::max(_maxY, y);
		}

		if (_changes.size() > CompressThreshold) {
			_compressed.Compress(ArrayView<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(_changes.data()),
				_changes.size() * sizeof(Change)));
			_changes = SmallVector<Change, 0>();
		} else if (_changes.capacity() > _changes.size() + _changes.size() / 4 + 16) {
			// A stroke that went over the same tiles many times leaves far fewer changes than it collected
			SmallVector<Change, 0> exact;
			exact.reserve(_changes.size());
			exact.append(_changes.begin(), _changes.end());
			_changes = Death::move(exact);
		}
	}

	bool SetTilesCommand::IsEmpty() const
	{
		return (_changes.empty() && _compressed.IsEmpty());
	}

	Recti SetTilesCommand::GetArea() const
	{
		return MakeArea(_minX, _minY, _maxX, _maxY);
	}

	void SetTilesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Seal();
		Apply(document, changes, false);
	}

	void SetTilesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, true);
	}

	String SetTilesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetTilesCommand::GetMemoryUsage() const
	{
		return sizeof(SetTilesCommand) + _changes.capacity() * sizeof(Change) + _compressed.GetMemoryUsage() + _name.size();
	}

	void SetTilesCommand::Apply(LevelDocument& document, DocumentChanges& changes, bool undo)
	{
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			LOGE("Tiles of layer {} cannot be changed, the level has only {} layers", _layer, document.Layers.size());
			return;
		}

		SmallVector<std::uint8_t, 0> decompressed;
		ArrayView<const Change> list = _changes;
		if (!_compressed.IsEmpty()) {
			// Checked for real, the buffer would be garbage otherwise
			if (!_compressed.Decompress(decompressed)) {
				LOGE("Changes of tiles cannot be decompressed");
				return;
			}
			list = ArrayView<const Change>(reinterpret_cast<const Change*>(decompressed.data()), decompressed.size() / sizeof(Change));
		}

		DocumentLayer& layer = document.Layers[_layer];
		const std::size_t tileCount = layer.Tiles.size();
		for (const Change& change : list) {
			if (change.Cell < tileCount) {
				layer.Tiles[change.Cell] = (undo ? change.Old : change.New);
			}
		}

		changes.AddTiles(_layer, GetArea());
	}

	SetEventsCommand::SetEventsCommand(const LevelDocument& document, String name)
		: _document(document), _name(Death::move(name)),
			_minX(INT32_MAX), _minY(INT32_MAX), _maxX(INT32_MIN), _maxY(INT32_MIN), _sealed(false)
	{
		Vector2i gridSize = document.GetEventGridSize();
		_width = gridSize.X;
		_height = gridSize.Y;
	}

	void SetEventsCommand::Set(std::int32_t x, std::int32_t y, const Formats::EventRecord& event)
	{
		DEATH_DEBUG_ASSERT(!_sealed, "The command was already sealed", );

		if (x < 0 || y < 0 || x >= _width || y >= _height) {
			return;
		}

		Change& change = _changes.emplace_back();
		change.Cell = std::uint32_t(y) * std::uint32_t(_width) + std::uint32_t(x);
		change.Old = _document.GetEvent(x, y);
		change.New = event;
		LevelDocument::NormalizeEvent(change.New);

		_minX = std::min(_minX, x);
		_minY = std::min(_minY, y);
		_maxX = std::max(_maxX, x);
		_maxY = std::max(_maxY, y);
	}

	void SetEventsCommand::Seal()
	{
		if (_sealed) {
			return;
		}
		_sealed = true;

		MergeChanges(_changes, [](const Formats::EventRecord& a, const Formats::EventRecord& b) {
			return LevelDocument::AreEventsEqual(a, b);
		});
	}

	bool SetEventsCommand::IsEmpty() const
	{
		return _changes.empty();
	}

	Recti SetEventsCommand::GetArea() const
	{
		return MakeArea(_minX, _minY, _maxX, _maxY);
	}

	void SetEventsCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Seal();
		Apply(document, false);
		changes.AddEvents(GetArea());
	}

	void SetEventsCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, true);
		changes.AddEvents(GetArea());
	}

	void SetEventsCommand::Apply(LevelDocument& document, bool undo)
	{
		if (_width <= 0) {
			return;
		}
		// Each event inserted into or removed from the sorted list moves all after it, so many of them are merged
		// into the list in a single pass instead
		constexpr std::size_t MergeThreshold = 16;
		if (_changes.size() < MergeThreshold) {
			for (const Change& change : _changes) {
				document.SetEvent(std::int32_t(change.Cell % std::uint32_t(_width)), std::int32_t(change.Cell / std::uint32_t(_width)), undo ? change.Old : change.New);
			}
			return;
		}

		SmallVector<DocumentEvent, 0> events;
		events.reserve(_changes.size());
		for (const Change& change : _changes) {
			DocumentEvent& event = events.emplace_back();
			event.Cell = change.Cell;
			event.Record = (undo ? change.Old : change.New);
		}
		document.SetEvents(events);
	}

	bool SetEventsCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetEventsCommand>(&next);
		if (other == nullptr || other->_name != _name || other->_width != _width) {
			return false;
		}
		Seal();
		other->Seal();
		if (other->_changes.size() != _changes.size()) {
			return false;
		}
		// Both are sorted by the tile after sealing
		for (std::size_t i = 0; i < _changes.size(); i++) {
			if (_changes[i].Cell != other->_changes[i].Cell) {
				return false;
			}
		}
		return true;
	}

	bool SetEventsCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		auto* other = runtime_cast<SetEventsCommand>(&next);
		for (std::size_t i = 0; i < _changes.size(); i++) {
			_changes[i].New = other->_changes[i].New;
		}
		return true;
	}

	String SetEventsCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetEventsCommand::GetMemoryUsage() const
	{
		return sizeof(SetEventsCommand) + _changes.capacity() * sizeof(Change) + _name.size();
	}

	SnapshotCommand::SnapshotCommand(String name, DocumentChangeKind kinds)
		: _name(Death::move(name)), _kinds(kinds)
	{
	}

	std::unique_ptr<SnapshotCommand> SnapshotCommand::Begin(const LevelDocument& document, SnapshotParts parts, String name, DocumentChangeKind kinds, std::int32_t layer)
	{
		std::unique_ptr<SnapshotCommand> command(new SnapshotCommand(Death::move(name), kinds != DocumentChangeKind::None ? kinds : DocumentSnapshot::GetChangeKinds(parts)));
		command->_before = DocumentSnapshot::Capture(document, parts, layer);
		return command;
	}

	void SnapshotCommand::End(const LevelDocument& document, DocumentChanges& changes)
	{
		// The tile sets the level stores in itself are mostly the same before and after, they're kept once then
		_after = DocumentSnapshot::Capture(document, _before.GetParts(), _before.GetLayer(), &_before);
		changes.Add(_kinds);
	}

	void SnapshotCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		// A damaged snapshot leaves the document as it is, there's nothing to show then
		if (_after.Restore(document)) {
			changes.Add(_kinds);
		}
	}

	void SnapshotCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_before.Restore(document)) {
			changes.Add(_kinds);
		}
	}

	String SnapshotCommand::GetName() const
	{
		return _name;
	}

	std::size_t SnapshotCommand::GetMemoryUsage() const
	{
		return sizeof(SnapshotCommand) + _before.GetMemoryUsage() + _after.GetMemoryUsage() + _name.size();
	}

	SetChunkCommand::SetChunkCommand(const LevelDocument& document, std::uint32_t tag, ArrayView<const std::uint8_t> data, String name)
		: _name(Death::move(name)), _tag(tag)
	{
		if (const Formats::ChunkRecord* chunk = document.FindChunk(tag)) {
			_oldData = chunk->Data;
		}
		_newData.append(data.begin(), data.end());
	}

	void SetChunkCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		document.SetChunk(_tag, _newData);
		changes.Add(DocumentChangeKind::Chunks);
	}

	void SetChunkCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		document.SetChunk(_tag, _oldData);
		changes.Add(DocumentChangeKind::Chunks);
	}

	bool SetChunkCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetChunkCommand>(&next);
		return (other != nullptr && other->_tag == _tag && other->_name == _name);
	}

	bool SetChunkCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		_newData = Death::move(runtime_cast<SetChunkCommand>(&next)->_newData);
		return true;
	}

	String SetChunkCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetChunkCommand::GetMemoryUsage() const
	{
		return sizeof(SetChunkCommand) + _name.size() + _oldData.capacity() + _newData.capacity();
	}

	MoveLayerCommand::MoveLayerCommand(std::int32_t from, std::int32_t to, String name)
		: _name(Death::move(name)), _from(from), _to(to)
	{
	}

	void MoveLayerCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Move(document, _from, _to, changes);
	}

	void MoveLayerCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Move(document, _to, _from, changes);
	}

	void MoveLayerCommand::Move(LevelDocument& document, std::int32_t from, std::int32_t to, DocumentChanges& changes)
	{
		std::int32_t count = std::int32_t(document.Layers.size());
		if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
			return;
		}

		DocumentLayer layer = Death::move(document.Layers[from]);
		document.Layers.erase(document.Layers.begin() + from);
		document.Layers.insert(document.Layers.begin() + to, Death::move(layer));

		// Ground lines stay on their layers, a move is undone by the opposite move, so nothing of them is lost
		SmallVector<std::int32_t, 16> order;
		for (std::int32_t i = 0; i < count; i++) {
			order.push_back(i);
		}
		std::int32_t moved = order[from];
		order.erase(order.begin() + from);
		order.insert(order.begin() + to, moved);
		SmallVector<std::int32_t, 16> newIndices;
		newIndices.resize(std::size_t(count));
		for (std::int32_t i = 0; i < count; i++) {
			newIndices[std::size_t(order[i])] = i;
		}
		TerrainSpline::RemapLayers(document, newIndices);

		// Told as a move, so the shown level can move its layers too instead of being loaded again
		changes.Add(DocumentSnapshot::GetChangeKinds(SnapshotParts::Layers | SnapshotParts::Chunks) & ~DocumentChangeKind::LayerStructure);
		changes.AddLayerMove(from, to, count);
	}

	String MoveLayerCommand::GetName() const
	{
		return _name;
	}

	std::size_t MoveLayerCommand::GetMemoryUsage() const
	{
		return sizeof(MoveLayerCommand) + _name.size();
	}

	InsertLayerCommand::InsertLayerCommand(String name, std::int32_t index, bool insert)
		: _name(Death::move(name)), _index(index), _insert(insert), _hasNewSplines(false)
	{
	}

	std::unique_ptr<InsertLayerCommand> InsertLayerCommand::Add(const LevelDocument& document, std::int32_t index, const DocumentLayer& layer, String name)
	{
		std::unique_ptr<InsertLayerCommand> command(new InsertLayerCommand(Death::move(name), index, true));
		DocumentSnapshot::CompressLayer(layer, command->_layer);
		TerrainSpline::GetChunkData(document, command->_oldSplines);
		return command;
	}

	std::unique_ptr<InsertLayerCommand> InsertLayerCommand::Remove(const LevelDocument& document, std::int32_t index, String name)
	{
		std::unique_ptr<InsertLayerCommand> command(new InsertLayerCommand(Death::move(name), index, false));
		// The layer is taken when it's removed
		TerrainSpline::GetChunkData(document, command->_oldSplines);
		return command;
	}

	void InsertLayerCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		// The ground lines stay as they are if no layer came or went
		if (!(_insert ? Insert(document, changes) : Erase(document, changes))) {
			return;
		}
		if (_hasNewSplines) {
			TerrainSpline::SetChunkData(document, _newSplines);
		} else {
			// The ground lines of the layers after it follow them, those of a removed layer are gone with it
			std::int32_t oldCount = std::int32_t(document.Layers.size()) + (_insert ? -1 : 1);
			SmallVector<std::int32_t, 16> newIndices;
			for (std::int32_t i = 0; i < oldCount; i++) {
				if (_insert) {
					newIndices.push_back(i < _index ? i : i + 1);
				} else {
					newIndices.push_back(i < _index ? i : (i == _index ? -1 : i - 1));
				}
			}
			TerrainSpline::RemapLayers(document, newIndices);
			TerrainSpline::GetChunkData(document, _newSplines);
			_hasNewSplines = true;
		}
	}

	void InsertLayerCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_insert ? Erase(document, changes) : Insert(document, changes)) {
			TerrainSpline::SetChunkData(document, _oldSplines);
		}
	}

	bool InsertLayerCommand::Insert(LevelDocument& document, DocumentChanges& changes)
	{
		DocumentLayer layer;
		if (_index < 0 || _index > std::int32_t(document.Layers.size()) || !DocumentSnapshot::DecompressLayer(_layer, layer)) {
			LOGE("Layer cannot be put back at {}, the level stays as it is", _index);
			return false;
		}
		document.Layers.insert(document.Layers.begin() + _index, Death::move(layer));
		// Kept only while the layer isn't in the level
		_layer = CompressedBuffer();
		changes.Add(DocumentSnapshot::GetChangeKinds(SnapshotParts::Layers | SnapshotParts::Chunks));
		return true;
	}

	bool InsertLayerCommand::Erase(LevelDocument& document, DocumentChanges& changes)
	{
		if (_index < 0 || _index >= std::int32_t(document.Layers.size())) {
			LOGE("Layer {} cannot be removed, the level has only {} layers", _index, document.Layers.size());
			return false;
		}
		// As it is now, with its editor state too
		DocumentSnapshot::CompressLayer(document.Layers[_index], _layer);
		document.Layers.erase(document.Layers.begin() + _index);
		changes.Add(DocumentSnapshot::GetChangeKinds(SnapshotParts::Layers | SnapshotParts::Chunks));
		return true;
	}

	String InsertLayerCommand::GetName() const
	{
		return _name;
	}

	std::size_t InsertLayerCommand::GetMemoryUsage() const
	{
		return sizeof(InsertLayerCommand) + _name.size() + _layer.GetMemoryUsage() + _oldSplines.capacity() + _newSplines.capacity();
	}

	SetLayerPropertiesCommand::SetLayerPropertiesCommand(const LevelDocument& document, std::int32_t layer,
		const Formats::LayerRecord& record, StringView layerName, String name)
		: _name(Death::move(name)), _layer(layer), _newRecord(record), _newName(layerName), _changedFields(0)
	{
		if (layer < 0 || layer >= std::int32_t(document.Layers.size())) {
			// Nothing to change, the record stays as it was given
			_oldRecord = record;
			_oldName = layerName;
			return;
		}
		const DocumentLayer& current = document.Layers[layer];
		_oldRecord = current.Record;
		_oldName = current.Name;

		// The size is changed only with the tiles, by another command
		_newRecord.Width = _oldRecord.Width;
		_newRecord.Height = _oldRecord.Height;
		_changedFields = GetChangedFields(_oldRecord, _newRecord, _oldName, _newName);
	}

	std::uint32_t SetLayerPropertiesCommand::GetChangedFields(const Formats::LayerRecord& a, const Formats::LayerRecord& b, StringView nameA, StringView nameB)
	{
		std::uint32_t fields = 0;
		// Each flag on its own, turning on two different ones are two changes
		fields |= std::uint32_t(a.Flags ^ b.Flags) & 0xFFFFu;
		if (a.Kind != b.Kind) fields |= (1u << 16);
		if (a.SpeedModels != b.SpeedModels) fields |= (1u << 17);
		if (a.OffsetX != b.OffsetX) fields |= (1u << 18);
		if (a.OffsetY != b.OffsetY) fields |= (1u << 19);
		if (a.SpeedX != b.SpeedX) fields |= (1u << 20);
		if (a.SpeedY != b.SpeedY) fields |= (1u << 21);
		if (a.AutoSpeedX != b.AutoSpeedX) fields |= (1u << 22);
		if (a.AutoSpeedY != b.AutoSpeedY) fields |= (1u << 23);
		if (a.Depth != b.Depth) fields |= (1u << 24);
		if (a.RendererType != b.RendererType) fields |= (1u << 25);
		// The color as a whole, a color picker changes different bytes of it as it's dragged
		if (std::memcmp(a.Color, b.Color, sizeof(a.Color)) != 0) fields |= (1u << 26);
		if (nameA != nameB) fields |= (1u << 27);
		if (a.Textured.FadeX != b.Textured.FadeX) fields |= (1u << 28);
		if (a.Textured.FadeY != b.Textured.FadeY) fields |= (1u << 29);
		if (a.Textured.Fade != b.Textured.Fade || a.Textured.Flags != b.Textured.Flags) fields |= (1u << 30);
		if (a.Textured.InnerSpeedX != b.Textured.InnerSpeedX || a.Textured.InnerSpeedY != b.Textured.InnerSpeedY ||
			a.Textured.InnerAutoSpeedX != b.Textured.InnerAutoSpeedX || a.Textured.InnerAutoSpeedY != b.Textured.InnerAutoSpeedY) fields |= (1u << 31);
		return fields;
	}

	void SetLayerPropertiesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		DocumentLayer& layer = document.Layers[_layer];
		SetRecord(layer, _newRecord);
		layer.Name = _newName;
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	void SetLayerPropertiesCommand::SetRecord(DocumentLayer& layer, const Formats::LayerRecord& record)
	{
		// The size goes with the tiles, the record of a command read back from a recovery copy could say any other
		const std::int32_t width = layer.Record.Width, height = layer.Record.Height;
		layer.Record = record;
		layer.Record.Width = width;
		layer.Record.Height = height;
	}

	void SetLayerPropertiesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		DocumentLayer& layer = document.Layers[_layer];
		SetRecord(layer, _oldRecord);
		layer.Name = _oldName;
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	bool SetLayerPropertiesCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLayerPropertiesCommand>(&next);
		// Only a change of the same property continues this one, as while its value is dragged, a change of another
		// one is a step of its own; but a layer moved by its frame changes either offset or both as it goes
		return (other != nullptr && other->_layer == _layer && other->_name == _name &&
			(other->_changedFields == 0 || other->_changedFields == _changedFields || (_mergesAnyField && other->_mergesAnyField)));
	}

	bool SetLayerPropertiesCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		auto* other = runtime_cast<SetLayerPropertiesCommand>(&next);
		_newRecord = other->_newRecord;
		_newName = Death::move(other->_newName);
		if (_mergesAnyField) {
			// All the step changes now, against where it started; a value dragged keeps the field it changes, also when it's
			// dragged back to where it was
			_changedFields = GetChangedFields(_oldRecord, _newRecord, _oldName, _newName);
		}
		return true;
	}

	String SetLayerPropertiesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLayerPropertiesCommand::GetMemoryUsage() const
	{
		return sizeof(SetLayerPropertiesCommand) + _oldName.size() + _newName.size() + _name.size();
	}

	SetLayerTextureCommand::SetLayerTextureCommand(const LevelDocument& document, std::int32_t layer, SmallVector<std::uint8_t, 0> texture, String name)
		: _name(Death::move(name)), _layer(layer), _new(Death::move(texture))
	{
		if (layer >= 0 && layer < std::int32_t(document.Layers.size())) {
			const SmallVector<std::uint8_t, 0>& current = document.Layers[layer].Texture;
			_old.assign(current.begin(), current.end());
		}
	}

	void SetLayerTextureCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		document.Layers[_layer].Texture.assign(_new.begin(), _new.end());
		// The texture goes with the description of the layer to the shown level, see RuntimeSync::Apply()
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	void SetLayerTextureCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		document.Layers[_layer].Texture.assign(_old.begin(), _old.end());
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	String SetLayerTextureCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLayerTextureCommand::GetMemoryUsage() const
	{
		return sizeof(SetLayerTextureCommand) + _name.size() + _old.capacity() + _new.capacity();
	}

	namespace
	{
		// The same bytes, so the list ends up exactly as it was given
		bool AreOffGridRecordsSame(const Formats::OffGridEventRecord& a, const Formats::OffGridEventRecord& b)
		{
			return (std::memcmp(&a, &b, sizeof(Formats::OffGridEventRecord)) == 0);
		}
	}

	SetOffGridEventsCommand::SetOffGridEventsCommand(const LevelDocument& document, SmallVector<Formats::OffGridEventRecord, 0> events, String name)
		: _name(Death::move(name)), _oldCount(std::uint32_t(document.OffGridEvents.size())), _newCount(std::uint32_t(events.size()))
	{
		const auto& before = document.OffGridEvents;
		if (before.size() == events.size()) {
			// Runs of the events that changed, the others stay where they are
			std::size_t i = 0;
			while (i < events.size()) {
				if (AreOffGridRecordsSame(before[i], events[i])) {
					i++;
					continue;
				}
				std::size_t start = i;
				while (i < events.size() && !AreOffGridRecordsSame(before[i], events[i])) {
					_oldRecords.push_back(before[i]);
					_newRecords.push_back(events[i]);
					i++;
				}
				_hunks.push_back({ std::uint32_t(start), std::uint32_t(i - start), std::uint32_t(i - start) });
			}
		} else {
			// Added or removed, a single run between what stays the same at the start and at the end
			std::size_t common = std::min(before.size(), events.size());
			std::size_t prefix = 0;
			while (prefix < common && AreOffGridRecordsSame(before[prefix], events[prefix])) {
				prefix++;
			}
			std::size_t suffix = 0;
			while (suffix < common - prefix && AreOffGridRecordsSame(before[before.size() - 1 - suffix], events[events.size() - 1 - suffix])) {
				suffix++;
			}
			std::size_t oldCount = before.size() - prefix - suffix, newCount = events.size() - prefix - suffix;
			_oldRecords.append(before.begin() + prefix, before.begin() + prefix + oldCount);
			_newRecords.append(events.begin() + prefix, events.begin() + prefix + newCount);
			_hunks.push_back({ std::uint32_t(prefix), std::uint32_t(oldCount), std::uint32_t(newCount) });
		}
		FindChangedEvents();
	}

	void SetOffGridEventsCommand::FindChangedEvents()
	{
		_changedEvents.clear();
		_structural = (_oldCount != _newCount);
		for (const Hunk& hunk : _hunks) {
			_structural |= (hunk.OldCount != hunk.NewCount);
		}
		if (_structural) {
			return;
		}
		std::size_t offset = 0;
		for (const Hunk& hunk : _hunks) {
			for (std::uint32_t i = 0; i < hunk.OldCount; i++) {
				const Formats::OffGridEventRecord& a = _oldRecords[offset + i];
				const Formats::OffGridEventRecord& b = _newRecords[offset + i];
				if (a.X != b.X || a.Y != b.Y || !LevelDocument::AreEventsEqual(a.Event, b.Event)) {
					_changedEvents.push_back(hunk.Index + i);
				}
			}
			offset += hunk.OldCount;
		}
	}

	bool SetOffGridEventsCommand::AreHunksValid() const
	{
		std::uint64_t oldTotal = 0, newTotal = 0, end = 0;
		for (const Hunk& hunk : _hunks) {
			if (hunk.Index < end || std::uint64_t(hunk.Index) + hunk.OldCount > _oldCount) {
				return false;
			}
			end = std::uint64_t(hunk.Index) + hunk.OldCount;
			oldTotal += hunk.OldCount;
			newTotal += hunk.NewCount;
		}
		return (oldTotal == _oldRecords.size() && newTotal == _newRecords.size() &&
			std::uint64_t(_oldCount) - oldTotal + newTotal == _newCount);
	}

	void SetOffGridEventsCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, false);
	}

	void SetOffGridEventsCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, true);
	}

	void SetOffGridEventsCommand::Apply(LevelDocument& document, DocumentChanges& changes, bool undo)
	{
		auto& events = document.OffGridEvents;
		if (events.size() != std::size_t(undo ? _newCount : _oldCount)) {
			LOGE("Events off the grid cannot be changed, the level has {} of them instead of {}", events.size(), undo ? _newCount : _oldCount);
			return;
		}

		// From the last run back, so the indices of the runs before stay as they are; a run of the list after the change
		// starts where it did before, moved by what the runs before it added or removed
		std::int64_t shift = 0;
		for (const Hunk& hunk : _hunks) {
			shift += std::int64_t(hunk.NewCount) - std::int64_t(hunk.OldCount);
		}
		std::size_t oldOffset = _oldRecords.size(), newOffset = _newRecords.size();
		std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
		auto include = [&](const Formats::OffGridEventRecord& event) {
			// The tile the position is in, as the events off the grid are drawn
			std::int32_t x = std::int32_t(std::min<std::uint32_t>(event.X / 32, INT32_MAX));
			std::int32_t y = std::int32_t(std::min<std::uint32_t>(event.Y / 32, INT32_MAX));
			minX = std::min(minX, x);
			minY = std::min(minY, y);
			maxX = std::max(maxX, x);
			maxY = std::max(maxY, y);
		};
		for (std::size_t h = _hunks.size(); h > 0; h--) {
			const Hunk& hunk = _hunks[h - 1];
			shift -= std::int64_t(hunk.NewCount) - std::int64_t(hunk.OldCount);
			oldOffset -= hunk.OldCount;
			newOffset -= hunk.NewCount;
			std::size_t index = std::size_t(undo ? std::int64_t(hunk.Index) + shift : std::int64_t(hunk.Index));
			std::uint32_t removeCount = (undo ? hunk.NewCount : hunk.OldCount);
			std::uint32_t insertCount = (undo ? hunk.OldCount : hunk.NewCount);
			const Formats::OffGridEventRecord* inserted = (undo ? _oldRecords.data() + oldOffset : _newRecords.data() + newOffset);
			for (std::uint32_t i = 0; i < hunk.OldCount; i++) {
				include(_oldRecords[oldOffset + i]);
			}
			for (std::uint32_t i = 0; i < hunk.NewCount; i++) {
				include(_newRecords[newOffset + i]);
			}

			std::uint32_t common = std::min(removeCount, insertCount);
			for (std::uint32_t i = 0; i < common; i++) {
				events[index + i] = inserted[i];
			}
			if (removeCount > common) {
				events.erase(events.begin() + index + common, events.begin() + index + removeCount);
			} else if (insertCount > common) {
				events.insert(events.begin() + index + common, inserted + common, inserted + insertCount);
			}
		}

		if (maxX >= minX) {
			changes.AddEvents(MakeArea(minX, minY, maxX, maxY));
		} else {
			// Nothing changed, but it's told as a change of the events still
			changes.Add(DocumentChangeKind::Events);
		}
	}

	bool SetOffGridEventsCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetOffGridEventsCommand>(&next);
		// The same property of the same events, as while it's dragged over all of them, not the same property of others,
		// and never a change that adds or removes events
		if (other == nullptr || other->_name != _name || _structural || other->_structural || _changedEvents.empty() || other->_oldCount != _newCount) {
			return false;
		}
		if (other->_changedEvents.empty()) {
			return true;
		}
		return (other->_changedEvents.size() == _changedEvents.size() &&
			std::memcmp(other->_changedEvents.data(), _changedEvents.data(), _changedEvents.size() * sizeof(std::uint32_t)) == 0);
	}

	bool SetOffGridEventsCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		auto* other = runtime_cast<SetOffGridEventsCommand>(&next);
		// Neither adds nor removes events, so the runs of both are at the same indices; an event in both goes from how
		// it was before this one to how the other one leaves it, an event in one of them only is as that one has it
		struct Entry
		{
			std::uint32_t Index;
			const Formats::OffGridEventRecord* Old;
			const Formats::OffGridEventRecord* New;
		};
		auto expand = [](const SetOffGridEventsCommand& command, SmallVectorImpl<Entry>& entries) {
			std::size_t offset = 0;
			for (const Hunk& hunk : command._hunks) {
				for (std::uint32_t i = 0; i < hunk.OldCount; i++) {
					entries.push_back({ hunk.Index + i, &command._oldRecords[offset + i], &command._newRecords[offset + i] });
				}
				offset += hunk.OldCount;
			}
		};
		SmallVector<Entry, 0> first, second;
		expand(*this, first);
		expand(*other, second);

		SmallVector<Hunk, 0> hunks;
		SmallVector<Formats::OffGridEventRecord, 0> oldRecords, newRecords;
		auto add = [&](std::uint32_t index, const Formats::OffGridEventRecord& before, const Formats::OffGridEventRecord& after) {
			if (!hunks.empty() && hunks.back().Index + hunks.back().OldCount == index) {
				hunks.back().OldCount++;
				hunks.back().NewCount++;
			} else {
				hunks.push_back({ index, 1, 1 });
			}
			oldRecords.push_back(before);
			newRecords.push_back(after);
		};
		std::size_t i = 0, j = 0;
		while (i < first.size() || j < second.size()) {
			if (j >= second.size() || (i < first.size() && first[i].Index < second[j].Index)) {
				add(first[i].Index, *first[i].Old, *first[i].New);
				i++;
			} else if (i >= first.size() || second[j].Index < first[i].Index) {
				add(second[j].Index, *second[j].Old, *second[j].New);
				j++;
			} else {
				add(first[i].Index, *first[i].Old, *second[j].New);
				i++;
				j++;
			}
		}
		_hunks = Death::move(hunks);
		_oldRecords = Death::move(oldRecords);
		_newRecords = Death::move(newRecords);
		_newCount = other->_newCount;
		return true;
	}

	String SetOffGridEventsCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetOffGridEventsCommand::GetMemoryUsage() const
	{
		return sizeof(SetOffGridEventsCommand) + _name.size() + _hunks.capacity() * sizeof(Hunk) +
			(_oldRecords.capacity() + _newRecords.capacity()) * sizeof(Formats::OffGridEventRecord) + _changedEvents.capacity() * sizeof(std::uint32_t);
	}

	SetLevelPropertiesCommand::SetLevelPropertiesCommand(const LevelDocument& document, LevelFlags flags,
		const Formats::LevelProperties& properties, String name)
		: _name(Death::move(name)), _oldFlags(document.Flags), _newFlags(flags), _oldProperties(document.Properties),
			_newProperties(properties)
	{
		_changedFields = GetChangedFields(_oldFlags, _newFlags, _oldProperties, _newProperties);
	}

	std::uint32_t SetLevelPropertiesCommand::GetChangedFields(LevelFlags flagsA, LevelFlags flagsB, const Formats::LevelProperties& a, const Formats::LevelProperties& b)
	{
		// The flags take the low half, each of them on its own
		std::uint32_t fields = std::uint32_t(std::uint16_t(flagsA) ^ std::uint16_t(flagsB));
		if (a.DisplayName != b.DisplayName) fields |= (1u << 16);
		if (a.Author != b.Author) fields |= (1u << 17);
		if (a.NextLevel != b.NextLevel) fields |= (1u << 18);
		if (a.SecretLevel != b.SecretLevel) fields |= (1u << 19);
		if (a.BonusLevel != b.BonusLevel) fields |= (1u << 20);
		if (a.DefaultTileSet != b.DefaultTileSet) fields |= (1u << 21);
		if (a.Music != b.Music) fields |= (1u << 22);
		if (a.AmbientColor != b.AmbientColor) fields |= (1u << 23);
		if (a.Weather != b.Weather) fields |= (1u << 24);
		if (a.WeatherIntensity != b.WeatherIntensity) fields |= (1u << 25);
		if (a.WaterLevel != b.WaterLevel) fields |= (1u << 26);
		if (a.CaptionTileId != b.CaptionTileId) fields |= (1u << 27);
		return fields;
	}

	void SetLevelPropertiesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		document.Flags = _newFlags;
		document.Properties = _newProperties;
		changes.Add(DocumentChangeKind::LevelProperties);
	}

	void SetLevelPropertiesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		document.Flags = _oldFlags;
		document.Properties = _oldProperties;
		changes.Add(DocumentChangeKind::LevelProperties);
	}

	SetAnimationCommand::SetAnimationCommand(const LevelDocument& document, std::int32_t index, DocumentAnimation animation, String name)
		: _name(Death::move(name)), _index(index), _new(Death::move(animation)), _changedFields(0)
	{
		if (index >= 0 && index < std::int32_t(document.Animations.size())) {
			_old = document.Animations[index];
			_changedFields = GetChangedFields(_old, _new);
		}
	}

	std::uint32_t SetAnimationCommand::GetChangedFields(const DocumentAnimation& a, const DocumentAnimation& b)
	{
		std::uint32_t fields = 0;
		if (a.Timing.FrameDuration16 != b.Timing.FrameDuration16) fields |= 0x01;
		if (a.Timing.Delay != b.Timing.Delay) fields |= 0x02;
		if (a.Timing.DelayJitter != b.Timing.DelayJitter) fields |= 0x04;
		if (a.Timing.PingPongDelay != b.Timing.PingPongDelay) fields |= 0x08;
		if (a.Timing.IsPingPong != b.Timing.IsPingPong) fields |= 0x10;
		bool sameFrames = (a.Frames.size() == b.Frames.size());
		for (std::size_t i = 0; sameFrames && i < a.Frames.size(); i++) {
			sameFrames = (a.Frames[i] == b.Frames[i]);
		}
		if (!sameFrames) fields |= 0x20;
		return fields;
	}

	void SetAnimationCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		if (_index >= 0 && _index < std::int32_t(document.Animations.size())) {
			document.Animations[_index] = _new;
			changes.Add(DocumentChangeKind::AnimationTiming);
		}
	}

	void SetAnimationCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_index >= 0 && _index < std::int32_t(document.Animations.size())) {
			document.Animations[_index] = _old;
			changes.Add(DocumentChangeKind::AnimationTiming);
		}
	}

	bool SetAnimationCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetAnimationCommand>(&next);
		return (other != nullptr && other->_index == _index && other->_name == _name &&
			(other->_changedFields == 0 || other->_changedFields == _changedFields));
	}

	bool SetAnimationCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		_new = Death::move(runtime_cast<SetAnimationCommand>(&next)->_new);
		return true;
	}

	String SetAnimationCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetAnimationCommand::GetMemoryUsage() const
	{
		return sizeof(SetAnimationCommand) + (_old.Frames.capacity() + _new.Frames.capacity()) * sizeof(TileRef) + _name.size();
	}

	bool SetLevelPropertiesCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLevelPropertiesCommand>(&next);
		// Only a change of the same property continues this one, as while its value is dragged
		return (other != nullptr && other->_name == _name && (other->_changedFields == 0 || other->_changedFields == _changedFields));
	}

	bool SetLevelPropertiesCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		auto* other = runtime_cast<SetLevelPropertiesCommand>(&next);
		_newFlags = other->_newFlags;
		_newProperties = Death::move(other->_newProperties);
		return true;
	}

	String SetLevelPropertiesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLevelPropertiesCommand::GetMemoryUsage() const
	{
		return sizeof(SetLevelPropertiesCommand) + _name.size();
	}

	SetTileOverrideCommand::SetTileOverrideCommand(const LevelDocument& document, std::uint16_t tileId, const std::uint8_t* indices,
		const std::uint8_t* mask, String name)
		: _name(Death::move(name)), _tileId(tileId)
	{
		if (const Formats::TileDiffuseOverride* diffuse = FindDiffuse(document, tileId)) {
			_old.HasDiffuse = true;
			std::memcpy(_old.Indices, diffuse->Indices, sizeof(_old.Indices));
		}
		if (const Formats::TileMaskOverride* oldMask = FindMask(document, tileId)) {
			_old.HasMask = true;
			std::memcpy(_old.Mask, oldMask->Mask, sizeof(_old.Mask));
		}
		_new.HasDiffuse = (indices != nullptr);
		if (indices != nullptr) {
			std::memcpy(_new.Indices, indices, sizeof(_new.Indices));
		}
		_new.HasMask = (mask != nullptr);
		if (mask != nullptr) {
			std::memcpy(_new.Mask, mask, sizeof(_new.Mask));
		}
	}

	const Formats::TileDiffuseOverride* SetTileOverrideCommand::FindDiffuse(const LevelDocument& document, std::uint16_t tileId)
	{
		for (const Formats::TileDiffuseOverride& tile : document.DiffuseOverrides) {
			if (tile.TileId == tileId) {
				return &tile;
			}
		}
		return nullptr;
	}

	const Formats::TileMaskOverride* SetTileOverrideCommand::FindMask(const LevelDocument& document, std::uint16_t tileId)
	{
		for (const Formats::TileMaskOverride& tile : document.MaskOverrides) {
			if (tile.TileId == tileId) {
				return &tile;
			}
		}
		return nullptr;
	}

	void SetTileOverrideCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, _new);
	}

	void SetTileOverrideCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, _old);
	}

	void SetTileOverrideCommand::Apply(LevelDocument& document, DocumentChanges& changes, const State& state)
	{
		// Both lists are kept sorted by the tile, as the file has them; returns the entry to fill, or nullptr if removed
		auto place = [this](auto& list, bool has) -> decltype(&list[0]) {
			auto it = std::lower_bound(list.begin(), list.end(), _tileId, [](const auto& tile, std::uint16_t tileId) { return tile.TileId < tileId; });
			bool exists = (it != list.end() && it->TileId == _tileId);
			if (!has) {
				if (exists) {
					list.erase(it);
				}
				return nullptr;
			}
			if (!exists) {
				it = list.insert(it, std::remove_reference_t<decltype(*it)>{});
				it->TileId = _tileId;
			}
			return &*it;
		};
		if (Formats::TileDiffuseOverride* diffuse = place(document.DiffuseOverrides, state.HasDiffuse)) {
			std::memcpy(diffuse->Indices, state.Indices, sizeof(diffuse->Indices));
		}
		if (Formats::TileMaskOverride* mask = place(document.MaskOverrides, state.HasMask)) {
			std::memcpy(mask->Mask, state.Mask, sizeof(mask->Mask));
		}
		changes.Add(DocumentChangeKind::TileOverrides);
	}

	bool SetTileOverrideCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetTileOverrideCommand>(&next);
		return (other != nullptr && other->_tileId == _tileId && other->_name == _name);
	}

	bool SetTileOverrideCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		_new = runtime_cast<SetTileOverrideCommand>(&next)->_new;
		return true;
	}

	String SetTileOverrideCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetTileOverrideCommand::GetMemoryUsage() const
	{
		return sizeof(SetTileOverrideCommand) + _name.size();
	}

	SetLevelPaletteCommand::SetLevelPaletteCommand(const LevelDocument& document, bool usePalette, const std::uint32_t* palette, String name)
		: _name(Death::move(name)), _oldUse((document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette), _newUse(usePalette)
	{
		std::memcpy(_oldPalette, document.LevelPalette, sizeof(_oldPalette));
		std::memcpy(_newPalette, palette, sizeof(_newPalette));
		std::memset(_changedEntries, 0, sizeof(_changedEntries));
		for (std::int32_t i = 0; i < Formats::LevelFile::ColorsPerPalette; i++) {
			if (_oldPalette[i] != _newPalette[i]) {
				_changedEntries[i / 32] |= (1u << (i % 32));
			}
		}
	}

	void SetLevelPaletteCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, _newUse, _newPalette);
	}

	void SetLevelPaletteCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Apply(document, changes, _oldUse, _oldPalette);
	}

	void SetLevelPaletteCommand::Apply(LevelDocument& document, DocumentChanges& changes, bool use, const std::uint32_t* palette)
	{
		bool used = (document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette;
		if (used != use) {
			document.Flags = (use ? (document.Flags | LevelFlags::UseLevelPalette) : (document.Flags & ~LevelFlags::UseLevelPalette));
			// The level is shown with another palette, which is taken when it's loaded
			changes.Add(DocumentChangeKind::LevelProperties);
		}
		std::memcpy(document.LevelPalette, palette, sizeof(document.LevelPalette));
		changes.Add(DocumentChangeKind::Palette);
	}

	bool SetLevelPaletteCommand::CanMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLevelPaletteCommand>(&next);
		// Turning the palette on or off is a step of its own
		if (other == nullptr || other->_oldUse != other->_newUse || _oldUse != _newUse || other->_name != _name) {
			return false;
		}
		// So is a change of other entries than these, a color dragged is the same entries all the time
		bool otherChangesAny = false;
		for (std::size_t i = 0; i < sizeof(_changedEntries) / sizeof(_changedEntries[0]); i++) {
			otherChangesAny |= (other->_changedEntries[i] != 0);
		}
		return (!otherChangesAny || std::memcmp(other->_changedEntries, _changedEntries, sizeof(_changedEntries)) == 0);
	}

	bool SetLevelPaletteCommand::TryMerge(EditCommand& next)
	{
		if (!CanMerge(next)) {
			return false;
		}
		std::memcpy(_newPalette, runtime_cast<SetLevelPaletteCommand>(&next)->_newPalette, sizeof(_newPalette));
		return true;
	}

	String SetLevelPaletteCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLevelPaletteCommand::GetMemoryUsage() const
	{
		return sizeof(SetLevelPaletteCommand) + _name.size();
	}

	SetLevelTilesCommand::SetLevelTilesCommand(const LevelDocument& document, std::int32_t slot, SmallVector<std::uint8_t, 0>&& data, String name, std::int32_t flags)
		: _name(Death::move(name)), _slot(slot), _data(Death::move(data)), _flags(flags)
	{
		// A tile set file has no data in the level, which it would take as its tiles then
		if (slot < 0 || slot >= std::int32_t(document.ExtraTileSets.size()) ||
			(document.ExtraTileSets[slot].Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
			_slot = -1;
		}
		if (_flags >= 0) {
			_flags = (_flags & 0xFF) | Formats::ExtraTileSetRecord::Embedded;
		}
	}

	void SetLevelTilesCommand::Swap(LevelDocument& document, DocumentChanges& changes)
	{
		if (_slot < 0 || _slot >= std::int32_t(document.ExtraTileSets.size()) ||
			(document.ExtraTileSets[_slot].Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
			return;
		}
		Formats::ExtraTileSetRecord& record = document.ExtraTileSets[_slot];
		std::swap(record.Data, _data);
		// Only pixels and masks, as tile overrides are
		changes.Add(DocumentChangeKind::TileOverrides);
		if (_flags >= 0 && std::uint8_t(_flags) != record.Flags) {
			std::uint8_t previous = record.Flags;
			record.Flags = std::uint8_t(_flags);
			_flags = previous;
			// The colors the tile set is drawn in, which the runtime takes when it's loaded
			changes.Add(DocumentChangeKind::TileSets);
		}
	}

	void SetLevelTilesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		Swap(document, changes);
	}

	void SetLevelTilesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		Swap(document, changes);
	}

	bool SetLevelTilesCommand::CanMerge(EditCommand& next)
	{
		// A change of the flags is a step of its own
		auto* other = runtime_cast<SetLevelTilesCommand>(&next);
		return (other != nullptr && other->_slot == _slot && other->_name == _name && other->_flags < 0 && _flags < 0);
	}

	bool SetLevelTilesCommand::TryMerge(EditCommand& next)
	{
		// Both are done already, this one keeps the data from before both, the document has the newest
		return CanMerge(next);
	}

	String SetLevelTilesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLevelTilesCommand::GetMemoryUsage() const
	{
		return sizeof(SetLevelTilesCommand) + _name.size() + _data.size();
	}

	namespace
	{
		// Number of tile IDs below the animated tiles the tiles of a change need, 0 if they all can be addressed already
		std::int32_t GetStaticTileIdsNeeded(const LevelDocument& document, const DocumentChanges& changes)
		{
			std::int32_t needed = 0;
			auto check = [&document, &needed](const TileRef& tile) {
				if (!document.IsTileAddressable(tile)) {
					needed = std::max<std::int32_t>(needed, document.ToTileId(tile) + 1);
				}
			};

			// Only the tiles the change changed, all of them if layers came or went, the indices of the others moved then
			if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
				for (const DocumentLayer& layer : document.Layers) {
					for (const TileRef& tile : layer.Tiles) {
						check(tile);
					}
				}
			} else if (changes.HasAny(DocumentChangeKind::Tiles)) {
				for (const DocumentTileArea& area : changes.GetTileAreas()) {
					if (area.Layer < 0 || area.Layer >= std::int32_t(document.Layers.size())) {
						continue;
					}
					const DocumentLayer& layer = document.Layers[area.Layer];
					std::int32_t x1 = std::max<std::int32_t>(area.Area.X, 0);
					std::int32_t y1 = std::max<std::int32_t>(area.Area.Y, 0);
					std::int32_t x2 = std::min<std::int32_t>(area.Area.X + area.Area.W, layer.GetWidth());
					std::int32_t y2 = std::min<std::int32_t>(area.Area.Y + area.Area.H, layer.GetHeight());
					for (std::int32_t y = y1; y < y2; y++) {
						for (std::int32_t x = x1; x < x2; x++) {
							check(layer.GetTile(x, y));
						}
					}
				}
			}
			if (changes.HasAny(DocumentChangeKind::AnimationStructure | DocumentChangeKind::AnimationTiming)) {
				for (const DocumentAnimation& animation : document.Animations) {
					for (const TileRef& frame : animation.Frames) {
						check(frame);
					}
				}
			}
			return needed;
		}
	}

	void ExecuteGrowingTileIdSpace(CommandHistory& history, std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge)
	{
		LevelDocument& document = history.GetDocument();
		// What this change changed alone, the caller may have collected more
		DocumentChanges done;
		command->Do(document, done);

		std::int32_t needed = GetStaticTileIdsNeeded(document, done);
		// Overrides are kept by tile ID, one of a tile past the space would be read back as of an animated tile, and
		// dropped or put on it as the tile sets change --- only a tile of the tile sets, an ID past them isn't of any
		if (auto* tileOverride = runtime_cast<SetTileOverrideCommand>(command.get())) {
			std::int32_t tileId = tileOverride->GetTileId();
			std::int32_t staticCount = document.GetStaticTileCount();
			if (tileOverride->IsOverriddenAfter() && tileId >= document.GetAnimatedTilesOffset() && (staticCount < 0 || tileId < staticCount)) {
				needed = std::max<std::int32_t>(needed, tileId + 1);
			}
		}
		if (needed <= document.GetAnimatedTilesOffset()) {
			history.Record(Death::move(command), allowMerge);
			changes.Add(done);
			return;
		}

		// The animated tiles take the top of the space, they move up, so all tiles that show them do so by other IDs
		String name = command->GetName();
		auto grow = SnapshotCommand::Begin(document, SnapshotParts::Animations, name, DocumentChangeKind::AnimationStructure);
		std::int32_t staticCount = std::max<std::int32_t>(needed, document.GetStaticTileCount());
		document.TileIdSpace = std::min<std::int32_t>(staticCount + std::int32_t(document.Animations.size()), UINT16_MAX + 1);
		grow->End(document, done);
		LOGI("Tile ID space grown to {} for tile ID {}, the animated tiles start at {} now", document.TileIdSpace, needed - 1, document.GetAnimatedTilesOffset());

		// The change can't be undone without the growth, nor the growth without the change
		bool outermost = !history.IsInTransaction();
		if (outermost) {
			history.BeginTransaction(Death::move(name));
		}
		history.Record(Death::move(command), allowMerge);
		history.Record(Death::move(grow));
		if (outermost) {
			history.EndTransaction();
		}
		changes.Add(done);
	}
}

#endif
