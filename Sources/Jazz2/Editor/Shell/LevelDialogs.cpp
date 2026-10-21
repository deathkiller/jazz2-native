#include "LevelDialogs.h"

#if defined(WITH_EDITOR)

#include "PropertyRows.h"
#include "ShellCommon.h"
#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../Document/DocumentSnapshot.h"
#include "../Document/LevelDocument.h"
#include "../Tools/EditorTool.h"
#include "../Document/Commands.h"
#include "../Document/TerrainSplines.h"
#include "LevelTextLanguages.h"
#include "../../Formats/LevelTextsChunk.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../PreferencesCache.h"
#include "../../WeatherType.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float LabelWidth = 96.0f;

		UiStack* AddRow(UiElement* parent, StringView label)
		{
			return AddLabeledRow(parent, label, {}, LabelWidth);
		}

		// Changes the flags or the properties as an undoable step, the changes of a value while it's dragged are merged,
		// @p final ends the step, a change that isn't dragged is always final
		void ChangeProperties(IEditorContext& session, Function<void(LevelFlags&, Formats::LevelProperties&)>&& change, bool final = true)
		{
			LevelDocument& document = session.GetDocument();
			LevelFlags flags = document.Flags;
			Formats::LevelProperties properties = document.Properties;
			change(flags, properties);
			const Formats::LevelProperties& p = document.Properties;
			bool changed = (flags != document.Flags || properties.DisplayName != p.DisplayName || properties.Author != p.Author ||
				properties.NextLevel != p.NextLevel || properties.SecretLevel != p.SecretLevel || properties.BonusLevel != p.BonusLevel ||
				properties.DefaultTileSet != p.DefaultTileSet || properties.Music != p.Music || properties.AmbientColor != p.AmbientColor ||
				properties.Weather != p.Weather || properties.WeatherIntensity != p.WeatherIntensity || properties.WaterLevel != p.WaterLevel ||
				properties.CaptionTileId != p.CaptionTileId);
			if (changed) {
				// TRANSLATORS: Name of an undo step in the history, a change in the Level Properties dialog
				session.Execute(std::make_unique<SetLevelPropertiesCommand>(document, flags, properties, _x("editor", "Level properties")), true);
			}
			if (final) {
				session.GetHistory().BreakMerge();
			}
		}

		void SetTextProperty(IEditorContext& session, String Formats::LevelProperties::* member, StringView text)
		{
			if (session.GetDocument().Properties.*member != text) {
				String value = text.trimmed();
				ChangeProperties(session, [member, &value](LevelFlags&, Formats::LevelProperties& properties) { properties.*member = value; });
			}
		}

		UiStack* AddTextProperty(UiElement* parent, IEditorContext& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			IEditorContext* s = &session;
			auto* row = AddRow(parent, label);
			auto* box = row->Add<UiTextBox>();
			// At least this wide, wider in a wider dialog
			box->Width = 190.0f;
			box->Flex = 1.0f;
			box->MaxLength = 255;
			box->Placeholder = placeholder;
			box->TextFunc = [s, member]() { return s->GetDocument().Properties.*member; };
			box->OnCommit = [s, member](StringView text) {
				SetTextProperty(*s, member, text);
			};
			return row;
		}

		// A level of the same episode is written without the episode, as the original levels have it
		String MakeLevelReference(StringView levelName, StringView currentLevelName)
		{
			auto picked = levelName.partition('/');
			auto current = currentLevelName.partition('/');
			if (!picked[1].empty() && !current[1].empty() && picked[0] == current[0]) {
				return picked[2];
			}
			return levelName;
		}

		void AddLevelProperty(UiElement* parent, IEditorContext& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			IEditorContext* s = &session;
			auto* row = AddTextProperty(parent, session, label, member, placeholder);
			String title = label;
			auto* pick = row->Add<UiButton>(StringView(), EditorIcon::Folder, [s, member, title]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				SmallVector<UiFileRoot, 4> roots;
				s->GetFileRoots(false, roots);
				// Opens where the level is now, or next to the level being edited, the next level is usually there
				StringView filePath = s->GetFilePath();
				String directory = (!filePath.empty() ? String(fs::GetDirectoryName(filePath)) : String());
				String fileName;
				StringView current = s->GetDocument().Properties.*member;
				StringView episode = s->GetLevelName().partition('/')[0];
				if (!current.empty() && (current.contains('/') || !episode.empty())) {
					String levelName = (current.contains('/') ? String(current) : String(episode + "/"_s + current));
					for (const UiFileRoot& root : roots) {
						String path = fs::FindPathCaseInsensitive(fs::CombinePath(root.Path, String(levelName + ".j2l"_s)));
						if (fs::IsReadableFile(path)) {
							directory = fs::GetDirectoryName(path);
							fileName = fs::GetFileName(path);
							break;
						}
					}
				}
				UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, Death::move(roots), ".j2l"_s, directory, fileName, [s, member](StringView path) {
					// The level is looked up by its name, so it doesn't matter which root it was picked from
					SetTextProperty(*s, member, MakeLevelReference(IEditorContext::MakeLevelName(path), s->GetLevelName()));
				});
			});
			pick->Tooltip = _x("editor", "Choose a level");
		}

		void AddMusicProperty(UiElement* parent, IEditorContext& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			IEditorContext* s = &session;
			auto* row = AddTextProperty(parent, session, label, member, placeholder);
			String title = label;
			auto* pick = row->Add<UiButton>(StringView(), EditorIcon::Folder, [s, member, title]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				// As ContentResolver::GetMusic() looks for it, the value is the path relative to one of them
				auto& resolver = ContentResolver::Get();
				SmallVector<UiFileRoot, 4> roots;
				StringView userContent = resolver.GetUserContentPath();
				if (!userContent.empty()) {
					// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of content added by the user
					roots.push_back({ _x("editor", "User content"), fs::CombinePath(userContent, "Music"_s), false });
				}
				StringView content = resolver.GetContentPath();
				if (!content.empty()) {
					// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of content of the game
					roots.push_back({ _x("editor", "Content"), fs::CombinePath(content, "Music"_s), false });
				}
				StringView source = resolver.GetSourcePath();
				if (!source.empty()) {
					// Without the trailing separator, so the paths of files in it can be made relative to it
					// TRANSLATORS: Place in the file dialog choosing the music of the level, the folder of the original game
					roots.push_back({ _x("editor", "Original game"), String(source.trimmedSuffix("/\\"_s)), false });
				}
				// Opens where the music is now, the first place that has it is the one that is played
				StringView current = s->GetDocument().Properties.*member;
				String directory, fileName;
				SmallVector<String, 4> rootPaths;
				for (std::size_t i = 0; i < roots.size(); ) {
					if (!fs::DirectoryExists(roots[i].Path)) {
						roots.erase(roots.begin() + i);
						continue;
					}
					if (directory.empty() && !current.empty()) {
						String path = fs::FindPathCaseInsensitive(fs::CombinePath(roots[i].Path, current));
						if (fs::IsReadableFile(path)) {
							directory = fs::GetDirectoryName(path);
							fileName = fs::GetFileName(path);
						}
					}
					rootPaths.push_back(roots[i].Path);
					i++;
				}
				UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, Death::move(roots), ".j2b;.it;.mo3;.mod;.s3m;.xm;.ogg;.wav"_s, directory, fileName,
					[s, member, rootPaths = Death::move(rootPaths)](StringView path) {
						String value = fs::GetFileName(path);
						for (const String& rootPath : rootPaths) {
							if (path.hasPrefix(rootPath)) {
								StringView rest = path.exceptPrefix(rootPath.size());
								while (!rest.empty() && (rest[0] == '/' || rest[0] == '\\')) {
									rest = rest.exceptPrefix(1);
								}
								value = fs::FromNativeSeparators(rest);
								break;
							}
						}
						SetTextProperty(*s, member, value);
					});
			});
			pick->Tooltip = _x("editor", "Choose music, it starts playing right away");
		}

		// A tile of the default tile set that shows the level well on an RGB keyboard: covered by its pixels, colorful
		// and used a lot, the score is all three multiplied
		std::int32_t PickCaptionTile(IEditorContext& session)
		{
			const LevelDocument& document = session.GetDocument();
			const Formats::TileSetData* tileSet = session.GetTileSetData(document.Properties.DefaultTileSet);
			if (tileSet == nullptr || tileSet->TileCount <= 0) {
				return -1;
			}
			SmallVector<std::int32_t, 0> usage;
			usage.resize(std::size_t(tileSet->TileCount), 0);
			for (const DocumentLayer& layer : document.Layers) {
				for (const TileRef& tile : layer.Tiles) {
					if (tile.Source == TileRef::DefaultTileSet && tile.Index < usage.size() && !tile.IsEmpty()) {
						usage[tile.Index]++;
					}
				}
			}

			const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
			std::int32_t best = -1;
			float bestScore = 0.0f;
			for (std::int32_t index = 1; index < tileSet->TileCount; index++) {
				if (usage[index] == 0) {
					continue;
				}
				const std::uint8_t* pixels = &tileSet->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
				bool trueColor = (tileSet->TrueColor[index] != 0);
				float saturation = 0.0f;
				std::int32_t covered = 0;
				for (std::int32_t i = 0; i < 32 * 32; i++) {
					float r, g, b;
					if (trueColor) {
						if (pixels[i * 4 + 3] < 128) {
							continue;
						}
						r = pixels[i * 4] / 255.0f; g = pixels[i * 4 + 1] / 255.0f; b = pixels[i * 4 + 2] / 255.0f;
					} else {
						if (pixels[i] == 0) {
							continue;
						}
						std::uint32_t color = palette[pixels[i]];
						r = (color & 0xFF) / 255.0f; g = ((color >> 8) & 0xFF) / 255.0f; b = ((color >> 16) & 0xFF) / 255.0f;
					}
					float max = std::max({ r, g, b }), min = std::min({ r, g, b });
					saturation += (max > 0.0f ? (max - min) / max : 0.0f) * max;
					covered++;
				}
				if (covered == 0) {
					continue;
				}
				float coverage = covered / 1024.0f;
				float score = coverage * coverage * (saturation / covered + 0.05f) * std::log(float(usage[index]) + 1.0f);
				if (score > bestScore) {
					bestScore = score;
					best = index;
				}
			}
			return best;
		}

		// A byte of the ambient color, 0 is red, 3 is the light intensity
		void AddColorByte(UiElement* row, IEditorContext& session, std::int32_t byteIndex)
		{
			IEditorContext* s = &session;
			std::int32_t shift = byteIndex * 8;
			auto* number = AddByteBox(row, [s, shift]() {
				return std::uint8_t((s->GetDocument().Properties.AmbientColor >> shift) & 0xFF);
			}, [s, shift](std::uint8_t value, bool final) {
				std::uint32_t byte = value;
				ChangeProperties(*s, [shift, byte](LevelFlags&, Formats::LevelProperties& properties) {
					properties.AmbientColor = (properties.AmbientColor & ~(0xFFu << shift)) | (byte << shift);
				}, final);
			});
			number->Width = 36.0f;
		}

		// Formatting of a text is written out while it's edited, as a backslash and "f", so does a backslash itself
		String EscapeForEditing(StringView text)
		{
			String result;
			std::size_t start = 0;
			for (std::size_t i = 0; i < text.size(); i++) {
				if (text[i] == '\\' || text[i] == '\f') {
					result += text.slice(start, i);
					result += (text[i] == '\\' ? "\\\\"_s : "\\f"_s);
					start = i + 1;
				}
			}
			result += text.exceptPrefix(start);
			return result;
		}

		String UnescapeFromEditing(StringView text)
		{
			String result;
			std::size_t start = 0;
			for (std::size_t i = 0; i + 1 < text.size(); i++) {
				if (text[i] != '\\') {
					continue;
				}
				char next = text[i + 1];
				StringView replacement = (next == '\\' ? "\\"_s : (next == 'f' ? "\f"_s : (next == 'n' ? "\n"_s : StringView())));
				if (!replacement.empty()) {
					result += text.slice(start, i);
					result += replacement;
					start = i + 2;
					i++;
				}
			}
			result += text.exceptPrefix(start);
			return result;
		}
		/** The texts of the level, a row for each, rebuilt when their number changes */
		class TextsList : public UiStack
		{
		public:
			TextsList(IEditorContext& session, std::shared_ptr<LevelTextLanguageState> language)
				: UiStack(UiOrientation::Vertical, 2.0f, UiThickness(0.0f, 0.0f, 3.0f, 0.0f)), _session(session), _language(Death::move(language)), _shownCount(-1)
			{
			}

			void Update(float timeMult) override
			{
				std::int32_t count = std::int32_t(_session.GetDocument().Texts.size());
				if (count != _shownCount) {
					Rebuild(count);
				}
				UiStack::Update(timeMult);
			}

		private:
			IEditorContext& _session;
			// Language of the translations shown next to the texts, see LevelTextLanguages.h
			std::shared_ptr<LevelTextLanguageState> _language;
			std::int32_t _shownCount;

			void Rebuild(std::int32_t count)
			{
				_shownCount = count;
				ClearChildren();
				if (count == 0) {
					Add<UiLabel>(_x("editor", "The level has no texts"), UiTheme::TextDisabled);
					return;
				}
				IEditorContext* s = &_session;
				for (std::int32_t i = 0; i < count; i++) {
					char number[16];
					std::int32_t length = std::snprintf(number, sizeof(number), "%d", i);
					auto* row = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
					row->Add<UiLabel>(StringView(number, std::size_t(length)), UiTheme::TextDim)->Width = 22.0f;
					auto* box = row->Add<UiTextBox>();
					box->Flex = 1.0f;
					box->MaxLength = 4096;
					box->Multiline = true;
					box->TextFunc = [s, i]() {
						const auto& texts = s->GetDocument().Texts;
						return (i < std::int32_t(texts.size()) ? EscapeForEditing(texts[i]) : String());
					};
					box->OnCommit = [s, i](StringView text) {
						LevelDocument& document = s->GetDocument();
						String unescaped = UnescapeFromEditing(text);
						if (i >= std::int32_t(document.Texts.size()) || document.Texts[i] == unescaped) {
							return;
						}
						// TRANSLATORS: Name of an undo step in the history, a text of the level changed in the Level Texts dialog
						s->RecordSnapshotStep(_x("editor", "Level text"), SnapshotParts::Texts, [i, &unescaped](LevelDocument& document) {
							document.Texts[i] = Death::move(unescaped);
							return true;
						});
					};
					// The translation to the chosen language next to it
					AddLevelTextTranslationBox(row, _session, _language, i);
					// Moved up or down, the events that show it follow it
					auto* up = row->Add<UiButton>(StringView(), EditorIcon::ArrowUp, [s, i]() { SwapTexts(*s, i, i - 1); });
					up->Style = UiButtonStyle::Flat;
					up->Tooltip = _x("editor", "Moves the text up, the events showing it and its translations follow it");
					up->IsEnabledFunc = [i]() { return (i > 0); };
					auto* down = row->Add<UiButton>(StringView(), EditorIcon::ArrowDown, [s, i]() { SwapTexts(*s, i, i + 1); });
					down->Style = UiButtonStyle::Flat;
					down->Tooltip = _x("editor", "Moves the text down, the events showing it and its translations follow it");
					down->IsEnabledFunc = [s, i]() { return (i + 1 < std::int32_t(s->GetDocument().Texts.size())); };
				}
			}

			// Swaps two texts, as one step with the events that show them and their translations
			static void SwapTexts(IEditorContext& session, std::int32_t a, std::int32_t b)
			{
				LevelDocument& document = session.GetDocument();
				std::int32_t count = std::int32_t(document.Texts.size());
				if (a < 0 || b < 0 || a >= count || b >= count || a == b) {
					return;
				}
				if (!session.IsLayerEditable(document.GetSpriteLayerIndex())) {
					session.ShowMessage(_x("editor", "The sprite layer is locked or hidden, the events showing the texts couldn't follow them"));
					return;
				}

				CommandHistory& history = session.GetHistory();
				// TRANSLATORS: Name of an undo step in the history, a text of the level moved up or down in the Level Texts dialog
				history.BeginTransaction(_x("editor", "Move text"));
				session.RecordSnapshotStep(_x("editor", "Move text"), SnapshotParts::Texts | SnapshotParts::Chunks, [a, b](LevelDocument& document) {
					std::swap(document.Texts[a], document.Texts[b]);
					// The translations, as numbered as the texts
					if (const Formats::ChunkRecord* chunk = document.FindChunk(Formats::LevelTextsChunk::Tag)) {
						SmallVector<LocalizedLevelTexts, 0> languages;
						if (Formats::LevelTextsChunk::Read(chunk->Data, languages)) {
							for (LocalizedLevelTexts& language : languages) {
								if (language.Texts.size() <= std::size_t(std::max(a, b))) {
									language.Texts.resize(std::size_t(std::max(a, b)) + 1);
								}
								std::swap(language.Texts[a], language.Texts[b]);
							}
							SmallVector<std::uint8_t, 0> data;
							Formats::LevelTextsChunk::Write(languages, data);
							document.SetChunk(Formats::LevelTextsChunk::Tag, data);
						}
					}
					return true;
				});

				// The events showing them, on the grid and off it
				auto remap = [a, b](Formats::EventRecord& event) {
					const EventParamField* field = FindEventField(EventCatalog::Find(EventType(event.Type)), ParamType::TextId);
					if (field == nullptr) {
						return false;
					}
					std::int32_t index = EventCatalog::ReadField(event, *field);
					if (index != a && index != b) {
						return false;
					}
					EventCatalog::WriteField(event, *field, index == a ? b : a);
					return true;
				};
				Vector2i gridSize = document.GetEventGridSize();
				if (gridSize.X > 0) {
					auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Move text"));
					for (const DocumentEvent& event : document.Events) {
						Formats::EventRecord record = event.Record;
						if (remap(record)) {
							command->Set(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)), record);
						}
					}
					command->Seal();
					if (!command->IsEmpty()) {
						session.Execute(Death::move(command));
					}
				}
				SmallVector<Formats::OffGridEventRecord, 0> offGrid;
				bool offGridChanged = false;
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					Formats::OffGridEventRecord& copy = offGrid.emplace_back(event);
					offGridChanged |= remap(copy.Event);
				}
				if (offGridChanged) {
					session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), _x("editor", "Move text")));
				}
				history.EndTransaction();
			}
		};
	}

	namespace
	{
		/** A single tile, as high as a row */
		class TilePreview : public UiElement
		{
		public:
			TilePreview(IEditorContext& session, Function<std::int32_t()>&& tileId)
				: _session(session), _tileId(Death::move(tileId))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float size = context.Px(UiTheme::RowHeight);
				return Vector2f(size, size);
			}

			void Draw(UiPainter& painter) override
			{
				painter.RoundRect(Bounds, UiTheme::InputFill);
				std::int32_t tileId = _tileId();
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap != nullptr && tileId > 0) {
					float inset = std::max(1.0f, painter.Px(1.0f));
					painter.Tile(*tileMap, tileId, 0, Rectf(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f));
				}
			}

		private:
			IEditorContext& _session;
			Function<std::int32_t()> _tileId;
		};

		/** Tiles in a grid to choose one from, the frames of an animation or the animations themselves */
		class TileListGrid : public UiElement
		{
		public:
			static constexpr float CellSize = 24.0f;

			/** Number of tiles in a row, or 0 for as many as fit */
			std::int32_t FixedColumns = 0;

			TileListGrid(IEditorContext& session, Function<std::int32_t()>&& count, Function<std::int32_t(std::int32_t)>&& tileId,
				Function<std::int32_t()>&& selected, Function<void(std::int32_t)>&& onSelect)
				: _session(session), _count(Death::move(count)), _tileId(Death::move(tileId)), _selected(Death::move(selected)),
					_onSelect(Death::move(onSelect)), _cellSize(1.0f), _columns(1)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				if (FixedColumns > 0) {
					_columns = FixedColumns;
					_cellSize = std::max(1.0f, std::floor(available.X / float(FixedColumns)));
				} else {
					_cellSize = context.Px(CellSize);
					_columns = std::max<std::int32_t>(1, std::int32_t(available.X / _cellSize));
				}
				std::int32_t rows = std::max<std::int32_t>(1, (_count() + _columns - 1) / _columns);
				return Vector2f(available.X, rows * _cellSize);
			}

			void Draw(UiPainter& painter) override
			{
				painter.RoundRect(Bounds, UiTheme::InputFill);
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr) {
					return;
				}
				std::int32_t count = _count();
				std::int32_t selected = _selected();
				float inset = std::max(1.0f, painter.Px(1.0f));
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				for (std::int32_t i = 0; i < count; i++) {
					Rectf rect = GetCellRect(i);
					painter.Tile(*tileMap, _tileId(i), 0, Rectf(rect.X + inset, rect.Y + inset, rect.W - inset * 2.0f, rect.H - inset * 2.0f));
					if (i == selected) {
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
				if (event.Button != MouseButton::Left) {
					return false;
				}
				std::int32_t column = std::int32_t((event.Pos.X - Bounds.X) / _cellSize);
				std::int32_t row = std::int32_t((event.Pos.Y - Bounds.Y) / _cellSize);
				std::int32_t index = row * _columns + column;
				if (column >= 0 && column < _columns && index >= 0 && index < _count()) {
					_onSelect(index);
				}
				return false;
			}

		private:
			IEditorContext& _session;
			Function<std::int32_t()> _count;
			Function<std::int32_t(std::int32_t)> _tileId;
			Function<std::int32_t()> _selected;
			Function<void(std::int32_t)> _onSelect;
			float _cellSize;
			std::int32_t _columns;

			Rectf GetCellRect(std::int32_t index) const
			{
				return Rectf(Bounds.X + (index % _columns) * _cellSize, Bounds.Y + (index / _columns) * _cellSize, _cellSize, _cellSize);
			}
		};

		struct AnimationSelection
		{
			std::int32_t Animation = 0;
			std::int32_t Frame = 0;
		};

		// Changes the selected animated tile as an undoable step, the changes of a value while it's dragged are merged,
		// @p final ends the step, a change that isn't dragged is always final
		void ChangeAnimation(IEditorContext& session, std::int32_t index, const char* name, Function<void(DocumentAnimation&)>&& change, bool final = true)
		{
			LevelDocument& document = session.GetDocument();
			if (index < 0 || index >= std::int32_t(document.Animations.size())) {
				return;
			}
			DocumentAnimation animation = document.Animations[index];
			change(animation);
			session.Execute(std::make_unique<SetAnimationCommand>(document, index, Death::move(animation), _x("editor", name)), true);
			if (final) {
				session.GetHistory().BreakMerge();
			}
		}

		void AddAnimationNumber(UiElement* parent, IEditorContext& session, const std::shared_ptr<AnimationSelection>& selection, StringView label,
			StringView tooltip, std::uint16_t Formats::AnimatedTileRecord::* member, float divisor, float step, std::int32_t decimals)
		{
			IEditorContext* s = &session;
			auto* row = AddRow(parent, label);
			row->Tooltip = tooltip;
			auto* number = row->Add<UiNumberBox>([s, selection, member, divisor]() {
				const auto& animations = s->GetDocument().Animations;
				return (selection->Animation < std::int32_t(animations.size()) ? float(animations[selection->Animation].Timing.*member) / divisor : 0.0f);
			}, [s, selection, member, divisor](float value, bool final) {
				std::uint16_t stored = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * divisor)), 0, 65535));
				// TRANSLATORS: Name of an undo step in the history, the timing of an animated tile changed in the Animated Tiles dialog
				ChangeAnimation(*s, selection->Animation, NC_("editor", "Animation timing"), [member, stored](DocumentAnimation& animation) {
					animation.Timing.*member = stored;
				}, final);
			}, 0.0f, 65535.0f / divisor, step, decimals);
			number->Width = 60.0f;
			number->Tooltip = tooltip;
		}
	}

	void ShowAnimatedTilesDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "animatedTiles")) {
			return;
		}

		IEditorContext* s = &session;
		auto selection = std::make_shared<AnimationSelection>();
		// TRANSLATORS: Title of the dialog that edits the animated tiles of the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Animated Tiles"));
		// The animated tiles on the left, scrolled, and the selected one on the right, always in view
		auto* columns = dialog->GetContent()->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		auto* left = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		// Eight animated tiles in a row, and the scroll bar
		left->Width = TileListGrid::CellSize * 8.0f + UiTheme::ScrollBarWidth + 3.0f;
		columns->Add<UiSeparator>(UiOrientation::Vertical);
		auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		right->Width = 220.0f;

		auto hasAnimation = [s, selection]() {
			return (selection->Animation < std::int32_t(s->GetDocument().Animations.size()));
		};

		// TRANSLATORS: Header of the list of the animated tiles in the Animated Tiles dialog
		left->Add<UiLabel>(_x("editor", "Animations"), UiTheme::TextHeader);
		auto* listScroll = left->Add<UiScrollView>();
		listScroll->Height = 200.0f;
		auto* animationList = listScroll->SetContent<TileListGrid>(session, [s]() {
			return std::int32_t(s->GetDocument().Animations.size());
		}, [s](std::int32_t i) {
			return s->GetDocument().ToTileId(TileRef{ std::uint16_t(i), TileRef::Animated, 0 });
		}, [selection]() {
			return selection->Animation;
		}, [selection](std::int32_t i) {
			selection->Animation = i;
			selection->Frame = 0;
		});
		animationList->FixedColumns = 8;
		{
			auto* buttons = left->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Button in the Animated Tiles dialog, adds a new animated tile
			buttons->Add<UiButton>(_x("editor", "New"), EditorIcon::Plus, [s, selection]() {
				LevelDocument& document = s->GetDocument();
				if (std::max<std::int32_t>(document.GetStaticTileCount(), 0) + std::int32_t(document.Animations.size()) >= UINT16_MAX + 1) {
					s->ShowMessage(_x("editor", "There's no room for another animated tile"));
					return;
				}
				// Made of the tiles of the brush, or of one empty tile
				DocumentAnimation animation;
				animation.Timing.FrameDuration16 = 6 * 16;
				animation.Timing.Delay = 0;
				animation.Timing.DelayJitter = 0;
				animation.Timing.PingPongDelay = 0;
				animation.Timing.IsPingPong = 0;
				const TileStamp& stamp = s->GetStamp();
				for (std::size_t i = 0; i < stamp.Tiles.size() && animation.Frames.size() < UINT8_MAX; i++) {
					if (stamp.Present[i]) {
						animation.Frames.push_back(stamp.Tiles[i]);
					}
				}
				if (animation.Frames.empty()) {
					animation.Frames.push_back(TileRef{});
				}
				// TRANSLATORS: Name of an undo step in the history
				s->RecordSnapshotStep(_x("editor", "Add animated tile"), SnapshotParts::Animations, [&animation](LevelDocument& document) {
					document.Animations.push_back(Death::move(animation));
					// The animated tiles take the top of the tile ID space, it grows if they'd reach the tile sets
					document.FitTileIdSpace();
					return true;
				});
				selection->Animation = std::int32_t(document.Animations.size()) - 1;
				selection->Frame = 0;
			});
			auto* remove = buttons->Add<UiButton>(StringView(), EditorIcon::Delete, [s, selection]() {
				LevelDocument& document = s->GetDocument();
				std::int32_t index = selection->Animation;
				if (index >= std::int32_t(document.Animations.size())) {
					return;
				}
				// Tiles that showed it show its first frame instead, tiles of the animated tiles after it move down
				TileRef replacement = (!document.Animations[index].Frames.empty() ? document.Animations[index].Frames[0] : TileRef{});
				if (replacement.IsAnimated() && replacement.Index == index) {
					replacement = TileRef{};
				}
				auto remap = [index, &replacement](TileRef& tile) {
					if (!tile.IsAnimated()) {
						return;
					}
					if (tile.Index == index) {
						std::uint8_t flags = tile.Flags;
						tile = replacement;
						tile.Flags ^= (flags & 0x03);
					} else if (tile.Index > index) {
						tile.Index--;
					}
				};
				if (replacement.IsAnimated() && replacement.Index > index) {
					replacement.Index--;
				}
				// Only tile references of the layers change, the level doesn't have to be loaded again
				// TRANSLATORS: Name of an undo step in the history
				s->RecordSnapshotStep(_x("editor", "Remove animated tile"), SnapshotParts::Animations | SnapshotParts::Layers, [index, &remap](LevelDocument& document) {
					document.Animations.erase(document.Animations.begin() + index);
					for (DocumentAnimation& animation : document.Animations) {
						for (TileRef& frame : animation.Frames) {
							remap(frame);
						}
					}
					for (DocumentLayer& layer : document.Layers) {
						for (TileRef& tile : layer.Tiles) {
							remap(tile);
						}
					}
					// Ground lines remember the tiles they wrote, kept by the snapshot of the layers too
					Function<bool(TileRef&)> remapLines = [&remap](TileRef& tile) {
						remap(tile);
						return true;
					};
					TerrainSpline::RemapTiles(document, remapLines);
					return true;
				}, DocumentChangeKind::AnimationStructure);
				// The brush goes on painting with the animated tiles it had, those after the removed one moved down
				for (TileRef& tile : s->GetStamp().Tiles) {
					remap(tile);
				}
				selection->Animation = std::max<std::int32_t>(0, std::min<std::int32_t>(index, std::int32_t(document.Animations.size()) - 1));
				selection->Frame = 0;
			});
			remove->IsEnabledFunc = hasAnimation;
			remove->Tooltip = _x("editor", "Removes the animated tile, tiles showing it show its first frame instead");
			auto* paint = buttons->Add<UiButton>(StringView(), EditorIcon::Brush, [s, selection]() {
				if (selection->Animation < std::int32_t(s->GetDocument().Animations.size())) {
					s->SetStamp(TileStamp::FromTile(TileRef{ std::uint16_t(selection->Animation), TileRef::Animated, 0 }));
					s->GetTools().SetActive(EditorToolType::Brush);
					s->SetPickerPanel(PickerPanel::Tiles);
				}
			});
			paint->IsEnabledFunc = hasAnimation;
			paint->Tooltip = _x("editor", "Paints with the animated tile");
		}

		// TRANSLATORS: Header of the frames of the selected animated tile in the Animated Tiles dialog, noun
		right->Add<UiLabel>(_x("editor", "Frames"), UiTheme::TextHeader);
		auto* frames = right->Add<TileListGrid>(session, [s, selection]() {
			const auto& animations = s->GetDocument().Animations;
			return (selection->Animation < std::int32_t(animations.size()) ? std::int32_t(animations[selection->Animation].Frames.size()) : 0);
		}, [s, selection](std::int32_t i) {
			LevelDocument& document = s->GetDocument();
			return document.ToTileId(document.Animations[selection->Animation].Frames[i]);
		}, [selection]() {
			return selection->Frame;
		}, [selection](std::int32_t i) {
			selection->Frame = i;
		});
		frames->IsVisibleFunc = hasAnimation;
		{
			auto* buttons = right->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			buttons->IsVisibleFunc = hasAnimation;
			// TRANSLATORS: Button in the Animated Tiles dialog, adds the tiles of the brush as frames of the animated tile
			auto* add = buttons->Add<UiButton>(_x("editor", "Add Brush"), EditorIcon::Plus, [s, selection]() {
				// The tiles of the brush follow the selected frame
				std::int32_t index = selection->Animation;
				const TileStamp& stamp = s->GetStamp();
				std::int32_t added = 0;
				// TRANSLATORS: Name of an undo step in the history, frames added to an animated tile
				ChangeAnimation(*s, index, NC_("editor", "Add frames"), [&stamp, selection, index, &added](DocumentAnimation& animation) {
					std::int32_t at = std::min(selection->Frame + 1, std::int32_t(animation.Frames.size()));
					for (std::size_t i = 0; i < stamp.Tiles.size() && animation.Frames.size() < UINT8_MAX; i++) {
						const TileRef& tile = stamp.Tiles[i];
						if (stamp.Present[i] && !(tile.IsAnimated() && tile.Index == index)) {
							animation.Frames.insert(animation.Frames.begin() + at + added, tile);
							added++;
						}
					}
				});
				if (added > 0) {
					selection->Frame += added;
				}
			});
			add->Tooltip = _x("editor", "Adds the tiles of the brush after the selected frame");
			auto* remove = buttons->Add<UiButton>(StringView(), EditorIcon::Delete, [s, selection]() {
				// TRANSLATORS: Name of an undo step in the history, a frame removed from an animated tile
				ChangeAnimation(*s, selection->Animation, NC_("editor", "Remove frame"), [selection](DocumentAnimation& animation) {
					// At least one frame stays
					if (animation.Frames.size() > 1 && selection->Frame < std::int32_t(animation.Frames.size())) {
						animation.Frames.erase(animation.Frames.begin() + selection->Frame);
					}
				});
				const auto& animations = s->GetDocument().Animations;
				selection->Frame = std::min(selection->Frame, std::int32_t(animations[selection->Animation].Frames.size()) - 1);
			});
			remove->Tooltip = _x("editor", "Removes the selected frame");
			for (std::int32_t direction : { -1, 1 }) {
				auto* move = buttons->Add<UiButton>(StringView(), direction < 0 ? EditorIcon::ChevronLeft : EditorIcon::ChevronRight, [s, selection, direction]() {
					std::int32_t from = selection->Frame;
					std::int32_t to = from + direction;
					bool moved = false;
					// TRANSLATORS: Name of an undo step in the history, a frame of an animated tile moved earlier or later
					ChangeAnimation(*s, selection->Animation, NC_("editor", "Move frame"), [from, to, &moved](DocumentAnimation& animation) {
						if (from >= 0 && to >= 0 && from < std::int32_t(animation.Frames.size()) && to < std::int32_t(animation.Frames.size())) {
							std::swap(animation.Frames[from], animation.Frames[to]);
							moved = true;
						}
					});
					if (moved) {
						selection->Frame = to;
					}
				});
				move->Tooltip = (direction < 0 ? _x("editor", "Moves the selected frame earlier") : _x("editor", "Moves the selected frame later"));
			}
		}

		auto* timing = right->Add<UiForm>();
		timing->IsVisibleFunc = hasAnimation;
		// TRANSLATORS: Label of a property in the Animated Tiles dialog, how long each frame is shown, and its tooltip
		AddAnimationNumber(timing, session, selection, _x("editor", "Frame time"), _x("editor", "How many frames of the game each frame is shown for, at 60 a second"),
			&Formats::AnimatedTileRecord::FrameDuration16, 16.0f, 0.25f, 2);
		// TRANSLATORS: Label of a property in the Animated Tiles dialog, a pause before the animation starts again, noun
		AddAnimationNumber(timing, session, selection, _x("editor", "Delay"), _x("editor", "Frames the last frame stays before the animation starts again"),
			&Formats::AnimatedTileRecord::Delay, 1.0f, 1.0f, 0);
		// TRANSLATORS: Label of a property in the Animated Tiles dialog, a random part added to the delay, noun
		AddAnimationNumber(timing, session, selection, _x("editor", "Random delay"), _x("editor", "Up to this many frames are added to the delay at random"),
			&Formats::AnimatedTileRecord::DelayJitter, 1.0f, 1.0f, 0);
		// TRANSLATORS: Checkbox in the Animated Tiles dialog, the animation plays forward and then backward (ping-pong)
		timing->Add<UiCheckBox>(_x("editor", "Forward and back"), [s, selection]() {
			const auto& animations = s->GetDocument().Animations;
			return (selection->Animation < std::int32_t(animations.size()) && animations[selection->Animation].Timing.IsPingPong != 0);
		}, [s, selection](bool value) {
			ChangeAnimation(*s, selection->Animation, NC_("editor", "Animation timing"), [value](DocumentAnimation& animation) {
				animation.Timing.IsPingPong = (value ? 1 : 0);
			});
		});
		// TRANSLATORS: Label of a property in the Animated Tiles dialog, a pause before the animation turns to play backward, noun
		AddAnimationNumber(timing, session, selection, _x("editor", "Turn delay"), _x("editor", "Frames the last frame stays before playing back"),
			&Formats::AnimatedTileRecord::PingPongDelay, 1.0f, 1.0f, 0);

		// Tiles to add are picked in the panel below while it's open
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "animatedTiles", Death::move(dialog), Death::move(options));
	}

	void ShowLevelPropertiesDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog with the properties of the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Level Properties"));
		// As large as the screen allows, scrolled only if it doesn't fit, as on a small screen
		dialog->MaxScreenHeightFraction = 0.85f;
		dialog->MinScreenWidthFraction = 0.6f;
		auto* scroll = dialog->GetContent()->Add<UiScrollView>();
		scroll->Flex = 1.0f;
		UiForm* content = scroll->SetContent<UiForm>(UiTheme::Spacing, UiThickness(0.0f, 0.0f, 4.0f, 0.0f));

		// TRANSLATORS: Label of the name of the level shown in the game in the Level Properties dialog
		AddTextProperty(content, session, _x("editor", "Name"), &Formats::LevelProperties::DisplayName);
		{
			// The player name of the options, as a new level gets it
			// TRANSLATORS: Label of the author of the level in the Level Properties dialog, and the placeholder of its empty box
			auto* row = AddTextProperty(content, session, _x("editor", "Author"), &Formats::LevelProperties::Author, _x("editor", "unknown"));
			// TRANSLATORS: Button next to the author of a level, puts the player name there
			auto* me = row->Add<UiButton>(_x("editor", "Me"), EditorIcon::None, [s]() {
				SetTextProperty(*s, &Formats::LevelProperties::Author, PreferencesCache::GetEffectivePlayerName());
			});
			me->Tooltip = _x("editor", "Your player name, as set in the options");
		}
		// TRANSLATORS: Label in the Level Properties dialog, and the placeholder of its empty box showing how a level of another episode is written
		AddLevelProperty(content, session, _x("editor", "Next level"), &Formats::LevelProperties::NextLevel, _x("editor", "episode/level"));
		AddLevelProperty(content, session, _x("editor", "Secret level"), &Formats::LevelProperties::SecretLevel);
		AddLevelProperty(content, session, _x("editor", "Bonus level"), &Formats::LevelProperties::BonusLevel);
		// TRANSLATORS: Label in the Level Properties dialog, and the placeholder of its empty box
		AddMusicProperty(content, session, _x("editor", "Music"), &Formats::LevelProperties::Music, _x("editor", "file name"));
		{
			// TRANSLATORS: Label of the default tile set of the level in the Level Properties dialog
			auto* row = AddRow(content, _x("editor", "Tile set"));
			auto* label = row->Add<UiLabel>();
			label->TextFunc = [s]() { return s->GetDocument().Properties.DefaultTileSet; };
		}

		content->Add<UiSeparator>();
		{
			// TRANSLATORS: Label of the light intensity of the level in the Level Properties dialog, noun
			auto* row = AddRow(content, _x("editor", "Light"));
			AddColorByte(row, session, 3);
			// TRANSLATORS: After the number of the light intensity in the Level Properties dialog, 255 is the highest value
			row->Add<UiLabel>(_x("editor", "of 255"), UiTheme::TextDim);
		}
		{
			auto* row = AddRow(content, _x("editor", "Ambient color"));
			auto getColor = [s]() {
				std::uint32_t color = s->GetDocument().Properties.AmbientColor;
				return Colorf((color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, 1.0f);
			};
			row->Add<UiColorSwatch>(getColor, [s, getColor]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				// All picked while the picker is open is a single step, which starts here
				s->GetHistory().BreakMerge();
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), getColor, [s](const Colorf& color) {
					std::uint32_t rgb = std::uint32_t(std::round(color.R * 255.0f)) | (std::uint32_t(std::round(color.G * 255.0f)) << 8) |
						(std::uint32_t(std::round(color.B * 255.0f)) << 16);
					ChangeProperties(*s, [rgb](LevelFlags&, Formats::LevelProperties& properties) {
						// The light intensity in the highest byte stays
						properties.AmbientColor = (properties.AmbientColor & 0xFF000000u) | rgb;
					}, false);
				});
			})->Tooltip = _x("editor", "Pick a color");
			for (std::int32_t i = 0; i < 3; i++) {
				AddColorByte(row, session, i);
			}
		}
		{
			static const PropertyChoice WeatherChoices[] = {
				// TRANSLATORS: Choices of the weather of the level in the Level Properties dialog, what falls from the sky
				{ NC_("editor", "None"), std::int32_t(WeatherType::None) },
				{ NC_("editor", "Snow"), std::int32_t(WeatherType::Snow) },
				{ NC_("editor", "Flowers"), std::int32_t(WeatherType::Flowers) },
				{ NC_("editor", "Rain"), std::int32_t(WeatherType::Rain) },
				{ NC_("editor", "Leaves"), std::int32_t(WeatherType::Leaf) },
			};
			// TRANSLATORS: Label of a property in the Level Properties dialog
			auto* row = AddRow(content, _x("editor", "Weather"));
			auto* dropdown = AddChoiceDropdown(row, WeatherChoices, [s]() {
				return std::int32_t(s->GetDocument().Properties.Weather & ~std::uint8_t(WeatherType::OutdoorsOnly));
			}, [s](std::int32_t value) {
				std::uint8_t type = std::uint8_t(value);
				ChangeProperties(*s, [type](LevelFlags&, Formats::LevelProperties& properties) {
					properties.Weather = std::uint8_t((properties.Weather & std::uint8_t(WeatherType::OutdoorsOnly)) | type);
				});
			}, [](std::int32_t) {
				// TRANSLATORS: In the Level Properties dialog for a weather the editor doesn't know
				return String(_x("editor", "Unknown"));
			});
			dropdown->Width = 80.0f;
			auto* intensity = AddByteBox(row, [s]() {
				return s->GetDocument().Properties.WeatherIntensity;
			}, [s](std::uint8_t intensity, bool final) {
				ChangeProperties(*s, [intensity](LevelFlags&, Formats::LevelProperties& properties) { properties.WeatherIntensity = intensity; }, final);
			});
			intensity->Width = 40.0f;
			// TRANSLATORS: Tooltip of the number next to the weather in the Level Properties dialog, how strong the weather is
			intensity->Tooltip = _x("editor", "Intensity");
		}
		content->Add<UiCheckBox>(_x("editor", "Weather only outdoors"), [s]() {
			return (s->GetDocument().Properties.Weather & std::uint8_t(WeatherType::OutdoorsOnly)) != 0;
		}, [s](bool value) {
			ChangeProperties(*s, [value](LevelFlags&, Formats::LevelProperties& properties) {
				properties.Weather = std::uint8_t(value ? (properties.Weather | std::uint8_t(WeatherType::OutdoorsOnly)) : (properties.Weather & ~std::uint8_t(WeatherType::OutdoorsOnly)));
			});
		});
		{
			// TRANSLATORS: Label of the height of the water in the Level Properties dialog, and the unit after it, short for pixels
			auto* row = AddRow(content, String(_x("editor", "Water level") + " ("_s + _x("editor", "px") + ")"_s));
			auto* number = row->Add<UiNumberBox>([s]() {
				return float(s->GetDocument().Properties.WaterLevel);
			}, [s](float value, bool final) {
				std::uint16_t level = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 65535));
				ChangeProperties(*s, [level](LevelFlags&, Formats::LevelProperties& properties) { properties.WaterLevel = level; }, final);
			}, 0.0f, 65535.0f, 4.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _x("editor", "From the top of the level, below the level means no water");
		}
		{
			// TRANSLATORS: Label in the Level Properties dialog, the tile whose colors light up an RGB keyboard while the level is played
			auto* row = AddRow(content, _x("editor", "Caption tile"));
			row->Add<TilePreview>(session, [s]() { return std::int32_t(s->GetDocument().Properties.CaptionTileId); });
			auto* number = row->Add<UiNumberBox>([s]() {
				return float(s->GetDocument().Properties.CaptionTileId);
			}, [s](float value, bool final) {
				std::uint16_t tileId = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 65535));
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = tileId; }, final);
			}, 0.0f, 65535.0f, 1.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _x("editor", "Tile of the default tile set whose colors light up an RGB keyboard while the level is played");
			auto* fromBrush = row->Add<UiButton>(StringView(), EditorIcon::Brush, [s]() {
				// The top left tile of the brush, the caption is always a tile of the default tile set
				const TileStamp& stamp = s->GetStamp();
				if (stamp.IsEmpty() || stamp.Tiles[0].Source != TileRef::DefaultTileSet) {
					s->ShowMessage(_x("editor", "The brush has to start with a tile of the default tile set"));
					return;
				}
				std::uint16_t tileId = stamp.Tiles[0].Index;
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = tileId; });
			});
			fromBrush->Tooltip = _x("editor", "Use the tile of the brush");
			auto* automatic = row->Add<UiButton>(_("Auto"), EditorIcon::None, [s]() {
				std::int32_t tileId = PickCaptionTile(*s);
				if (tileId < 0) {
					s->ShowMessage(_x("editor", "The level uses no tile of its tile set that has colors"));
					return;
				}
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = std::uint16_t(tileId); });
			});
			automatic->Tooltip = _x("editor", "Pick a colorful tile the level uses a lot");
		}

		content->Add<UiSeparator>();
		struct FlagOption { const char* Label; LevelFlags Flag; };
		// Races with laps, Capture the Flag and multiplayer spawn points follow the events, see LevelDocument::GetFlagsFromEvents()
		static const FlagOption FlagOptions[] = {
			// TRANSLATORS: Checkboxes in the Level Properties dialog
			{ NC_("editor", "Pit at the bottom"), LevelFlags::HasPit },
			{ NC_("editor", "The pit kills at once"), LevelFlags::HasPitInstantDeath },
			{ NC_("editor", "Hidden from the list of levels"), LevelFlags::IsHidden },
			{ NC_("editor", "Multiplayer level"), LevelFlags::IsMultiplayerLevel },
			{ NC_("editor", "Split the screen vertically"), LevelFlags::HasVerticalSplitscreen },
		};
		for (const FlagOption& option : FlagOptions) {
			LevelFlags flag = option.Flag;
			content->Add<UiCheckBox>(_x("editor", option.Label), [s, flag]() {
				return (s->GetDocument().Flags & flag) == flag;
			}, [s, flag](bool value) {
				ChangeProperties(*s, [flag, value](LevelFlags& flags, Formats::LevelProperties&) {
					flags = (value ? (flags | flag) : (flags & ~flag));
				});
			});
		}

		ShowModal(*surface, Death::move(dialog));
	}

	void ShowLevelTextsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that edits the texts of the level, shown by its events
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Level Texts"));
		// As large as the screen allows, the texts are long and a translation is next to each, the list scrolls
		dialog->MaxScreenHeightFraction = 0.8f;
		dialog->MinScreenWidthFraction = 0.7f;
		UiStack* content = dialog->GetContent();
		content->Add<UiLabel>(_x("editor", "Events refer to texts by their number, | splits a text into parts"), UiTheme::TextDim);
		content->Add<UiLabel>(_x("editor", "Enter breaks a line, Ctrl+Enter confirms, \\f is formatting"), UiTheme::TextDim);
		auto language = std::make_shared<LevelTextLanguageState>();
		AddLevelTextLanguageBar(content, session, language);
		// What's left between the rows above and the buttons below, which stay
		auto* scroll = content->Add<UiScrollView>();
		scroll->Flex = 1.0f;
		scroll->SetContent<TextsList>(session, language);

		// In the content, Enter and Escape choose the buttons of the dialog
		auto* buttons = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Button in the Level Texts dialog
		buttons->Add<UiButton>(_x("editor", "Add Text"), EditorIcon::Plus, [s]() {
			LevelDocument& document = s->GetDocument();
			// Event parameters refer to a text by a byte
			if (document.Texts.size() >= 256) {
				s->ShowMessage(_x("editor", "A level can't have more texts"));
				return;
			}
			// TRANSLATORS: Name of an undo step in the history, a text added to the level
			s->RecordSnapshotStep(_x("editor", "Add text"), SnapshotParts::Texts, [](LevelDocument& document) {
				document.Texts.emplace_back();
				return true;
			});
		});
		// TRANSLATORS: Button in the Level Texts dialog, removes the last text of the level
		buttons->Add<UiButton>(_x("editor", "Remove Last"), EditorIcon::Minus, [s]() {
			LevelDocument& document = s->GetDocument();
			if (document.Texts.empty()) {
				return;
			}
			std::size_t count = document.Texts.size();
			auto removeLast = [s, count]() {
				LevelDocument& document = s->GetDocument();
				// Only if it's still the text that was asked about
				if (document.Texts.size() != count) {
					return;
				}
				// TRANSLATORS: Name of an undo step in the history, the last text of the level removed
				s->RecordSnapshotStep(_x("editor", "Remove text"), SnapshotParts::Texts, [](LevelDocument& document) {
					document.Texts.pop_back();
					return true;
				});
			};

			// Events that show it would show nothing, and a text with something in it is asked about too
			std::int32_t index = std::int32_t(count) - 1;
			std::int32_t users = 0;
			auto countUser = [index, &users](const Formats::EventRecord& event) {
				if (const EventParamField* field = FindEventField(EventCatalog::Find(EventType(event.Type)), ParamType::TextId)) {
					users += (EventCatalog::ReadField(event, *field) == index ? 1 : 0);
				}
			};
			for (const DocumentEvent& event : document.Events) {
				countUser(event.Record);
			}
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				countUser(event.Event);
			}
			UiSurface* surface = s->GetSurface();
			if ((users == 0 && document.Texts[index].empty()) || surface == nullptr) {
				removeLast();
				return;
			}
			String text = (users > 0 ? _fnx("editor", "Text {} is shown by {} event, it would show nothing. Remove it anyway?",
				"Text {} is shown by {} events, they would show nothing. Remove it anyway?", users, index, users)
				: _fx("editor", "Remove text {} with all it says?", index));
			// TRANSLATORS: Title and buttons of the message confirming that the last text of the level is removed
			UiMessageDialog::Show(*surface, _x("editor", "Remove Text"), text, { _x("editor", "Remove"), _x("editor", "Cancel") }, [removeLast](std::int32_t button) {
				if (button == 0) {
					removeLast();
				}
			});
		});

		ShowModal(*surface, Death::move(dialog));
	}
}

#endif
