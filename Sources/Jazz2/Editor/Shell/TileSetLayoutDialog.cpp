#include "TileSetLayoutDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelFile.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Tiles in a row of the grid, as tile sets are drawn
		constexpr std::int32_t Columns = 10;
		constexpr float CellUnits = 18.0f;

		struct LayoutState
		{
			String TileSet;
			std::int32_t OriginalCount = 0;
			// The old position of the tile at each new position, -1 for a new empty tile
			SmallVector<std::int32_t, 0> Order;
			// Selected positions, inclusive
			std::int32_t SelectionStart = -1;
			std::int32_t SelectionEnd = -1;
			// Other levels that use the tile set, those in places that can be written to and the others
			SmallVector<String, 0> WritableLevels;
			SmallVector<String, 0> ReadOnlyLevels;

			bool HasSelection() const {
				return (SelectionStart >= 1 && SelectionEnd >= SelectionStart && SelectionEnd < std::int32_t(Order.size()));
			}
			bool IsChanged() const {
				if (std::int32_t(Order.size()) != OriginalCount) {
					return true;
				}
				for (std::int32_t i = 0; i < std::int32_t(Order.size()); i++) {
					if (Order[i] != i) {
						return true;
					}
				}
				return false;
			}
		};

		void CollectLevels(StringView path, SmallVectorImpl<String>& files)
		{
			if (!fs::DirectoryExists(path)) {
				return;
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
				if (StringUtils::lowercase(fs::GetExtension(item)) == "j2l"_s) {
					files.push_back(item);
				}
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
				CollectLevels(item, files);
			}
		}

		// Whether a level uses a tile set as its default one, read only from its header
		bool UsesTileSet(StringView path, StringView tileSet)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return false;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			return (reader.IsValid() && StringUtils::lowercase(properties.DefaultTileSet) == StringUtils::lowercase(tileSet));
		}

		void ScanLevels(EditorSession& session, LayoutState& state)
		{
			auto& resolver = ContentResolver::Get();
			SmallVector<String, 0> files;
			CollectLevels(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s), files);
			CollectLevels(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s), files);
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				CollectLevels(fs::CombinePath(userContent, "Episodes"_s), files);
			}
			std::sort(files.begin(), files.end());
			files.erase(std::unique(files.begin(), files.end()), files.end());

			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(true, roots);
			for (const String& file : files) {
				if (!UsesTileSet(file, state.TileSet)) {
					continue;
				}
				bool writable = false;
				for (const UiFileRoot& root : roots) {
					if (root.Writable && file.hasPrefix(root.Path)) {
						writable = true;
						break;
					}
				}
				(writable ? state.WritableLevels : state.ReadOnlyLevels).push_back(file);
			}
		}

		void BuildTileSet(const Formats::TileSetData& original, ArrayView<const std::int32_t> order, Formats::TileSetData& result)
		{
			result = Formats::TileSetData();
			std::memcpy(result.Palette, original.Palette, sizeof(result.Palette));
			result.Resize(std::int32_t(order.size()));
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				std::int32_t from = order[i];
				if (from < 0 || from >= original.TileCount) {
					continue;
				}
				std::memcpy(&result.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile],
					&original.Images[std::size_t(from) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
				std::memcpy(&result.Masks[std::size_t(i) * Formats::TileSetData::MaskBytesPerTile],
					&original.Masks[std::size_t(from) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
				result.TrueColor[i] = original.TrueColor[from];
			}
		}

		bool WriteTileSet(StringView path, const Formats::TileSetData& tileSet)
		{
			String tempPath = path + ".tmp"_s;
			bool written;
			{
				auto s = fs::Open(tempPath, FileAccess::Write);
				written = (s->IsValid() && Formats::TileSetFile::Write(*s, tileSet));
			}
			if (written && fs::FileExists(path)) {
				fs::RemoveFile(path);
			}
			if (!written || !fs::Move(tempPath, path)) {
				fs::RemoveFile(tempPath);
				return false;
			}
			return true;
		}

		// A level file changed to the new order of its tile set, written next to it first
		bool RewriteLevelFile(EditorSession& session, StringView path, ArrayView<const std::int32_t> order)
		{
			LevelDocument document;
			{
				auto s = fs::Open(path, FileAccess::Read);
				if (!s->IsValid() || !document.Load(*s, &session.GetTileSetLibrary())) {
					return false;
				}
			}
			if (RemapLevelToTileSetOrder(document, order) < 0) {
				return false;
			}
			String tempPath = path + ".tmp"_s;
			bool written;
			{
				auto s = fs::Open(tempPath, FileAccess::Write);
				written = (s->IsValid() && document.Save(*s, false, 9));
			}
			if (written && fs::FileExists(path)) {
				fs::RemoveFile(path);
			}
			if (!written || !fs::Move(tempPath, path)) {
				fs::RemoveFile(tempPath);
				return false;
			}
			return true;
		}

		// The level being edited follows the new order as one step, with the brush, and with a new tile set if it's given
		void ApplyToDocument(EditorSession& session, ArrayView<const std::int32_t> order, StringView newTileSet, StringView name)
		{
			LevelDocument& document = session.GetDocument();
			SnapshotParts parts = SnapshotParts::TileSets | SnapshotParts::Animations | SnapshotParts::Layers;
			if (!newTileSet.empty()) {
				parts |= SnapshotParts::Properties;
			}
			auto command = SnapshotCommand::Begin(document, parts, name);
			if (!newTileSet.empty()) {
				document.Properties.DefaultTileSet = newTileSet;
			}
			std::int32_t cleared = RemapLevelToTileSetOrder(document, order);

			SmallVector<std::int32_t, 0> newIndex;
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				if (order[i] >= 0) {
					if (std::size_t(order[i]) >= newIndex.size()) {
						newIndex.resize(std::size_t(order[i]) + 1, -1);
					}
					newIndex[order[i]] = i;
				}
			}
			TileStamp stamp = session.GetStamp();
			for (TileRef& tile : stamp.Tiles) {
				if (tile.Source == TileRef::DefaultTileSet && !tile.IsEmpty()) {
					std::int32_t index = (tile.Index < newIndex.size() ? newIndex[tile.Index] : -1);
					tile = (index > 0 ? TileRef{ std::uint16_t(index), TileRef::DefaultTileSet, tile.Flags } : TileRef{});
				}
			}
			session.SetStamp(std::move(stamp));

			DocumentChanges changes;
			command->End(document, changes);
			session.Execute(std::move(command));
			if (cleared > 0) {
				session.ShowMessage(_fn("{} tile that isn't in the tile set anymore was cleared", "{} tiles that aren't in the tile set anymore were cleared", cleared, cleared));
			}
		}

		/** The tiles in their new order, a click selects, with Shift a range */
		class LayoutGrid : public UiElement
		{
		public:
			LayoutGrid(EditorSession& session, std::shared_ptr<LayoutState> state)
				: _session(session), _state(std::move(state))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float cell = context.Px(CellUnits);
				std::int32_t rows = (std::int32_t(_state->Order.size()) + Columns - 1) / Columns;
				return Vector2f(cell * Columns, cell * rows);
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				UiElement::Arrange(context, bounds);
				_cell = context.Px(CellUnits);
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				LevelDocument& document = _session.GetDocument();
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				const Rectf& clip = painter.GetClip();
				for (std::int32_t i = 0; i < std::int32_t(_state->Order.size()); i++) {
					Rectf rect = GetCellRect(i);
					if (clip.W > 0.0f && (rect.Y + rect.H < clip.Y || rect.Y > clip.Y + clip.H)) {
						continue;
					}
					std::int32_t from = _state->Order[i];
					bool moved = (from != i);
					painter.FillRect(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f),
						from < 0 ? Colorf(0.2f, 0.4f, 0.25f, 0.5f) : Colorf(0.0f, 0.0f, 0.0f, 0.3f), UiLayer::Fill);
					if (from > 0 && tileMap != nullptr) {
						painter.Tile(*tileMap, document.ToTileId(TileRef{ std::uint16_t(from), TileRef::DefaultTileSet, 0 }), 0,
							Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f), 1.0f, UiLayer::Content);
					}
					if (moved && from >= 0) {
						// A dot in the corner of a tile that isn't where it was
						painter.FillRect(Rectf(rect.X + 2.0f, rect.Y + 2.0f, thickness * 3.0f, thickness * 3.0f), UiTheme::Accent, UiLayer::Overlay);
					}
					if (i >= _state->SelectionStart && i <= _state->SelectionEnd) {
						painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (index < 1) {
					// The empty tile stays first
					return false;
				}
				if ((event.Modifiers & KeyMod::Shift) != 0 && _state->SelectionStart >= 1) {
					std::int32_t anchor = _anchor;
					_state->SelectionStart = std::min(anchor, index);
					_state->SelectionEnd = std::max(anchor, index);
				} else {
					_anchor = index;
					_state->SelectionStart = _state->SelectionEnd = index;
				}
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = GetIndexAt(pos);
				if (index < 0) {
					return {};
				}
				std::int32_t from = _state->Order[index];
				_tooltip = (from < 0 ? String(_f("{}: a new empty tile", index)) : (from == index ? String(_f("Tile {}", index)) : String(_f("{}: tile {} before", index, from))));
				return _tooltip;
			}

		private:
			EditorSession& _session;
			std::shared_ptr<LayoutState> _state;
			float _cell = 0.0f;
			std::int32_t _anchor = -1;
			mutable String _tooltip;

			Rectf GetCellRect(std::int32_t index) const
			{
				return Rectf(Bounds.X + (index % Columns) * _cell, Bounds.Y + (index / Columns) * _cell, _cell, _cell);
			}

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				if (!Bounds.Contains(pos) || _cell <= 0.0f) {
					return -1;
				}
				std::int32_t x = std::int32_t((pos.X - Bounds.X) / _cell), y = std::int32_t((pos.Y - Bounds.Y) / _cell);
				std::int32_t index = y * Columns + x;
				return (x >= 0 && x < Columns && index >= 0 && index < std::int32_t(_state->Order.size()) ? index : -1);
			}
		};
	}

	std::int32_t RemapLevelToTileSetOrder(LevelDocument& document, ArrayView<const std::int32_t> order)
	{
		if (order.empty()) {
			return -1;
		}
		SmallVector<std::int32_t, 0> newIndex;
		for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
			if (order[i] >= 0) {
				if (std::size_t(order[i]) >= newIndex.size()) {
					newIndex.resize(std::size_t(order[i]) + 1, -1);
				}
				newIndex[order[i]] = i;
			}
		}
		LevelDocument::OverrideRefs overrides = document.CaptureOverrideRefs();
		document.DefaultTileCount = std::int32_t(order.size());
		Function<bool(TileRef&)> remap = [&newIndex](TileRef& tile) {
			if (tile.Source != TileRef::DefaultTileSet) {
				return true;
			}
			std::int32_t index = (tile.Index < newIndex.size() ? newIndex[tile.Index] : -1);
			if (index < 0) {
				return false;
			}
			tile.Index = std::uint16_t(index);
			return true;
		};
		std::int32_t cleared = document.RemapTiles(overrides, remap);
		document.FitTileIdSpace();
		return cleared;
	}

	void ShowTileSetLayoutDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		LevelDocument& document = session.GetDocument();
		const Formats::TileSetData* data = session.GetTileSetData(document.Properties.DefaultTileSet);
		if (data == nullptr) {
			session.ShowMessage(_("The tile set of the level can't be read"));
			return;
		}

		auto state = std::make_shared<LayoutState>();
		state->TileSet = document.Properties.DefaultTileSet;
		state->OriginalCount = data->TileCount;
		for (std::int32_t i = 0; i < data->TileCount; i++) {
			state->Order.push_back(i);
		}
		ScanLevels(session, *state);

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_f("Rearrange Tile Set \"{}\"", state->TileSet));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		content->Add<UiLabel>(_("Click a tile to select it, Shift+click selects a range"), UiTheme::TextDim);

		auto* scroll = content->Add<UiScrollView>();
		scroll->Height = 200.0f;
		scroll->SetContent<LayoutGrid>(session, state);

		auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto addOperation = [row, state](StringView text, EditorIcon icon, StringView tooltip, Function<void()>&& operation) {
			auto* button = row->Add<UiButton>(text, icon, std::move(operation));
			button->Style = UiButtonStyle::Flat;
			button->Tooltip = tooltip;
			button->IsEnabledFunc = [state]() { return state->HasSelection(); };
			return button;
		};
		addOperation(_("Earlier"), EditorIcon::ChevronLeft, _("Move the selected tiles a place earlier"), [state]() {
			auto& order = state->Order;
			if (state->SelectionStart <= 1) {
				return;
			}
			std::rotate(order.begin() + (state->SelectionStart - 1), order.begin() + state->SelectionStart, order.begin() + (state->SelectionEnd + 1));
			state->SelectionStart--;
			state->SelectionEnd--;
		});
		addOperation(_("Later"), EditorIcon::ChevronRight, _("Move the selected tiles a place later"), [state]() {
			auto& order = state->Order;
			if (state->SelectionEnd + 1 >= std::int32_t(order.size())) {
				return;
			}
			std::rotate(order.begin() + state->SelectionStart, order.begin() + (state->SelectionEnd + 1), order.begin() + (state->SelectionEnd + 2));
			state->SelectionStart++;
			state->SelectionEnd++;
		});
		addOperation(_("Delete"), EditorIcon::Delete, _("Remove the selected tiles, the tiles after them move up"), [state]() {
			auto& order = state->Order;
			order.erase(order.begin() + state->SelectionStart, order.begin() + (state->SelectionEnd + 1));
			state->SelectionEnd = state->SelectionStart = std::min(state->SelectionStart, std::int32_t(order.size()) - 1);
		});
		addOperation(_("Insert Empty"), EditorIcon::Plus, _("Put an empty tile before the selected ones"), [state]() {
			state->Order.insert(state->Order.begin() + state->SelectionStart, -1);
			state->SelectionStart++;
			state->SelectionEnd++;
		});
		auto* reset = row->Add<UiButton>(_("Reset"), EditorIcon::Undo, [state]() {
			state->Order.clear();
			for (std::int32_t i = 0; i < state->OriginalCount; i++) {
				state->Order.push_back(i);
			}
			state->SelectionStart = state->SelectionEnd = -1;
		});
		reset->Style = UiButtonStyle::Flat;

		auto* status = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
		status->TextFunc = [state]() {
			std::int32_t moved = 0, inserted = 0;
			SmallVector<bool, 0> kept;
			kept.resize(std::size_t(state->OriginalCount), false);
			for (std::int32_t i = 0; i < std::int32_t(state->Order.size()); i++) {
				std::int32_t from = state->Order[i];
				if (from < 0) {
					inserted++;
				} else {
					kept[from] = true;
					moved += (from != i ? 1 : 0);
				}
			}
			std::int32_t removed = 0;
			for (bool k : kept) {
				removed += (k ? 0 : 1);
			}
			return String(_f("{} tiles - {} in another place, {} removed, {} new empty ones", std::int32_t(state->Order.size()), moved, removed, inserted));
		};

		std::int32_t writable = std::int32_t(state->WritableLevels.size());
		std::int32_t readOnly = std::int32_t(state->ReadOnlyLevels.size());
		content->Add<UiLabel>(_f("Used by {} levels - {} of them are changed with the tile set", writable + readOnly, writable), UiTheme::TextDim);
		if (readOnly > 0) {
			content->Add<UiLabel>(_fn("{} level is in the files of the game and can't be changed, it would show other tiles",
				"{} levels are in the files of the game and can't be changed, they would show other tiles", readOnly, readOnly), UiTheme::TextDanger);
			content->Add<UiLabel>(_("Saving it as a new tile set changes only this level and leaves the others as they are"), UiTheme::TextDim);
		}

		auto* nameRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		nameRow->Add<UiLabel>(_("New name"), UiTheme::TextDim);
		auto* nameBox = nameRow->Add<UiTextBox>(String(state->TileSet + "-new"_s));
		nameBox->Flex = 1.0f;
		nameBox->MaxLength = 64;

		dialog->AddButton(_("Save as New Tile Set"), [s, state, nameBox, dialogPtr]() {
			if (!state->IsChanged()) {
				dialogPtr->Close();
				return;
			}
			String name = nameBox->Text.trimmed();
			if (!ContentTileSetLibrary::IsValidName(name)) {
				s->ShowMessage(_("The name of a tile set can have only letters, digits, - and _"));
				return;
			}
			if (!ContentTileSetLibrary::FindTileSetFile(name).empty()) {
				s->ShowMessage(_f("A tile set \"{}\" exists already", name));
				return;
			}
			const Formats::TileSetData* original = s->GetTileSetData(state->TileSet);
			String path = ContentTileSetLibrary::GetWritablePath(name, true);
			if (original == nullptr || path.empty()) {
				s->ShowMessage(_("There's no place to save tile sets to"));
				return;
			}
			Formats::TileSetData tileSet;
			BuildTileSet(*original, state->Order, tileSet);
			if (!WriteTileSet(path, tileSet)) {
				s->ShowMessage(_("The tile set couldn't be saved"));
				return;
			}
			s->InvalidateTileSet(name);
			ApplyToDocument(*s, state->Order, name, _("Rearrange tile set"));
			dialogPtr->Close();
			s->ShowMessage(_f("The level uses the new tile set \"{}\" now", name));
		}, true);
		dialog->AddButton(_("Change the Tile Set..."), [s, state, dialogPtr]() {
			if (!state->IsChanged()) {
				dialogPtr->Close();
				return;
			}
			std::int32_t writable = std::int32_t(state->WritableLevels.size());
			std::int32_t readOnly = std::int32_t(state->ReadOnlyLevels.size());
			String text = _f("The tile set and {} levels that use it are saved changed, this can't be undone.", writable);
			if (readOnly > 0) {
				text += "\n"_s + _fn("{} level can't be changed and would show other tiles.", "{} levels can't be changed and would show other tiles.", readOnly, readOnly);
			}
			UiMessageDialog::Show(*s->GetSurface(), _("Change the Tile Set"), text, { _("Change It"), _("Cancel") }, [s, state, dialogPtr](std::int32_t choice) {
				if (choice != 0) {
					return;
				}
				const Formats::TileSetData* original = s->GetTileSetData(state->TileSet);
				String path = ContentTileSetLibrary::GetWritablePath(state->TileSet, true);
				if (original == nullptr || path.empty()) {
					s->ShowMessage(_("There's no place to save tile sets to"));
					return;
				}
				Formats::TileSetData tileSet;
				BuildTileSet(*original, state->Order, tileSet);

				// The levels are changed while their tile set is still the old one, which they are read with
				std::int32_t failed = 0;
				for (const String& level : state->WritableLevels) {
					if (!RewriteLevelFile(*s, level, state->Order)) {
						LOGW("Level \"{}\" couldn't be changed to the new order of its tile set", level);
						failed++;
					}
				}
				if (!WriteTileSet(path, tileSet)) {
					s->ShowMessage(_("The tile set couldn't be saved"));
					return;
				}
				s->InvalidateTileSet(state->TileSet);
				ApplyToDocument(*s, state->Order, {}, _("Rearrange tile set"));
				dialogPtr->Close();
				std::int32_t changed = std::int32_t(state->WritableLevels.size()) - failed;
				s->ShowMessage(failed > 0
					? String(_f("The tile set was changed with {} levels, {} levels couldn't be changed", changed, failed))
					: String(_fn("The tile set was changed with {} level", "The tile set was changed with {} levels", changed, changed)));
			});
		});
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
