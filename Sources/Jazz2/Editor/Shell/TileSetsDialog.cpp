#include "TileSetsDialog.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "ShellCommon.h"
#include "TextPrompt.h"
#include "TileSetLayoutDialog.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Common/TileSource.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../Imaging/TileMasks.h"
#include "../Runtime/TileDrawing.h"
#include "../UI/UiMenu.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		struct TileSetsSelection
		{
			// Index of the selected extra tile set
			std::int32_t Index = 0;
			// Tile set shown in the preview, 0 for the default one, otherwise 1 + index of the extra one
			std::int32_t Preview = 0;
		};

		/**
		 * Changes the tile sets as one undoable step, with every tile of the level following the change
		 *
		 * @p remap tells where a tile of the tile sets as they were is now, or returns `false` if it's gone.
		 */
		void ChangeTileSets(IEditorContext& session, StringView name, bool changesProperties, Function<void(LevelDocument&)>&& change,
			Function<bool(TileRef&)>&& remap, Function<void(LevelDocument&)>&& prepare = {})
		{
			SnapshotParts parts = SnapshotParts::TileSets | SnapshotParts::Animations | SnapshotParts::Layers;
			if (changesProperties) {
				parts |= SnapshotParts::Properties;
			}
			std::int32_t cleared = 0;
			session.RecordSnapshotStep(name, parts, [&](LevelDocument& document) {
				// What goes before the tiles are followed, still with the tile sets as they were
				if (prepare) {
					prepare(document);
				}
				LevelDocument::OverrideRefs overrides = document.CaptureOverrideRefs();
				change(document);
				cleared = document.RemapTiles(overrides, remap);
				document.FitTileIdSpace();

				// The brush isn't part of the level, but it shouldn't paint other tiles than it shows either
				TileStamp stamp = session.GetStamp();
				bool stampChanged = false;
				for (TileRef& tile : stamp.Tiles) {
					TileRef before = tile;
					if (!tile.IsEmpty() && !remap(tile)) {
						tile = TileRef{};
					}
					stampChanged |= (tile != before);
				}
				if (stampChanged) {
					session.SetStamp(Death::move(stamp));
				}
				return true;
			});
			if (cleared > 0) {
				session.ShowMessage(_fnx("editor", "{} tile that isn't in the tile sets anymore was cleared", "{} tiles that aren't in the tile sets anymore were cleared", cleared, cleared));
			}
		}

		// Places tile sets are looked for, the first one that has a tile set is the one the game uses
		void GetTileSetRoots(SmallVectorImpl<UiFileRoot>& roots)
		{
			auto& resolver = ContentResolver::Get();
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				// TRANSLATORS: Name of a place tile sets are picked from, the folder of the content added by the user
				roots.push_back({ _x("editor", "User content"), fs::CombinePath(userContent, "Tilesets"_s), false });
			}
			// TRANSLATORS: Name of a place tile sets are picked from, the folder of the content of the game
			roots.push_back({ _x("editor", "Content"), fs::CombinePath(resolver.GetContentPath(), "Tilesets"_s), false });
			// TRANSLATORS: Name of a place tile sets are picked from, the folder of the files converted from the original game
			roots.push_back({ _x("editor", "Cache"), fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s), false });
			for (std::size_t i = 0; i < roots.size(); ) {
				if (!fs::DirectoryExists(roots[i].Path)) {
					roots.erase(roots.begin() + i);
				} else {
					i++;
				}
			}
		}

		// Lets a tile set be picked, the name it's referred to by is passed on
		void PickTileSet(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			GetTileSetRoots(roots);
			SmallVector<String, 4> rootPaths;
			for (const UiFileRoot& root : roots) {
				rootPaths.push_back(root.Path);
			}
			String directory, fileName;
			String currentPath = ContentTileSetLibrary::FindTileSetFile(current);
			if (!current.empty() && !currentPath.empty()) {
				directory = fs::GetDirectoryName(currentPath);
				fileName = fs::GetFileName(currentPath);
			}
			UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, Death::move(roots), ".j2t"_s, directory, fileName,
				[rootPaths = Death::move(rootPaths), onPicked = Death::move(onPicked)](StringView path) mutable {
					// Tile sets are referred to by their path relative to "Tilesets", without the extension
					StringView name = fs::GetFileName(path);
					for (const String& rootPath : rootPaths) {
						// As the file system compares paths, the dialog may give the path in another case
						if (IsPathInside(path, rootPath) && path.size() > rootPath.size()) {
							name = path.exceptPrefix(rootPath.size());
							while (!name.empty() && (name[0] == '/' || name[0] == '\\')) {
								name = name.exceptPrefix(1);
							}
							break;
						}
					}
					String result = fs::FromNativeSeparators(name);
					if (StringUtils::lowercase(result).hasSuffix(".j2t"_s)) {
						result = result.exceptSuffix(std::size_t(4));
					}
					onPicked(result);
				});
		}

		// Returns whether a tile set has any true color tile, which isn't remapped to the palette
		bool HasTrueColorTiles(StringView name)
		{
			String path = ContentTileSetLibrary::FindTileSetFile(name);
			if (path.empty()) {
				return false;
			}
			auto s = fs::Open(path, FileAccess::Read);
			Formats::TileSetHeader header;
			return (s->IsValid() && Formats::TileSetFile::ReadHeader(*s, header) && header.ChannelCount == 4);
		}

		// Number of tiles in the file of an extra tile set, or in the level if it's stored there, -1 if it can't be read
		std::int32_t GetTileCountOf(IEditorContext& session, const Formats::ExtraTileSetRecord& tileSet)
		{
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
				const Formats::TileSetData* data = session.GetTileSetData(tileSet.Name);
				return (data != nullptr ? data->TileCount : -1);
			}
			return session.GetTileSetLibrary().GetTileCount(tileSet.Name);
		}

		// Matches the colors of an extra tile set to the palette the level is shown with
		bool MatchColors(IEditorContext& session, Formats::ExtraTileSetRecord& tileSet)
		{
			LevelDocument& document = session.GetDocument();
			ContentTileSetLibrary& library = session.GetTileSetLibrary();
			std::uint32_t from[256], to[256];
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
				const Formats::TileSetData* data = session.GetTileSetData(tileSet.Name);
				if (data == nullptr) {
					return false;
				}
				std::memcpy(from, data->Palette, sizeof(from));
			} else if (!library.ReadPalette(tileSet.Name, from)) {
				return false;
			}
			if ((document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette) {
				std::memcpy(to, document.LevelPalette, sizeof(to));
			} else if (!library.ReadPalette(document.Properties.DefaultTileSet, to)) {
				return false;
			}
			MatchPalette(from, to, tileSet.Remapping);
			return true;
		}

		// Whether the palette of a tile set file differs from the one the level is shown in, its 8-bit tiles would show
		// other colors then without being matched
		bool HasOtherPalette(IEditorContext& session, StringView name)
		{
			std::uint32_t own[256], shown[256];
			if (!session.GetTileSetLibrary().ReadPalette(name, own)) {
				return false;
			}
			GetLevelPalette(session, shown);
			// The first entry is always transparent, its color doesn't matter
			for (std::int32_t i = 1; i < 256; i++) {
				if ((own[i] & 0x00FFFFFFu) != (shown[i] & 0x00FFFFFFu)) {
					return true;
				}
			}
			return false;
		}

		/** Row of the list of extra tile sets */
		class ExtraTileSetRow : public UiElement
		{
		public:
			ExtraTileSetRow(IEditorContext& session, std::shared_ptr<TileSetsSelection> selection, std::int32_t index)
				: _session(session), _selection(Death::move(selection)), _index(index)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::RowHeight + 2.0f));
			}

			void Draw(UiPainter& painter) override
			{
				LevelDocument& document = _session.GetDocument();
				if (_index >= std::int32_t(document.ExtraTileSets.size())) {
					return;
				}
				const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[_index];
				if (_selection->Index == _index) {
					painter.RoundRect(Bounds, UiTheme::Selection);
				} else if (_hoverAmount > 0.0f) {
					Colorf hover = UiTheme::Hover;
					hover.A *= _hoverAmount;
					painter.RoundRect(Bounds, hover);
				}
				float padding = painter.Px(UiTheme::Padding);
				Rectf rect(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H);
				String range = FormatInteger(tileSet.Offset) + " - "_s + FormatInteger(tileSet.Offset + tileSet.Count - 1);
				float rangeWidth = painter.MeasureText(range);
				painter.PushClip(Rectf(rect.X, rect.Y, std::max(rect.W - rangeWidth - padding, 0.0f), rect.H));
				painter.TextInRect(String(FormatInteger(_index + 1) + ". "_s + GetTileSetLabel(tileSet.Name)), rect, UiTheme::Text);
				painter.PopClip();
				painter.TextInRect(range, rect, UiTheme::TextDim, Alignment::Right);
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				if (event.Button == MouseButton::Left) {
					_selection->Index = _index;
					_selection->Preview = _index + 1;
				}
				return false;
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<TileSetsSelection> _selection;
			std::int32_t _index;
		};

		/** Tiles of the tile set shown in the preview as the level shows them, as many in a row as fit */
		class TileSetPreviewGrid : public UiElement
		{
		public:
			static constexpr float CellUnits = 22.0f;

			TileSetPreviewGrid(IEditorContext& session, std::shared_ptr<TileSetsSelection> selection)
				: _session(session), _selection(Death::move(selection)), _cell(1.0f), _columns(1)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				_cell = context.Px(CellUnits);
				_columns = std::max<std::int32_t>(1, std::int32_t(available.X / _cell));
				std::int32_t rows = std::max<std::int32_t>(1, (GetCount() + _columns - 1) / _columns);
				return Vector2f(available.X, rows * _cell);
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				// The size may have been measured for another width last, the columns are those of the width it got
				UiElement::Arrange(context, bounds);
				_cell = context.Px(CellUnits);
				_columns = std::max<std::int32_t>(1, std::int32_t(bounds.W / _cell));
			}

			void Draw(UiPainter& painter) override
			{
				painter.RoundRect(Bounds, UiTheme::InputFill);
				Tiles::TileMap* tileMap = _session.GetTileMap();
				std::int32_t count = GetCount();
				if (tileMap == nullptr || count <= 0) {
					return;
				}
				const LevelDocument& document = _session.GetDocument();
				std::uint8_t source = GetSource();
				float inset = std::max(1.0f, painter.Px(1.0f));
				// Only the rows in view, a tile set can have thousands of tiles
				std::int32_t firstRow = 0, lastRow = (count + _columns - 1) / _columns;
				const Rectf& clip = painter.GetClip();
				if (clip.W > 0.0f && clip.H > 0.0f) {
					firstRow = std::max<std::int32_t>(firstRow, std::int32_t((clip.Y - Bounds.Y) / _cell));
					lastRow = std::min<std::int32_t>(lastRow, std::int32_t((clip.Y + clip.H - Bounds.Y) / _cell) + 1);
				}
				for (std::int32_t row = firstRow; row < lastRow; row++) {
					for (std::int32_t column = 0; column < _columns; column++) {
						std::int32_t i = row * _columns + column;
						if (i >= count) {
							break;
						}
						Rectf rect(Bounds.X + column * _cell + inset, Bounds.Y + row * _cell + inset, _cell - inset * 2.0f, _cell - inset * 2.0f);
						DrawTile(painter, *tileMap, document, TileRef{ std::uint16_t(i), source, 0 }, 0, rect);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t column = std::int32_t((pos.X - Bounds.X) / _cell);
				std::int32_t row = std::int32_t((pos.Y - Bounds.Y) / _cell);
				std::int32_t index = row * _columns + column;
				if (pos.X < Bounds.X || pos.Y < Bounds.Y || column >= _columns || index >= GetCount()) {
					return {};
				}
				std::uint8_t source = GetSource();
				std::int32_t tileId = _session.GetDocument().ToTileId(TileRef{ std::uint16_t(index), source, 0 });
				if (source == TileRef::DefaultTileSet) {
					// TRANSLATORS: Tooltip of a tile in the preview of the Tile Sets dialog, with its index in the tile set and its tile ID in the level
					_tooltip = _fx("editor", "Tile {} - tile ID {}", index, tileId);
				} else {
					// The tile of the file it is, the level takes only a part of it
					std::int32_t fileIndex = index + _session.GetDocument().ExtraTileSets[source - 1].Offset;
					// TRANSLATORS: Tooltip of a tile in the preview of the Tile Sets dialog, with its index in the tile set file and its tile ID in the level
					_tooltip = _fx("editor", "Tile {} of the file - tile ID {}", fileIndex, tileId);
				}
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<TileSetsSelection> _selection;
			float _cell;
			std::int32_t _columns;
			mutable String _tooltip;

			std::uint8_t GetSource() const
			{
				std::int32_t preview = _selection->Preview;
				std::int32_t extraCount = std::int32_t(_session.GetDocument().ExtraTileSets.size());
				return std::uint8_t(preview > 0 && preview <= extraCount && preview <= TileRef::LastExtraTileSet ? preview : TileRef::DefaultTileSet);
			}

			std::int32_t GetCount() const
			{
				return std::max<std::int32_t>(_session.GetDocument().GetSourceTileCount(GetSource()), 0);
			}
		};

		/** The window of the tile sets, the preview of the tiles takes what the window can grow by */
		class TileSetsWindow : public UiDialog
		{
			DEATH_RUNTIME_OBJECT(UiDialog);

		public:
			UiScrollView* List = nullptr;
			UiElement* PreviewColumn = nullptr;
			UiScrollView* Preview = nullptr;

			TileSetsWindow()
				// TRANSLATORS: Title of the dialog of the tile sets the level uses
				: UiDialog(_x("editor", "Tile Sets"), true)
			{
				MaxScreenHeightFraction = 0.9f;
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float scale = std::max(context.Scale, 0.5f);
				Vector2f units(available.X / scale, available.Y * MaxScreenHeightFraction / scale);
				if (Preview != nullptr) {
					// Next to the settings, which take about 300 units with the padding, and as tall as the window may be
					// without the title, the header of the preview and the padding
					Preview->Width = std::floor(std::clamp(std::min(units.X * 0.45f, units.X - 340.0f), 120.0f, 640.0f));
					Preview->Height = std::floor(std::clamp(units.Y - 110.0f, 120.0f, 640.0f));
					if (PreviewColumn != nullptr) {
						PreviewColumn->Width = Preview->Width;
					}
				}
				if (List != nullptr) {
					List->Height = std::floor(std::clamp(units.Y * 0.2f, 80.0f, 200.0f));
				}
				return UiDialog::Measure(context, available);
			}
		};

		/** The list of extra tile sets, rebuilt when their number changes */
		class ExtraTileSetList : public UiStack
		{
		public:
			ExtraTileSetList(IEditorContext& session, std::shared_ptr<TileSetsSelection> selection)
				: UiStack(UiOrientation::Vertical, 1.0f), _session(session), _selection(Death::move(selection)), _shownCount(-1)
			{
			}

			void Update(float timeMult) override
			{
				std::int32_t count = std::int32_t(_session.GetDocument().ExtraTileSets.size());
				if (count != _shownCount) {
					_shownCount = count;
					ClearChildren();
					for (std::int32_t i = 0; i < count; i++) {
						Add<ExtraTileSetRow>(_session, _selection, i);
					}
					if (count == 0) {
						Add<UiLabel>(_x("editor", "The level has no extra tile sets"), UiTheme::TextDim);
					}
				}
				UiStack::Update(timeMult);
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<TileSetsSelection> _selection;
			std::int32_t _shownCount;
		};
	}

	bool SetDefaultTileSet(IEditorContext& session, StringView name)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t tileCount = session.GetTileSetLibrary().GetTileCount(name);
		if (tileCount <= 0) {
			session.ShowMessage(_x("editor", "The tile set can't be read"));
			return false;
		}
		if (name == document.Properties.DefaultTileSet) {
			return true;
		}
		if (!CanAddTileIds(session, tileCount - std::max<std::int32_t>(document.DefaultTileCount, 0))) {
			return false;
		}
		// The tiles of the default tile set keep their indices, so they show what is there in the new one
		String newName = name;
		// TRANSLATORS: Name of an undo step in the history
		ChangeTileSets(session, _x("editor", "Change default tile set"), true, [&newName, tileCount](LevelDocument& document) {
			document.Properties.DefaultTileSet = Death::move(newName);
			document.DefaultTileCount = tileCount;
		}, [](TileRef&) { return true; });
		return true;
	}

	bool AddExtraTileSet(IEditorContext& session, StringView name, std::int32_t matchColors)
	{
		if (session.GetDocument().ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
			session.ShowMessage(_x("editor", "A level can't have more extra tile sets"));
			return false;
		}
		std::int32_t tileCount = session.GetTileSetLibrary().GetTileCount(name);
		if (tileCount <= 0) {
			session.ShowMessage(_x("editor", "The tile set can't be read"));
			return false;
		}
		// All of its tiles after the tiles of the tile sets before it, it doesn't move any of them
		Formats::ExtraTileSetRecord tileSet = {};
		tileSet.Name = name;
		tileSet.Offset = 0;
		tileSet.Count = std::uint16_t(std::min<std::int32_t>(tileCount, UINT16_MAX));
		if (!CanAddTileIds(session, tileSet.Count)) {
			return false;
		}
		bool matched = false;
		if (HasTrueColorTiles(name)) {
			tileSet.Flags |= Formats::ExtraTileSetRecord::TrueColor;
		} else if ((matchColors > 0 || (matchColors < 0 && HasOtherPalette(session, name))) && MatchColors(session, tileSet)) {
			// The 8-bit tiles of a tile set of another palette would show the colors of the palette of the level
			// at their indices, so they get the closest colors of it instead
			tileSet.Flags |= Formats::ExtraTileSetRecord::Remapped;
			matched = true;
		}
		// TRANSLATORS: Name of an undo step in the history
		ChangeTileSets(session, _x("editor", "Add tile set"), false, [&tileSet](LevelDocument& document) {
			document.ExtraTileSets.push_back(Death::move(tileSet));
		}, [](TileRef&) { return true; });
		if (matched) {
			session.ShowMessage(_x("editor", "The tile set has another palette, its colors were matched to the palette of the level"));
		}
		return true;
	}

	void ShowTileSetsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		auto selection = std::make_shared<TileSetsSelection>();
		auto dialog = std::make_unique<TileSetsWindow>();
		// The settings on the left, the tiles of a tile set on the right
		auto* columns = dialog->GetContent()->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		auto* content = columns->Add<UiForm>(UiTheme::Spacing);
		content->Width = 280.0f;
		columns->Add<UiSeparator>(UiOrientation::Vertical);
		{
			auto* previewColumn = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			dialog->PreviewColumn = previewColumn;
			auto* header = previewColumn->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Header of the preview of the tiles of a tile set in the Tile Sets dialog, a noun
			header->Add<UiLabel>(_x("editor", "Preview"), UiTheme::TextHeader);
			auto* shown = header->Add<UiDropdown>([s, selection]() {
				const LevelDocument& document = s->GetDocument();
				std::int32_t preview = selection->Preview;
				if (preview > 0 && preview <= std::int32_t(document.ExtraTileSets.size())) {
					return String(FormatInteger(preview) + ". "_s + GetTileSetLabel(document.ExtraTileSets[preview - 1].Name));
				}
				return String(_("Default") + " - "_s + document.Properties.DefaultTileSet);
			}, [s, selection](SmallVectorImpl<UiMenuEntry>& entries) {
				const LevelDocument& document = s->GetDocument();
				for (std::int32_t i = 0; i <= std::int32_t(document.ExtraTileSets.size()); i++) {
					UiMenuEntry entry;
					entry.Label = (i == 0 ? String(_("Default") + " - "_s + document.Properties.DefaultTileSet)
						: String(FormatInteger(i) + ". "_s + GetTileSetLabel(document.ExtraTileSets[i - 1].Name)));
					entry.IsChecked = [selection, i]() { return (selection->Preview == i); };
					entry.Execute = [selection, i]() {
						selection->Preview = i;
						if (i > 0) {
							selection->Index = i - 1;
						}
					};
					entries.push_back(Death::move(entry));
				}
			});
			shown->Flex = 1.0f;
			shown->Tooltip = _x("editor", "Tile set whose tiles are shown, as the level takes them");
			auto* previewScroll = previewColumn->Add<UiScrollView>();
			previewScroll->SetContent<TileSetPreviewGrid>(session, selection);
			dialog->Preview = previewScroll;
			auto* summary = previewColumn->Add<UiLabel>(StringView(), UiTheme::TextDim);
			summary->TextFunc = [s, selection]() {
				const LevelDocument& document = s->GetDocument();
				std::int32_t preview = selection->Preview;
				if (preview > 0 && preview <= std::int32_t(document.ExtraTileSets.size())) {
					const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[preview - 1];
					std::int32_t first = document.ToTileId(TileRef{ 0, std::uint8_t(preview), 0 });
					// TRANSLATORS: Below the preview in the Tile Sets dialog, the first and the last tile ID of the tile set shown
					return _fx("editor", "Tile IDs {} - {}", first, first + tileSet.Count - 1);
				}
				if (document.DefaultTileCount <= 0) {
					return String(_x("editor", "The tile set can't be read"));
				}
				return _fx("editor", "Tile IDs {} - {}", 0, document.DefaultTileCount - 1);
			};
		}

		auto selected = [s, selection]() -> Formats::ExtraTileSetRecord* {
			LevelDocument& document = s->GetDocument();
			return (selection->Index >= 0 && selection->Index < std::int32_t(document.ExtraTileSets.size())
				? &document.ExtraTileSets[selection->Index] : nullptr);
		};

		{
			auto* row = content->AddRow(_("Default"));
			auto* name = row->Add<UiLabel>();
			name->Flex = 1.0f;
			name->TextFunc = [s]() {
				const LevelDocument& document = s->GetDocument();
				String text = document.Properties.DefaultTileSet;
				if (document.DefaultTileCount >= 0) {
					text += " ("_s + _fnx("editor", "{} tile", "{} tiles", document.DefaultTileCount, document.DefaultTileCount) + ")"_s;
				}
				return text;
			};
			auto* pick = row->Add<UiButton>(StringView(), EditorIcon::Folder, [s]() {
				// TRANSLATORS: Title of the file picker that chooses the default tile set of the level
				PickTileSet(*s, _x("editor", "Default Tile Set"), s->GetDocument().Properties.DefaultTileSet, [s](StringView name) {
					SetDefaultTileSet(*s, name);
				});
			});
			pick->Tooltip = _x("editor", "Choose another default tile set, the tiles keep their positions in it");
		}

		content->Add<UiSeparator>();
		// TRANSLATORS: Header of the list of the tile sets the level uses besides the default one
		content->Add<UiLabel>(_x("editor", "Extra tile sets"), UiTheme::TextHeader);
		auto* listScroll = content->Add<UiScrollView>();
		listScroll->Height = 80.0f;
		listScroll->SetContent<ExtraTileSetList>(session, selection);
		dialog->List = listScroll;

		{
			auto* buttons = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Button below the list of extra tile sets in the Tile Sets dialog, a verb
			buttons->Add<UiButton>(_x("editor", "Add"), EditorIcon::Plus, [s, selection]() {
				if (s->GetDocument().ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
					s->ShowMessage(_x("editor", "A level can't have more extra tile sets"));
					return;
				}
				// TRANSLATORS: Title of the file picker and the question that add an extra tile set to the level
				PickTileSet(*s, _x("editor", "Add Tile Set"), {}, [s, selection](StringView name) {
					String nameCopy = name;
					auto add = [s, selection, nameCopy](std::int32_t matchColors) {
						if (AddExtraTileSet(*s, nameCopy, matchColors)) {
							selection->Index = std::int32_t(s->GetDocument().ExtraTileSets.size()) - 1;
							selection->Preview = selection->Index + 1;
						}
					};
					// An 8-bit tile set of another palette shows other colors unless it's matched, the choice is the user's
					if (HasTrueColorTiles(nameCopy) || !HasOtherPalette(*s, nameCopy) || s->GetSurface() == nullptr) {
						add(-1);
						return;
					}
					UiMessageDialog::Show(*s->GetSurface(), _x("editor", "Add Tile Set"),
						_x("editor", "The tile set has another palette than the level. Its tiles can get the closest colors of the palette of the level, "
							"otherwise they show the colors the palette of the level has at their indices."),
						// TRANSLATORS: Buttons of the question when an added tile set has another palette, match its colors to the level palette or keep its color indices
						{ _x("editor", "Match Colors"), _x("editor", "Keep Indices"), _x("editor", "Cancel") }, [add](std::int32_t choice) {
						if (choice == 0 || choice == 1) {
							add(choice == 0 ? 1 : 0);
						}
					});
				});
			});
			auto* remove = buttons->Add<UiButton>(StringView(), EditorIcon::Delete, [s, selection, selected]() mutable {
				if (selected() == nullptr) {
					return;
				}
				std::uint8_t source = std::uint8_t(selection->Index + 1);
				std::int32_t index = selection->Index;
				// TRANSLATORS: Name of an undo step in the history
				ChangeTileSets(*s, _x("editor", "Remove tile set"), false, [index](LevelDocument& document) {
					document.ExtraTileSets.erase(document.ExtraTileSets.begin() + index);
				}, [source](TileRef& tile) {
					// Tiles of the removed tile set are gone, those of the tile sets after it move down
					if (tile.Source == source) {
						return false;
					}
					if (tile.Source > source && tile.Source <= TileRef::LastExtraTileSet) {
						tile.Source--;
					}
					return true;
				});
				selection->Index = std::max<std::int32_t>(0, std::min<std::int32_t>(index, std::int32_t(s->GetDocument().ExtraTileSets.size()) - 1));
				// The preview of the removed one goes back to the default tile set, others keep showing the same tile set
				if (selection->Preview == source) {
					selection->Preview = 0;
				} else if (selection->Preview > source) {
					selection->Preview--;
				}
			});
			remove->Tooltip = _x("editor", "Remove the tile set, its tiles are cleared from the level");
			remove->IsEnabledFunc = [selected]() mutable { return (selected() != nullptr); };

			for (std::int32_t direction : { -1, 1 }) {
				auto* move = buttons->Add<UiButton>(StringView(), direction < 0 ? EditorIcon::ArrowUp : EditorIcon::ArrowDown, [s, selection, direction]() {
					LevelDocument& document = s->GetDocument();
					std::int32_t index = selection->Index;
					std::int32_t target = index + direction;
					if (index < 0 || target < 0 || target >= std::int32_t(document.ExtraTileSets.size())) {
						return;
					}
					std::uint8_t a = std::uint8_t(index + 1), b = std::uint8_t(target + 1);
					// TRANSLATORS: Name of an undo step in the history
					ChangeTileSets(*s, _x("editor", "Move tile set"), false, [index, target](LevelDocument& document) {
						std::swap(document.ExtraTileSets[index], document.ExtraTileSets[target]);
					}, [a, b](TileRef& tile) {
						if (tile.Source == a) {
							tile.Source = b;
						} else if (tile.Source == b) {
							tile.Source = a;
						}
						return true;
					});
					selection->Index = target;
					if (selection->Preview == a) {
						selection->Preview = b;
					} else if (selection->Preview == b) {
						selection->Preview = a;
					}
				});
				move->Tooltip = (direction < 0 ? _x("editor", "Move up, its tiles get lower tile IDs") : _x("editor", "Move down, its tiles get higher tile IDs"));
				move->IsEnabledFunc = [s, selection, direction]() {
					std::int32_t target = selection->Index + direction;
					return (selection->Index >= 0 && target >= 0 && target < std::int32_t(s->GetDocument().ExtraTileSets.size()));
				};
			}
		}

		// The selected extra tile set
		auto hasSelected = [selected]() mutable { return (selected() != nullptr); };
		{
			// TRANSLATORS: Label of the name of the extra tile set selected in the Tile Sets dialog
			auto* row = content->AddRow(_x("editor", "Tile set"));
			auto* name = row->Add<UiLabel>();
			name->Flex = 1.0f;
			name->TextFunc = [selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr ? GetTileSetLabel(tileSet->Name) : String());
			};
			auto* pick = row->Add<UiButton>(StringView(), EditorIcon::Folder, [s, selection, selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				if (tileSet == nullptr) {
					return;
				}
				std::int32_t index = selection->Index;
				// TRANSLATORS: Title of the file picker that chooses another tile set in place of the selected extra tile set
				PickTileSet(*s, _x("editor", "Replace Tile Set"), tileSet->Name, [s, index](StringView name) {
					LevelDocument& document = s->GetDocument();
					std::int32_t tileCount = s->GetTileSetLibrary().GetTileCount(name);
					if (tileCount <= 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
						s->ShowMessage(_x("editor", "The tile set can't be read"));
						return;
					}
					// The same part of the other tile set, as much of it as there is
					Formats::ExtraTileSetRecord replaced = document.ExtraTileSets[index];
					replaced.Name = name;
					// The tiles the level had in itself are gone then
					replaced.Flags &= ~Formats::ExtraTileSetRecord::Embedded;
					replaced.Data.clear();
					replaced.Offset = std::uint16_t(std::min<std::int32_t>(replaced.Offset, tileCount - 1));
					replaced.Count = std::uint16_t(std::min<std::int32_t>(replaced.Count, tileCount - replaced.Offset));
					// The colors of the other tile set are matched to the palette again, its own palette is another one;
					// a true color tile set isn't matched at all, the flag would mean an alternate palette for it
					bool wasMatched = ((replaced.Flags & Formats::ExtraTileSetRecord::Remapped) != 0);
					replaced.Flags &= ~(Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor);
					if (HasTrueColorTiles(name)) {
						replaced.Flags |= Formats::ExtraTileSetRecord::TrueColor;
					} else if ((wasMatched || HasOtherPalette(*s, name)) && MatchColors(*s, replaced)) {
						replaced.Flags |= Formats::ExtraTileSetRecord::Remapped;
					}
					// TRANSLATORS: Name of an undo step in the history
					ChangeTileSets(*s, _x("editor", "Replace tile set"), false, [index, &replaced](LevelDocument& document) {
						document.ExtraTileSets[index] = Death::move(replaced);
					}, [](TileRef&) { return true; });
				});
			});
			pick->Tooltip = _x("editor", "Use another tile set in its place");
			pick->IsEnabledFunc = hasSelected;
		}
		{
			// TRANSLATORS: Label of the first tile of the file the level takes from the selected extra tile set
			auto* row = content->AddRow(_x("editor", "First tile"));
			auto* number = row->Add<UiNumberBox>([selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr ? float(tileSet->Offset) : 0.0f);
			}, [s, selection](float value, bool final) {
				LevelDocument& document = s->GetDocument();
				std::int32_t index = selection->Index;
				if (!final || index < 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
					return;
				}
				const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[index];
				std::int32_t fileTileCount = GetTileCountOf(*s, tileSet);
				std::int32_t offset = std::max<std::int32_t>(0, std::int32_t(std::round(value)));
				if (fileTileCount > 0) {
					offset = std::min<std::int32_t>(offset, fileTileCount - 1);
				}
				if (offset == tileSet.Offset) {
					return;
				}
				// The tiles keep showing the same pictures, those that aren't taken anymore are cleared
				std::uint8_t source = std::uint8_t(index + 1);
				std::int32_t delta = std::int32_t(tileSet.Offset) - offset;
				// TRANSLATORS: Name of an undo step in the history, the range of tiles taken from an extra tile set was changed
				ChangeTileSets(*s, _x("editor", "Tile set range"), false, [index, offset, fileTileCount](LevelDocument& document) {
					Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[index];
					tileSet.Offset = std::uint16_t(offset);
					if (fileTileCount > 0) {
						tileSet.Count = std::uint16_t(std::max<std::int32_t>(1, std::min<std::int32_t>(tileSet.Count, fileTileCount - offset)));
					}
				}, [source, delta](TileRef& tile) {
					if (tile.Source == source) {
						std::int32_t newIndex = std::int32_t(tile.Index) + delta;
						if (newIndex < 0) {
							return false;
						}
						tile.Index = std::uint16_t(newIndex);
					}
					return true;
				});
			}, 0.0f, 65535.0f, 1.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _x("editor", "First tile taken from the tile set");
			number->IsEnabledFunc = hasSelected;
		}
		{
			// TRANSLATORS: Label of the number of tiles the level takes from the selected extra tile set
			auto* row = content->AddRow(_x("editor", "Tiles"));
			auto* number = row->Add<UiNumberBox>([selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr ? float(tileSet->Count) : 0.0f);
			}, [s, selection](float value, bool final) {
				LevelDocument& document = s->GetDocument();
				std::int32_t index = selection->Index;
				if (!final || index < 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
					return;
				}
				const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[index];
				std::int32_t fileTileCount = GetTileCountOf(*s, tileSet);
				std::int32_t count = std::clamp<std::int32_t>(std::int32_t(std::round(value)), 1, UINT16_MAX);
				if (fileTileCount > 0) {
					count = std::min<std::int32_t>(count, fileTileCount - tileSet.Offset);
				}
				if (count == tileSet.Count || !CanAddTileIds(*s, count - tileSet.Count)) {
					return;
				}
				// Tiles of the tile sets after it move with it, those past the end of it are cleared
				ChangeTileSets(*s, _x("editor", "Tile set range"), false, [index, count](LevelDocument& document) {
					document.ExtraTileSets[index].Count = std::uint16_t(count);
				}, [](TileRef&) { return true; });
			}, 1.0f, 65535.0f, 1.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _x("editor", "Number of tiles taken from the tile set");
			number->IsEnabledFunc = hasSelected;
		}
		{
			auto* check = content->Add<UiCheckBox>(_x("editor", "Match colors to the level palette"), [selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr && (tileSet->Flags & Formats::ExtraTileSetRecord::Remapped) != 0);
			}, [s, selection](bool value) {
				LevelDocument& document = s->GetDocument();
				std::int32_t index = selection->Index;
				if (index < 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
					return;
				}
				Formats::ExtraTileSetRecord tileSet = document.ExtraTileSets[index];
				bool embedded = ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0);
				if (value) {
					if (embedded) {
						// Its tiles are in the palette of the level already, as they were made
						return;
					}
					if (!MatchColors(*s, tileSet)) {
						s->ShowMessage(_x("editor", "The palettes can't be read"));
						return;
					}
					tileSet.Flags |= Formats::ExtraTileSetRecord::Remapped;
				} else {
					if (embedded && (tileSet.Flags & Formats::ExtraTileSetRecord::Remapped) != 0) {
						// The tiles of the level keep the colors they were shown in, in the indices of the palette
						const Formats::TileSetData* data = s->GetTileSetData(tileSet.Name);
						SmallVector<std::uint8_t, 0> bytes;
						if (data == nullptr) {
							s->ShowMessage(_x("editor", "The tile set can't be read"));
							return;
						}
						Formats::TileSetData baked = *data;
						BakeRemapping(baked, tileSet.Remapping);
						if (!SerializeTileSet(baked, bytes)) {
							s->ShowMessage(_x("editor", "The tile set couldn't be saved"));
							return;
						}
						tileSet.Data = Death::move(bytes);
					}
					tileSet.Flags &= ~Formats::ExtraTileSetRecord::Remapped;
				}
				// Only the colors change, all tiles stay where they are
				// TRANSLATORS: Name of an undo step in the history, the colors of an extra tile set were matched or unmatched to the level palette
				ChangeTileSets(*s, _x("editor", "Tile set colors"), false, [index, &tileSet](LevelDocument& document) {
					document.ExtraTileSets[index] = Death::move(tileSet);
				}, [](TileRef&) { return true; });
			});
			check->Tooltip = _x("editor", "Each color of the tile set is replaced by the closest one of the palette of the level, "
				"otherwise its colors are the entries of the level palette of the same index");
			check->IsEnabledFunc = [selected]() mutable {
				// The tiles of the level are made in its palette, they can only stop being matched
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr && (tileSet->Flags & Formats::ExtraTileSetRecord::TrueColor) == 0 &&
					((tileSet->Flags & Formats::ExtraTileSetRecord::Embedded) == 0 || (tileSet->Flags & Formats::ExtraTileSetRecord::Remapped) != 0));
			};
		}

		content->Add<UiSeparator>();
		{
			auto* usage = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
			usage->TextFunc = [s]() {
				const LevelDocument& document = s->GetDocument();
				std::int32_t staticCount = document.GetStaticTileCount();
				std::int32_t used = std::max<std::int32_t>(staticCount, 0) + std::int32_t(document.Animations.size());
				// TRANSLATORS: Bottom of the Tile Sets dialog, the number of tile IDs the level uses and the number it can have
				String text = _fx("editor", "Tile IDs used: {} of {}", used, document.TileIdSpace);
				if (used > LevelDocument::ScriptTileIdLimit) {
					text += " "_s + _x("editor", "(scripts can't refer to all of them)");
				}
				return text;
			};
		}

		ShowModal(*surface, Death::move(dialog));
	}

	String GetTileSetLabel(StringView name)
	{
		// TRANSLATORS: Shown in place of the name of a tile set for the tiles stored in the level itself
		return (name.hasPrefix(':') ? String(_x("editor", "Tiles of this level")) : String(name));
	}

	bool IsLevelTileSet(const LevelDocument& document, std::uint8_t source)
	{
		return (source != TileRef::DefaultTileSet && std::int32_t(source) - 1 < std::int32_t(document.ExtraTileSets.size()) &&
			(document.ExtraTileSets[std::size_t(source) - 1].Flags & Formats::ExtraTileSetRecord::Embedded) != 0);
	}

	bool ReadTileSetOf(IEditorContext& session, std::uint8_t source, Formats::TileSetData& tileSet)
	{
		const LevelDocument& document = session.GetDocument();
		String name;
		if (source == TileRef::DefaultTileSet) {
			name = document.Properties.DefaultTileSet;
		} else if (std::int32_t(source) - 1 < std::int32_t(document.ExtraTileSets.size())) {
			name = document.ExtraTileSets[std::size_t(source) - 1].Name;
		}
		if (name.empty()) {
			return false;
		}
		if (IsLevelTileSet(document, source)) {
			const Formats::TileSetData* data = session.GetTileSetData(name);
			if (data == nullptr) {
				return false;
			}
			tileSet = *data;
			return true;
		}
		String path = ContentTileSetLibrary::FindTileSetFile(name);
		auto s = (path.empty() ? nullptr : fs::Open(path, FileAccess::Read));
		return (s != nullptr && s->IsValid() && Formats::TileSetFile::Read(*s, tileSet));
	}

	bool SerializeTileSet(const Formats::TileSetData& tileSet, SmallVectorImpl<std::uint8_t>& bytes)
	{
		MemoryStream ms(65536);
		if (!Formats::TileSetFile::Write(ms, tileSet)) {
			return false;
		}
		bytes.resize_for_overwrite(std::size_t(ms.GetSize()));
		std::memcpy(bytes.data(), ms.GetBuffer(), bytes.size());
		return true;
	}

	void GetLevelPalette(IEditorContext& session, std::uint32_t (&palette)[256])
	{
		const LevelDocument& document = session.GetDocument();
		if ((document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette) {
			std::memcpy(palette, document.LevelPalette, sizeof(palette));
		} else if (!session.GetTileSetLibrary().ReadPalette(document.Properties.DefaultTileSet, palette)) {
			std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
		}
		// The first entry is the transparent one
		palette[0] &= 0x00FFFFFFu;
	}

	void BakeRemapping(Formats::TileSetData& tileSet, const std::uint8_t* remapping)
	{
		constexpr std::int32_t PixelCount = 32 * 32;
		for (std::int32_t i = 0; i < tileSet.TileCount; i++) {
			if (tileSet.TrueColor[i] != 0) {
				continue;
			}
			std::uint8_t* image = &tileSet.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile];
			for (std::int32_t p = 0; p < PixelCount; p++) {
				// Transparent stays transparent, whatever the first entry is matched to
				image[p] = (image[p] == 0 ? 0 : remapping[image[p]]);
			}
		}
	}

	bool CanAddTileIds(IEditorContext& session, std::int32_t added)
	{
		const LevelDocument& document = session.GetDocument();
		if (added <= 0) {
			return true;
		}
		std::int64_t total = std::int64_t(std::max<std::int32_t>(document.GetStaticTileCount(), 0)) + added + std::int64_t(document.Animations.size());
		if (total > std::int64_t(UINT16_MAX) + 1) {
			session.ShowMessage(_x("editor", "The level can't refer to so many tiles, it has 65536 tile IDs for all of its tile sets and animated tiles"));
			return false;
		}
		return true;
	}

	String SuggestTileSetName(StringView base, StringView suffix)
	{
		constexpr std::size_t MaxLength = 32;
		// Room for "-99" to make it unique
		constexpr std::size_t MaxBaseLength = MaxLength - 3;
		String name;
		auto append = [&name](StringView text) {
			for (char c : text) {
				char l = (c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
				if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-') {
					name += StringView(&l, 1);
				} else if (l == ' ' || l == '.') {
					name += "_"_s;
				}
			}
		};
		append(fs::GetFileName(fs::FromNativeSeparators(base)));
		if (name.empty()) {
			name = "tiles"_s;
		}
		String cleanSuffix;
		std::swap(name, cleanSuffix);
		append(suffix);
		std::swap(name, cleanSuffix);
		std::size_t baseLength = std::min(name.size(), MaxBaseLength - std::min(cleanSuffix.size(), MaxBaseLength - 1));
		String result = String(name.prefix(baseLength)) + cleanSuffix;
		String unique = result;
		for (std::int32_t i = 2; !ContentTileSetLibrary::FindTileSetFile(unique).empty() && i < 100; i++) {
			unique = result + "-"_s + FormatInteger(i);
		}
		return unique;
	}

	bool IsOwnTileSetCopy(StringView name)
	{
		String found = ContentTileSetLibrary::FindTileSetFile(name);
		String writable = ContentTileSetLibrary::GetWritablePath(name, false);
		// Both as the file system compares them, they're made in different ways
		return (!found.empty() && StringUtils::lowercase(fs::FromNativeSeparators(found)) == StringUtils::lowercase(fs::FromNativeSeparators(writable)));
	}

	namespace
	{
		// The extra tile set the level stores in itself, -1 if it has none
		std::int32_t FindLevelTileSet(const LevelDocument& document)
		{
			for (std::int32_t i = 0; i < std::int32_t(document.ExtraTileSets.size()); i++) {
				if ((document.ExtraTileSets[i].Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
					return i;
				}
			}
			return -1;
		}

		// Name of a tile set tiles are added to, as it's shown
		String GetTargetLabel(IEditorContext& session, const TileTarget& target)
		{
			const LevelDocument& document = session.GetDocument();
			switch (target.Type) {
				case TileTarget::Kind::NewTileSet: return target.Name;
				case TileTarget::Kind::Level: return GetTileSetLabel(LevelTileSetName);
				default:
					if (target.Source == TileRef::DefaultTileSet) {
						return document.Properties.DefaultTileSet;
					}
					return (std::int32_t(target.Source) - 1 < std::int32_t(document.ExtraTileSets.size())
						? GetTileSetLabel(document.ExtraTileSets[std::size_t(target.Source) - 1].Name) : String());
			}
		}

		// Reads the tile set tiles are added to; a new tile set file has only the empty tile, the one of the level
		// nothing, both in the palette of the level
		bool LoadTarget(IEditorContext& session, const TileTarget& target, Formats::TileSetData& tileSet)
		{
			const LevelDocument& document = session.GetDocument();
			bool newSlot = (target.Type == TileTarget::Kind::NewTileSet || (target.Type == TileTarget::Kind::Level && FindLevelTileSet(document) < 0));
			if (newSlot && document.ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
				session.ShowMessage(_x("editor", "A level can't have more extra tile sets"));
				return false;
			}
			switch (target.Type) {
				case TileTarget::Kind::NewTileSet: {
					if (!ContentTileSetLibrary::IsValidName(target.Name)) {
						session.ShowMessage(_x("editor", "A tile set is named by lowercase letters, digits, dashes and underscores"));
						return false;
					}
					if (!ContentTileSetLibrary::FindTileSetFile(target.Name).empty()) {
						session.ShowMessage(_x("editor", "A tile set of this name exists already"));
						return false;
					}
					GetLevelPalette(session, tileSet.Palette);
					tileSet.Resize(1);
					return true;
				}
				case TileTarget::Kind::Level: {
					std::int32_t slot = FindLevelTileSet(document);
					if (slot >= 0) {
						if (!ReadTileSetOf(session, std::uint8_t(slot + 1), tileSet)) {
							session.ShowMessage(_x("editor", "The tile set can't be read"));
							return false;
						}
						return true;
					}
					GetLevelPalette(session, tileSet.Palette);
					tileSet.Resize(0);
					return true;
				}
				default: {
					if (!ReadTileSetOf(session, target.Source, tileSet)) {
						session.ShowMessage(_x("editor", "The tile set can't be read"));
						return false;
					}
					return true;
				}
			}
		}

		// Adds tiles to the end of the tile set read by LoadTarget(), saves it and makes the level follow, as one step
		bool StoreTiles(IEditorContext& session, const TileTarget& target, Formats::TileSetData& tileSet, const Formats::TileSetData& from,
			ArrayView<const std::int32_t> tiles, TileRef& first, const TileRef* replace = nullptr)
		{
			LevelDocument& document = session.GetDocument();
			if (tiles.empty()) {
				return false;
			}

			// Which tile set of the level it is, -1 for the default one or one it doesn't have yet
			std::int32_t slot = -1;
			bool embedded = false;
			String name;
			switch (target.Type) {
				case TileTarget::Kind::NewTileSet:
					name = target.Name;
					break;
				case TileTarget::Kind::Level:
					slot = FindLevelTileSet(document);
					embedded = true;
					name = (slot >= 0 ? document.ExtraTileSets[slot].Name : String(LevelTileSetName));
					break;
				default:
					if (target.Source != TileRef::DefaultTileSet) {
						slot = std::int32_t(target.Source) - 1;
						if (slot >= std::int32_t(document.ExtraTileSets.size())) {
							return false;
						}
						embedded = ((document.ExtraTileSets[slot].Flags & Formats::ExtraTileSetRecord::Embedded) != 0);
						name = document.ExtraTileSets[slot].Name;
					} else {
						name = document.Properties.DefaultTileSet;
					}
					break;
			}
			bool isDefault = (target.Type == TileTarget::Kind::TileSet && target.Source == TileRef::DefaultTileSet);
			bool newSlot = (!isDefault && slot < 0);

			std::int32_t oldCount = tileSet.TileCount;
			if (oldCount + std::int32_t(tiles.size()) > std::int32_t(UINT16_MAX)) {
				session.ShowMessage(_x("editor", "A tile set can't have more tiles"));
				return false;
			}
			// An extra tile set that takes only a part of its file wouldn't take the new tiles at its end, they'd be
			// added and the level couldn't use them
			std::uint16_t offset = (slot >= 0 ? document.ExtraTileSets[slot].Offset : 0);
			bool takesAll = (slot >= 0 && std::int32_t(offset) + std::int32_t(document.ExtraTileSets[slot].Count) >= oldCount);
			if (slot >= 0 && !takesAll) {
				session.ShowMessage(_x("editor", "The level takes only a part of this tile set, let it take the tiles up to its end in Tile Sets first"));
				return false;
			}
			if (!CanAddTileIds(session, std::int32_t(tiles.size()) + (newSlot ? oldCount : 0))) {
				return false;
			}
			// The tiles of the level matched to the palette get the colors they're shown in, a true color tile added
			// would make the record mean an alternate palette instead, and new tiles come in the palette of the level
			std::uint8_t keptFlags = (slot >= 0 ? document.ExtraTileSets[slot].Flags : 0);
			if (embedded && slot >= 0 && (keptFlags & Formats::ExtraTileSetRecord::Remapped) != 0 &&
				(keptFlags & Formats::ExtraTileSetRecord::TrueColor) == 0) {
				BakeRemapping(tileSet, document.ExtraTileSets[slot].Remapping);
				keptFlags &= ~Formats::ExtraTileSetRecord::Remapped;
			}
			tileSet.Resize(oldCount + std::int32_t(tiles.size()));
			for (std::size_t i = 0; i < tiles.size(); i++) {
				std::int32_t to = oldCount + std::int32_t(i);
				std::memcpy(&tileSet.Images[std::size_t(to) * Formats::TileSetData::ImageBytesPerTile],
					&from.Images[std::size_t(tiles[i]) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
				std::memcpy(&tileSet.Masks[std::size_t(to) * Formats::TileSetData::MaskBytesPerTile],
					&from.Masks[std::size_t(tiles[i]) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
				tileSet.TrueColor[to] = from.TrueColor[tiles[i]];
			}

			// Tiles of the level are kept in the level, the others in a tile set file in the user content
			SmallVector<std::uint8_t, 0> bytes;
			if (embedded) {
				if (!SerializeTileSet(tileSet, bytes)) {
					session.ShowMessage(_x("editor", "The tile set couldn't be saved"));
					return false;
				}
			} else {
				String path = ContentTileSetLibrary::GetWritablePath(name, true);
				if (path.empty()) {
					session.ShowMessage(_x("editor", "There's no place to save tile sets to"));
					return false;
				}
				// Only appended to, the tiles other levels take from it stay where they are, but a level that has it as its
				// default tile set refers to the tiles of its extra ones by IDs after all of its tiles, so those levels are
				// changed with it, and it isn't written if any of them can't be; copies of the files before are kept
				if (target.Type == TileTarget::Kind::NewTileSet) {
					if (!AtomicFile::Write(path, [&tileSet](Stream& s) { return Formats::TileSetFile::Write(s, tileSet); })) {
						session.ShowMessage(_x("editor", "The tile set couldn't be saved"));
						return false;
					}
				} else if (!WriteTileSetWithAddedTiles(session, name, path, tileSet, oldCount)) {
					return false;
				}
				session.InvalidateTileSet(name);
			}

			// The tiles of the level stay where they are, the tile set has more of them, and an extra one that takes all of
			// its tiles takes the new ones too
			std::int32_t newCount = tileSet.TileCount;
			bool trueColor = tileSet.HasTrueColorTiles();
			std::uint8_t source = (isDefault ? TileRef::DefaultTileSet : std::uint8_t(newSlot ? document.ExtraTileSets.size() + 1 : slot + 1));
			// TRANSLATORS: Names of undo steps in the history, a tile copied into the tiles stored in the level, a new tile set added, or tiles added to a tile set
			String label = (replace != nullptr ? String(_x("editor", "Copy tile into the level")) : (newSlot ? String(_x("editor", "Add tile set")) : String(_x("editor", "Add tiles to tile set"))));
			// A tile copied in its place is shown by the copy everywhere, and the level doesn't change the tile copied anymore
			TileRef replaced = (replace != nullptr ? *replace : TileRef{});
			std::int32_t replacedId = (replace != nullptr ? document.ToTileId(replaced) : -1);
			TileRef replacement = TileRef{ std::uint16_t(oldCount - offset), source, 0 };
			Function<bool(TileRef&)> remap = [replace, replaced, replacement](TileRef& tile) {
				if (replace != nullptr && tile.Source == replaced.Source && tile.Index == replaced.Index) {
					tile.Source = replacement.Source;
					tile.Index = replacement.Index;
				}
				return true;
			};
			Function<void(LevelDocument&)> prepare = [replacedId](LevelDocument& changed) {
				if (replacedId < 0) {
					return;
				}
				auto& diffuse = changed.DiffuseOverrides;
				diffuse.erase(std::remove_if(diffuse.begin(), diffuse.end(), [replacedId](const Formats::TileDiffuseOverride& tile) { return tile.TileId == replacedId; }), diffuse.end());
				auto& masks = changed.MaskOverrides;
				masks.erase(std::remove_if(masks.begin(), masks.end(), [replacedId](const Formats::TileMaskOverride& tile) { return tile.TileId == replacedId; }), masks.end());
			};
			ChangeTileSets(session, label, isDefault, [&](LevelDocument& changed) {
				if (isDefault) {
					changed.DefaultTileCount = newCount;
					return;
				}
				if (newSlot) {
					Formats::ExtraTileSetRecord record = {};
					record.Name = name;
					record.Count = std::uint16_t(std::min<std::int32_t>(newCount, UINT16_MAX));
					record.Flags = std::uint8_t((embedded ? Formats::ExtraTileSetRecord::Embedded : 0) | (trueColor ? Formats::ExtraTileSetRecord::TrueColor : 0));
					record.Data = Death::move(bytes);
					changed.ExtraTileSets.push_back(Death::move(record));
					return;
				}
				Formats::ExtraTileSetRecord& record = changed.ExtraTileSets[slot];
				if (embedded) {
					record.Data = Death::move(bytes);
					record.Flags = keptFlags;
				}
				// Not for a tile set file still matched to the palette, the flag would mean an alternate palette then
				if (trueColor && (record.Flags & Formats::ExtraTileSetRecord::Remapped) == 0) {
					record.Flags |= Formats::ExtraTileSetRecord::TrueColor;
				}
				if (takesAll) {
					record.Count = std::uint16_t(std::min<std::int32_t>(newCount - std::int32_t(record.Offset), UINT16_MAX));
				}
			}, Death::move(remap), Death::move(prepare));
			if (embedded) {
				session.InvalidateTileSet(name);
			}

			first = replacement;
			return true;
		}
	}

	void AddTileTargetRow(IEditorContext& session, UiStack* row, std::shared_ptr<TileTarget> target)
	{
		IEditorContext* s = &session;
		auto* dropdown = row->Add<UiDropdown>([s, target]() {
			// TRANSLATORS: Choice of where new tiles are added, a tile set made for them, followed by a box for its name
			return (target->Type == TileTarget::Kind::NewTileSet ? String(_x("editor", "New tile set")) : GetTargetLabel(*s, *target));
		}, [s, target](SmallVectorImpl<UiMenuEntry>& entries) {
			const LevelDocument& document = s->GetDocument();
			std::int32_t count = 1 + std::min<std::int32_t>(std::int32_t(document.ExtraTileSets.size()), TileRef::LastExtraTileSet);
			for (std::int32_t i = 0; i < count; i++) {
				// The tiles of the level are the last entry
				if (IsLevelTileSet(document, std::uint8_t(i))) {
					continue;
				}
				std::uint8_t source = std::uint8_t(i);
				UiMenuEntry entry;
				entry.Label = (i == 0 ? String(document.Properties.DefaultTileSet) : GetTileSetLabel(document.ExtraTileSets[i - 1].Name));
				entry.IsChecked = [target, source]() { return (target->Type == TileTarget::Kind::TileSet && target->Source == source); };
				entry.Execute = [target, source]() {
					target->Type = TileTarget::Kind::TileSet;
					target->Source = source;
				};
				entries.push_back(Death::move(entry));
			}
			UiMenuEntry newEntry;
			newEntry.Label = _x("editor", "New tile set");
			newEntry.IsChecked = [target]() { return (target->Type == TileTarget::Kind::NewTileSet); };
			newEntry.Execute = [target]() { target->Type = TileTarget::Kind::NewTileSet; };
			entries.push_back(Death::move(newEntry));
			UiMenuEntry levelEntry;
			levelEntry.Label = _x("editor", "Tiles of this level");
			levelEntry.IsChecked = [target]() { return (target->Type == TileTarget::Kind::Level); };
			levelEntry.Execute = [target]() { target->Type = TileTarget::Kind::Level; };
			entries.push_back(Death::move(levelEntry));
		});
		dropdown->Width = 170.0f;
		dropdown->Tooltip = _x("editor", "Tiles of this level are stored in the level itself, not in any tile set file");

		if (target->Name.empty()) {
			// A name of the level, as tile sets are named
			target->Name = SuggestTileSetName(session.GetLevelName(), "-tiles"_s);
		}
		auto* nameBox = row->Add<UiTextBox>(target->Name);
		nameBox->Width = 110.0f;
		nameBox->MaxLength = 32;
		nameBox->Tooltip = _x("editor", "The tile set is saved in the user content, Tilesets/<name>.j2t");
		nameBox->OnChanged = [target](StringView text) { target->Name = String(text).trimmed(); };
		nameBox->OnCommit = [target](StringView text) { target->Name = String(text).trimmed(); };
		nameBox->IsVisibleFunc = [target]() { return (target->Type == TileTarget::Kind::NewTileSet); };
	}

	void AddTileTargetNote(IEditorContext& session, UiStack* parent, std::shared_ptr<TileTarget> target)
	{
		IEditorContext* s = &session;
		auto* note = parent->Add<UiLabel>(StringView(), UiTheme::TextAccent);
		note->TextFunc = [s, target]() {
			switch (target->Type) {
				case TileTarget::Kind::Level:
					return String(_x("editor", "The level keeps the tiles, no other level has them"));
				case TileTarget::Kind::NewTileSet:
					return String();
				default: {
					if (IsLevelTileSet(s->GetDocument(), target->Source)) {
						return String();
					}
					// Its copy in the user content is written over, any other one is covered by a copy there
					String name = GetTargetLabel(*s, *target);
					return (IsOwnTileSetCopy(name) ? String() : String(_x("editor", "A copy in the user content is used instead of it from now on")));
				}
			}
		};
	}

	bool CopyTileIntoLevel(IEditorContext& session, const TileRef& tile, TileRef& copy)
	{
		const LevelDocument& document = session.GetDocument();
		if (tile.IsAnimated() || tile.IsEmpty() || tile.Source == TileRef::Raw || IsLevelTileSet(document, tile.Source)) {
			return false;
		}
		Formats::TileSetData original;
		if (!ReadTileSetOf(session, tile.Source, original)) {
			session.ShowMessage(_x("editor", "The tile set can't be read"));
			return false;
		}
		TileSource source;
		if (!ResolveTileSource(document, tile.Source, source)) {
			return false;
		}
		std::int32_t index = source.Offset + tile.Index;
		if (index >= original.TileCount || (source.Extra != nullptr && tile.Index >= source.Extra->Count)) {
			return false;
		}

		// The tile as the level shows it: in the palette of the level, and as the level changes it
		Formats::TileSetData from;
		from.Resize(1);
		std::memcpy(&from.Images[0], &original.Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
		std::memcpy(&from.Masks[0], &original.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
		from.TrueColor[0] = original.TrueColor[index];
		if (from.TrueColor[0] == 0 && source.Remapping != nullptr) {
			for (std::int32_t i = 0; i < 32 * 32; i++) {
				from.Images[std::size_t(i)] = source.Remapping[from.Images[std::size_t(i)]];
			}
		}
		std::int32_t tileId = document.ToTileId(tile);
		if (const Formats::TileDiffuseOverride* diffuse = SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId))) {
			from.TrueColor[0] = 0;
			std::memcpy(&from.Images[0], diffuse->Indices, sizeof(diffuse->Indices));
		}
		if (const Formats::TileMaskOverride* mask = SetTileOverrideCommand::FindMask(document, std::uint16_t(tileId))) {
			TileMasks::Pack(mask->Mask, &from.Masks[0]);
		}

		TileTarget target;
		target.Type = TileTarget::Kind::Level;
		Formats::TileSetData tileSet;
		if (!LoadTarget(session, target, tileSet)) {
			return false;
		}
		std::int32_t indices[] = { 0 };
		return StoreTiles(session, target, tileSet, from, indices, copy, &tile);
	}

	bool AppendTiles(IEditorContext& session, const TileTarget& target, const Formats::TileSetData& tiles, TileRef& first)
	{
		Formats::TileSetData tileSet;
		if (!LoadTarget(session, target, tileSet)) {
			return false;
		}
		SmallVector<std::int32_t, 0> indices;
		for (std::int32_t i = 0; i < tiles.TileCount; i++) {
			indices.push_back(i);
		}
		return StoreTiles(session, target, tileSet, tiles, indices, first);
	}

	namespace
	{
		// Tiles of an image added to a tile set: what is read on the main thread, cut off the user interface, and saved
		// on the main thread again
		struct AddTilesJob
		{
			TileTarget Target;
			String ImagePath;
			AddTilesOptions Options;
			PngImage Image;
			PngImage Masks;
			bool HasMasks = false;
			Formats::TileSetData TileSet;
			// The file the tile set was read from, as it was then, it's written with the tiles added to what was read
			String SourcePath;
			std::int64_t SourceTime = 0;
			std::int64_t SourceSize = -1;
			std::uint32_t Palette[256];
			Formats::TileSetData Sliced;
			TileImportResult Result;
			bool Success = false;
		};

		// When a file was changed and how large it is, to tell whether it was changed since
		void GetFileStamp(StringView path, std::int64_t& time, std::int64_t& size)
		{
			time = 0;
			size = -1;
			if (path.empty() || !fs::FileExists(path)) {
				return;
			}
			time = fs::GetLastModificationTime(path).ToUnixMilliseconds();
			size = fs::GetFileSize(path);
		}

		// Reads the image and the tile set the tiles go to, returns `nullptr` if it can't
		std::shared_ptr<AddTilesJob> PrepareAddTiles(IEditorContext& session, const TileTarget& target, StringView imagePath, const AddTilesOptions& options)
		{
			auto job = std::make_shared<AddTilesJob>();
			job->Target = target;
			job->ImagePath = imagePath;
			job->Options = options;
			{
				// The masks next to it, as the editor exports them
				String error;
				if (!PngCodec::LoadWithMasks(imagePath, job->Image, job->Masks, job->HasMasks, &error)) {
					session.ShowMessage(_fx("editor", "The image can't be read: {}", error));
					return nullptr;
				}
			}
			if (!LoadTarget(session, target, job->TileSet)) {
				return nullptr;
			}
			if (target.Type == TileTarget::Kind::TileSet && !IsLevelTileSet(session.GetDocument(), target.Source)) {
				const LevelDocument& document = session.GetDocument();
				StringView name = (target.Source == TileRef::DefaultTileSet ? StringView(document.Properties.DefaultTileSet)
					: (std::size_t(target.Source) - 1 < document.ExtraTileSets.size() ? StringView(document.ExtraTileSets[std::size_t(target.Source) - 1].Name) : StringView()));
				job->SourcePath = ContentTileSetLibrary::FindTileSetFile(name);
				GetFileStamp(job->SourcePath, job->SourceTime, job->SourceSize);
			}

			// The closest colors of the palette the tiles are shown in: the palette of the file of a tile set matched to
			// the palette of the level (it keeps indices of its own palette), otherwise the palette of the level, the
			// indices of which an 8-bit tile of an extra tile set or of the level shows
			bool matchedSet = false;
			if (target.Type == TileTarget::Kind::TileSet && target.Source != TileRef::DefaultTileSet &&
				std::size_t(target.Source) - 1 < session.GetDocument().ExtraTileSets.size()) {
				const Formats::ExtraTileSetRecord& record = session.GetDocument().ExtraTileSets[std::size_t(target.Source) - 1];
				matchedSet = ((record.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::Embedded)) == Formats::ExtraTileSetRecord::Remapped);
			}
			if (matchedSet) {
				std::memcpy(job->Palette, job->TileSet.Palette, sizeof(job->Palette));
			} else {
				GetLevelPalette(session, job->Palette);
			}
			return job;
		}

		// Cuts the image into tiles, on any thread, it only reads the image
		void SliceAddTiles(AddTilesJob& job, JobProgress* progress)
		{
			TileImportOptions options;
			options.Colors = TileImportColors::MatchPalette;
			options.Palette = job.Palette;
			options.Dither = job.Options.Dither;
			options.ErrorDiffusion = job.Options.ErrorDiffusion;
			options.SkipDuplicates = true;
			options.Masks = (job.HasMasks ? &job.Masks : nullptr);
			options.OffsetX = job.Options.OffsetX;
			options.OffsetY = job.Options.OffsetY;
			options.SpacingX = job.Options.SpacingX;
			options.SpacingY = job.Options.SpacingY;
			options.UseColorKey = job.Options.UseColorKey;
			options.ColorKey = job.Options.ColorKey;
			options.AlphaThreshold = std::uint8_t(std::clamp<std::int32_t>(job.Options.AlphaThreshold, 1, 255));
			if (progress != nullptr) {
				options.Progress = [progress](float value) {
					return progress->Report(value);
				};
			}
			job.Success = SliceTileSet(job.Image, options, job.Sliced, job.Result);
		}

		bool FinishAddTiles(IEditorContext& session, AddTilesJob& job);
	}

	bool AddTilesFromImage(IEditorContext& session, const TileTarget& target, StringView imagePath, bool skipExisting, bool dither)
	{
		AddTilesOptions options;
		options.SkipExisting = skipExisting;
		options.Dither = dither;
		std::shared_ptr<AddTilesJob> job = PrepareAddTiles(session, target, imagePath, options);
		if (job == nullptr) {
			return false;
		}
		SliceAddTiles(*job, nullptr);
		return FinishAddTiles(session, *job);
	}

	void AddTilesFromImageAsync(IEditorContext& session, const TileTarget& target, StringView imagePath, const AddTilesOptions& options, Function<void(bool)>&& onDone)
	{
		std::shared_ptr<AddTilesJob> job = PrepareAddTiles(session, target, imagePath, options);
		if (job == nullptr) {
			if (onDone) {
				onDone(false);
			}
			return;
		}
		IEditorContext* s = &session;
		auto callback = std::make_shared<Function<void(bool)>>(Death::move(onDone));
		// TRANSLATORS: Title of the progress dialog while tiles from an image are added to a tile set
		RunWithProgress(session, _x("editor", "Adding Tiles"), [job](JobProgress& progress) {
			SliceAddTiles(*job, &progress);
		}, [s, job, callback](bool cancelled) {
			bool added = (!cancelled || job->Success) && FinishAddTiles(*s, *job);
			if (*callback) {
				(*callback)(added);
			}
		});
	}

	namespace
	{
	bool FinishAddTiles(IEditorContext& session, AddTilesJob& job)
	{
		const TileTarget& target = job.Target;
		const AddTilesOptions& options = job.Options;
		Formats::TileSetData& tileSet = job.TileSet;
		const Formats::TileSetData& sliced = job.Sliced;
		const TileImportResult& result = job.Result;
		StringView imagePath = job.ImagePath;
		bool skipExisting = options.SkipExisting;
		String name = GetTargetLabel(session, target);
		if (!job.Success) {
			switch (result.Error) {
				case TileImportError::TooManyTiles: session.ShowMessage(_fx("editor", "The image has more than {} different tiles, a tile set can't hold them", MaxImportedTiles)); break;
				case TileImportError::Cancelled: session.ShowMessage(_x("editor", "Adding the tiles was stopped")); break;
				default: session.ShowMessage(_x("editor", "The image has no tile in it, it's smaller than a tile with the margin and gutters")); break;
			}
			return false;
		}

		// Tiles it has already are found by their pixels and mask
		auto hashTile = [](const Formats::TileSetData& data, std::int32_t tile) {
			return HashTileOf(data, tile);
		};
		SmallVector<std::uint64_t, 0> existing;
		if (skipExisting) {
			for (std::int32_t i = 0; i < tileSet.TileCount; i++) {
				existing.push_back(hashTile(tileSet, i));
			}
			std::sort(existing.begin(), existing.end());
		}

		// The first tile of the slices is the empty one, it's left out
		std::int32_t oldCount = tileSet.TileCount;
		SmallVector<std::int32_t, 0> added;
		for (std::int32_t i = 1; i < sliced.TileCount; i++) {
			if (skipExisting && std::binary_search(existing.begin(), existing.end(), hashTile(sliced, i))) {
				continue;
			}
			added.push_back(i);
		}
		if (added.empty()) {
			session.ShowMessage(_x("editor", "The tile set has all tiles of the image already"));
			return false;
		}
		if (!job.SourcePath.empty()) {
			// Written over by something else while the image was cut, the tiles would be added to what it was before
			std::int64_t time, size;
			GetFileStamp(job.SourcePath, time, size);
			if (time != job.SourceTime || size != job.SourceSize) {
				session.ShowMessage(_x("editor", "The tile set was changed meanwhile, add the tiles again"));
				return false;
			}
		}
		TileRef first;
		if (!StoreTiles(session, target, tileSet, sliced, added, first)) {
			return false;
		}
		LOGI("{} tiles of \"{}\" added to \"{}\"", std::int32_t(added.size()), imagePath, name);

		std::int32_t count = std::int32_t(added.size());
		String message = _fnx("editor", "{} tile was added to tile set \"{}\", from tile {}", "{} tiles were added to tile set \"{}\", from tile {}", count, count, name, oldCount);
		if (result.ColorsChanged) {
			// TRANSLATORS: Added to the end of the message about how many tiles from an image were added to a tile set
			message += " "_s + _x("editor", "- some colors became the closest ones of its palette");
		}
		session.ShowMessage(message);
		return true;
	}
	}

	void ShowAddTilesFromImageDialog(IEditorContext& session)
	{
		IEditorContext* s = &session;
		// TRANSLATORS: Title of the file picker and the dialog that add tiles from an image file to a tile set
		ShowOpenImageDialog(session, _x("editor", "Add Tiles from Image"), [s](StringView path) {
			struct AddState
			{
				String Path;
				AddTilesOptions Options;
			};
			auto state = std::make_shared<AddState>();
			state->Path = path;
			auto target = std::make_shared<TileTarget>();

			auto dialog = std::make_unique<UiDialog>(_x("editor", "Add Tiles from Image"));
			UiDialog* dialogPtr = dialog.get();
			auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
			content->Add<UiLabel>(_x("editor", "The tiles of the image go to the end of the tile set, in its colors."), UiTheme::TextDim);
			// TRANSLATORS: Label of the choice of where the tiles are added, followed by a tile set or the level
			AddTileTargetRow(*s, content->AddRow(_x("editor", "Add to")), target);
			content->Add<UiCheckBox>(_x("editor", "Skip tiles the tile set has already"), [state]() { return state->Options.SkipExisting; },
				[state](bool value) { state->Options.SkipExisting = value; });
			content->Add<UiCheckBox>(_x("editor", "Mix colors between two of the palette (dither)"), [state]() { return state->Options.Dither || state->Options.ErrorDiffusion; },
				[state](bool value) { state->Options.Dither = value; state->Options.ErrorDiffusion = false; });
			content->Add<UiCheckBox>(_x("editor", "Spread what each pixel misses to the next ones (Floyd-Steinberg)"), [state]() { return state->Options.ErrorDiffusion; },
				[state](bool value) { state->Options.ErrorDiffusion = value; state->Options.Dither = false; })
				->Tooltip = _x("editor", "Mixes colors more smoothly than a regular pattern, best for pictures with gradients");

			// Sprite sheets have a margin around the tiles and gutters between them, and a color for transparent pixels
			auto addNumber = [](UiStack* row, std::int32_t* value, float max, StringView tooltip, std::shared_ptr<AddState> keep) {
				auto* number = row->Add<UiNumberBox>([value, keep]() { return float(*value); }, [value, keep](float v, bool) {
					*value = std::int32_t(std::round(v));
				}, 0.0f, max, 1.0f, 0);
				number->Width = 48.0f;
				number->Tooltip = tooltip;
			};
			{
				auto* row = content->AddRow(_x("editor", "Margin"));
				addNumber(row, &state->Options.OffsetX, 1024.0f, _x("editor", "Pixels left of the first column of tiles"), state);
				addNumber(row, &state->Options.OffsetY, 1024.0f, _x("editor", "Pixels above the first row of tiles"), state);
				auto* gutterRow = content->AddRow(_x("editor", "Gutters"));
				addNumber(gutterRow, &state->Options.SpacingX, 256.0f, _x("editor", "Pixels between two tiles next to each other"), state);
				addNumber(gutterRow, &state->Options.SpacingY, 256.0f, _x("editor", "Pixels between two rows of tiles"), state);
				auto* transparentRow = content->AddRow(_x("editor", "Transparent"));
				// TRANSLATORS: Checkbox after "Transparent" in the Add Tiles from Image dialog, the color, magenta pixels are transparent
				transparentRow->Add<UiCheckBox>(_x("editor", "Magenta"), [state]() { return state->Options.UseColorKey; }, [state](bool value) { state->Options.UseColorKey = value; })
					->Tooltip = _x("editor", "Magenta pixels are transparent, as many sprite sheets without alpha have them");
				transparentRow->Add<UiLabel>(_x("editor", "Alpha"), UiTheme::TextDim);
				addNumber(transparentRow, &state->Options.AlphaThreshold, 255.0f, _x("editor", "How opaque a pixel has to be to be seen, and a pixel of the masks to be solid, from 1 to 255"), state);
			}
			AddTileTargetNote(*s, content, target);

			// TRANSLATORS: Button in the Add Tiles from Image dialog
			dialog->AddButton(_x("editor", "Add Tiles"), [s, state, target, dialogPtr]() {
				AddTilesFromImageAsync(*s, *target, state->Path, state->Options, [dialogPtr](bool added) {
					if (added) {
						dialogPtr->Close();
					}
				});
			}, true);
			dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });

			ShowModal(*s->GetSurface(), Death::move(dialog));
		});
	}
}

#endif
