#include "TileSetsDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../UI/UiMenu.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

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
		};

		String FormatInteger(std::int32_t value)
		{
			char buffer[16];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%d", value);
			return String(buffer, std::size_t(std::max(length, 0)));
		}

		/**
		 * Changes the tile sets as one undoable step, with every tile of the level following the change
		 *
		 * @p remap tells where a tile of the tile sets as they were is now, or returns `false` if it's gone.
		 */
		void ChangeTileSets(EditorSession& session, StringView name, bool changesProperties, Function<void(LevelDocument&)>&& change,
			Function<bool(TileRef&)>&& remap, Function<void(LevelDocument&)>&& prepare = {})
		{
			LevelDocument& document = session.GetDocument();
			SnapshotParts parts = SnapshotParts::TileSets | SnapshotParts::Animations | SnapshotParts::Layers;
			if (changesProperties) {
				parts |= SnapshotParts::Properties;
			}
			auto command = SnapshotCommand::Begin(document, parts, name);
			// What goes before the tiles are followed, still with the tile sets as they were
			if (prepare) {
				prepare(document);
			}
			LevelDocument::OverrideRefs overrides = document.CaptureOverrideRefs();
			change(document);
			std::int32_t cleared = document.RemapTiles(overrides, remap);
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
				session.SetStamp(std::move(stamp));
			}

			DocumentChanges changes;
			command->End(document, changes);
			session.Execute(std::move(command));
			if (cleared > 0) {
				session.ShowMessage(_fn("{} tile that isn't in the tile sets anymore was cleared", "{} tiles that aren't in the tile sets anymore were cleared", cleared, cleared));
			}
		}

		// Places tile sets are looked for, the first one that has a tile set is the one the game uses
		void GetTileSetRoots(SmallVectorImpl<UiFileRoot>& roots)
		{
			auto& resolver = ContentResolver::Get();
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				roots.push_back({ _("User content"), fs::CombinePath(userContent, "Tilesets"_s), false });
			}
			roots.push_back({ _("Content"), fs::CombinePath(resolver.GetContentPath(), "Tilesets"_s), false });
			roots.push_back({ _("Cache"), fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s), false });
			for (std::size_t i = 0; i < roots.size(); ) {
				if (!fs::DirectoryExists(roots[i].Path)) {
					roots.erase(roots.begin() + i);
				} else {
					i++;
				}
			}
		}

		// Lets a tile set be picked, the name it's referred to by is passed on
		void PickTileSet(EditorSession& session, StringView title, StringView current, Function<void(StringView)>&& onPicked)
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
			UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, std::move(roots), ".j2t"_s, directory, fileName,
				[rootPaths = std::move(rootPaths), onPicked = std::move(onPicked)](StringView path) mutable {
					// Tile sets are referred to by their path relative to "Tilesets", without the extension
					StringView name = fs::GetFileName(path);
					for (const String& rootPath : rootPaths) {
						if (path.hasPrefix(rootPath)) {
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
		std::int32_t GetTileCountOf(EditorSession& session, const Formats::ExtraTileSetRecord& tileSet)
		{
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
				const Formats::TileSetData* data = session.GetTileSetData(tileSet.Name);
				return (data != nullptr ? data->TileCount : -1);
			}
			return session.GetTileSetLibrary().GetTileCount(tileSet.Name);
		}

		// Matches the colors of an extra tile set to the palette the level is shown with
		bool MatchColors(EditorSession& session, Formats::ExtraTileSetRecord& tileSet)
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

		/** Row of the list of extra tile sets */
		class ExtraTileSetRow : public UiElement
		{
		public:
			ExtraTileSetRow(EditorSession& session, std::shared_ptr<TileSetsSelection> selection, std::int32_t index)
				: _session(session), _selection(std::move(selection)), _index(index)
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
				String range = FormatInteger(tileSet.Offset) + "–"_s + FormatInteger(tileSet.Offset + tileSet.Count - 1);
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
				}
				return false;
			}

		private:
			EditorSession& _session;
			std::shared_ptr<TileSetsSelection> _selection;
			std::int32_t _index;
		};

		/** The list of extra tile sets, rebuilt when their number changes */
		class ExtraTileSetList : public UiStack
		{
		public:
			ExtraTileSetList(EditorSession& session, std::shared_ptr<TileSetsSelection> selection)
				: UiStack(UiOrientation::Vertical, 1.0f), _session(session), _selection(std::move(selection)), _shownCount(-1)
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
						Add<UiLabel>(_("The level has no extra tile sets"), UiTheme::TextDim);
					}
				}
				UiStack::Update(timeMult);
			}

		private:
			EditorSession& _session;
			std::shared_ptr<TileSetsSelection> _selection;
			std::int32_t _shownCount;
		};
	}

	bool SetDefaultTileSet(EditorSession& session, StringView name)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t tileCount = session.GetTileSetLibrary().GetTileCount(name);
		if (tileCount <= 0) {
			session.ShowMessage(_("The tile set can't be read"));
			return false;
		}
		if (name == document.Properties.DefaultTileSet) {
			return true;
		}
		// The tiles of the default tile set keep their indices, so they show what is there in the new one
		String newName = name;
		ChangeTileSets(session, _("Change default tile set"), true, [&newName, tileCount](LevelDocument& document) {
			document.Properties.DefaultTileSet = std::move(newName);
			document.DefaultTileCount = tileCount;
		}, [](TileRef&) { return true; });
		return true;
	}

	bool AddExtraTileSet(EditorSession& session, StringView name)
	{
		if (session.GetDocument().ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
			session.ShowMessage(_("A level can't have more extra tile sets"));
			return false;
		}
		std::int32_t tileCount = session.GetTileSetLibrary().GetTileCount(name);
		if (tileCount <= 0) {
			session.ShowMessage(_("The tile set can't be read"));
			return false;
		}
		// All of its tiles after the tiles of the tile sets before it, it doesn't move any of them
		Formats::ExtraTileSetRecord tileSet = {};
		tileSet.Name = name;
		tileSet.Offset = 0;
		tileSet.Count = std::uint16_t(std::min(tileCount, std::int32_t(UINT16_MAX)));
		if (HasTrueColorTiles(name)) {
			tileSet.Flags |= Formats::ExtraTileSetRecord::TrueColor;
		}
		ChangeTileSets(session, _("Add tile set"), false, [&tileSet](LevelDocument& document) {
			document.ExtraTileSets.push_back(std::move(tileSet));
		}, [](TileRef&) { return true; });
		return true;
	}

	void ShowTileSetsDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto selection = std::make_shared<TileSetsSelection>();
		auto dialog = std::make_unique<UiDialog>(_("Tile Sets"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
		content->Width = 260.0f;

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
					text += " ("_s + _fn("{} tile", "{} tiles", document.DefaultTileCount, document.DefaultTileCount) + ")"_s;
				}
				return text;
			};
			auto* pick = row->Add<UiButton>(StringView(), EditorIcon::Folder, [s]() {
				PickTileSet(*s, _("Default Tile Set"), s->GetDocument().Properties.DefaultTileSet, [s](StringView name) {
					SetDefaultTileSet(*s, name);
				});
			});
			pick->Tooltip = _("Choose another default tile set, the tiles keep their positions in it");
		}

		content->Add<UiSeparator>();
		content->Add<UiLabel>(_("Extra tile sets"), UiTheme::TextHeader);
		auto* listScroll = content->Add<UiScrollView>();
		listScroll->Height = 80.0f;
		listScroll->SetContent<ExtraTileSetList>(session, selection);

		{
			auto* buttons = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			buttons->Add<UiButton>(_("Add"), EditorIcon::Plus, [s, selection]() {
				if (s->GetDocument().ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
					s->ShowMessage(_("A level can't have more extra tile sets"));
					return;
				}
				PickTileSet(*s, _("Add Tile Set"), {}, [s, selection](StringView name) {
					if (AddExtraTileSet(*s, name)) {
						selection->Index = std::int32_t(s->GetDocument().ExtraTileSets.size()) - 1;
					}
				});
			});
			auto* remove = buttons->Add<UiButton>(StringView(), EditorIcon::Delete, [s, selection, selected]() mutable {
				if (selected() == nullptr) {
					return;
				}
				std::uint8_t source = std::uint8_t(selection->Index + 1);
				std::int32_t index = selection->Index;
				ChangeTileSets(*s, _("Remove tile set"), false, [index](LevelDocument& document) {
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
				selection->Index = std::max(0, std::min(index, std::int32_t(s->GetDocument().ExtraTileSets.size()) - 1));
			});
			remove->Tooltip = _("Remove the tile set, its tiles are cleared from the level");
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
					ChangeTileSets(*s, _("Move tile set"), false, [index, target](LevelDocument& document) {
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
				});
				move->Tooltip = (direction < 0 ? _("Move up, its tiles get lower tile IDs") : _("Move down, its tiles get higher tile IDs"));
				move->IsEnabledFunc = [s, selection, direction]() {
					std::int32_t target = selection->Index + direction;
					return (selection->Index >= 0 && target >= 0 && target < std::int32_t(s->GetDocument().ExtraTileSets.size()));
				};
			}
		}

		// The selected extra tile set
		auto hasSelected = [selected]() mutable { return (selected() != nullptr); };
		{
			auto* row = content->AddRow(_("Tile set"));
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
				PickTileSet(*s, _("Replace Tile Set"), tileSet->Name, [s, index](StringView name) {
					LevelDocument& document = s->GetDocument();
					std::int32_t tileCount = s->GetTileSetLibrary().GetTileCount(name);
					if (tileCount <= 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
						s->ShowMessage(_("The tile set can't be read"));
						return;
					}
					// The same part of the other tile set, as much of it as there is
					String newName = name;
					bool trueColor = HasTrueColorTiles(name);
					ChangeTileSets(*s, _("Replace tile set"), false, [index, &newName, tileCount, trueColor](LevelDocument& document) {
						Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[index];
						tileSet.Name = std::move(newName);
						// The tiles the level had in itself are gone then
						tileSet.Flags &= ~Formats::ExtraTileSetRecord::Embedded;
						tileSet.Data.clear();
						tileSet.Offset = std::uint16_t(std::min<std::int32_t>(tileSet.Offset, tileCount - 1));
						tileSet.Count = std::uint16_t(std::min<std::int32_t>(tileSet.Count, tileCount - tileSet.Offset));
						tileSet.Flags = (trueColor ? (tileSet.Flags | Formats::ExtraTileSetRecord::TrueColor) : (tileSet.Flags & ~Formats::ExtraTileSetRecord::TrueColor));
					}, [](TileRef&) { return true; });
				});
			});
			pick->Tooltip = _("Use another tile set in its place");
			pick->IsEnabledFunc = hasSelected;
		}
		{
			auto* row = content->AddRow(_("First tile"));
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
				std::int32_t offset = std::max(0, std::int32_t(std::round(value)));
				if (fileTileCount > 0) {
					offset = std::min(offset, fileTileCount - 1);
				}
				if (offset == tileSet.Offset) {
					return;
				}
				// The tiles keep showing the same pictures, those that aren't taken anymore are cleared
				std::uint8_t source = std::uint8_t(index + 1);
				std::int32_t delta = std::int32_t(tileSet.Offset) - offset;
				ChangeTileSets(*s, _("Tile set range"), false, [index, offset, fileTileCount](LevelDocument& document) {
					Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[index];
					tileSet.Offset = std::uint16_t(offset);
					if (fileTileCount > 0) {
						tileSet.Count = std::uint16_t(std::max(1, std::min<std::int32_t>(tileSet.Count, fileTileCount - offset)));
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
			number->Tooltip = _("First tile taken from the tile set");
			number->IsEnabledFunc = hasSelected;
		}
		{
			auto* row = content->AddRow(_("Tiles"));
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
				std::int32_t count = std::max(1, std::int32_t(std::round(value)));
				if (fileTileCount > 0) {
					count = std::min(count, fileTileCount - tileSet.Offset);
				}
				if (count == tileSet.Count) {
					return;
				}
				// Tiles of the tile sets after it move with it, those past the end of it are cleared
				ChangeTileSets(*s, _("Tile set range"), false, [index, count](LevelDocument& document) {
					document.ExtraTileSets[index].Count = std::uint16_t(count);
				}, [](TileRef&) { return true; });
			}, 1.0f, 65535.0f, 1.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _("Number of tiles taken from the tile set");
			number->IsEnabledFunc = hasSelected;
		}
		{
			auto* check = content->Add<UiCheckBox>(_("Match colors to the level palette"), [selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr && (tileSet->Flags & Formats::ExtraTileSetRecord::Remapped) != 0);
			}, [s, selection](bool value) {
				LevelDocument& document = s->GetDocument();
				std::int32_t index = selection->Index;
				if (index < 0 || index >= std::int32_t(document.ExtraTileSets.size())) {
					return;
				}
				Formats::ExtraTileSetRecord tileSet = document.ExtraTileSets[index];
				if (value) {
					if (!MatchColors(*s, tileSet)) {
						s->ShowMessage(_("The palettes can't be read"));
						return;
					}
					tileSet.Flags |= Formats::ExtraTileSetRecord::Remapped;
				} else {
					tileSet.Flags &= ~Formats::ExtraTileSetRecord::Remapped;
				}
				// Only the colors change, all tiles stay where they are
				ChangeTileSets(*s, _("Tile set colors"), false, [index, &tileSet](LevelDocument& document) {
					document.ExtraTileSets[index] = std::move(tileSet);
				}, [](TileRef&) { return true; });
			});
			check->Tooltip = _("Each color of the tile set is replaced by the closest one of the palette of the level, "
				"otherwise its colors are the entries of the level palette of the same index");
			check->IsEnabledFunc = [selected]() mutable {
				Formats::ExtraTileSetRecord* tileSet = selected();
				return (tileSet != nullptr && (tileSet->Flags & Formats::ExtraTileSetRecord::TrueColor) == 0);
			};
		}

		content->Add<UiSeparator>();
		{
			auto* usage = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
			usage->TextFunc = [s]() {
				const LevelDocument& document = s->GetDocument();
				std::int32_t staticCount = document.GetStaticTileCount();
				std::int32_t used = std::max(staticCount, 0) + std::int32_t(document.Animations.size());
				String text = _f("Tile IDs used: {} of {}", used, document.TileIdSpace);
				if (used > LevelDocument::ScriptTileIdLimit) {
					text += " "_s + _("(scripts can't refer to all of them)");
				}
				return text;
			};
		}

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}

	String GetTileSetLabel(StringView name)
	{
		return (name.hasPrefix(':') ? String(_("Tiles of this level")) : String(name));
	}

	bool IsLevelTileSet(const LevelDocument& document, std::uint8_t source)
	{
		return (source != TileRef::DefaultTileSet && std::int32_t(source) - 1 < std::int32_t(document.ExtraTileSets.size()) &&
			(document.ExtraTileSets[std::size_t(source) - 1].Flags & Formats::ExtraTileSetRecord::Embedded) != 0);
	}

	bool ReadTileSetOf(EditorSession& session, std::uint8_t source, Formats::TileSetData& tileSet)
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

	namespace
	{
		// The palette the level is shown in, its own one or the one of its default tile set
		void GetLevelPalette(EditorSession& session, std::uint32_t (&palette)[256])
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
		String GetTargetLabel(EditorSession& session, const TileTarget& target)
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
		bool LoadTarget(EditorSession& session, const TileTarget& target, Formats::TileSetData& tileSet)
		{
			const LevelDocument& document = session.GetDocument();
			bool newSlot = (target.Type == TileTarget::Kind::NewTileSet || (target.Type == TileTarget::Kind::Level && FindLevelTileSet(document) < 0));
			if (newSlot && document.ExtraTileSets.size() >= TileRef::LastExtraTileSet) {
				session.ShowMessage(_("A level can't have more extra tile sets"));
				return false;
			}
			switch (target.Type) {
				case TileTarget::Kind::NewTileSet: {
					if (!ContentTileSetLibrary::IsValidName(target.Name)) {
						session.ShowMessage(_("A tile set is named by lowercase letters, digits, dashes and underscores"));
						return false;
					}
					if (!ContentTileSetLibrary::FindTileSetFile(target.Name).empty()) {
						session.ShowMessage(_("A tile set of this name exists already"));
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
							session.ShowMessage(_("The tile set can't be read"));
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
						session.ShowMessage(_("The tile set can't be read"));
						return false;
					}
					return true;
				}
			}
		}

		// Adds tiles to the end of the tile set read by LoadTarget(), saves it and makes the level follow, as one step
		bool StoreTiles(EditorSession& session, const TileTarget& target, Formats::TileSetData& tileSet, const Formats::TileSetData& from,
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
				session.ShowMessage(_("A tile set can't have more tiles"));
				return false;
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
					session.ShowMessage(_("The tile set couldn't be saved"));
					return false;
				}
			} else {
				String path = ContentTileSetLibrary::GetWritablePath(name, true);
				if (path.empty()) {
					session.ShowMessage(_("There's no place to save tile sets to"));
					return false;
				}
				String tempPath = path + ".tmp"_s;
				auto s = fs::Open(tempPath, FileAccess::Write);
				bool written = (s->IsValid() && Formats::TileSetFile::Write(*s, tileSet));
				s = nullptr;
				if (!written || !fs::Move(tempPath, path)) {
					fs::RemoveFile(tempPath);
					session.ShowMessage(_("The tile set couldn't be saved"));
					return false;
				}
				session.InvalidateTileSet(name);
			}

			// The tiles of the level stay where they are, the tile set has more of them, and an extra one that takes all of
			// its tiles takes the new ones too
			std::int32_t newCount = tileSet.TileCount;
			bool trueColor = tileSet.HasTrueColorTiles();
			std::uint16_t offset = (slot >= 0 ? document.ExtraTileSets[slot].Offset : 0);
			bool takesAll = (slot >= 0 && std::int32_t(offset) + std::int32_t(document.ExtraTileSets[slot].Count) >= oldCount);
			std::uint8_t source = (isDefault ? TileRef::DefaultTileSet : std::uint8_t(newSlot ? document.ExtraTileSets.size() + 1 : slot + 1));
			String label = (replace != nullptr ? String(_("Copy tile into the level")) : (newSlot ? String(_("Add tile set")) : String(_("Add tiles to tile set"))));
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
					record.Count = std::uint16_t(std::min(newCount, std::int32_t(UINT16_MAX)));
					record.Flags = std::uint8_t((embedded ? Formats::ExtraTileSetRecord::Embedded : 0) | (trueColor ? Formats::ExtraTileSetRecord::TrueColor : 0));
					record.Data = std::move(bytes);
					changed.ExtraTileSets.push_back(std::move(record));
					return;
				}
				Formats::ExtraTileSetRecord& record = changed.ExtraTileSets[slot];
				if (embedded) {
					record.Data = std::move(bytes);
					if (trueColor) {
						record.Flags |= Formats::ExtraTileSetRecord::TrueColor;
					}
				}
				if (takesAll) {
					record.Count = std::uint16_t(std::min(newCount - std::int32_t(record.Offset), std::int32_t(UINT16_MAX)));
				}
			}, std::move(remap), std::move(prepare));
			if (embedded) {
				session.InvalidateTileSet(name);
			}

			first = replacement;
			return true;
		}
	}

	void AddTileTargetRow(EditorSession& session, UiStack* row, std::shared_ptr<TileTarget> target)
	{
		EditorSession* s = &session;
		auto* dropdown = row->Add<UiDropdown>([s, target]() {
			return (target->Type == TileTarget::Kind::NewTileSet ? String(_("New tile set")) : GetTargetLabel(*s, *target));
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
				entries.push_back(std::move(entry));
			}
			UiMenuEntry newEntry;
			newEntry.Label = _("New tile set");
			newEntry.IsChecked = [target]() { return (target->Type == TileTarget::Kind::NewTileSet); };
			newEntry.Execute = [target]() { target->Type = TileTarget::Kind::NewTileSet; };
			entries.push_back(std::move(newEntry));
			UiMenuEntry levelEntry;
			levelEntry.Label = _("Tiles of this level");
			levelEntry.IsChecked = [target]() { return (target->Type == TileTarget::Kind::Level); };
			levelEntry.Execute = [target]() { target->Type = TileTarget::Kind::Level; };
			entries.push_back(std::move(levelEntry));
		});
		dropdown->Width = 170.0f;
		dropdown->Tooltip = _("Tiles of this level are stored in the level itself, not in any tile set file");

		if (target->Name.empty()) {
			// A name of the level, as tile sets are named
			char buffer[26];
			std::size_t length = 0;
			for (char c : fs::GetFileName(session.GetLevelName())) {
				char l = (c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
				if (((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-') && length < sizeof(buffer)) {
					buffer[length++] = l;
				}
			}
			target->Name = (length == 0 ? String("tiles"_s) : String(StringView(buffer, length) + "-tiles"_s));
		}
		auto* nameBox = row->Add<UiTextBox>(target->Name);
		nameBox->Width = 110.0f;
		nameBox->MaxLength = 32;
		nameBox->Tooltip = _("The tile set is saved in the user content, Tilesets/<name>.j2t");
		nameBox->OnChanged = [target](StringView text) { target->Name = String(text).trimmed(); };
		nameBox->OnCommit = [target](StringView text) { target->Name = String(text).trimmed(); };
		nameBox->IsVisibleFunc = [target]() { return (target->Type == TileTarget::Kind::NewTileSet); };
	}

	void AddTileTargetNote(EditorSession& session, UiStack* parent, std::shared_ptr<TileTarget> target)
	{
		EditorSession* s = &session;
		auto* note = parent->Add<UiLabel>(StringView(), UiTheme::TextAccent);
		note->TextFunc = [s, target]() {
			switch (target->Type) {
				case TileTarget::Kind::Level:
					return String(_("The level keeps the tiles, no other level has them"));
				case TileTarget::Kind::NewTileSet:
					return String();
				default: {
					if (IsLevelTileSet(s->GetDocument(), target->Source)) {
						return String();
					}
					// Its copy in the user content is written over, any other one is covered by a copy there
					String name = GetTargetLabel(*s, *target);
					bool ownCopy = (ContentTileSetLibrary::FindTileSetFile(name) == ContentTileSetLibrary::GetWritablePath(name, false));
					return (ownCopy ? String() : String(_("A copy in the user content is used instead of it from now on")));
				}
			}
		};
	}

	bool CopyTileIntoLevel(EditorSession& session, const TileRef& tile, TileRef& copy)
	{
		const LevelDocument& document = session.GetDocument();
		if (tile.IsAnimated() || tile.IsEmpty() || tile.Source == TileRef::Raw || IsLevelTileSet(document, tile.Source)) {
			return false;
		}
		Formats::TileSetData original;
		if (!ReadTileSetOf(session, tile.Source, original)) {
			session.ShowMessage(_("The tile set can't be read"));
			return false;
		}
		const Formats::ExtraTileSetRecord* extra = (tile.Source != TileRef::DefaultTileSet ? &document.ExtraTileSets[std::size_t(tile.Source) - 1] : nullptr);
		std::int32_t index = tile.Index + (extra != nullptr ? extra->Offset : 0);
		if (index >= original.TileCount) {
			return false;
		}

		// The tile as the level shows it: in the palette of the level, and as the level changes it
		Formats::TileSetData from;
		from.Resize(1);
		std::memcpy(&from.Images[0], &original.Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
		std::memcpy(&from.Masks[0], &original.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
		from.TrueColor[0] = original.TrueColor[index];
		if (from.TrueColor[0] == 0 && extra != nullptr && (extra->Flags & Formats::ExtraTileSetRecord::Remapped) != 0 &&
			(extra->Flags & Formats::ExtraTileSetRecord::TrueColor) == 0) {
			for (std::int32_t i = 0; i < 32 * 32; i++) {
				from.Images[std::size_t(i)] = extra->Remapping[from.Images[std::size_t(i)]];
			}
		}
		std::int32_t tileId = document.ToTileId(tile);
		if (const Formats::TileDiffuseOverride* diffuse = SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId))) {
			from.TrueColor[0] = 0;
			std::memcpy(&from.Images[0], diffuse->Indices, sizeof(diffuse->Indices));
		}
		if (const Formats::TileMaskOverride* mask = SetTileOverrideCommand::FindMask(document, std::uint16_t(tileId))) {
			std::memset(&from.Masks[0], 0, Formats::TileSetData::MaskBytesPerTile);
			for (std::int32_t i = 0; i < 32 * 32; i++) {
				if (mask->Mask[i] != 0) {
					from.Masks[std::size_t(i / 8)] |= std::uint8_t(1 << (i % 8));
				}
			}
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

	bool AppendTiles(EditorSession& session, const TileTarget& target, const Formats::TileSetData& tiles, TileRef& first)
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

	bool AddTilesFromImage(EditorSession& session, const TileTarget& target, StringView imagePath, bool skipExisting, bool dither)
	{
		PngImage image, masks;
		bool hasMasks = false;
		{
			auto s = fs::Open(imagePath, FileAccess::Read);
			String error;
			if (!s->IsValid() || !PngCodec::Decode(*s, image, &error)) {
				session.ShowMessage(_f("The image can't be read: {}", error));
				return false;
			}
			// The masks next to it, as the editor exports them
			String maskPath = String(imagePath.exceptSuffix(std::size_t(4))) + ".mask.png"_s;
			if (fs::IsReadableFile(maskPath)) {
				auto m = fs::Open(maskPath, FileAccess::Read);
				hasMasks = (m->IsValid() && PngCodec::Decode(*m, masks));
			}
		}

		Formats::TileSetData tileSet;
		if (!LoadTarget(session, target, tileSet)) {
			return false;
		}
		String name = GetTargetLabel(session, target);

		// The closest colors of the palette of the tile set, its file keeps indices of its own palette
		TileImportOptions options;
		options.Colors = TileImportColors::MatchPalette;
		options.Palette = tileSet.Palette;
		options.Dither = dither;
		options.SkipDuplicates = true;
		options.Masks = (hasMasks ? &masks : nullptr);
		Formats::TileSetData sliced;
		TileImportResult result;
		if (!SliceTileSet(image, options, sliced, result)) {
			session.ShowMessage(_("The image is smaller than a tile"));
			return false;
		}

		// Tiles it has already are found by their pixels and mask
		auto hashTile = [](const Formats::TileSetData& data, std::int32_t tile) {
			std::uint64_t hash = 14695981039346656037ull;
			auto add = [&hash](const std::uint8_t* bytes, std::size_t size) {
				for (std::size_t i = 0; i < size; i++) {
					hash = (hash ^ bytes[i]) * 1099511628211ull;
				}
			};
			std::size_t imageBytes = (data.TrueColor[tile] != 0 ? std::size_t(Formats::TileSetData::ImageBytesPerTile) : 32 * 32);
			add(&data.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile], imageBytes);
			add(&data.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
			return hash ^ data.TrueColor[tile];
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
			session.ShowMessage(_("The tile set has all tiles of the image already"));
			return false;
		}
		TileRef first;
		if (!StoreTiles(session, target, tileSet, sliced, added, first)) {
			return false;
		}
		LOGI("{} tiles of \"{}\" added to \"{}\"", std::int32_t(added.size()), imagePath, name);

		std::int32_t count = std::int32_t(added.size());
		String message = _fn("{} tile was added to tile set \"{}\", from tile {}", "{} tiles were added to tile set \"{}\", from tile {}", count, count, name, oldCount);
		if (result.ColorsChanged) {
			message += " "_s + _("- some colors became the closest ones of its palette");
		}
		session.ShowMessage(message);
		return true;
	}

	void ShowAddTilesFromImageDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		EditorSession* s = &session;
		SmallVector<UiFileRoot, 4> roots;
		session.GetImageRoots(false, roots);
		UiFileDialog::Show(*surface, _("Add Tiles from Image"), UiFileDialogMode::Open, std::move(roots), ".png"_s, {}, {}, [s](StringView path) {
			struct AddState
			{
				String Path;
				bool SkipExisting = true;
				bool Dither = false;
			};
			auto state = std::make_shared<AddState>();
			state->Path = path;
			auto target = std::make_shared<TileTarget>();

			auto dialog = std::make_unique<UiDialog>(_("Add Tiles from Image"));
			UiDialog* dialogPtr = dialog.get();
			auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
			content->Add<UiLabel>(_("The tiles of the image go to the end of the tile set, in its colors."), UiTheme::TextDim);
			AddTileTargetRow(*s, content->AddRow(_("Add to")), target);
			content->Add<UiCheckBox>(_("Skip tiles the tile set has already"), [state]() { return state->SkipExisting; },
				[state](bool value) { state->SkipExisting = value; });
			content->Add<UiCheckBox>(_("Mix colors between two of the palette (dither)"), [state]() { return state->Dither; },
				[state](bool value) { state->Dither = value; });
			AddTileTargetNote(*s, content, target);

			dialog->AddButton(_("Add Tiles"), [s, state, target, dialogPtr]() {
				if (AddTilesFromImage(*s, *target, state->Path, state->SkipExisting, state->Dither)) {
					dialogPtr->Close();
				}
			}, true);
			dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

			UiOverlayOptions options;
			options.Centered = true;
			options.Modal = true;
			options.CloseOnOutsideClick = false;
			s->GetSurface()->PushOverlay(std::move(dialog), std::move(options));
		});
	}
}

#endif
