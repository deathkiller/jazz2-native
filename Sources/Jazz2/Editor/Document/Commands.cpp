#include "Commands.h"

#if defined(WITH_EDITOR)

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
		: _document(document), _layer(layer), _width(0), _name(std::move(name)),
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
		SmallVector<std::uint8_t, 0> decompressed;
		ArrayView<const Change> list = _changes;
		if (!_compressed.IsEmpty()) {
			if (!_compressed.Decompress(decompressed)) {
				DEATH_ASSERT_UNREACHABLE("Changes cannot be decompressed", );
			}
			list = ArrayView<const Change>(reinterpret_cast<const Change*>(decompressed.data()), decompressed.size() / sizeof(Change));
		}

		DocumentLayer& layer = document.Layers[_layer];
		for (const Change& change : list) {
			layer.Tiles[change.Cell] = (undo ? change.Old : change.New);
		}

		changes.AddTiles(_layer, GetArea());
	}

	SetEventsCommand::SetEventsCommand(const LevelDocument& document, String name)
		: _document(document), _name(std::move(name)),
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
		for (const Change& change : _changes) {
			document.SetEvent(std::int32_t(change.Cell % std::uint32_t(_width)), std::int32_t(change.Cell / std::uint32_t(_width)), change.New);
		}
		changes.AddEvents(GetArea());
	}

	void SetEventsCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		for (const Change& change : _changes) {
			document.SetEvent(std::int32_t(change.Cell % std::uint32_t(_width)), std::int32_t(change.Cell / std::uint32_t(_width)), change.Old);
		}
		changes.AddEvents(GetArea());
	}

	bool SetEventsCommand::TryMerge(EditCommand& next)
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
		: _name(std::move(name)), _kinds(kinds)
	{
	}

	std::unique_ptr<SnapshotCommand> SnapshotCommand::Begin(const LevelDocument& document, SnapshotParts parts, String name, DocumentChangeKind kinds)
	{
		std::unique_ptr<SnapshotCommand> command(new SnapshotCommand(std::move(name), kinds != DocumentChangeKind::None ? kinds : DocumentSnapshot::GetChangeKinds(parts)));
		command->_before = DocumentSnapshot::Capture(document, parts);
		return command;
	}

	void SnapshotCommand::End(const LevelDocument& document, DocumentChanges& changes)
	{
		_after = DocumentSnapshot::Capture(document, _before.GetParts());
		changes.Add(_kinds);
	}

	void SnapshotCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		_after.Restore(document);
		changes.Add(_kinds);
	}

	void SnapshotCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		_before.Restore(document);
		changes.Add(_kinds);
	}

	String SnapshotCommand::GetName() const
	{
		return _name;
	}

	std::size_t SnapshotCommand::GetMemoryUsage() const
	{
		return sizeof(SnapshotCommand) + _before.GetMemoryUsage() + _after.GetMemoryUsage() + _name.size();
	}

	SetLayerPropertiesCommand::SetLayerPropertiesCommand(const LevelDocument& document, std::int32_t layer,
		const Formats::LayerRecord& record, StringView layerName, String name)
		: _name(std::move(name)), _layer(layer), _newRecord(record), _newName(layerName)
	{
		const DocumentLayer& current = document.Layers[layer];
		_oldRecord = current.Record;
		_oldName = current.Name;

		// The size is changed only with the tiles, by another command
		_newRecord.Width = _oldRecord.Width;
		_newRecord.Height = _oldRecord.Height;
	}

	void SetLayerPropertiesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		DocumentLayer& layer = document.Layers[_layer];
		layer.Record = _newRecord;
		layer.Name = _newName;
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	void SetLayerPropertiesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		DocumentLayer& layer = document.Layers[_layer];
		layer.Record = _oldRecord;
		layer.Name = _oldName;
		changes.Add(DocumentChangeKind::LayerProperties);
	}

	bool SetLayerPropertiesCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLayerPropertiesCommand>(&next);
		if (other == nullptr || other->_layer != _layer) {
			return false;
		}
		_newRecord = other->_newRecord;
		_newName = std::move(other->_newName);
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

	SetOffGridEventsCommand::SetOffGridEventsCommand(const LevelDocument& document, SmallVector<Formats::OffGridEventRecord, 0> events, String name)
		: _name(std::move(name)), _new(std::move(events))
	{
		_old.reserve(document.OffGridEvents.size());
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			_old.push_back(event);
		}
	}

	void SetOffGridEventsCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		document.OffGridEvents.clear();
		for (const Formats::OffGridEventRecord& event : _new) {
			document.OffGridEvents.push_back(event);
		}
		changes.Add(DocumentChangeKind::Events);
	}

	void SetOffGridEventsCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		document.OffGridEvents.clear();
		for (const Formats::OffGridEventRecord& event : _old) {
			document.OffGridEvents.push_back(event);
		}
		changes.Add(DocumentChangeKind::Events);
	}

	bool SetOffGridEventsCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetOffGridEventsCommand>(&next);
		if (other == nullptr || other->_name != _name) {
			return false;
		}
		_new = std::move(other->_new);
		return true;
	}

	String SetOffGridEventsCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetOffGridEventsCommand::GetMemoryUsage() const
	{
		return sizeof(SetOffGridEventsCommand) + _name.size() + (_old.size() + _new.size()) * sizeof(Formats::OffGridEventRecord);
	}

	SetLevelPropertiesCommand::SetLevelPropertiesCommand(const LevelDocument& document, LevelFlags flags,
		const Formats::LevelProperties& properties, String name)
		: _name(std::move(name)), _oldFlags(document.Flags), _newFlags(flags), _oldProperties(document.Properties),
			_newProperties(properties)
	{
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
		: _name(std::move(name)), _index(index), _new(std::move(animation))
	{
		if (index >= 0 && index < std::int32_t(document.Animations.size())) {
			_old = document.Animations[index];
		}
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

	bool SetAnimationCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetAnimationCommand>(&next);
		if (other == nullptr || other->_index != _index || other->_name != _name) {
			return false;
		}
		_new = std::move(other->_new);
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

	bool SetLevelPropertiesCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLevelPropertiesCommand>(&next);
		if (other == nullptr) {
			return false;
		}
		_newFlags = other->_newFlags;
		_newProperties = std::move(other->_newProperties);
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
		: _name(std::move(name)), _tileId(tileId)
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

	bool SetTileOverrideCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetTileOverrideCommand>(&next);
		if (other == nullptr || other->_tileId != _tileId) {
			return false;
		}
		_new = other->_new;
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
		: _name(std::move(name)), _oldUse((document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette), _newUse(usePalette)
	{
		std::memcpy(_oldPalette, document.LevelPalette, sizeof(_oldPalette));
		std::memcpy(_newPalette, palette, sizeof(_newPalette));
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

	bool SetLevelPaletteCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLevelPaletteCommand>(&next);
		// Turning the palette on or off is a step of its own
		if (other == nullptr || other->_oldUse != other->_newUse || _oldUse != _newUse) {
			return false;
		}
		std::memcpy(_newPalette, other->_newPalette, sizeof(_newPalette));
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

	SetLevelTilesCommand::SetLevelTilesCommand(const LevelDocument& document, std::int32_t slot, SmallVector<std::uint8_t, 0>&& data, String name)
		: _name(std::move(name)), _slot(slot), _newData(std::move(data))
	{
		if (slot >= 0 && slot < std::int32_t(document.ExtraTileSets.size())) {
			_oldData = document.ExtraTileSets[slot].Data;
		}
	}

	void SetLevelTilesCommand::Do(LevelDocument& document, DocumentChanges& changes)
	{
		if (_slot >= 0 && _slot < std::int32_t(document.ExtraTileSets.size())) {
			document.ExtraTileSets[_slot].Data = _newData;
			// Only pixels and masks, as tile overrides are
			changes.Add(DocumentChangeKind::TileOverrides);
		}
	}

	void SetLevelTilesCommand::Undo(LevelDocument& document, DocumentChanges& changes)
	{
		if (_slot >= 0 && _slot < std::int32_t(document.ExtraTileSets.size())) {
			document.ExtraTileSets[_slot].Data = _oldData;
			changes.Add(DocumentChangeKind::TileOverrides);
		}
	}

	bool SetLevelTilesCommand::TryMerge(EditCommand& next)
	{
		auto* other = runtime_cast<SetLevelTilesCommand>(&next);
		if (other == nullptr || other->_slot != _slot || other->_name != _name) {
			return false;
		}
		_newData = other->_newData;
		return true;
	}

	String SetLevelTilesCommand::GetName() const
	{
		return _name;
	}

	std::size_t SetLevelTilesCommand::GetMemoryUsage() const
	{
		return sizeof(SetLevelTilesCommand) + _name.size() + _oldData.size() + _newData.size();
	}
}

#endif
