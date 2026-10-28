#include "PackageDialog.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/Commands.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Files/EpisodeFile.h"
#include "../Files/JsonAccess.h"
#include "../Files/JsonFile.h"
#include "../Files/ZipWriter.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Imaging/PngCodec.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../UI/UiVirtualList.h"
#include "../../ContentResolver.h"

#include "../../../nCine/I18n.h"

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <jsoncpp/json.h>

#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Editor-only chunk with what the package says of itself: u8 version (1), then a JSON object
		constexpr std::uint32_t PackageInfoChunk = Formats::LevelFile::MakeChunkTag('p', 'k', 'g', 'i');
		constexpr std::uint8_t PackageInfoVersion = 1;

		struct PackageItem
		{
			// Path in the archive, with "/" between directories
			String ArchivePath;
			// File it's read from, empty for the level being edited
			String SourcePath;
			// Why it can't be in the package, if it can't
			String Problem;
		};

		struct PackageState
		{
			String Name;
			String Author;
			String Version;
			String Description;
			String Tags;
			bool WholeEpisode = false;
			bool EditorData = false;
			SmallVector<PackageItem, 0> Items;
			SmallVector<String, 0> Levels;
		};

		String GetEpisodeName(IEditorContext& session)
		{
			return String(session.GetLevelName().partition('/')[0]);
		}

		// "unknown" isn't an episode, only where the levels of no episode are, it has nothing of an episode to pack
		bool HasEpisode(IEditorContext& session)
		{
			return (!session.GetFilePath().empty() && GetEpisodeName(session) != "unknown"_s);
		}

		void ReadPackageInfo(const LevelDocument& document, PackageState& state)
		{
			const Formats::ChunkRecord* chunk = document.FindChunk(PackageInfoChunk);
			if (chunk == nullptr || chunk->Data.size() < 2 || chunk->Data[0] == 0) {
				return;
			}
			Json::Value info;
			StringView text(reinterpret_cast<const char*>(chunk->Data.data()) + 1, chunk->Data.size() - 1);
			if (!JsonFile::Parse(text, info) || !info.isObject()) {
				return;
			}
			state.Name = JsonAccess::GetString(info, "name", state.Name);
			state.Author = JsonAccess::GetString(info, "author", state.Author);
			state.Version = JsonAccess::GetString(info, "version", state.Version);
			state.Description = JsonAccess::GetString(info, "description", state.Description);
			state.Tags = JsonAccess::GetString(info, "tags", state.Tags);
			state.WholeEpisode = JsonAccess::GetBool(info, "wholeEpisode", state.WholeEpisode);
			state.EditorData = JsonAccess::GetBool(info, "editorData", state.EditorData);
		}

		void StorePackageInfo(IEditorContext& session, const PackageState& state)
		{
			Json::Value info(Json::objectValue);
			info["name"] = state.Name.data();
			info["author"] = state.Author.data();
			info["version"] = state.Version.data();
			info["description"] = state.Description.data();
			info["tags"] = state.Tags.data();
			info["wholeEpisode"] = state.WholeEpisode;
			info["editorData"] = state.EditorData;
			std::string text = JsonFile::ToString(info, true);

			SmallVector<std::uint8_t, 0> data;
			data.push_back(PackageInfoVersion);
			data.append(reinterpret_cast<const std::uint8_t*>(text.data()), reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
			const Formats::ChunkRecord* chunk = session.GetDocument().FindChunk(PackageInfoChunk);
			if (chunk == nullptr || chunk->Data.size() != data.size() || std::memcmp(chunk->Data.data(), data.data(), data.size()) != 0) {
				// TRANSLATORS: Name of an undo step in the history, the details of the package changed in the Export Package dialog
				session.Execute(std::make_unique<SetChunkCommand>(session.GetDocument(), PackageInfoChunk, data, String(_x("editor", "Package details"))));
			}
		}

		// Whether a file is one of the game itself, which every player has already
		bool IsOfGame(StringView path)
		{
			auto& resolver = ContentResolver::Get();
			StringView userContent = resolver.GetUserContentPath();
			if (IsPathInside(path, userContent)) {
				return false;
			}
			return (IsPathInside(path, resolver.GetContentPath()) || IsPathInside(path, resolver.GetCachePath()) || IsPathInside(path, resolver.GetSourcePath()));
		}

		void AddItem(PackageState& state, const String& archivePath, StringView sourcePath, StringView problem = {})
		{
			for (const PackageItem& item : state.Items) {
				if (StringUtils::equalsIgnoreCase(item.ArchivePath, archivePath)) {
					return;
				}
			}
			PackageItem& item = state.Items.emplace_back();
			item.ArchivePath = archivePath;
			item.SourcePath = sourcePath;
			item.Problem = problem;
			// The names come from the levels, an entry must stay inside the archive where it's unpacked
			if (!Formats::LevelFile::IsSafeTileSetPath(archivePath)) {
				item.SourcePath = {};
				// TRANSLATORS: In parentheses after a file in the list of files of the Export Package dialog, the package can't have it
				item.Problem = _x("editor", "not a valid name");
			}
		}

		void AddTileSet(PackageState& state, StringView name)
		{
			if (name.empty()) {
				return;
			}
			String archivePath = "Tilesets/"_s + fs::FromNativeSeparators(String(name)) + ".j2t"_s;
			if (!Formats::LevelFile::IsSafeTileSetPath(name)) {
				AddItem(state, archivePath, {}, _x("editor", "not a valid name"));
				return;
			}
			String path = ContentTileSetLibrary::FindTileSetFile(name);
			if (path.empty()) {
				// TRANSLATORS: In parentheses after a file in the list of files of the Export Package dialog, the file wasn't found
				AddItem(state, archivePath, {}, _x("editor", "not found"));
			} else if (!IsOfGame(path)) {
				AddItem(state, archivePath, path);
			}
		}

		void AddMusic(PackageState& state, StringView music)
		{
			if (music.empty()) {
				return;
			}
			auto& resolver = ContentResolver::Get();
			String archivePath = "Music/"_s + fs::FromNativeSeparators(String(music));
			if (!Formats::LevelFile::IsSafeTileSetPath(music)) {
				AddItem(state, archivePath, {}, _x("editor", "not a valid name"));
				return;
			}
			for (StringView root : { resolver.GetUserContentPath(), resolver.GetContentPath() }) {
				if (root.empty()) {
					continue;
				}
				String path = fs::FindPathCaseInsensitive(fs::CombinePath({ root, "Music"_s, String(fs::ToNativeSeparators(music)) }));
				if (fs::IsReadableFile(path)) {
					if (!IsOfGame(path)) {
						AddItem(state, archivePath, path);
					}
					return;
				}
			}
			// The original game has its music elsewhere, a music the package doesn't need to have
			StringView source = resolver.GetSourcePath();
			if (!source.empty() && fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(source, String(fs::ToNativeSeparators(music)))))) {
				return;
			}
			AddItem(state, archivePath, {}, _x("editor", "not found"));
		}

		void AddScript(PackageState& state, StringView episode, StringView levelPath)
		{
			if (levelPath.empty()) {
				return;
			}
			String stem = fs::GetFileNameWithoutExtension(levelPath);
			String script = fs::CombinePath(fs::GetDirectoryName(levelPath), String(stem + ".j2as"_s));
			if (fs::IsReadableFile(script)) {
				AddItem(state, "Episodes/"_s + episode + "/"_s + stem + ".j2as"_s, script);
			}
		}

		void CollectItems(IEditorContext& session, PackageState& state)
		{
			state.Items.clear();
			state.Levels.clear();
			const LevelDocument& document = session.GetDocument();
			StringView filePath = session.GetFilePath();
			String episode = GetEpisodeName(session);
			String levelStem = String(session.GetLevelName().partition('/')[2]);
			if (levelStem.empty()) {
				levelStem = "level"_s;
			}

			// The level being edited, as it is now
			AddItem(state, "Episodes/"_s + episode + "/"_s + levelStem + ".j2l"_s, {});
			state.Levels.push_back(episode + "/"_s + levelStem);
			AddScript(state, episode, filePath);
			AddTileSet(state, document.Properties.DefaultTileSet);
			for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
					AddTileSet(state, tileSet.Name);
				}
			}
			AddMusic(state, document.Properties.Music);

			if (state.WholeEpisode && HasEpisode(session)) {
				String directory = fs::GetDirectoryName(filePath);
				String episodeFile = fs::CombinePath(fs::GetDirectoryName(directory), String(fs::GetFileName(directory) + ".j2e"_s));
				if (fs::IsReadableFile(episodeFile)) {
					AddItem(state, "Episodes/"_s + episode + ".j2e"_s, episodeFile);
				}
				SmallVector<EpisodeLevelInfo, 0> levels;
				EpisodeLevelInfo::ReadDirectory(directory, levels);
				for (const EpisodeLevelInfo& level : levels) {
					if (level.Name == StringUtils::lowercase(levelStem)) {
						continue;
					}
					String stem = fs::GetFileNameWithoutExtension(level.Path);
					AddItem(state, "Episodes/"_s + episode + "/"_s + stem + ".j2l"_s, level.Path);
					state.Levels.push_back(episode + "/"_s + level.Name);
					AddScript(state, episode, level.Path);
					AddTileSet(state, level.DefaultTileSet);
					for (const String& tileSet : level.ExtraTileSets) {
						AddTileSet(state, tileSet);
					}
					AddMusic(state, level.Music);
				}
			}

			// The descriptions of events of the user content, the scripts may need them
			StringView userContent = ContentResolver::Get().GetUserContentPath();
			if (!userContent.empty()) {
				String directory = fs::CombinePath({ userContent, "Editor"_s, "Events"_s });
				if (fs::DirectoryExists(directory)) {
					for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
						if (StringUtils::lowercase(item).hasSuffix(".json"_s)) {
							AddItem(state, "Editor/Events/"_s + fs::GetFileName(item), item);
						}
					}
				}
			}
		}

		bool ReadWholeFile(StringView path, SmallVectorImpl<std::uint8_t>& data)
		{
			auto s = fs::Open(path, FileAccess::Read);
			std::int64_t size = (s->IsValid() ? s->GetSize() : -1);
			if (size < 0 || size >= std::int64_t(UINT32_MAX)) {
				return false;
			}
			data.resize_for_overwrite(std::size_t(size));
			return (s->Read(data.data(), size) == size);
		}

		// The preview of the level as an image, from the chunk the list of levels shows
		bool MakePreviewImage(ArrayView<const std::uint8_t> preview, SmallVectorImpl<std::uint8_t>& png)
		{
			SmallVector<std::uint8_t, 0> rgba;
			Vector2i size;
			if (!Formats::LevelPreview::Decode(preview, rgba, size)) {
				return false;
			}
			PngImage image;
			image.Create(size.X, size.Y, false);
			std::memcpy(image.Pixels.data(), rgba.data(), rgba.size());
			MemoryStream stream(std::int64_t(size.X) * size.Y * 3 + 1024);
			if (!PngCodec::Encode(stream, image)) {
				return false;
			}
			png.assign(stream.GetBuffer(), stream.GetBuffer() + stream.GetSize());
			return true;
		}

		bool WritePackage(IEditorContext& session, PackageState& state, StringView path, String& problem)
		{
			// A fresh preview, it goes into the packaged level and into the package as an image, the level being edited
			// stays as it is, it gets its preview when it's saved
			SmallVector<std::uint8_t, 0> preview;
			LevelThumbnail::MakePreview(session.GetDocument(), session.GetTileMap(), session.GetTileSetDataCache(), preview);
			LevelDocument packaged = session.GetDocument();
			packaged.SetChunk(Formats::LevelFile::PreviewChunk, preview);

			MemoryStream level(1024 * 1024);
			if (!packaged.Save(level, !state.EditorData)) {
				problem = _x("editor", "The level couldn't be written");
				return false;
			}

			Json::Value manifest(Json::objectValue);
			manifest["format"] = 1;
			manifest["game"] = "Jazz\xC2\xB2 Resurrection";
			manifest["name"] = state.Name.data();
			manifest["author"] = state.Author.data();
			manifest["version"] = state.Version.data();
			manifest["description"] = state.Description.data();
			Json::Value& tags = manifest["tags"];
			tags = Json::Value(Json::arrayValue);
			for (StringView tag : state.Tags.split(',')) {
				tag = tag.trimmed();
				if (!tag.empty()) {
					tags.append(Json::Value(tag.data(), tag.data() + tag.size()));
				}
			}
			Json::Value& levels = manifest["levels"];
			levels = Json::Value(Json::arrayValue);
			for (const String& name : state.Levels) {
				levels.append(name.data());
			}
			if (state.WholeEpisode && HasEpisode(session)) {
				manifest["episode"] = GetEpisodeName(session).data();
			}
			Json::Value& files = manifest["files"];
			files = Json::Value(Json::arrayValue);
			for (const PackageItem& item : state.Items) {
				if (item.Problem.empty()) {
					files.append(item.ArchivePath.data());
				}
			}
			std::string manifestText = JsonFile::ToString(manifest);

			bool missing = false;
			bool written = AtomicFile::Write(path, [&](Stream& s) {
				ZipWriter zip(s);
				if (!zip.AddFile("manifest.json"_s, ArrayView<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(manifestText.data()), manifestText.size()))) {
					return false;
				}
				SmallVector<std::uint8_t, 0> png;
				if (MakePreviewImage(preview, png) && !zip.AddFile("preview.png"_s, png)) {
					return false;
				}
				SmallVector<std::uint8_t, 0> data;
				for (const PackageItem& item : state.Items) {
					if (!item.Problem.empty()) {
						continue;
					}
					if (item.SourcePath.empty()) {
						if (!zip.AddFile(item.ArchivePath, ArrayView<const std::uint8_t>(level.GetBuffer(), std::size_t(level.GetSize())))) {
							return false;
						}
						continue;
					}
					if (!ReadWholeFile(item.SourcePath, data)) {
						missing = true;
						continue;
					}
					if (!zip.AddFile(item.ArchivePath, data)) {
						return false;
					}
				}
				return zip.Finish();
			});
			if (!written) {
				problem = _x("editor", "The package couldn't be written");
				return false;
			}
			if (missing) {
				problem = _x("editor", "Some of the files couldn't be read, the package lacks them");
			}
			return true;
		}

		/** The files of the package as they're collected now, only to be read, only the rows seen are drawn, a whole episode has many */
		class PackageFileList : public UiVirtualList
		{
		public:
			explicit PackageFileList(std::shared_ptr<PackageState> state)
				: _state(Death::move(state))
			{
				// As high as a label and the space between labels in a stack
				RowHeight = UiTheme::RowHeight + 1.0f;
				Selectable = false;
				// The manifest and the preview first, every package has them
				CountFunc = [this]() { return std::int32_t(_state->Items.size()) + 2; };
				DrawRow = [this](UiPainter& painter, std::int32_t index, const Rectf& rect, bool, float) {
					// Kept off the scroll bar a little, as a label is high
					Rectf textRect(rect.X, rect.Y, std::max(rect.W - painter.Px(3.0f), 0.0f), painter.Px(UiTheme::RowHeight));
					painter.PushClip(textRect);
					if (index < 2) {
						painter.TextInRect(index == 0 ? "manifest.json"_s : "preview.png"_s, textRect, UiTheme::TextDim);
					} else {
						const PackageItem& item = _state->Items[index - 2];
						if (item.Problem.empty()) {
							painter.TextInRect(item.ArchivePath, textRect, UiTheme::Text);
						} else {
							painter.TextInRect(String(item.ArchivePath + " ("_s + item.Problem + ")"_s), textRect, UiTheme::TextDanger);
						}
					}
					painter.PopClip();
				};
			}

		private:
			std::shared_ptr<PackageState> _state;
		};

		UiTextBox* AddTextRow(UiForm* form, StringView label, String PackageState::* member, const std::shared_ptr<PackageState>& state, std::int32_t maxLength)
		{
			auto* row = form->AddRow(label);
			auto* box = row->Add<UiTextBox>(state.get()->*member);
			box->Width = 220.0f;
			box->MaxLength = maxLength;
			box->OnChanged = [state, member](StringView text) { (*state).*member = text; };
			box->OnCommit = [state, member](StringView text) { (*state).*member = text.trimmed(); };
			return box;
		}
	}

	void ShowExportPackageDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		auto state = std::make_shared<PackageState>();
		const LevelDocument& document = session.GetDocument();
		state->Name = (document.Properties.DisplayName.empty() ? String(session.GetLevelName()) : document.Properties.DisplayName);
		state->Author = document.Properties.Author;
		state->Version = "1.0"_s;
		ReadPackageInfo(document, *state);
		CollectItems(session, *state);

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that saves the level with the files it needs as a ZIP archive to publish
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Export Package"));
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();
		content->Add<UiLabel>(_x("editor", "A ZIP archive to publish, laid out as the content of the game"), UiTheme::TextDim);

		UiForm* form = content->Add<UiForm>(UiTheme::Spacing);
		// TRANSLATORS: Labels of the details of the package in the Export Package dialog
		AddTextRow(form, _x("editor", "Name"), &PackageState::Name, state, 255);
		AddTextRow(form, _x("editor", "Author"), &PackageState::Author, state, 255);
		AddTextRow(form, _x("editor", "Version"), &PackageState::Version, state, 32);
		auto* description = AddTextRow(form, _x("editor", "Description"), &PackageState::Description, state, 4096);
		description->Multiline = true;
		// TRANSLATORS: Label of the tags of the package in the Export Package dialog, and the placeholder of its empty box
		AddTextRow(form, _x("editor", "Tags"), &PackageState::Tags, state, 255)->Placeholder = _x("editor", "separated by commas");

		auto* episode = content->Add<UiCheckBox>(_x("editor", "All levels of the episode"), [s, state]() { return (state->WholeEpisode && HasEpisode(*s)); }, [s, state](bool value) {
			state->WholeEpisode = value;
			CollectItems(*s, *state);
		});
		episode->Tooltip = _x("editor", "The other levels of the episode as they are saved, the episode file and what they need");
		episode->IsEnabledFunc = [s]() { return HasEpisode(*s); };
		auto* editorData = content->Add<UiCheckBox>(_x("editor", "Keep what only the editor uses"), [state]() { return state->EditorData; }, [state](bool value) {
			state->EditorData = value;
		});
		editorData->Tooltip = _x("editor", "Layer names, notes and ground lines stay in the level, so it can be edited further as it was");

		// TRANSLATORS: Header of the list of files the package will have in the Export Package dialog
		content->Add<UiLabel>(_x("editor", "Files"), UiTheme::TextHeader);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 340.0f;
		scroll->Height = 110.0f;
		scroll->SetContent<PackageFileList>(state);

		// TRANSLATORS: Button in the Export Package dialog, verb
		dialog->AddButton(_x("editor", "Export"), [s, state, dialogPtr]() {
			UiSurface* surface = s->GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			s->GetImageRoots(true, roots);
			if (roots.empty()) {
				s->ShowMessage(_x("editor", "There is no place to save files to"));
				return;
			}
			// Collected again, the files may have changed since the dialog was opened
			CollectItems(*s, *state);
			String name = String(s->GetLevelName().partition('/')[2]);
			if (name.empty()) {
				name = "level"_s;
			}
			UiFileDialog::Show(*surface, _x("editor", "Export Package"), UiFileDialogMode::Save, Death::move(roots), ".zip"_s, {}, String(name + ".zip"_s),
				[s, state, dialogPtr](StringView path) {
				StorePackageInfo(*s, *state);
				String problem;
				bool success = WritePackage(*s, *state, path, problem);
				if (!success) {
					s->ShowMessage(problem);
					return;
				}
				s->ShowMessage(problem.empty() ? String(_x("editor", "The package was exported")) : problem);
				dialogPtr->Close();
			});
		}, true);
		// Closed by its cross or Escape, exporting isn't what they do
		dialog->OnCancel = [dialogPtr]() { dialogPtr->Close(); };

		ShowModal(*surface, Death::move(dialog));
	}
}

#endif
