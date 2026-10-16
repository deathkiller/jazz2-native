#include "LevelDialogs.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
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
			// A form puts the labels in a column as wide as the longest of them
			if (auto* form = runtime_cast<UiForm>(parent)) {
				return form->AddRow(label);
			}
			auto* row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(label, UiTheme::TextDim)->Width = LabelWidth;
			return row;
		}

		// Changes the flags or the properties as an undoable step, consecutive changes are merged
		void ChangeProperties(EditorSession& session, Function<void(LevelFlags&, Formats::LevelProperties&)>&& change)
		{
			LevelDocument& document = session.GetDocument();
			LevelFlags flags = document.Flags;
			Formats::LevelProperties properties = document.Properties;
			change(flags, properties);
			session.Execute(std::make_unique<SetLevelPropertiesCommand>(document, flags, properties, _("Level properties")), true);
		}

		void SetTextProperty(EditorSession& session, String Formats::LevelProperties::* member, StringView text)
		{
			if (session.GetDocument().Properties.*member != text) {
				String value = text.trimmed();
				ChangeProperties(session, [member, &value](LevelFlags&, Formats::LevelProperties& properties) { properties.*member = value; });
			}
		}

		UiStack* AddTextProperty(UiElement* parent, EditorSession& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			EditorSession* s = &session;
			auto* row = AddRow(parent, label);
			auto* box = row->Add<UiTextBox>();
			box->Width = 190.0f;
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

		void AddLevelProperty(UiElement* parent, EditorSession& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			EditorSession* s = &session;
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
				UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, std::move(roots), ".j2l"_s, directory, fileName, [s, member](StringView path) {
					// The level is looked up by its name, so it doesn't matter which root it was picked from
					SetTextProperty(*s, member, MakeLevelReference(EditorSession::MakeLevelName(path), s->GetLevelName()));
				});
			});
			pick->Tooltip = _("Choose a level");
		}

		void AddMusicProperty(UiElement* parent, EditorSession& session, StringView label, String Formats::LevelProperties::* member, StringView placeholder = {})
		{
			EditorSession* s = &session;
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
					roots.push_back({ _("User content"), fs::CombinePath(userContent, "Music"_s), false });
				}
				StringView content = resolver.GetContentPath();
				if (!content.empty()) {
					roots.push_back({ _("Content"), fs::CombinePath(content, "Music"_s), false });
				}
				StringView source = resolver.GetSourcePath();
				if (!source.empty()) {
					// Without the trailing separator, so the paths of files in it can be made relative to it
					roots.push_back({ _("Original game"), String(source.trimmedSuffix("/\\"_s)), false });
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
				UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, std::move(roots), ".j2b;.it;.mo3;.mod;.s3m;.xm;.ogg;.wav"_s, directory, fileName,
					[s, member, rootPaths = std::move(rootPaths)](StringView path) {
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
			pick->Tooltip = _("Choose music, it starts playing right away");
		}

		// A tile of the default tile set that shows the level well on an RGB keyboard: covered by its pixels, colorful
		// and used a lot, the score is all three multiplied
		std::int32_t PickCaptionTile(EditorSession& session)
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
		void AddColorByte(UiElement* row, EditorSession& session, std::int32_t byteIndex)
		{
			EditorSession* s = &session;
			std::int32_t shift = byteIndex * 8;
			auto* number = row->Add<UiNumberBox>([s, shift]() {
				return float((s->GetDocument().Properties.AmbientColor >> shift) & 0xFF);
			}, [s, shift](float value, bool final) {
				std::uint32_t byte = std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
				ChangeProperties(*s, [shift, byte](LevelFlags&, Formats::LevelProperties& properties) {
					properties.AmbientColor = (properties.AmbientColor & ~(0xFFu << shift)) | (byte << shift);
				});
			}, 0.0f, 255.0f, 1.0f, 0);
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
			explicit TextsList(EditorSession& session)
				: UiStack(UiOrientation::Vertical, 2.0f, UiThickness(0.0f, 0.0f, 3.0f, 0.0f)), _session(session), _shownCount(-1)
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
			EditorSession& _session;
			std::int32_t _shownCount;

			void Rebuild(std::int32_t count)
			{
				_shownCount = count;
				ClearChildren();
				if (count == 0) {
					Add<UiLabel>(_("The level has no texts"), UiTheme::TextDisabled);
					return;
				}
				EditorSession* s = &_session;
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
						auto command = SnapshotCommand::Begin(document, SnapshotParts::Texts, _("Level text"));
						document.Texts[i] = std::move(unescaped);
						DocumentChanges changes;
						command->End(document, changes);
						s->Execute(std::move(command));
					};
				}
			}
		};
	}

	namespace
	{
		/** A single tile, as high as a row */
		class TilePreview : public UiElement
		{
		public:
			TilePreview(EditorSession& session, Function<std::int32_t()>&& tileId)
				: _session(session), _tileId(std::move(tileId))
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
			EditorSession& _session;
			Function<std::int32_t()> _tileId;
		};

		/** Tiles in a grid to choose one from, the frames of an animation or the animations themselves */
		class TileListGrid : public UiElement
		{
		public:
			static constexpr float CellSize = 24.0f;

			/** Number of tiles in a row, or 0 for as many as fit */
			std::int32_t FixedColumns = 0;

			TileListGrid(EditorSession& session, Function<std::int32_t()>&& count, Function<std::int32_t(std::int32_t)>&& tileId,
				Function<std::int32_t()>&& selected, Function<void(std::int32_t)>&& onSelect)
				: _session(session), _count(std::move(count)), _tileId(std::move(tileId)), _selected(std::move(selected)),
					_onSelect(std::move(onSelect)), _cellSize(1.0f), _columns(1)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				if (FixedColumns > 0) {
					_columns = FixedColumns;
					_cellSize = std::max(1.0f, std::floor(available.X / float(FixedColumns)));
				} else {
					_cellSize = context.Px(CellSize);
					_columns = std::max(1, std::int32_t(available.X / _cellSize));
				}
				std::int32_t rows = std::max(1, (_count() + _columns - 1) / _columns);
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
			EditorSession& _session;
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

		// Changes the selected animated tile as an undoable step, consecutive changes of the same value are merged
		void ChangeAnimation(EditorSession& session, std::int32_t index, const char* name, Function<void(DocumentAnimation&)>&& change)
		{
			LevelDocument& document = session.GetDocument();
			if (index < 0 || index >= std::int32_t(document.Animations.size())) {
				return;
			}
			DocumentAnimation animation = document.Animations[index];
			change(animation);
			session.Execute(std::make_unique<SetAnimationCommand>(document, index, std::move(animation), _(name)), true);
		}

		void AddAnimationNumber(UiElement* parent, EditorSession& session, const std::shared_ptr<AnimationSelection>& selection, StringView label,
			StringView tooltip, std::uint16_t Formats::AnimatedTileRecord::* member, float divisor, float step, std::int32_t decimals)
		{
			EditorSession* s = &session;
			auto* row = AddRow(parent, label);
			row->Tooltip = tooltip;
			auto* number = row->Add<UiNumberBox>([s, selection, member, divisor]() {
				const auto& animations = s->GetDocument().Animations;
				return (selection->Animation < std::int32_t(animations.size()) ? float(animations[selection->Animation].Timing.*member) / divisor : 0.0f);
			}, [s, selection, member, divisor](float value, bool final) {
				std::uint16_t stored = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * divisor)), 0, 65535));
				ChangeAnimation(*s, selection->Animation, N_("Animation timing"), [member, stored](DocumentAnimation& animation) {
					animation.Timing.*member = stored;
				});
			}, 0.0f, 65535.0f / divisor, step, decimals);
			number->Width = 60.0f;
			number->Tooltip = tooltip;
		}
	}

	void ShowAnimatedTilesDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto selection = std::make_shared<AnimationSelection>();
		auto dialog = std::make_unique<UiDialog>(_("Animated Tiles"));
		UiDialog* dialogPtr = dialog.get();
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

		left->Add<UiLabel>(_("Animations"), UiTheme::TextHeader);
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
			buttons->Add<UiButton>(_("New"), EditorIcon::Plus, [s, selection]() {
				LevelDocument& document = s->GetDocument();
				if (std::max(document.GetStaticTileCount(), 0) + std::int32_t(document.Animations.size()) >= UINT16_MAX + 1) {
					s->ShowMessage(_("There's no room for another animated tile"));
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
				auto command = SnapshotCommand::Begin(document, SnapshotParts::Animations, _("Add animated tile"));
				document.Animations.push_back(std::move(animation));
				// The animated tiles take the top of the tile ID space, it grows if they'd reach the tile sets
				document.FitTileIdSpace();
				DocumentChanges changes;
				command->End(document, changes);
				s->Execute(std::move(command));
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
				// Only tile references of the layers change, the level doesn't have to be loaded again
				auto command = SnapshotCommand::Begin(document, SnapshotParts::Animations | SnapshotParts::Layers, _("Remove animated tile"),
					DocumentChangeKind::AnimationStructure);
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
				DocumentChanges changes;
				command->End(document, changes);
				s->Execute(std::move(command));
				selection->Animation = std::max(0, std::min(index, std::int32_t(document.Animations.size()) - 1));
				selection->Frame = 0;
			});
			remove->IsEnabledFunc = hasAnimation;
			remove->Tooltip = _("Removes the animated tile, tiles showing it show its first frame instead");
			auto* paint = buttons->Add<UiButton>(StringView(), EditorIcon::Brush, [s, selection]() {
				if (selection->Animation < std::int32_t(s->GetDocument().Animations.size())) {
					s->SetStamp(TileStamp::FromTile(TileRef{ std::uint16_t(selection->Animation), TileRef::Animated, 0 }));
					s->GetTools().SetActive(EditorToolType::Brush);
					s->SetPickerPanel(PickerPanel::Tiles);
				}
			});
			paint->IsEnabledFunc = hasAnimation;
			paint->Tooltip = _("Paints with the animated tile");
		}

		right->Add<UiLabel>(_("Frames"), UiTheme::TextHeader);
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
			auto* add = buttons->Add<UiButton>(_("Add Brush"), EditorIcon::Plus, [s, selection]() {
				// The tiles of the brush follow the selected frame
				std::int32_t index = selection->Animation;
				const TileStamp& stamp = s->GetStamp();
				std::int32_t added = 0;
				ChangeAnimation(*s, index, N_("Add frames"), [&stamp, selection, index, &added](DocumentAnimation& animation) {
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
			add->Tooltip = _("Adds the tiles of the brush after the selected frame");
			auto* remove = buttons->Add<UiButton>(StringView(), EditorIcon::Delete, [s, selection]() {
				ChangeAnimation(*s, selection->Animation, N_("Remove frame"), [selection](DocumentAnimation& animation) {
					// At least one frame stays
					if (animation.Frames.size() > 1 && selection->Frame < std::int32_t(animation.Frames.size())) {
						animation.Frames.erase(animation.Frames.begin() + selection->Frame);
					}
				});
				const auto& animations = s->GetDocument().Animations;
				selection->Frame = std::min(selection->Frame, std::int32_t(animations[selection->Animation].Frames.size()) - 1);
			});
			remove->Tooltip = _("Removes the selected frame");
			for (std::int32_t direction : { -1, 1 }) {
				auto* move = buttons->Add<UiButton>(StringView(), direction < 0 ? EditorIcon::ChevronLeft : EditorIcon::ChevronRight, [s, selection, direction]() {
					std::int32_t from = selection->Frame;
					std::int32_t to = from + direction;
					bool moved = false;
					ChangeAnimation(*s, selection->Animation, N_("Move frame"), [from, to, &moved](DocumentAnimation& animation) {
						if (from >= 0 && to >= 0 && from < std::int32_t(animation.Frames.size()) && to < std::int32_t(animation.Frames.size())) {
							std::swap(animation.Frames[from], animation.Frames[to]);
							moved = true;
						}
					});
					if (moved) {
						selection->Frame = to;
					}
				});
				move->Tooltip = (direction < 0 ? _("Moves the selected frame earlier") : _("Moves the selected frame later"));
			}
		}

		auto* timing = right->Add<UiForm>();
		timing->IsVisibleFunc = hasAnimation;
		AddAnimationNumber(timing, session, selection, _("Frame time"), _("How many frames of the game each frame is shown for, at 60 a second"),
			&Formats::AnimatedTileRecord::FrameDuration16, 16.0f, 0.25f, 2);
		AddAnimationNumber(timing, session, selection, _("Delay"), _("Frames the last frame stays before the animation starts again"),
			&Formats::AnimatedTileRecord::Delay, 1.0f, 1.0f, 0);
		AddAnimationNumber(timing, session, selection, _("Random delay"), _("Up to this many frames are added to the delay at random"),
			&Formats::AnimatedTileRecord::DelayJitter, 1.0f, 1.0f, 0);
		timing->Add<UiCheckBox>(_("Forward and back"), [s, selection]() {
			const auto& animations = s->GetDocument().Animations;
			return (selection->Animation < std::int32_t(animations.size()) && animations[selection->Animation].Timing.IsPingPong != 0);
		}, [s, selection](bool value) {
			ChangeAnimation(*s, selection->Animation, N_("Animation timing"), [value](DocumentAnimation& animation) {
				animation.Timing.IsPingPong = (value ? 1 : 0);
			});
		});
		AddAnimationNumber(timing, session, selection, _("Turn delay"), _("Frames the last frame stays before playing back"),
			&Formats::AnimatedTileRecord::PingPongDelay, 1.0f, 1.0f, 0);

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// Tiles to add are picked in the panel below while it's open
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}

	void ShowLevelPropertiesDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Level Properties"));
		UiDialog* dialogPtr = dialog.get();
		// Scrolled, it doesn't fit on a small screen
		auto* scroll = dialog->GetContent()->Add<UiScrollView>();
		scroll->Height = 240.0f;
		UiForm* content = scroll->SetContent<UiForm>(UiTheme::Spacing, UiThickness(0.0f, 0.0f, 4.0f, 0.0f));

		AddTextProperty(content, session, _("Name"), &Formats::LevelProperties::DisplayName);
		{
			// The player name of the options, as a new level gets it
			auto* row = AddTextProperty(content, session, _("Author"), &Formats::LevelProperties::Author, _("unknown"));
			// TRANSLATORS: Button next to the author of a level, puts the player name there
			auto* me = row->Add<UiButton>(_("Me"), EditorIcon::None, [s]() {
				SetTextProperty(*s, &Formats::LevelProperties::Author, PreferencesCache::GetEffectivePlayerName());
			});
			me->Tooltip = _("Your player name, as set in the options");
		}
		AddLevelProperty(content, session, _("Next level"), &Formats::LevelProperties::NextLevel, _("episode/level"));
		AddLevelProperty(content, session, _("Secret level"), &Formats::LevelProperties::SecretLevel);
		AddLevelProperty(content, session, _("Bonus level"), &Formats::LevelProperties::BonusLevel);
		AddMusicProperty(content, session, _("Music"), &Formats::LevelProperties::Music, _("file name"));
		{
			auto* row = AddRow(content, _("Tile set"));
			auto* label = row->Add<UiLabel>();
			label->TextFunc = [s]() { return s->GetDocument().Properties.DefaultTileSet; };
		}

		content->Add<UiSeparator>();
		{
			auto* row = AddRow(content, _("Light"));
			AddColorByte(row, session, 3);
			row->Add<UiLabel>(_("of 255"), UiTheme::TextDim);
		}
		{
			auto* row = AddRow(content, _("Ambient color"));
			auto getColor = [s]() {
				std::uint32_t color = s->GetDocument().Properties.AmbientColor;
				return Colorf((color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, 1.0f);
			};
			row->Add<UiColorSwatch>(getColor, [s, getColor]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), getColor, [s](const Colorf& color) {
					std::uint32_t rgb = std::uint32_t(std::round(color.R * 255.0f)) | (std::uint32_t(std::round(color.G * 255.0f)) << 8) |
						(std::uint32_t(std::round(color.B * 255.0f)) << 16);
					ChangeProperties(*s, [rgb](LevelFlags&, Formats::LevelProperties& properties) {
						// The light intensity in the highest byte stays
						properties.AmbientColor = (properties.AmbientColor & 0xFF000000u) | rgb;
					});
				});
			})->Tooltip = _("Pick a color");
			for (std::int32_t i = 0; i < 3; i++) {
				AddColorByte(row, session, i);
			}
		}
		{
			struct WeatherOption { const char* Label; WeatherType Type; };
			static const WeatherOption WeatherOptions[] = {
				{ N_("None"), WeatherType::None },
				{ N_("Snow"), WeatherType::Snow },
				{ N_("Flowers"), WeatherType::Flowers },
				{ N_("Rain"), WeatherType::Rain },
				{ N_("Leaves"), WeatherType::Leaf },
			};
			auto* row = AddRow(content, _("Weather"));
			auto* dropdown = row->Add<UiDropdown>([s]() {
				std::uint8_t type = s->GetDocument().Properties.Weather & ~std::uint8_t(WeatherType::OutdoorsOnly);
				for (const WeatherOption& option : WeatherOptions) {
					if (std::uint8_t(option.Type) == type) {
						return String(_(option.Label));
					}
				}
				return String(_("Unknown"));
			}, [s](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const WeatherOption& option : WeatherOptions) {
					std::uint8_t type = std::uint8_t(option.Type);
					UiMenuEntry entry;
					entry.Label = _(option.Label);
					entry.IsChecked = [s, type]() { return ((s->GetDocument().Properties.Weather & ~std::uint8_t(WeatherType::OutdoorsOnly)) == type); };
					entry.Execute = [s, type]() {
						ChangeProperties(*s, [type](LevelFlags&, Formats::LevelProperties& properties) {
							properties.Weather = std::uint8_t((properties.Weather & std::uint8_t(WeatherType::OutdoorsOnly)) | type);
						});
					};
					entries.push_back(std::move(entry));
				}
			});
			dropdown->Width = 80.0f;
			auto* intensity = row->Add<UiNumberBox>([s]() {
				return float(s->GetDocument().Properties.WeatherIntensity);
			}, [s](float value, bool final) {
				std::uint8_t intensity = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
				ChangeProperties(*s, [intensity](LevelFlags&, Formats::LevelProperties& properties) { properties.WeatherIntensity = intensity; });
			}, 0.0f, 255.0f, 1.0f, 0);
			intensity->Width = 40.0f;
			intensity->Tooltip = _("Intensity");
		}
		content->Add<UiCheckBox>(_("Weather only outdoors"), [s]() {
			return (s->GetDocument().Properties.Weather & std::uint8_t(WeatherType::OutdoorsOnly)) != 0;
		}, [s](bool value) {
			ChangeProperties(*s, [value](LevelFlags&, Formats::LevelProperties& properties) {
				properties.Weather = std::uint8_t(value ? (properties.Weather | std::uint8_t(WeatherType::OutdoorsOnly)) : (properties.Weather & ~std::uint8_t(WeatherType::OutdoorsOnly)));
			});
		});
		{
			auto* row = AddRow(content, String(_("Water level") + " ("_s + _("px") + ")"_s));
			auto* number = row->Add<UiNumberBox>([s]() {
				return float(s->GetDocument().Properties.WaterLevel);
			}, [s](float value, bool final) {
				std::uint16_t level = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 65535));
				ChangeProperties(*s, [level](LevelFlags&, Formats::LevelProperties& properties) { properties.WaterLevel = level; });
			}, 0.0f, 65535.0f, 4.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _("From the top of the level, below the level means no water");
		}
		{
			auto* row = AddRow(content, _("Caption tile"));
			row->Add<TilePreview>(session, [s]() { return std::int32_t(s->GetDocument().Properties.CaptionTileId); });
			auto* number = row->Add<UiNumberBox>([s]() {
				return float(s->GetDocument().Properties.CaptionTileId);
			}, [s](float value, bool final) {
				std::uint16_t tileId = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 65535));
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = tileId; });
			}, 0.0f, 65535.0f, 1.0f, 0);
			number->Width = 60.0f;
			number->Tooltip = _("Tile of the default tile set whose colors light up an RGB keyboard while the level is played");
			auto* fromBrush = row->Add<UiButton>(StringView(), EditorIcon::Brush, [s]() {
				// The top left tile of the brush, the caption is always a tile of the default tile set
				const TileStamp& stamp = s->GetStamp();
				if (stamp.IsEmpty() || stamp.Tiles[0].Source != TileRef::DefaultTileSet) {
					s->ShowMessage(_("The brush has to start with a tile of the default tile set"));
					return;
				}
				std::uint16_t tileId = stamp.Tiles[0].Index;
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = tileId; });
			});
			fromBrush->Tooltip = _("Use the tile of the brush");
			auto* automatic = row->Add<UiButton>(_("Auto"), EditorIcon::None, [s]() {
				std::int32_t tileId = PickCaptionTile(*s);
				if (tileId < 0) {
					s->ShowMessage(_("The level uses no tile of its tile set that has colors"));
					return;
				}
				ChangeProperties(*s, [tileId](LevelFlags&, Formats::LevelProperties& properties) { properties.CaptionTileId = std::uint16_t(tileId); });
			});
			automatic->Tooltip = _("Pick a colorful tile the level uses a lot");
		}

		content->Add<UiSeparator>();
		struct FlagOption { const char* Label; LevelFlags Flag; };
		static const FlagOption FlagOptions[] = {
			{ N_("Pit at the bottom"), LevelFlags::HasPit },
			{ N_("The pit kills at once"), LevelFlags::HasPitInstantDeath },
			{ N_("Hidden from the list of levels"), LevelFlags::IsHidden },
			{ N_("Multiplayer level"), LevelFlags::IsMultiplayerLevel },
			{ N_("Race with laps"), LevelFlags::HasLaps },
			{ N_("Capture the flag"), LevelFlags::HasCaptureTheFlag },
			{ N_("Split the screen vertically"), LevelFlags::HasVerticalSplitscreen },
		};
		for (const FlagOption& option : FlagOptions) {
			LevelFlags flag = option.Flag;
			content->Add<UiCheckBox>(_(option.Label), [s, flag]() {
				return (s->GetDocument().Flags & flag) == flag;
			}, [s, flag](bool value) {
				ChangeProperties(*s, [flag, value](LevelFlags& flags, Formats::LevelProperties&) {
					flags = (value ? (flags | flag) : (flags & ~flag));
				});
			});
		}

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}

	void ShowLevelTextsDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Level Texts"));
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();
		content->Add<UiLabel>(_("Events refer to texts by their number, | splits a text into parts"), UiTheme::TextDim);
		content->Add<UiLabel>(_("Enter breaks a line, Ctrl+Enter confirms, \\f is formatting"), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 320.0f;
		scroll->Height = 200.0f;
		scroll->SetContent<TextsList>(session);

		// In the content, Enter and Escape choose the buttons of the dialog
		auto* buttons = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		buttons->Add<UiButton>(_("Add Text"), EditorIcon::Plus, [s]() {
			LevelDocument& document = s->GetDocument();
			// Event parameters refer to a text by a byte
			if (document.Texts.size() >= 256) {
				s->ShowMessage(_("A level can't have more texts"));
				return;
			}
			auto command = SnapshotCommand::Begin(document, SnapshotParts::Texts, _("Add text"));
			document.Texts.emplace_back();
			DocumentChanges changes;
			command->End(document, changes);
			s->Execute(std::move(command));
		});
		buttons->Add<UiButton>(_("Remove Last"), EditorIcon::Minus, [s]() {
			LevelDocument& document = s->GetDocument();
			if (document.Texts.empty()) {
				return;
			}
			auto command = SnapshotCommand::Begin(document, SnapshotParts::Texts, _("Remove text"));
			document.Texts.pop_back();
			DocumentChanges changes;
			command->End(document, changes);
			s->Execute(std::move(command));
		});
		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
