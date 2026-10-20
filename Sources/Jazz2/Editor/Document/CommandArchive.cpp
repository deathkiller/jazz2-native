#include "CommandArchive.h"

#if defined(WITH_EDITOR)

#include "Commands.h"
#include "TerrainSplines.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace Jazz2::Editor
{
	namespace
	{
		// 2 --- the events off the grid a change changes aren't stored, they're worked out of the lists
		// 3 --- a change of many tiles is stored compressed, as it's kept in memory
		// 4 --- a change of the events off the grid is stored as the runs of them it changes, not as both lists
		constexpr std::uint8_t Version = 4;
		// Commands nested deeper than this are damaged, a transaction holds commands, not other transactions so deep
		constexpr std::int32_t MaxDepth = 16;
		// Deflate makes about 1032 bytes of a byte at most, a buffer said to be larger is damaged
		constexpr std::uint64_t MaxInflateRatio = 1032;
		// Nothing the editor keeps in a single buffer is larger, all layers of a level of the most tiles included
		constexpr std::uint64_t MaxBufferSize = std::uint64_t(1) << 30;

		enum class CommandType : std::uint8_t {
			SetTiles = 1,
			SetEvents,
			Snapshot,
			SetChunk,
			MoveLayer,
			InsertLayer,
			SetLayerProperties,
			SetAnimation,
			SetOffGridEvents,
			SetLevelProperties,
			SetTileOverride,
			SetLevelPalette,
			SetLevelTiles,
			SetTerrainSplines,
			Transaction,
			// Added later, so the values before stay those of stored histories
			SetLayerTexture
		};

		// The records are written as they are in memory, a history of a build with other records isn't read; the size of
		// a record private to its command is given by CommandArchive, which can see it
		std::uint32_t GetLayoutFingerprint(std::size_t tileOverrideStateSize)
		{
			std::uint32_t hash = 0x811C9DC5u;
			auto mix = [&hash](std::uint32_t value) {
				for (std::int32_t i = 0; i < 4; i++) {
					hash ^= std::uint8_t(value >> (i * 8));
					hash *= 0x01000193u;
				}
			};
			const std::uint16_t endianness = 0x0102;
			mix(*reinterpret_cast<const std::uint8_t*>(&endianness));
			mix(std::uint32_t(sizeof(TileRef)));
			mix(std::uint32_t(sizeof(Formats::LayerRecord)));
			mix(std::uint32_t(sizeof(Formats::EventRecord)));
			mix(std::uint32_t(sizeof(Formats::OffGridEventRecord)));
			mix(std::uint32_t(sizeof(SetTilesCommand::Change)));
			mix(std::uint32_t(sizeof(SetEventsCommand::Change)));
			mix(std::uint32_t(sizeof(LevelFlags)));
			mix(std::uint32_t(sizeof(DocumentChangeKind)));
			mix(std::uint32_t(sizeof(DocumentEvent)));
			mix(std::uint32_t(sizeof(Formats::TileDiffuseOverride)));
			mix(std::uint32_t(sizeof(Formats::TileMaskOverride)));
			mix(std::uint32_t(tileOverrideStateSize));
			// The snapshots and the layers compressed by them are kept as they are, so how they're written counts too
			mix(DocumentSnapshot::EncodingVersion);
			return hash;
		}
	}

	class CommandArchive::Writer
	{
	public:
		explicit Writer(SmallVectorImpl<std::uint8_t>& data) : _data(data) {}

		void Bytes(const void* source, std::size_t size) {
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(source);
			_data.append(bytes, bytes + size);
		}
		template<typename T>
		void Value(const T& value) {
			static_assert(std::is_trivially_copyable<T>::value, "The value has to be trivially copyable");
			Bytes(&value, sizeof(T));
		}
		void VarUint(std::uint64_t value) {
			do {
				std::uint8_t byte = std::uint8_t(value & 0x7F);
				value >>= 7;
				_data.push_back(value != 0 ? std::uint8_t(byte | 0x80) : byte);
			} while (value != 0);
		}
		void Text(StringView value) {
			VarUint(value.size());
			Bytes(value.data(), value.size());
		}
		void Blob(ArrayView<const std::uint8_t> value) {
			VarUint(value.size());
			Bytes(value.data(), value.size());
		}
		template<typename T>
		void Array(ArrayView<const T> values) {
			static_assert(std::is_trivially_copyable<T>::value, "The values have to be trivially copyable");
			VarUint(values.size());
			Bytes(values.data(), values.size() * sizeof(T));
		}

		std::size_t GetSize() const {
			return _data.size();
		}

	private:
		SmallVectorImpl<std::uint8_t>& _data;
	};

	class CommandArchive::Reader
	{
	public:
		explicit Reader(ArrayView<const std::uint8_t> data, LayerIdRemap* idRemap = nullptr)
			: IdRemap(idRemap), _pos(data.data()), _end(data.data() + data.size()), _failed(false) {}

		// New identities of the layers the history keeps, they're of another run of the editor
		LayerIdRemap* IdRemap;

		bool IsValid() const {
			return !_failed;
		}
		std::size_t GetRemaining() const {
			return std::size_t(_end - _pos);
		}
		const std::uint8_t* GetPosition() const {
			return _pos;
		}
		void SetPosition(const std::uint8_t* pos) {
			_pos = pos;
		}

		bool Bytes(void* destination, std::size_t size) {
			if (_failed || GetRemaining() < size) {
				_failed = true;
				return false;
			}
			std::memcpy(destination, _pos, size);
			_pos += size;
			return true;
		}
		template<typename T>
		T Value() {
			static_assert(std::is_trivially_copyable<T>::value, "The value has to be trivially copyable");
			T value;
			if (!Bytes(&value, sizeof(T))) {
				std::memset(&value, 0, sizeof(T));
			}
			return value;
		}
		std::uint64_t VarUint() {
			std::uint64_t value = 0;
			for (std::int32_t shift = 0; shift < 64; shift += 7) {
				if (_failed || _pos >= _end) {
					_failed = true;
					return 0;
				}
				std::uint8_t byte = *_pos++;
				value |= std::uint64_t(byte & 0x7F) << shift;
				if ((byte & 0x80) == 0) {
					return value;
				}
			}
			_failed = true;
			return 0;
		}
		String Text() {
			std::uint64_t size = VarUint();
			if (_failed || size > GetRemaining()) {
				_failed = true;
				return {};
			}
			String value(reinterpret_cast<const char*>(_pos), std::size_t(size));
			_pos += size;
			return value;
		}
		void Blob(SmallVectorImpl<std::uint8_t>& value) {
			std::uint64_t size = VarUint();
			if (_failed || size > GetRemaining()) {
				_failed = true;
				value.clear();
				return;
			}
			value.assign(_pos, _pos + size);
			_pos += size;
		}
		template<typename T>
		void Array(SmallVectorImpl<T>& values) {
			static_assert(std::is_trivially_copyable<T>::value, "The values have to be trivially copyable");
			std::uint64_t count = VarUint();
			if (_failed || count > GetRemaining() / sizeof(T)) {
				_failed = true;
				values.clear();
				return;
			}
			values.resize_for_overwrite(std::size_t(count));
			Bytes(values.data(), std::size_t(count) * sizeof(T));
		}

	private:
		const std::uint8_t* _pos;
		const std::uint8_t* _end;
		bool _failed;
	};

	void CommandArchive::WriteBuffer(Writer& w, const CompressedBuffer& buffer)
	{
		w.VarUint(buffer._originalSize);
		w.Blob(ArrayView<const std::uint8_t>(buffer._data.get(), buffer._size));
	}

	bool CommandArchive::ReadBuffer(Reader& r, CompressedBuffer& buffer)
	{
		std::uint64_t originalSize = r.VarUint();
		SmallVector<std::uint8_t, 0> data;
		r.Blob(data);
		// The size is taken whole by the buffer it's decompressed into, so a damaged one isn't trusted with that
		if (!r.IsValid() || (originalSize > 0 && data.empty()) || originalSize > MaxBufferSize ||
			originalSize > std::uint64_t(data.size()) * MaxInflateRatio + 64 || originalSize > std::uint64_t(SIZE_MAX)) {
			return false;
		}
		buffer._originalSize = std::size_t(originalSize);
		buffer._size = data.size();
		buffer._data = nullptr;
		if (!data.empty()) {
			buffer._data = std::make_unique<std::uint8_t[]>(data.size());
			std::memcpy(buffer._data.get(), data.data(), data.size());
		}
		return true;
	}

	void CommandArchive::WriteSnapshot(Writer& w, const DocumentSnapshot& snapshot)
	{
		w.Value<std::uint32_t>(std::uint32_t(snapshot._parts));
		w.Value<std::int32_t>(snapshot._layer);
		WriteBuffer(w, snapshot._data);
		w.VarUint(snapshot._blobs.size());
		for (const DocumentSnapshot::Blob& blob : snapshot._blobs) {
			if (blob != nullptr) {
				w.Blob(*blob);
			} else {
				w.VarUint(0);
			}
		}
	}

	bool CommandArchive::ReadSnapshot(Reader& r, DocumentSnapshot& snapshot)
	{
		snapshot._parts = SnapshotParts(r.Value<std::uint32_t>());
		snapshot._layer = r.Value<std::int32_t>();
		if (!ReadBuffer(r, snapshot._data)) {
			return false;
		}
		std::uint64_t count = r.VarUint();
		if (!r.IsValid() || count > r.GetRemaining()) {
			return false;
		}
		snapshot._blobs.clear();
		for (std::uint64_t i = 0; i < count && r.IsValid(); i++) {
			auto blob = std::make_shared<SmallVector<std::uint8_t, 0>>();
			r.Blob(*blob);
			snapshot._blobs.push_back(Death::move(blob));
		}
		// The layers get identities of this run of the editor, those of the run that wrote them could be of other layers now
		if (!r.IsValid() || (r.IdRemap != nullptr && !snapshot.RemapLayerIds(*r.IdRemap))) {
			return false;
		}
		return true;
	}

	bool CommandArchive::WriteCommand(Writer& w, const EditCommand& command)
	{
		auto writeAnimation = [&w](const DocumentAnimation& animation) {
			w.Value(animation.Timing.FrameDuration16);
			w.Value(animation.Timing.Delay);
			w.Value(animation.Timing.DelayJitter);
			w.Value(animation.Timing.PingPongDelay);
			w.Value(animation.Timing.IsPingPong);
			w.Array<TileRef>(animation.Frames);
		};
		auto writeProperties = [&w](const Formats::LevelProperties& p) {
			w.Text(p.DisplayName);
			w.Text(p.Author);
			w.Text(p.NextLevel);
			w.Text(p.SecretLevel);
			w.Text(p.BonusLevel);
			w.Text(p.DefaultTileSet);
			w.Text(p.Music);
			w.Value(p.AmbientColor);
			w.Value(p.Weather);
			w.Value(p.WeatherIntensity);
			w.Value(p.WaterLevel);
			w.Value(p.CaptionTileId);
		};

		// Cast to the types themselves, a const one is another type to runtime_cast<T>() and would never match, the
		// pointers are const already as the command is
		if (auto* c = runtime_cast<SetTilesCommand>(&command)) {
			w.Value(CommandType::SetTiles);
			w.Text(c->_name);
			w.Value(c->_layer);
			w.Value(c->_width);
			w.Value(c->_minX); w.Value(c->_minY); w.Value(c->_maxX); w.Value(c->_maxY);
			// The changes as the command keeps them, many of them compressed
			if (!c->_compressed.IsEmpty()) {
				w.Value<std::uint8_t>(1);
				WriteBuffer(w, c->_compressed);
			} else {
				w.Value<std::uint8_t>(0);
				w.Array<SetTilesCommand::Change>(c->_changes);
			}
			return true;
		}
		if (auto* c = runtime_cast<SetEventsCommand>(&command)) {
			w.Value(CommandType::SetEvents);
			w.Text(c->_name);
			w.Value(c->_width);
			w.Value(c->_height);
			w.Value(c->_minX); w.Value(c->_minY); w.Value(c->_maxX); w.Value(c->_maxY);
			w.Array<SetEventsCommand::Change>(c->_changes);
			return true;
		}
		if (auto* c = runtime_cast<SnapshotCommand>(&command)) {
			w.Value(CommandType::Snapshot);
			w.Text(c->_name);
			w.Value(c->_kinds);
			WriteSnapshot(w, c->_before);
			WriteSnapshot(w, c->_after);
			return true;
		}
		if (auto* c = runtime_cast<SetChunkCommand>(&command)) {
			w.Value(CommandType::SetChunk);
			w.Text(c->_name);
			w.Value(c->_tag);
			w.Blob(c->_oldData);
			w.Blob(c->_newData);
			return true;
		}
		if (auto* c = runtime_cast<MoveLayerCommand>(&command)) {
			w.Value(CommandType::MoveLayer);
			w.Text(c->_name);
			w.Value(c->_from);
			w.Value(c->_to);
			return true;
		}
		if (auto* c = runtime_cast<InsertLayerCommand>(&command)) {
			w.Value(CommandType::InsertLayer);
			w.Text(c->_name);
			w.Value(c->_index);
			w.Value<std::uint8_t>(c->_insert ? 1 : 0);
			WriteBuffer(w, c->_layer);
			w.Blob(c->_oldSplines);
			w.Blob(c->_newSplines);
			w.Value<std::uint8_t>(c->_hasNewSplines ? 1 : 0);
			return true;
		}
		if (auto* c = runtime_cast<SetLayerPropertiesCommand>(&command)) {
			w.Value(CommandType::SetLayerProperties);
			w.Text(c->_name);
			w.Value(c->_layer);
			w.Value(c->_oldRecord);
			w.Value(c->_newRecord);
			w.Text(c->_oldName);
			w.Text(c->_newName);
			w.Value(c->_changedFields);
			return true;
		}
		if (auto* c = runtime_cast<SetAnimationCommand>(&command)) {
			w.Value(CommandType::SetAnimation);
			w.Text(c->_name);
			w.Value(c->_index);
			writeAnimation(c->_old);
			writeAnimation(c->_new);
			w.Value(c->_changedFields);
			return true;
		}
		if (auto* c = runtime_cast<SetOffGridEventsCommand>(&command)) {
			w.Value(CommandType::SetOffGridEvents);
			w.Text(c->_name);
			w.VarUint(c->_oldCount);
			w.VarUint(c->_newCount);
			w.VarUint(c->_hunks.size());
			for (const SetOffGridEventsCommand::Hunk& hunk : c->_hunks) {
				w.VarUint(hunk.Index);
				w.VarUint(hunk.OldCount);
				w.VarUint(hunk.NewCount);
			}
			w.Array<Formats::OffGridEventRecord>(c->_oldRecords);
			w.Array<Formats::OffGridEventRecord>(c->_newRecords);
			// Which events it changes is worked out of the runs again when it's read
			return true;
		}
		if (auto* c = runtime_cast<SetLevelPropertiesCommand>(&command)) {
			w.Value(CommandType::SetLevelProperties);
			w.Text(c->_name);
			w.Value(c->_oldFlags);
			w.Value(c->_newFlags);
			writeProperties(c->_oldProperties);
			writeProperties(c->_newProperties);
			w.Value(c->_changedFields);
			return true;
		}
		if (auto* c = runtime_cast<SetLayerTextureCommand>(&command)) {
			w.Value(CommandType::SetLayerTexture);
			w.Text(c->_name);
			w.Value(c->_layer);
			w.Blob(c->_old);
			w.Blob(c->_new);
			return true;
		}
		if (auto* c = runtime_cast<SetTileOverrideCommand>(&command)) {
			w.Value(CommandType::SetTileOverride);
			w.Text(c->_name);
			w.Value(c->_tileId);
			w.Value(c->_old);
			w.Value(c->_new);
			return true;
		}
		if (auto* c = runtime_cast<SetLevelPaletteCommand>(&command)) {
			w.Value(CommandType::SetLevelPalette);
			w.Text(c->_name);
			w.Value<std::uint8_t>(c->_oldUse ? 1 : 0);
			w.Value<std::uint8_t>(c->_newUse ? 1 : 0);
			w.Bytes(c->_oldPalette, sizeof(c->_oldPalette));
			w.Bytes(c->_newPalette, sizeof(c->_newPalette));
			w.Bytes(c->_changedEntries, sizeof(c->_changedEntries));
			return true;
		}
		if (auto* c = runtime_cast<SetLevelTilesCommand>(&command)) {
			w.Value(CommandType::SetLevelTiles);
			w.Text(c->_name);
			w.Value(c->_slot);
			w.Blob(c->_data);
			w.Value(c->_flags);
			return true;
		}
		if (auto* c = runtime_cast<SetTerrainSplinesCommand>(&command)) {
			w.Value(CommandType::SetTerrainSplines);
			w.Text(c->_name);
			w.Blob(c->_oldData);
			w.Blob(c->_newData);
			w.Value<std::uint8_t>(c->_tiles != nullptr ? 1 : 0);
			return (c->_tiles == nullptr || WriteCommand(w, *c->_tiles));
		}
		String name;
		if (const auto* commands = CommandHistory::GetTransactionCommands(command, name)) {
			w.Value(CommandType::Transaction);
			w.Text(name);
			w.VarUint(commands->size());
			for (const auto& nested : *commands) {
				if (!WriteCommand(w, *nested)) {
					return false;
				}
			}
			return true;
		}
		// A command this version doesn't know how to keep
		return false;
	}

	std::unique_ptr<EditCommand> CommandArchive::ReadCommand(Reader& r, LevelDocument& document, std::int32_t depth)
	{
		if (depth > MaxDepth) {
			return nullptr;
		}
		auto readAnimation = [&r](DocumentAnimation& animation) {
			animation.Timing.FrameDuration16 = r.Value<std::uint16_t>();
			animation.Timing.Delay = r.Value<std::uint16_t>();
			animation.Timing.DelayJitter = r.Value<std::uint16_t>();
			animation.Timing.PingPongDelay = r.Value<std::uint16_t>();
			animation.Timing.IsPingPong = r.Value<std::uint8_t>();
			r.Array(animation.Frames);
		};
		auto readProperties = [&r](Formats::LevelProperties& p) {
			p.DisplayName = r.Text();
			p.Author = r.Text();
			p.NextLevel = r.Text();
			p.SecretLevel = r.Text();
			p.BonusLevel = r.Text();
			p.DefaultTileSet = r.Text();
			p.Music = r.Text();
			p.AmbientColor = r.Value<std::uint32_t>();
			p.Weather = r.Value<std::uint8_t>();
			p.WeatherIntensity = r.Value<std::uint8_t>();
			p.WaterLevel = r.Value<std::uint16_t>();
			p.CaptionTileId = r.Value<std::uint16_t>();
		};

		CommandType type = r.Value<CommandType>();
		String name = r.Text();
		if (!r.IsValid()) {
			return nullptr;
		}

		switch (type) {
			case CommandType::SetTiles: {
				std::int32_t layer = r.Value<std::int32_t>();
				auto command = std::make_unique<SetTilesCommand>(document, layer, Death::move(name));
				command->_width = r.Value<std::int32_t>();
				command->_minX = r.Value<std::int32_t>(); command->_minY = r.Value<std::int32_t>();
				command->_maxX = r.Value<std::int32_t>(); command->_maxY = r.Value<std::int32_t>();
				bool compressed = (r.Value<std::uint8_t>() != 0);
				if (compressed) {
					// Decompressed only when it's done or undone, a size of no whole number of changes is damaged
					if (!ReadBuffer(r, command->_compressed) || command->_compressed.GetOriginalSize() % sizeof(SetTilesCommand::Change) != 0) {
						return nullptr;
					}
				} else {
					r.Array(command->_changes);
				}
				command->_sealed = true;
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetEvents: {
				auto command = std::make_unique<SetEventsCommand>(document, Death::move(name));
				command->_width = r.Value<std::int32_t>();
				command->_height = r.Value<std::int32_t>();
				command->_minX = r.Value<std::int32_t>(); command->_minY = r.Value<std::int32_t>();
				command->_maxX = r.Value<std::int32_t>(); command->_maxY = r.Value<std::int32_t>();
				r.Array(command->_changes);
				command->_sealed = true;
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::Snapshot: {
				DocumentChangeKind kinds = r.Value<DocumentChangeKind>();
				std::unique_ptr<SnapshotCommand> command(new SnapshotCommand(Death::move(name), kinds));
				if (!ReadSnapshot(r, command->_before) || !ReadSnapshot(r, command->_after)) {
					return nullptr;
				}
				// Kept once in memory as when the command was made, the archive has them twice
				command->_after.ShareBlobsWith(command->_before);
				return command;
			}
			case CommandType::SetChunk: {
				std::uint32_t tag = r.Value<std::uint32_t>();
				auto command = std::make_unique<SetChunkCommand>(document, tag, ArrayView<const std::uint8_t>(), Death::move(name));
				r.Blob(command->_oldData);
				r.Blob(command->_newData);
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::MoveLayer: {
				std::int32_t from = r.Value<std::int32_t>();
				std::int32_t to = r.Value<std::int32_t>();
				return (r.IsValid() ? std::make_unique<MoveLayerCommand>(from, to, Death::move(name)) : nullptr);
			}
			case CommandType::InsertLayer: {
				std::int32_t index = r.Value<std::int32_t>();
				bool insert = (r.Value<std::uint8_t>() != 0);
				std::unique_ptr<InsertLayerCommand> command(new InsertLayerCommand(Death::move(name), index, insert));
				if (!ReadBuffer(r, command->_layer) || (r.IdRemap != nullptr && !DocumentSnapshot::RemapLayerId(command->_layer, *r.IdRemap))) {
					return nullptr;
				}
				r.Blob(command->_oldSplines);
				r.Blob(command->_newSplines);
				command->_hasNewSplines = (r.Value<std::uint8_t>() != 0);
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetLayerProperties: {
				std::int32_t layer = r.Value<std::int32_t>();
				Formats::LayerRecord oldRecord = r.Value<Formats::LayerRecord>();
				Formats::LayerRecord newRecord = r.Value<Formats::LayerRecord>();
				String oldName = r.Text();
				String newName = r.Text();
				// Made for no layer, everything is set below
				auto command = std::make_unique<SetLayerPropertiesCommand>(document, -1, newRecord, newName, Death::move(name));
				command->_layer = layer;
				command->_oldRecord = oldRecord;
				command->_newRecord = newRecord;
				command->_oldName = Death::move(oldName);
				command->_newName = Death::move(newName);
				command->_changedFields = r.Value<std::uint32_t>();
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetAnimation: {
				std::int32_t index = r.Value<std::int32_t>();
				auto command = std::make_unique<SetAnimationCommand>(document, -1, DocumentAnimation(), Death::move(name));
				command->_index = index;
				readAnimation(command->_old);
				readAnimation(command->_new);
				command->_changedFields = r.Value<std::uint32_t>();
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetOffGridEvents: {
				std::uint64_t oldCount = r.VarUint();
				std::uint64_t newCount = r.VarUint();
				std::uint64_t hunkCount = r.VarUint();
				// Each run takes 3 bytes at least
				if (!r.IsValid() || oldCount > UINT32_MAX || newCount > UINT32_MAX || hunkCount > r.GetRemaining() / 3) {
					return nullptr;
				}
				SmallVector<SetOffGridEventsCommand::Hunk, 0> hunks;
				hunks.reserve(std::size_t(hunkCount));
				for (std::uint64_t i = 0; i < hunkCount; i++) {
					std::uint64_t index = r.VarUint();
					std::uint64_t removed = r.VarUint();
					std::uint64_t added = r.VarUint();
					if (!r.IsValid() || index > UINT32_MAX || removed > UINT32_MAX || added > UINT32_MAX) {
						return nullptr;
					}
					hunks.push_back({ std::uint32_t(index), std::uint32_t(removed), std::uint32_t(added) });
				}
				// Made of the document as the same events after as before, everything is set below
				SmallVector<Formats::OffGridEventRecord, 0> unchanged;
				unchanged.append(document.OffGridEvents.begin(), document.OffGridEvents.end());
				auto command = std::make_unique<SetOffGridEventsCommand>(document, Death::move(unchanged), Death::move(name));
				command->_oldCount = std::uint32_t(oldCount);
				command->_newCount = std::uint32_t(newCount);
				command->_hunks = Death::move(hunks);
				r.Array(command->_oldRecords);
				r.Array(command->_newRecords);
				if (!r.IsValid() || !command->AreHunksValid()) {
					return nullptr;
				}
				command->FindChangedEvents();
				return command;
			}
			case CommandType::SetLevelProperties: {
				auto command = std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, document.Properties, Death::move(name));
				command->_oldFlags = r.Value<LevelFlags>();
				command->_newFlags = r.Value<LevelFlags>();
				readProperties(command->_oldProperties);
				readProperties(command->_newProperties);
				command->_changedFields = r.Value<std::uint32_t>();
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetLayerTexture: {
				std::int32_t layer = r.Value<std::int32_t>();
				SmallVector<std::uint8_t, 0> oldTexture, newTexture;
				r.Blob(oldTexture);
				r.Blob(newTexture);
				if (!r.IsValid()) {
					return nullptr;
				}
				auto command = std::make_unique<SetLayerTextureCommand>(document, -1, SmallVector<std::uint8_t, 0>(), Death::move(name));
				command->_layer = layer;
				command->_old = Death::move(oldTexture);
				command->_new = Death::move(newTexture);
				return command;
			}
			case CommandType::SetTileOverride: {
				std::uint16_t tileId = r.Value<std::uint16_t>();
				auto command = std::make_unique<SetTileOverrideCommand>(document, tileId, nullptr, nullptr, Death::move(name));
				// Written as the record is in memory, but its flags are taken byte by byte, a byte of another value isn't a bool
				using State = SetTileOverrideCommand::State;
				auto readState = [&r](State& state) {
					std::uint8_t raw[sizeof(State)];
					if (!r.Bytes(raw, sizeof(raw))) {
						return;
					}
					state.HasDiffuse = (raw[offsetof(State, HasDiffuse)] != 0);
					state.HasMask = (raw[offsetof(State, HasMask)] != 0);
					std::memcpy(state.Indices, raw + offsetof(State, Indices), sizeof(state.Indices));
					std::memcpy(state.Mask, raw + offsetof(State, Mask), sizeof(state.Mask));
				};
				readState(command->_old);
				readState(command->_new);
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetLevelPalette: {
				auto command = std::make_unique<SetLevelPaletteCommand>(document, false, document.LevelPalette, Death::move(name));
				command->_oldUse = (r.Value<std::uint8_t>() != 0);
				command->_newUse = (r.Value<std::uint8_t>() != 0);
				r.Bytes(command->_oldPalette, sizeof(command->_oldPalette));
				r.Bytes(command->_newPalette, sizeof(command->_newPalette));
				r.Bytes(command->_changedEntries, sizeof(command->_changedEntries));
				return (r.IsValid() ? Death::move(command) : nullptr);
			}
			case CommandType::SetLevelTiles: {
				std::int32_t slot = r.Value<std::int32_t>();
				SmallVector<std::uint8_t, 0> data;
				r.Blob(data);
				std::int32_t flags = r.Value<std::int32_t>();
				if (!r.IsValid()) {
					return nullptr;
				}
				auto command = std::make_unique<SetLevelTilesCommand>(document, slot, SmallVector<std::uint8_t, 0>(), Death::move(name));
				command->_slot = slot;
				command->_data = Death::move(data);
				command->_flags = flags;
				return command;
			}
			case CommandType::SetTerrainSplines: {
				std::unique_ptr<SetTerrainSplinesCommand> command(new SetTerrainSplinesCommand(Death::move(name)));
				r.Blob(command->_oldData);
				r.Blob(command->_newData);
				bool hasTiles = (r.Value<std::uint8_t>() != 0);
				if (!r.IsValid()) {
					return nullptr;
				}
				if (hasTiles) {
					std::unique_ptr<EditCommand> tiles = ReadCommand(r, document, depth + 1);
					auto* setTiles = (tiles != nullptr ? runtime_cast<SetTilesCommand>(tiles.get()) : nullptr);
					if (setTiles == nullptr) {
						return nullptr;
					}
					tiles.release();
					command->_tiles.reset(setTiles);
				}
				return command;
			}
			case CommandType::Transaction: {
				std::uint64_t count = r.VarUint();
				if (!r.IsValid() || count > r.GetRemaining()) {
					return nullptr;
				}
				SmallVector<std::unique_ptr<EditCommand>, 0> commands;
				for (std::uint64_t i = 0; i < count; i++) {
					std::unique_ptr<EditCommand> nested = ReadCommand(r, document, depth + 1);
					if (nested == nullptr) {
						return nullptr;
					}
					commands.push_back(Death::move(nested));
				}
				return CommandHistory::MakeTransaction(Death::move(name), Death::move(commands));
			}
			default:
				return nullptr;
		}
	}

	void CommandArchive::Write(const CommandHistory& history, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (!history._transactions.empty()) {
			// A change half made can't be kept, the state of the document isn't one of its own
			return;
		}

		// Each command is written once and kept with its entry while it stays the same, so a copy written every minute
		// only writes the commands that came since
		auto getRecord = [](const CommandHistory::Entry& entry, bool isDone) -> const SmallVector<std::uint8_t, 0>* {
			if (entry.ArchivedRevision != entry.Revision || entry.ArchivedDone != isDone) {
				entry.Archived.clear();
				Writer w(entry.Archived);
				entry.ArchivedValid = WriteCommand(w, *entry.Command);
				// One that could never fit isn't kept, only that it doesn't
				if (!entry.ArchivedValid || entry.Archived.size() > MaxSize) {
					entry.ArchivedValid = false;
					entry.Archived = SmallVector<std::uint8_t, 0>();
				}
				entry.ArchivedRevision = entry.Revision;
				entry.ArchivedDone = isDone;
			}
			return (entry.ArchivedValid ? &entry.Archived : nullptr);
		};

		// The commands that are done from the newest one back, then those undone, as long as they can be written and fit
		const std::int32_t count = std::int32_t(history._entries.size());
		const std::int32_t doneCount = history._doneCount;
		SmallVector<const SmallVector<std::uint8_t, 0>*, 0> done, undone;
		std::size_t total = 0;
		for (std::int32_t i = doneCount - 1; i >= 0; i--) {
			const SmallVector<std::uint8_t, 0>* record = getRecord(history._entries[i], true);
			if (record == nullptr || total + record->size() > MaxSize) {
				break;
			}
			total += record->size();
			done.push_back(record);
		}
		for (std::int32_t i = doneCount; i < count; i++) {
			const SmallVector<std::uint8_t, 0>* record = getRecord(history._entries[i], false);
			if (record == nullptr || total + record->size() > MaxSize) {
				break;
			}
			total += record->size();
			undone.push_back(record);
		}
		if (done.empty() && undone.empty()) {
			return;
		}

		// The saved state, if it's one of the states kept
		const std::int32_t first = doneCount - std::int32_t(done.size());
		std::int32_t savedDone = -1;
		std::uint32_t firstRevision = (first == 0 ? history._baseRevision : history._entries[first - 1].Revision);
		if (history._savedRevision == firstRevision) {
			savedDone = 0;
		} else {
			for (std::int32_t i = first; i < first + std::int32_t(done.size() + undone.size()); i++) {
				if (history._entries[i].Revision == history._savedRevision) {
					savedDone = i - first + 1;
					break;
				}
			}
		}

		data.reserve(total + (done.size() + undone.size()) * 5 + 32);
		Writer w(data);
		w.Value(Version);
		w.Value(GetLayoutFingerprint(sizeof(SetTileOverrideCommand::State)));
		w.VarUint(done.size() + undone.size());
		w.VarUint(done.size());
		w.VarUint(std::uint64_t(savedDone + 1));
		for (std::size_t i = done.size(); i > 0; i--) {
			w.Blob(*done[i - 1]);
		}
		for (const SmallVector<std::uint8_t, 0>* record : undone) {
			w.Blob(*record);
		}
	}

	bool CommandArchive::Read(ArrayView<const std::uint8_t> data, LevelDocument& document, CommandHistory& history)
	{
		if (data.empty()) {
			return false;
		}
		Reader r(data);
		std::uint8_t version = r.Value<std::uint8_t>();
		std::uint32_t fingerprint = r.Value<std::uint32_t>();
		if (!r.IsValid() || version != Version) {
			LOGW("History of the recovery copy is of another version, it's not restored");
			return false;
		}
		if (fingerprint != GetLayoutFingerprint(sizeof(SetTileOverrideCommand::State))) {
			LOGW("History of the recovery copy was made by another build, it's not restored");
			return false;
		}
		std::uint64_t count = r.VarUint();
		std::uint64_t doneCount = r.VarUint();
		std::uint64_t savedDone = r.VarUint();
		if (!r.IsValid() || doneCount > count || savedDone > count + 1 || count > r.GetRemaining()) {
			return false;
		}

		// The same new identity for a layer in all the commands
		LayerIdRemap idRemap;
		SmallVector<std::unique_ptr<EditCommand>, 0> commands;
		for (std::uint64_t i = 0; i < count; i++) {
			std::uint64_t size = r.VarUint();
			if (!r.IsValid() || size > r.GetRemaining()) {
				return false;
			}
			const std::uint8_t* recordEnd = r.GetPosition() + size;
			Reader record(ArrayView<const std::uint8_t>(r.GetPosition(), std::size_t(size)), &idRemap);
			std::unique_ptr<EditCommand> command = ReadCommand(record, document, 0);
			if (command == nullptr || !record.IsValid()) {
				LOGW("History of the recovery copy is damaged, it's not restored");
				return false;
			}
			commands.push_back(Death::move(command));
			r.SetPosition(recordEnd);
		}

		history.Clear();
		for (std::unique_ptr<EditCommand>& command : commands) {
			CommandHistory::Entry& entry = history._entries.emplace_back();
			entry.MemoryUsage = command->GetMemoryUsage();
			entry.Command = Death::move(command);
			entry.Revision = CommandHistory::_nextRevision++;
			history._memoryUsage += entry.MemoryUsage;
		}
		history._doneCount = std::int32_t(doneCount);
		if (savedDone == 0) {
			// The saved state isn't among the states kept
			history._savedRevision = 0;
		} else if (savedDone == 1) {
			history._savedRevision = history._baseRevision;
		} else {
			history._savedRevision = history._entries[std::size_t(savedDone - 2)].Revision;
		}
		history._mergeBroken = true;
		history.TrimToBudget();
		return true;
	}
}

#endif
