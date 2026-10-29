#include "EpisodeDialog.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "ShellCommon.h"
#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/Commands.h"
#include "../Document/LevelDocument.h"
#include "../Files/AtomicFile.h"
#include "../Files/EpisodeFile.h"
#include "../Imaging/PngCodec.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiPainter.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/Graphics/Texture.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

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
		struct EpisodeState
		{
			EpisodeFile Episode;
			// Name of the episode, the name of its directory
			String Name;
			String Directory;
			String FilePath;
			bool Exists = false;
			bool Changed = false;
			// The levels of "unknown", which isn't an episode, only where the levels of no episode are, it has no file
			bool IsNoEpisode = false;
			SmallVector<EpisodeLevelInfo, 0> Levels;
			// Bumped when the levels were read again, the list follows
			std::uint32_t Generation = 0;
			// Bumped when an image was replaced or removed, the previews follow
			std::uint32_t ImageGeneration = 0;

			void ReadLevels() {
				EpisodeLevelInfo::ReadDirectory(Directory, Levels);
				Generation++;
			}

			std::int32_t FindLevel(StringView name) const {
				for (std::size_t i = 0; i < Levels.size(); i++) {
					if (Levels[i].Name == name) {
						return std::int32_t(i);
					}
				}
				return -1;
			}
		};

		enum class LinkState {
			None,		// Nothing is linked
			Inside,		// A level of this episode that exists
			Outside,	// A level of another episode that exists
			End,		// The end of the episode, ":end" or alike
			Broken		// A level that doesn't exist
		};

		// Whether a level is in any of the places the game looks for levels in
		bool LevelExistsIn(ArrayView<const UiFileRoot> roots, StringView episode, StringView level)
		{
			for (const UiFileRoot& root : roots) {
				String path = fs::FindPathCaseInsensitive(fs::CombinePath({ root.Path, episode, String(level + ".j2l"_s) }));
				if (fs::IsReadableFile(path)) {
					return true;
				}
			}
			return false;
		}

		// Whether levels are in any of the places the game looks for levels in, each one looked for only once, as the same
		// few are asked about for every level found
		class LevelLookup
		{
		public:
			explicit LevelLookup(IEditorContext& session)
			{
				session.GetFileRoots(false, _roots);
			}

			bool Exists(StringView episode, StringView level)
			{
				String key = StringUtils::lowercase(String(episode + "/"_s + level));
				for (const auto& [known, exists] : _known) {
					if (known == key) {
						return exists;
					}
				}
				bool exists = LevelExistsIn(_roots, episode, level);
				_known.emplace_back(Death::move(key), exists);
				return exists;
			}

		private:
			SmallVector<UiFileRoot, 4> _roots;
			SmallVector<std::pair<String, bool>, 0> _known;
		};

		LinkState ResolveLink(IEditorContext& session, const EpisodeState& state, StringView link, String& target)
		{
			StringView trimmed = link.trimmed();
			if (trimmed.empty()) {
				return LinkState::None;
			}
			if (trimmed[0] == ':') {
				return LinkState::End;
			}
			String level;
			String episode = SplitLevelReference(trimmed, level);
			if (episode.empty() || episode == state.Name) {
				target = level;
				return (state.FindLevel(level) >= 0 ? LinkState::Inside : LinkState::Broken);
			}
			// Another episode, looked for where the game looks for levels
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(false, roots);
			return (LevelExistsIn(roots, episode, level) ? LinkState::Outside : LinkState::Broken);
		}

		// Whether a link of a level of @p fromEpisode leads to the level @p oldName of @p episodeName, a link without an
		// episode leads to a level of the episode the level is in
		bool RefersTo(StringView link, StringView fromEpisode, StringView episodeName, StringView oldName)
		{
			String level;
			String episode = SplitLevelReference(link, level);
			if (level.empty() || level != oldName) {
				return false;
			}
			return (episode.empty() ? fromEpisode == episodeName : episode == episodeName);
		}

		// Reads only the links of a level, not to read whole tile sets that some levels have in themselves
		bool ReadLevelLinks(StringView path, String& next, String& secret, String& bonus)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return false;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (!reader.IsValid()) {
				return false;
			}
			next = Death::move(properties.NextLevel);
			secret = Death::move(properties.SecretLevel);
			bonus = Death::move(properties.BonusLevel);
			return true;
		}

		// A level whose links have to change with a renamed level
		struct LinkChange
		{
			String Path;
			// Its episode, which a link without an episode leads within
			String Episode;
			// As it's shown, "episode/level"
			String Name;
		};

		// Changes a link to the renamed level @p oldName of @p episodeName to @p newName, returns whether it was one
		bool RenameLink(String& link, StringView fromEpisode, StringView episodeName, StringView oldName, StringView newName)
		{
			if (!RefersTo(link, fromEpisode, episodeName, oldName)) {
				return false;
			}
			String level;
			String episode = SplitLevelReference(link, level);
			// In the same form, with the episode only if it had it
			link = (episode.empty() ? String(newName) : String(episode + "/"_s + newName));
			return true;
		}

		// Changes the links of a level file by @p fix, which returns whether it changed the link it got; with @p stage, the
		// file is written only into its temporary file (see AtomicFile::Stage()), otherwise it's replaced and the previous
		// one is kept as ".bak", in both cases only if any link was changed
		template<class F>
		bool RewriteLinks(StringView path, F&& fix, bool& changed, bool stage = false)
		{
			changed = false;
			LevelDocument document;
			{
				auto s = fs::Open(path, FileAccess::Read);
				// Without the tile sets, the tile IDs are kept exactly as they are
				if (!s->IsValid() || !document.Load(*s, nullptr)) {
					return false;
				}
			}
			for (String* link : { &document.Properties.NextLevel, &document.Properties.SecretLevel, &document.Properties.BonusLevel }) {
				if (fix(*link)) {
					changed = true;
				}
			}
			if (!changed) {
				return true;
			}
			auto save = [&document](Stream& s) {
				return document.Save(s);
			};
			return (stage ? AtomicFile::Stage(path, save) : AtomicFile::Write(path, save, true));
		}

		// Changes the links of the level being edited by @p fix as a step of its history, as they're saved with it, returns
		// whether any was changed
		template<class F>
		bool RewriteOwnLinks(IEditorContext& session, F&& fix, StringView name)
		{
			LevelDocument& document = session.GetDocument();
			Formats::LevelProperties properties = document.Properties;
			bool changed = false;
			for (String* link : { &properties.NextLevel, &properties.SecretLevel, &properties.BonusLevel }) {
				if (fix(*link)) {
					changed = true;
				}
			}
			if (changed) {
				session.Execute(std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, properties, String(name)));
			}
			return changed;
		}

		// Calls @p callback with the path and the episode of each level in the places levels can be saved to, but those
		// in @p skipDirectory, which are known already
		template<class F>
		void ForEachWritableLevel(IEditorContext& session, StringView skipDirectory, F&& callback)
		{
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(false, roots);
			String skipped = StringUtils::lowercase(fs::FromNativeSeparators(String(skipDirectory)));
			for (const UiFileRoot& root : roots) {
				if (!root.Writable || !fs::DirectoryExists(root.Path)) {
					continue;
				}
				for (auto directory : fs::Directory(root.Path, fs::EnumerationOptions::SkipFiles)) {
					if (!skipped.empty() && StringUtils::lowercase(fs::FromNativeSeparators(String(directory))) == skipped) {
						continue;
					}
					String episode = StringUtils::lowercase(fs::GetFileName(directory));
					for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
						if (StringUtils::lowercase(fs::GetExtension(item)) == "j2l"_s) {
							callback(StringView(item), episode);
						}
					}
				}
			}
		}

		// Lists the levels whose links change below a message, as many as fit
		void AppendLevelList(String& message, ArrayView<const LinkChange> changes)
		{
			constexpr std::size_t MaxListed = 12;
			for (std::size_t i = 0; i < changes.size() && i < MaxListed; i++) {
				message = String(message + "\n - "_s + changes[i].Name);
			}
			if (changes.size() > MaxListed) {
				std::int32_t more = std::int32_t(changes.size() - MaxListed);
				// TRANSLATORS: Last line of a list of levels in a message, {} is the number of levels that aren't listed
				message = String(message + "\n"_s + _fnx("editor", "and {} more", "and {} more", more, more));
			}
		}

		// The paths may be relative to the working directory or not, as the level was opened and as the places of levels are
		bool IsSameFile(StringView a, StringView b)
		{
			auto normalize = [](StringView path) {
				String absolute = fs::GetAbsolutePath(path);
				return StringUtils::lowercase(fs::FromNativeSeparators(absolute.empty() ? String(path) : Death::move(absolute)));
			};
			return (normalize(a) == normalize(b));
		}

		bool IsValidLevelName(StringView name)
		{
			if (name.empty() || name.size() > 64) {
				return false;
			}
			for (char c : name) {
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ')) {
					return false;
				}
			}
			return true;
		}

		void PerformRename(IEditorContext& session, const std::shared_ptr<EpisodeState>& state, const EpisodeLevelInfo& level, StringView newName,
			StringView newLower, StringView newPath, ArrayView<const LinkChange> changes, bool isOpen);

		void RenameLevel(IEditorContext& session, const std::shared_ptr<EpisodeState>& state, std::int32_t index, StringView newNameRaw)
		{
			if (index < 0 || index >= std::int32_t(state->Levels.size())) {
				return;
			}
			String newName = newNameRaw.trimmed();
			if (!IsValidLevelName(newName)) {
				session.ShowMessage(_x("editor", "A level name may have only letters, numbers, spaces, - and _"));
				return;
			}
			EpisodeLevelInfo level = state->Levels[index];
			String newLower = StringUtils::lowercase(newName);
			if (newLower == level.Name) {
				return;
			}
			if (state->FindLevel(newLower) >= 0) {
				session.ShowMessage(_x("editor", "The episode has a level of that name already"));
				return;
			}
			bool isOpen = IsSameFile(session.GetFilePath(), level.Path);
			if (isOpen && session.GetHistory().IsDirty()) {
				session.ShowMessage(_x("editor", "Save the level first, it's the one being edited"));
				return;
			}
			String newPath = fs::CombinePath(state->Directory, String(newName + ".j2l"_s));
			if (fs::FileExists(newPath)) {
				session.ShowMessage(_x("editor", "The episode has a level of that name already"));
				return;
			}

			// The levels that link to it, of this episode, and of the other episodes where they can be changed
			auto changes = std::make_shared<SmallVector<LinkChange, 0>>();
			for (const EpisodeLevelInfo& other : state->Levels) {
				if (other.Name != level.Name && (RefersTo(other.NextLevel, state->Name, state->Name, level.Name) ||
					RefersTo(other.SecretLevel, state->Name, state->Name, level.Name) || RefersTo(other.BonusLevel, state->Name, state->Name, level.Name))) {
					LinkChange& change = changes->emplace_back();
					change.Path = other.Path;
					change.Episode = state->Name;
					change.Name = state->Name + "/"_s + other.Name;
				}
			}
			ForEachWritableLevel(session, state->Directory, [&](StringView path, const String& episode) {
				String next, secret, bonus;
				if (ReadLevelLinks(path, next, secret, bonus) && (RefersTo(next, episode, state->Name, level.Name) ||
					RefersTo(secret, episode, state->Name, level.Name) || RefersTo(bonus, episode, state->Name, level.Name))) {
					LinkChange& change = changes->emplace_back();
					change.Path = path;
					change.Episode = episode;
					change.Name = episode + "/"_s + StringUtils::lowercase(fs::GetFileNameWithoutExtension(path));
				}
			});

			// What changes is listed first, the files of the other levels are kept as ".bak"
			String message = _fx("editor", "Rename level \"{}\" to \"{}\"?", level.Name, newLower);
			if (changes->empty()) {
				message = String(message + "\n"_s + _x("editor", "No other level links to it."));
			} else {
				message = String(message + "\n"_s + _x("editor", "The links to it change in these levels too, their files are kept as .bak:"));
				AppendLevelList(message, *changes);
			}
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			IEditorContext* s = &session;
			// TRANSLATORS: Title and buttons of the message confirming that a level of the episode is renamed
			UiMessageDialog::Show(*surface, _x("editor", "Rename Level"), message, { _x("editor", "Rename"), _x("editor", "Cancel") },
				[s, state, level, newName, newLower, newPath, changes, isOpen](std::int32_t button) {
				if (button == 0) {
					PerformRename(*s, state, level, newName, newLower, newPath, *changes, isOpen);
				}
			});
		}

		void PerformRename(IEditorContext& session, const std::shared_ptr<EpisodeState>& state, const EpisodeLevelInfo& level, StringView newName,
			StringView newLower, StringView newPath, ArrayView<const LinkChange> changes, bool isOpen)
		{
			// Another editor would put an old link back by saving its level, or the level back under its old name
			StringView ownPath = session.GetFilePath();
			if (IsLevelOpenElsewhere(level.Path, ownPath)) {
				session.ShowMessage(_fx("editor", "Level \"{}\" is open in another editor, close it there first", level.Name));
				return;
			}
			for (const LinkChange& change : changes) {
				if (IsLevelOpenElsewhere(change.Path, ownPath)) {
					session.ShowMessage(_fx("editor", "Level \"{}\" is open in another editor, close it there first", change.Name));
					return;
				}
			}

			// The links of the other levels are written next to their files first, nothing is changed if any can't be;
			// they're kept as they were in ".bak" files
			auto fix = [&state, &level, newLower](String& link, StringView fromEpisode) {
				return RenameLink(link, fromEpisode, state->Name, level.Name, newLower);
			};
			SmallVector<String, 0> staged;
			SmallVector<String, 0> failedNames;
			bool ownLinked = false;
			for (const LinkChange& change : changes) {
				if (!isOpen && IsSameFile(change.Path, ownPath)) {
					// The level being edited, its file and the level in the editor both follow
					ownLinked = true;
				}
				bool changed;
				if (!RewriteLinks(change.Path, [&](String& link) { return fix(link, change.Episode); }, changed, true)) {
					failedNames.push_back(change.Name);
				} else if (changed) {
					staged.push_back(change.Path);
				}
			}
			auto discardStaged = [&staged]() {
				for (const String& path : staged) {
					AtomicFile::Discard(path);
				}
			};
			if (!failedNames.empty()) {
				discardStaged();
				String message = _x("editor", "The level wasn't renamed, the links to it couldn't be changed in these levels:");
				for (const String& name : failedNames) {
					message = String(message + "\n - "_s + name);
				}
				session.ShowMessage(message);
				return;
			}

			// The level and its script are moved, put back if anything after it fails
			if (fs::FileExists(newPath) || !fs::Move(level.Path, newPath)) {
				discardStaged();
				session.ShowMessage(_x("editor", "The level couldn't be renamed"));
				return;
			}
			String scriptFrom = fs::CombinePath(state->Directory, String(fs::GetFileNameWithoutExtension(level.Path) + ".j2as"_s));
			String scriptTo = fs::CombinePath(state->Directory, String(newName + ".j2as"_s));
			bool scriptMoved = false;
			auto rollBackMoves = [&]() {
				if (scriptMoved && !fs::Move(scriptTo, scriptFrom)) {
					LOGE("Script \"{}\" couldn't be moved back to \"{}\"", scriptTo, scriptFrom);
				}
				if (!fs::Move(newPath, level.Path)) {
					LOGE("Level \"{}\" couldn't be moved back to \"{}\"", newPath, level.Path);
				}
			};
			if (fs::FileExists(scriptFrom)) {
				if (fs::FileExists(scriptTo) || !fs::Move(scriptFrom, scriptTo)) {
					discardStaged();
					rollBackMoves();
					session.ShowMessage(_x("editor", "The script of the level couldn't be renamed, so the level wasn't renamed either"));
					return;
				}
				scriptMoved = true;
			}

			// The files replaced already are put back if one can't be
			std::size_t committed = 0;
			while (committed < staged.size() && AtomicFile::Commit(staged[committed], true)) {
				committed++;
			}
			auto rollBackLinks = [&]() {
				for (std::size_t i = 0; i < committed; i++) {
					if (!AtomicFile::RestoreBackup(staged[i], true)) {
						LOGE("File \"{}\" couldn't be put back, its previous content is in \"{}.bak\"", staged[i], staged[i]);
					}
				}
				for (std::size_t i = committed; i < staged.size(); i++) {
					AtomicFile::Discard(staged[i]);
				}
			};
			if (committed < staged.size()) {
				String failedName = StringUtils::lowercase(fs::GetFileNameWithoutExtension(staged[committed]));
				rollBackLinks();
				rollBackMoves();
				session.ShowMessage(_fx("editor", "Level \"{}\" couldn't be changed, so the level wasn't renamed", failedName));
				return;
			}

			String firstLevel;
			SplitLevelReference(state->Episode.FirstLevel, firstLevel);
			if (firstLevel == level.Name) {
				String previousFirst = state->Episode.FirstLevel;
				state->Episode.FirstLevel = newLower;
				if (state->Exists && !state->Episode.Write(state->FilePath, true)) {
					state->Episode.FirstLevel = Death::move(previousFirst);
					rollBackLinks();
					rollBackMoves();
					session.ShowMessage(_x("editor", "The episode couldn't be saved, so the level wasn't renamed"));
					return;
				}
			}
			state->ReadLevels();

			// The old name isn't a file anymore, also for the level being edited, which is moved here, not by Save As
			session.NoteLevelFileMoved(level.Path, newPath);

			if (!isOpen && !ownPath.empty()) {
				// The level in the editor gets the new links as a step of its history, otherwise saving it would put the
				// old ones back; if its file has them already, it's as saved as it was
				bool wasDirty = session.GetHistory().IsDirty();
				// Its episode is the directory it's in, as the levels found are given theirs
				StringView ownDirectory = fs::GetDirectoryName(ownPath);
				String ownEpisode = (IsSameFile(ownDirectory, state->Directory) ? state->Name : StringUtils::lowercase(fs::GetFileName(ownDirectory)));
				// TRANSLATORS: Name of an undo step in the history, the links of the level changed after another level was renamed
				if (RewriteOwnLinks(session, [&](String& link) { return fix(link, ownEpisode); }, _x("editor", "Links to a renamed level")) && ownLinked && !wasDirty) {
					session.GetHistory().MarkSaved();
				}
			}

			std::int32_t rewritten = std::int32_t(staged.size());
			session.ShowMessage(_fnx("editor", "The level was renamed, {} level linking to it was changed", "The level was renamed, {} levels linking to it were changed", rewritten, rewritten));
			if (isOpen) {
				// Opened again under its new name, it wasn't changed
				session.OpenFile(newPath);
			}
		}

		void ImportImage(IEditorContext& session, const std::shared_ptr<EpisodeState>& state, bool title)
		{
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the file dialog importing the title image or the background image of the episode
			ShowOpenImageDialog(session, title ? _x("editor", "Title Image") : _x("editor", "Background Image"), [s, state, title](StringView path) {
				PngImage image;
				String error;
				if (!PngCodec::Load(path, image, &error)) {
					s->ShowMessage(error);
					return;
				}
				if (image.Width > 4096 || image.Height > 4096) {
					s->ShowMessage(_x("editor", "The image is too large"));
					return;
				}
				EpisodeImage& target = (title ? state->Episode.Title : state->Episode.Background);
				target.Width = image.Width;
				target.Height = image.Height;
				target.Pixels.resize_for_overwrite(std::size_t(image.Width) * std::size_t(image.Height) * 4);
				for (std::int32_t y = 0; y < image.Height; y++) {
					for (std::int32_t x = 0; x < image.Width; x++) {
						std::uint32_t color = image.GetColor(x, y);
						std::uint8_t* pixel = &target.Pixels[(std::size_t(y) * std::size_t(image.Width) + std::size_t(x)) * 4];
						pixel[0] = std::uint8_t(color);
						pixel[1] = std::uint8_t(color >> 8);
						pixel[2] = std::uint8_t(color >> 16);
						pixel[3] = std::uint8_t(color >> 24);
					}
				}
				state->Changed = true;
				state->ImageGeneration++;
			});
		}

		void ExportImage(IEditorContext& session, const std::shared_ptr<EpisodeState>& state, bool title)
		{
			UiSurface* surface = session.GetSurface();
			const EpisodeImage& source = (title ? state->Episode.Title : state->Episode.Background);
			if (surface == nullptr || source.IsEmpty()) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetImageRoots(true, roots);
			if (roots.empty()) {
				session.ShowMessage(_x("editor", "There is no place to save files to"));
				return;
			}
			IEditorContext* s = &session;
			String name = state->Name + (title ? "-title.png"_s : "-background.png"_s);
			// TRANSLATORS: Title of the file dialog saving the title image or the background image of the episode as a PNG file
			UiFileDialog::Show(*surface, title ? _x("editor", "Export Title Image") : _x("editor", "Export Background Image"), UiFileDialogMode::Save, Death::move(roots), ".png"_s,
				{}, name, [s, state, title](StringView path) {
				const EpisodeImage& image = (title ? state->Episode.Title : state->Episode.Background);
				PngImage png;
				png.Create(image.Width, image.Height, false);
				std::memcpy(png.Pixels.data(), image.Pixels.data(), std::min<std::size_t>(png.Pixels.size(), image.Pixels.size()));
				if (!AtomicFile::Write(path, [&png](Stream& stream) { return PngCodec::Encode(stream, png); })) {
					s->ShowMessage(_x("editor", "The file couldn't be written"));
				}
			});
		}

		String FormatImage(const EpisodeImage& image)
		{
			if (image.IsEmpty()) {
				// TRANSLATORS: In the Episode dialog instead of the size of an image the episode doesn't have
				return _x("editor", "None");
			}
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%d x %d", image.Width, image.Height);
			return buffer;
		}

		/** A small preview of an image of the episode as it fits, made again when the image is replaced or removed */
		class EpisodeImagePreview : public UiElement
		{
		public:
			EpisodeImagePreview(std::shared_ptr<EpisodeState> state, bool title)
				: _state(Death::move(state)), _title(title), _shownGeneration(UINT32_MAX)
			{
				Width = 120.0f;
				Height = 36.0f;
			}

			void Draw(UiPainter& painter) override
			{
				const EpisodeImage& image = (_title ? _state->Episode.Title : _state->Episode.Background);
				if (_shownGeneration != _state->ImageGeneration) {
					_shownGeneration = _state->ImageGeneration;
					Rebuild(image);
				}
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
				if (_texture == nullptr || image.IsEmpty()) {
					return;
				}
				// The whole image, centered, its aspect kept
				float fit = std::min(Bounds.W / float(image.Width), Bounds.H / float(image.Height));
				float w = std::max(std::round(image.Width * fit), 1.0f);
				float h = std::max(std::round(image.Height * fit), 1.0f);
				Rectf rect(std::round(Bounds.X + (Bounds.W - w) * 0.5f), std::round(Bounds.Y + (Bounds.H - h) * 0.5f), w, h);
				painter.Image(*_texture, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Content);
			}

		private:
			// Larger images are made smaller before they're uploaded, they're shown much smaller anyway
			static constexpr std::int32_t MaxTextureSize = 256;

			std::shared_ptr<EpisodeState> _state;
			bool _title;
			std::uint32_t _shownGeneration;
			std::unique_ptr<nCine::Texture> _texture;

			void Rebuild(const EpisodeImage& image)
			{
				_texture = nullptr;
				if (image.IsEmpty() || image.Pixels.size() < std::size_t(image.Width) * std::size_t(image.Height) * 4) {
					return;
				}
				float scale = std::min(1.0f, std::min(float(MaxTextureSize) / float(image.Width), float(MaxTextureSize) / float(image.Height)));
				std::int32_t width = std::max<std::int32_t>(std::int32_t(std::round(image.Width * scale)), 1);
				std::int32_t height = std::max<std::int32_t>(std::int32_t(std::round(image.Height * scale)), 1);

				// Each texel is the average of the pixels it covers
				SmallVector<std::uint8_t, 0> texels;
				texels.resize_for_overwrite(std::size_t(width) * std::size_t(height) * 4);
				for (std::int32_t y = 0; y < height; y++) {
					std::int32_t y1 = std::int32_t(std::int64_t(y) * image.Height / height);
					std::int32_t y2 = std::max<std::int32_t>(std::int32_t(std::int64_t(y + 1) * image.Height / height), y1 + 1);
					for (std::int32_t x = 0; x < width; x++) {
						std::int32_t x1 = std::int32_t(std::int64_t(x) * image.Width / width);
						std::int32_t x2 = std::max<std::int32_t>(std::int32_t(std::int64_t(x + 1) * image.Width / width), x1 + 1);
						std::uint32_t sum[4] = {};
						for (std::int32_t sy = y1; sy < y2; sy++) {
							const std::uint8_t* pixel = &image.Pixels[(std::size_t(sy) * std::size_t(image.Width) + std::size_t(x1)) * 4];
							for (std::int32_t sx = x1; sx < x2; sx++, pixel += 4) {
								sum[0] += pixel[0];
								sum[1] += pixel[1];
								sum[2] += pixel[2];
								sum[3] += pixel[3];
							}
						}
						std::uint32_t count = std::uint32_t((y2 - y1) * (x2 - x1));
						std::uint8_t* texel = &texels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
						for (std::int32_t c = 0; c < 4; c++) {
							texel[c] = std::uint8_t(sum[c] / count);
						}
					}
				}

				_texture = std::make_unique<nCine::Texture>("EditorEpisodeImage", nCine::Texture::Format::RGBA8, width, height);
				// Made smaller smoothly, made larger in pixels, the images are pixel art
				_texture->SetMinFiltering(nCine::SamplerFilter::Linear);
				_texture->SetMagFiltering(nCine::SamplerFilter::Nearest);
				_texture->SetWrap(nCine::SamplerWrapping::ClampToEdge);
				_texture->LoadFromTexels(texels.data(), 0, 0, width, height);
			}
		};

		void AddImageRow(UiForm* form, IEditorContext& session, const std::shared_ptr<EpisodeState>& state, bool title)
		{
			IEditorContext* s = &session;
			// TRANSLATORS: Labels of the rows of the title image and the background image of the episode in the Episode dialog
			auto* row = form->AddRow(title ? _x("editor", "Title image") : _x("editor", "Background"));
			auto* size = row->Add<UiLabel>();
			size->TextFunc = [state, title]() { return FormatImage(title ? state->Episode.Title : state->Episode.Background); };
			size->Width = 90.0f;
			// TRANSLATORS: Button in the Episode dialog that replaces the title or background image of the episode by a PNG file, and its tooltip
			row->Add<UiButton>(_x("editor", "Import"), EditorIcon::Import, [s, state, title]() { ImportImage(*s, state, title); })->Tooltip = _x("editor", "Replace it by a PNG image");
			auto* exportButton = row->Add<UiButton>(StringView(), EditorIcon::Export, [s, state, title]() { ExportImage(*s, state, title); });
			exportButton->Tooltip = _x("editor", "Save it as a PNG image");
			exportButton->IsEnabledFunc = [state, title]() { return !(title ? state->Episode.Title : state->Episode.Background).IsEmpty(); };
			auto* removeButton = row->Add<UiButton>(StringView(), EditorIcon::Delete, [state, title]() {
				EpisodeImage& image = (title ? state->Episode.Title : state->Episode.Background);
				image = EpisodeImage();
				state->Changed = true;
				state->ImageGeneration++;
			});
			// TRANSLATORS: Tooltip of the button in the Episode dialog that removes the title or background image of the episode
			removeButton->Tooltip = _x("editor", "Remove it");
			removeButton->IsEnabledFunc = [state, title]() { return !(title ? state->Episode.Title : state->Episode.Background).IsEmpty(); };
			// Larger below the buttons
			auto* preview = form->AddRow(StringView())->Add<EpisodeImagePreview>(state, title);
			preview->Width = 240.0f;
			preview->Height = (title ? 60.0f : 135.0f);
		}

		/** A level and its links in the list of levels, every other one on a lighter background */
		class EpisodeLevelEntry : public UiStack
		{
		public:
			explicit EpisodeLevelEntry(bool striped)
				: UiStack(UiOrientation::Vertical, 1.0f, UiThickness(3.0f, 2.0f)), _striped(striped)
			{
			}

			void Draw(UiPainter& painter) override
			{
				if (_striped) {
					painter.FillRect(Bounds, Colorf(UiTheme::Hover.R, UiTheme::Hover.G, UiTheme::Hover.B, 0.05f), UiLayer::Fill);
				}
				UiStack::Draw(painter);
			}

		private:
			bool _striped;
		};

		/** The levels of the episode in the order the links lead, rebuilt when they're read again */
		class EpisodeLevelList : public UiStack
		{
		public:
			EpisodeLevelList(IEditorContext& session, std::shared_ptr<EpisodeState> state, UiDialog* dialog)
				: UiStack(UiOrientation::Vertical, 0.0f, UiThickness(0.0f, 0.0f, 3.0f, 0.0f)), _session(session), _state(Death::move(state)),
					_dialog(dialog), _shownGeneration(UINT32_MAX)
			{
			}

			void Update(float timeMult) override
			{
				// The first level changes the order too
				if (_state->Generation != _shownGeneration || _state->Episode.FirstLevel != _shownFirstLevel) {
					Rebuild();
				}
				UiStack::Update(timeMult);
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<EpisodeState> _state;
			// The dialog the list is in, it outlives the list
			UiDialog* _dialog;
			std::uint32_t _shownGeneration;
			String _shownFirstLevel;

			void Rebuild()
			{
				_shownGeneration = _state->Generation;
				_shownFirstLevel = _state->Episode.FirstLevel;
				ClearChildren();
				if (_state->Levels.empty()) {
					Add<UiLabel>(_state->IsNoEpisode ? _x("editor", "There are no levels") : _x("editor", "The episode has no levels yet"), UiTheme::TextDisabled);
					return;
				}
				if (_state->IsNoEpisode) {
					// Not an episode, nothing is first and nothing has to lead anywhere, by their names
					for (std::size_t i = 0; i < _state->Levels.size(); i++) {
						AddLevelRow(std::int32_t(i), false, false, (i & 1) != 0);
					}
					return;
				}

				// From the first level along the links, the levels nothing leads to come last
				SmallVector<std::int32_t, 0> order;
				SmallVector<bool, 0> reached;
				reached.resize(_state->Levels.size(), false);
				String firstLevel;
				SplitLevelReference(_state->Episode.FirstLevel, firstLevel);
				std::int32_t first = _state->FindLevel(firstLevel);
				if (first >= 0) {
					order.push_back(first);
					reached[first] = true;
				}
				for (std::size_t i = 0; i < order.size(); i++) {
					const EpisodeLevelInfo& level = _state->Levels[order[i]];
					for (const String* link : { &level.NextLevel, &level.SecretLevel, &level.BonusLevel }) {
						String target;
						if (ResolveLink(_session, *_state, *link, target) == LinkState::Inside) {
							std::int32_t index = _state->FindLevel(target);
							if (index >= 0 && !reached[index]) {
								reached[index] = true;
								order.push_back(index);
							}
						}
					}
				}
				std::int32_t reachableCount = std::int32_t(order.size());
				for (std::size_t i = 0; i < _state->Levels.size(); i++) {
					if (!reached[i]) {
						order.push_back(std::int32_t(i));
					}
				}

				for (std::size_t i = 0; i < order.size(); i++) {
					AddLevelRow(order[i], std::int32_t(order[i]) == first, std::int32_t(i) >= reachableCount, (i & 1) != 0);
				}
			}

			void AddLevelRow(std::int32_t index, bool isFirst, bool isOrphan, bool striped)
			{
				const EpisodeLevelInfo& level = _state->Levels[index];
				IEditorContext* s = &_session;
				std::shared_ptr<EpisodeState> state = _state;

				auto* entry = Add<EpisodeLevelEntry>(striped);
				auto* row = entry->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				String title = level.Name;
				if (!level.DisplayName.empty()) {
					title += " - "_s + level.DisplayName;
				}
				if (isFirst) {
					// TRANSLATORS: In parentheses after the first level of the episode in the list of levels of the Episode dialog
					title += " ("_s + _x("editor", "first") + ")"_s;
				} else if (isOrphan) {
					// TRANSLATORS: In parentheses after a level in the list of levels of the Episode dialog that no other level links to
					title += " ("_s + _x("editor", "nothing leads here") + ")"_s;
				}
				auto* label = row->Add<UiLabel>(title, isFirst ? UiTheme::TextAccent : (isOrphan ? UiTheme::TextDanger : UiTheme::Text));
				label->Flex = 1.0f;
				String path = level.Path;
				UiDialog* dialog = _dialog;
				// TRANSLATORS: Button next to a level in the Episode dialog, opens it in the editor
				auto* open = row->Add<UiButton>(_x("editor", "Open"), EditorIcon::None, [s, path, dialog]() {
					// The level being edited stays as it is, the dialog only goes away
					if (IsSameFile(s->GetFilePath(), path)) {
						if (dialog != nullptr) {
							dialog->Close();
						}
						return;
					}
					s->OpenFile(path);
				});
				open->Style = UiButtonStyle::Flat;
				// TRANSLATORS: Button next to a level in the Episode dialog, gives it another name
				auto* rename = row->Add<UiButton>(_x("editor", "Rename"), EditorIcon::None, [s, state, index]() {
					UiSurface* surface = s->GetSurface();
					if (surface == nullptr || index >= std::int32_t(state->Levels.size())) {
						return;
					}
					// TRANSLATORS: Title of the prompt for a new name of a level in the Episode dialog, with its description
					ShowTextPrompt(*surface, _x("editor", "Rename Level"), _x("editor", "The links to it in the other levels of the episode are changed too"),
						state->Levels[index].Name, 64, [s, state, index](StringView name) {
						RenameLevel(*s, state, index, name);
					});
				});
				rename->Style = UiButtonStyle::Flat;

				// Where its links lead
				struct LinkField { String Label; const String* Value; };
				static const LinkField fields[] = {
					// TRANSLATORS: Kinds of links of a level to another level (next, secret and bonus level), shown before the name of the level linked
					{ _x("editor", "Next"), &level.NextLevel },
					{ _x("editor", "Secret"), &level.SecretLevel },
					{ _x("editor", "Bonus"), &level.BonusLevel }
				};
				String links;
				bool broken = false;
				for (const LinkField& field : fields) {
					String target;
					LinkState linkState = ResolveLink(_session, *_state, *field.Value, target);
					if (linkState == LinkState::None) {
						continue;
					}
					if (!links.empty()) {
						links += ", "_s;
					}
					links += String(field.Label) + ": "_s + *field.Value;
					if (linkState == LinkState::Broken) {
						// TRANSLATORS: In parentheses after a link of a level in the Episode dialog, the level linked doesn't exist
						links += " ("_s + _x("editor", "doesn't exist") + ")"_s;
						broken = true;
					} else if (linkState == LinkState::Outside) {
						// TRANSLATORS: In parentheses after a link of a level in the Episode dialog, the level linked is in another episode
						links += " ("_s + _x("editor", "another episode") + ")"_s;
					} else if (linkState == LinkState::End) {
						// TRANSLATORS: In parentheses after a link of a level in the Episode dialog, the link ends the episode
						links += " ("_s + _x("editor", "end of the episode") + ")"_s;
					}
				}
				if (links.empty()) {
					// TRANSLATORS: Below a level in the Episode dialog that has no links to other levels
					links = _x("editor", "Leads nowhere");
				}
				auto* linksLabel = entry->Add<UiLabel>(links, broken ? UiTheme::TextDanger : UiTheme::TextDim);
				linksLabel->Tooltip = _x("editor", "Next, secret and bonus level of the level, as set in its properties");
			}
		};

		void ShowEpisodeWindow(IEditorContext& session, const std::shared_ptr<EpisodeState>& state)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			state->ReadLevels();
			IEditorContext* s = &session;
			// The list of levels has no scroll bar of its own, the whole content scrolls with it
			// TRANSLATORS: Title of the Episode dialog, which shows the episode of the level and its levels
			auto dialog = std::make_unique<UiDialog>(_x("editor", "Episode"), true);
			dialog->MinScreenWidthFraction = 0.7f;
			dialog->MaxStretchedWidth = 720.0f;
			dialog->MaxScreenHeightFraction = 0.8f;
			UiDialog* dialogPtr = dialog.get();
			UiStack* content = dialog->GetContent();

			if (state->IsNoEpisode) {
				// Only the levels, there's nothing of an episode to set
				// TRANSLATORS: Header of the Episode dialog for the levels that aren't in any episode
				content->Add<UiLabel>(_x("editor", "Levels of no episode"), UiTheme::TextHeader);
				// TRANSLATORS: "unknown" is the name of a folder, keep it as it is
				content->Add<UiLabel>(_x("editor", "They're in \"unknown\", which isn't an episode, the game finds them there by their names"), UiTheme::TextDim);
				content->Add<EpisodeLevelList>(session, state, dialogPtr);

				ShowModal(*surface, Death::move(dialog));
				return;
			}

			// TRANSLATORS: Header of the Episode dialog, {} is the name of the folder of the episode
			content->Add<UiLabel>(state->Exists ? _fx("editor", "Episode \"{}\"", state->Name) : _fx("editor", "Episode \"{}\" has no episode file yet, saving makes it", state->Name),
				UiTheme::TextHeader);

			// The properties in the first column, the levels in the second one
			auto* columns = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 4.0f);
			UiForm* form = columns->Add<UiForm>(UiTheme::Spacing);
			{
				// TRANSLATORS: Label of the name of the episode shown in the game in the Episode dialog
				auto* row = form->AddRow(_x("editor", "Name"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 190.0f;
				box->MaxLength = 255;
				box->TextFunc = [state]() { return state->Episode.DisplayName; };
				box->OnCommit = [state](StringView text) {
					if (state->Episode.DisplayName != text) {
						state->Episode.DisplayName = text.trimmed();
						state->Changed = true;
					}
				};
			}
			{
				// TRANSLATORS: Label of the level the episode starts with in the Episode dialog
				auto* row = form->AddRow(_x("editor", "First level"));
				auto* dropdown = row->Add<UiDropdown>([state]() {
					// TRANSLATORS: In the Episode dialog when the episode has no first level set
					return (state->Episode.FirstLevel.empty() ? String(_x("editor", "None")) : state->Episode.FirstLevel);
				}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
					for (const EpisodeLevelInfo& level : state->Levels) {
						String name = level.Name;
						UiMenuEntry entry;
						entry.Label = (level.DisplayName.empty() ? name : String(name + " - "_s + level.DisplayName));
						entry.IsChecked = [state, name]() { return StringUtils::lowercase(state->Episode.FirstLevel) == name; };
						entry.Execute = [state, name]() {
							state->Episode.FirstLevel = name;
							state->Changed = true;
						};
						entries.push_back(Death::move(entry));
					}
				});
				dropdown->Width = 190.0f;
				dropdown->Tooltip = _x("editor", "Level the episode starts with");
			}
			for (bool previous : { true, false }) {
				// TRANSLATORS: Labels of the episodes played before and after this one in the Episode dialog
				auto* row = form->AddRow(previous ? _x("editor", "Previous episode") : _x("editor", "Next episode"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 190.0f;
				box->MaxLength = 255;
				// TRANSLATORS: Placeholder of the empty box of the previous or next episode in the Episode dialog
				box->Placeholder = _x("editor", "none");
				box->TextFunc = [state, previous]() { return (previous ? state->Episode.PreviousEpisode : state->Episode.NextEpisode); };
				box->OnCommit = [state, previous](StringView text) {
					String& value = (previous ? state->Episode.PreviousEpisode : state->Episode.NextEpisode);
					String trimmed = text.trimmed();
					if (value != trimmed) {
						value = Death::move(trimmed);
						state->Changed = true;
					}
				};
				box->Tooltip = (previous ? _x("editor", "Name of the episode before this one, its file name without the extension")
					: _x("editor", "Name of the episode played after this one, its file name without the extension"));
			}
			{
				// TRANSLATORS: Label of the order of the episode in the list of episodes in the Episode dialog
				auto* row = form->AddRow(_x("editor", "Position"));
				auto* number = row->Add<UiNumberBox>([state]() { return float(state->Episode.Position); }, [state](float value, bool) {
					std::uint16_t position = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(value), 0, 65535));
					if (position != state->Episode.Position) {
						state->Episode.Position = position;
						state->Changed = true;
					}
				}, 0.0f, 65535.0f, 1.0f, 0);
				number->Width = 60.0f;
				number->Tooltip = _x("editor", "The list of episodes is sorted by it, the episodes of the original game are below 100");
			}
			AddImageRow(form, session, state, true);
			AddImageRow(form, session, state, false);

			auto* levels = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			levels->Flex = 1.0f;
			levels->Add<UiLabel>(_x("editor", "Levels, in the order the links lead from the first one"), UiTheme::TextHeader);
			levels->Add<EpisodeLevelList>(session, state, dialogPtr);

			dialog->AddButton(_("Save"), [s, state]() {
				if (state->Episode.Write(state->FilePath, state->Exists)) {
					state->Exists = true;
					state->Changed = false;
					s->ShowMessage(_x("editor", "The episode was saved"));
				} else {
					s->ShowMessage(_x("editor", "The episode couldn't be saved"));
				}
			}, true);
			// Closed by its cross or Escape, a changed episode asks to be saved first
			dialog->OnCancel = [dialogPtr, s, state]() {
				if (!state->Changed) {
					dialogPtr->Close();
					return;
				}
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					dialogPtr->Close();
					return;
				}
				// TRANSLATORS: Title and buttons of the message asking to save the changed episode when the Episode dialog is closed
				UiMessageDialog::Show(*surface, _x("editor", "Episode"), _x("editor", "The episode was changed, do you want to save it?"), { _("Save"), _x("editor", "Don't Save"), _x("editor", "Cancel") },
					[dialogPtr, s, state](std::int32_t button) {
					if (button == 0) {
						if (!state->Episode.Write(state->FilePath, state->Exists)) {
							s->ShowMessage(_x("editor", "The episode couldn't be saved"));
							return;
						}
						s->ShowMessage(_x("editor", "The episode was saved"));
						dialogPtr->Close();
					} else if (button == 1) {
						dialogPtr->Close();
					}
				});
			};

			ShowModal(*surface, Death::move(dialog));
		}

		// An episode a level of no episode can be moved into
		struct EpisodeTarget
		{
			// Name of the episode, the name of its directory, in lower case
			String Name;
			// Directory of its levels, it may not exist yet
			String Directory;
			String FilePath;
			String DisplayName;
			String FirstLevel;
			// Whether it has an episode file, the window of the episode makes one otherwise
			bool Exists = false;
			std::int32_t LevelCount = 0;
		};

		// The level being moved out of "unknown" into an episode, and what changes along
		struct MovePlan
		{
			EpisodeTarget Target;
			// Whether the episode is made for the level
			bool IsNew = false;
			// Name of the level, its file name without the extension, in lower case
			String LevelName;
			String NewPath;
			// The file in "unknown" can't be changed, as in the cache, so it stays and the level is only copied
			bool IsCopy = false;
			// The properties of the level with its own links changed, if any was
			Formats::LevelProperties Properties;
			bool OwnLinksChanged = false;
			// The other levels that link to it in "unknown"
			SmallVector<LinkChange, 0> Changes;
		};

		constexpr std::size_t MaxEpisodeNameLength = 32;

		// An episode is known by the name of its directory, links refer to its levels as "episode/level", so it has only
		// what any file system takes, in lower case as the game looks for it
		bool IsValidEpisodeName(StringView name)
		{
			if (name.empty() || name.size() > MaxEpisodeNameLength) {
				return false;
			}
			for (char c : name) {
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
					return false;
				}
			}
			return true;
		}

		// The name of the directory of an episode made of the name it's shown by
		String MakeEpisodeName(StringView displayName)
		{
			char name[MaxEpisodeNameLength];
			std::size_t length = 0;
			for (char c : displayName) {
				if (length >= MaxEpisodeNameLength) {
					break;
				}
				if (c >= 'A' && c <= 'Z') {
					c = char(c - 'A' + 'a');
				}
				if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
					name[length++] = c;
				} else if ((c == ' ' || c == '-') && length > 0 && name[length - 1] != '-') {
					name[length++] = '-';
				}
			}
			while (length > 0 && name[length - 1] == '-') {
				length--;
			}
			return String(name, length);
		}

		// Whether an episode of the name is anywhere the game looks for episodes, by its file or by the directory of its levels
		bool EpisodeExists(ArrayView<const UiFileRoot> roots, StringView name)
		{
			for (const UiFileRoot& root : roots) {
				if (fs::DirectoryExists(fs::FindPathCaseInsensitive(fs::CombinePath(root.Path, name))) ||
					fs::FileExists(fs::FindPathCaseInsensitive(fs::CombinePath(root.Path, String(name + ".j2e"_s))))) {
					return true;
				}
			}
			return false;
		}

		// The episodes in the places levels can be saved to, each name once, as the game finds the first of them
		void FindEpisodeTargets(IEditorContext& session, SmallVectorImpl<EpisodeTarget>& targets)
		{
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(true, roots);
			for (const UiFileRoot& root : roots) {
				if (!fs::DirectoryExists(root.Path)) {
					continue;
				}
				// An episode is its file, the directory of its levels, or both
				std::size_t firstOfRoot = targets.size();
				for (auto item : fs::Directory(root.Path)) {
					bool isDirectory = fs::DirectoryExists(item);
					if (!isDirectory && StringUtils::lowercase(fs::GetExtension(item)) != "j2e"_s) {
						continue;
					}
					String fileName = (isDirectory ? String(fs::GetFileName(item)) : String(fs::GetFileNameWithoutExtension(item)));
					String name = StringUtils::lowercase(fileName);
					if (name.empty() || name == "unknown"_s) {
						continue;
					}
					std::size_t index = 0;
					while (index < targets.size() && targets[index].Name != name) {
						index++;
					}
					if (index < firstOfRoot) {
						// Hidden by the episode of a place the game looks into first
						continue;
					}
					if (index == targets.size()) {
						targets.emplace_back().Name = Death::move(name);
						targets[index].Directory = fs::CombinePath(root.Path, fileName);
					} else if (isDirectory) {
						// As the directory is named, the file may differ in case
						targets[index].Directory = fs::CombinePath(root.Path, fileName);
					}
				}
			}

			for (EpisodeTarget& target : targets) {
				// The episode file is next to the directory of its levels, named as it is
				target.FilePath = fs::CombinePath(fs::GetDirectoryName(target.Directory), String(fs::GetFileName(target.Directory) + ".j2e"_s));
				EpisodeFile episode;
				target.Exists = EpisodeFile::Read(target.FilePath, episode);
				target.DisplayName = Death::move(episode.DisplayName);
				target.FirstLevel = Death::move(episode.FirstLevel);
				if (fs::DirectoryExists(target.Directory)) {
					for (auto item : fs::Directory(target.Directory, fs::EnumerationOptions::SkipDirectories)) {
						if (StringUtils::lowercase(fs::GetExtension(item)) == "j2l"_s) {
							target.LevelCount++;
						}
					}
				}
			}
			std::sort(targets.begin(), targets.end(), [](const EpisodeTarget& a, const EpisodeTarget& b) {
				return (StringUtils::lowercase(a.DisplayName.empty() ? StringView(a.Name) : StringView(a.DisplayName)) <
					StringUtils::lowercase(b.DisplayName.empty() ? StringView(b.Name) : StringView(b.DisplayName)));
			});
		}

		// Keeps a link of the level moved out of "unknown" leading where it led: a level by just its name was looked for in
		// "unknown", in the episode it'd be looked for there first, so the link gets "unknown/" --- unless it's the level
		// itself, which is in the episode then
		bool KeepOwnLink(String& link, StringView levelName, LevelLookup& levels)
		{
			StringView trimmed = link.trimmed();
			if (trimmed.empty() || trimmed[0] == ':') {
				return false;
			}
			String level;
			String episode = SplitLevelReference(trimmed, level);
			if (level == levelName) {
				if (episode == "unknown"_s) {
					link = level;
					return true;
				}
				return false;
			}
			if (!episode.empty() || !levels.Exists("unknown"_s, level)) {
				return false;
			}
			link = String("unknown/"_s + level);
			return true;
		}

		// Whether a link of a level of @p fromEpisode leads to the level @p levelName of "unknown": by "unknown/level", by
		// just its name within "unknown", or by just its name within an episode without such a level, as the game looks into
		// "unknown" then --- a level of the episode it's moved into finds it by its name there
		bool LeadsToNoEpisodeLevel(StringView link, StringView fromEpisode, StringView levelName, StringView targetEpisode, LevelLookup& levels)
		{
			String level;
			String episode = SplitLevelReference(link, level);
			if (level != levelName) {
				return false;
			}
			if (!episode.empty()) {
				return (episode == "unknown"_s);
			}
			if (fromEpisode == "unknown"_s) {
				return true;
			}
			return (fromEpisode != targetEpisode && !levels.Exists(fromEpisode, levelName));
		}

		// Changes a link that leads to the level of "unknown" to lead to it in the episode it was moved into
		bool MoveLink(String& link, StringView fromEpisode, StringView levelName, StringView targetEpisode, LevelLookup& levels)
		{
			if (!LeadsToNoEpisodeLevel(link, fromEpisode, levelName, targetEpisode, levels)) {
				return false;
			}
			// Within the same episode by just the name, as the original levels have it
			link = (fromEpisode == targetEpisode ? String(levelName) : String(targetEpisode + "/"_s + levelName));
			return true;
		}

		void PerformMove(IEditorContext& session, const MovePlan& plan)
		{
			// Another editor would put an old link back by saving its level
			for (const LinkChange& change : plan.Changes) {
				if (IsLevelOpenElsewhere(change.Path, session.GetFilePath())) {
					session.ShowMessage(_fx("editor", "Level \"{}\" is open in another editor, close it there first", change.Name));
					return;
				}
			}
			// Its own links are a step of its history as any change of its properties, they're saved with it
			if (plan.OwnLinksChanged) {
				LevelDocument& document = session.GetDocument();
				// TRANSLATORS: Name of an undo step in the history, the links of the level moved into an episode to the levels that aren't in any episode
				session.Execute(std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, plan.Properties, _x("editor", "Links to levels of no episode")));
			}
			// Saved as Save As does, the file in "unknown" is removed then, unless it can't be changed
			if (!session.SaveAsFile(plan.NewPath, true)) {
				if (plan.OwnLinksChanged) {
					session.Undo();
				}
				session.ShowMessage(_x("editor", "The level couldn't be moved"));
				return;
			}

			// The links of the other levels, they're kept as they were in ".bak" files
			std::int32_t rewritten = 0, failed = 0;
			if (!plan.Changes.empty()) {
				LevelLookup levels(session);
				for (const LinkChange& change : plan.Changes) {
					bool changed;
					if (!RewriteLinks(change.Path, [&](String& link) { return MoveLink(link, change.Episode, plan.LevelName, plan.Target.Name, levels); }, changed)) {
						failed++;
					} else if (changed) {
						rewritten++;
					}
				}
			}

			// A new episode starts with the level, as does one without a first level or without levels before
			if (plan.IsNew || (plan.Target.Exists && (plan.Target.FirstLevel.empty() || plan.Target.LevelCount == 0))) {
				EpisodeFile episode;
				bool read = true;
				if (plan.IsNew) {
					episode.DisplayName = plan.Target.DisplayName;
					// As the window of the episode starts a new one, after the episodes of the original game
					episode.Position = 100;
				} else {
					read = EpisodeFile::Read(plan.Target.FilePath, episode);
				}
				episode.FirstLevel = plan.LevelName;
				if (!read || !episode.Write(plan.Target.FilePath, !plan.IsNew)) {
					failed++;
				}
			}

			if (failed > 0) {
				session.ShowMessage(_fx("editor", "The level is in episode \"{}\" now, but {} of the other files couldn't be changed", plan.Target.Name, failed));
			} else if (plan.IsCopy) {
				// TRANSLATORS: "unknown" is the name of a folder, keep it as it is
				session.ShowMessage(_fx("editor", "The level was copied into episode \"{}\", the one in \"unknown\" stays as it was", plan.Target.Name));
			} else {
				session.ShowMessage(_fnx("editor", "The level was moved into episode \"{}\", {} level linking to it was changed",
					"The level was moved into episode \"{}\", {} levels linking to it were changed", rewritten, plan.Target.Name, rewritten));
			}
			// It's in the episode now, which is shown
			ShowEpisodeDialog(session);
		}

		// Asks to move the level out of "unknown" into an episode, with all that changes along listed first
		void ConfirmMove(IEditorContext& session, EpisodeTarget&& target, bool isNew)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			StringView filePath = session.GetFilePath();
			auto plan = std::make_shared<MovePlan>();
			plan->Target = Death::move(target);
			plan->IsNew = isNew;
			plan->LevelName = StringUtils::lowercase(fs::GetFileNameWithoutExtension(filePath));
			plan->NewPath = fs::CombinePath(plan->Target.Directory, fs::GetFileName(filePath));
			plan->IsCopy = !session.CanSaveInPlace();

			LevelLookup levels(session);
			if (levels.Exists(plan->Target.Name, plan->LevelName)) {
				session.ShowMessage(_fx("editor", "Episode \"{}\" has a level named \"{}\" already", plan->Target.Name, plan->LevelName));
				return;
			}

			// Its own links to levels of "unknown" by just their names
			String ownChanges;
			plan->Properties = session.GetDocument().Properties;
			struct LinkField { String Label; String Formats::LevelProperties::*Value; };
			static const LinkField fields[] = {
				// TRANSLATORS: Kinds of links of a level to another level (next, secret and bonus level), shown before the name of the level linked
				{ _x("editor", "Next"), &Formats::LevelProperties::NextLevel },
				{ _x("editor", "Secret"), &Formats::LevelProperties::SecretLevel },
				{ _x("editor", "Bonus"), &Formats::LevelProperties::BonusLevel }
			};
			for (const LinkField& field : fields) {
				String& link = plan->Properties.*field.Value;
				String previous = link;
				if (KeepOwnLink(link, plan->LevelName, levels)) {
					plan->OwnLinksChanged = true;
					// TRANSLATORS: In a list of changed links of the level, the old and the new name of the level linked
					ownChanges = String(ownChanges + "\n - "_s + String(field.Label) + ": "_s + _fx("editor", "\"{}\" becomes \"{}\"", previous, link));
				}
			}

			// The levels that link to it in "unknown", which is left without it, of the places that can be written to
			if (!plan->IsCopy) {
				String ownPath = filePath;
				ForEachWritableLevel(session, {}, [&](StringView path, const String& episode) {
					if (IsSameFile(path, ownPath)) {
						return;
					}
					String next, secret, bonus;
					if (ReadLevelLinks(path, next, secret, bonus) && (LeadsToNoEpisodeLevel(next, episode, plan->LevelName, plan->Target.Name, levels) ||
						LeadsToNoEpisodeLevel(secret, episode, plan->LevelName, plan->Target.Name, levels) ||
						LeadsToNoEpisodeLevel(bonus, episode, plan->LevelName, plan->Target.Name, levels))) {
						LinkChange& change = plan->Changes.emplace_back();
						change.Path = path;
						change.Episode = episode;
						change.Name = episode + "/"_s + StringUtils::lowercase(fs::GetFileNameWithoutExtension(path));
					}
				});
			}

			// What changes is listed first, as when a level is renamed
			String newName = plan->Target.Name + "/"_s + plan->LevelName;
			String message;
			if (plan->IsCopy) {
				message = (isNew ? _fx("editor", "Make episode \"{}\" and copy level \"{}\" into it?", plan->Target.Name, plan->LevelName)
					: _fx("editor", "Copy level \"{}\" into episode \"{}\"?", plan->LevelName, plan->Target.Name));
				// TRANSLATORS: {} is the new name of the level as "episode/level", "unknown" is the name of a folder, keep it as it is
				message = String(message + "\n"_s + _fx("editor", "It's saved as \"{}\".", newName) + "\n"_s + _x("editor", "The level in \"unknown\" can't be changed, so it stays as it is."));
			} else {
				message = (isNew ? _fx("editor", "Make episode \"{}\" and move level \"{}\" into it?", plan->Target.Name, plan->LevelName)
					: _fx("editor", "Move level \"{}\" into episode \"{}\"?", plan->LevelName, plan->Target.Name));
				// TRANSLATORS: {} is the new name of the level as "episode/level", "unknown" is the name of a folder, keep it as it is
				message = String(message + "\n"_s + _fx("editor", "It's saved as \"{}\", its file in \"unknown\" is removed.", newName));
			}
			if (session.GetHistory().IsDirty()) {
				message = String(message + "\n"_s + _x("editor", "The changes that weren't saved yet are saved with it."));
			}
			if (plan->OwnLinksChanged) {
				// TRANSLATORS: "unknown/" is the name of a folder put before the names of the levels linked, keep it as it is
				message = String(message + "\n"_s + _x("editor", "Its links to levels of no episode get \"unknown/\", so they lead where they did:") + ownChanges);
			}
			if (!plan->Changes.empty()) {
				message = String(message + "\n"_s + _x("editor", "The links to it change in these levels, their files are kept as .bak:"));
				AppendLevelList(message, plan->Changes);
			}
			if (!plan->IsCopy) {
				message = String(message + "\n"_s + _x("editor", "Moving the file can't be undone."));
			}

			IEditorContext* s = &session;
			// TRANSLATORS: Title of the message confirming that the level is moved into a new or an existing episode
			UiMessageDialog::Show(*surface, isNew ? _x("editor", "New Episode") : _x("editor", "Move to Episode"), message,
				// TRANSLATORS: Buttons of the message confirming that the level is moved or copied into a new or an existing episode
				{ isNew ? _x("editor", "Create") : (plan->IsCopy ? _x("editor", "Copy") : _x("editor", "Move")), _x("editor", "Cancel") },
				[s, plan](std::int32_t button) {
				if (button == 0) {
					PerformMove(*s, *plan);
				}
			});
		}

		void ShowNewEpisodeDialog(IEditorContext& session)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(true, roots);
			if (roots.empty()) {
				session.ShowMessage(_x("editor", "There is no place to save levels to"));
				return;
			}

			struct NewEpisodeSettings
			{
				String DisplayName;
				String Name;
				// The name of the directory follows the display name until it's typed
				bool NameTyped = false;
			};
			auto settings = std::make_shared<NewEpisodeSettings>();
			// A level alone in an episode is often all there is of it so far, its name is the first guess
			StringView filePath = session.GetFilePath();
			const Formats::LevelProperties& properties = session.GetDocument().Properties;
			settings->DisplayName = (properties.DisplayName.empty() ? String(fs::GetFileNameWithoutExtension(filePath)) : properties.DisplayName);
			settings->Name = MakeEpisodeName(settings->DisplayName);
			String levelName = StringUtils::lowercase(fs::GetFileNameWithoutExtension(filePath));
			// Into the first place levels are saved to, as Save As offers
			String directory = roots[0].Path;

			// TRANSLATORS: Title of the dialog that makes a new episode for a level that isn't in any episode
			auto dialog = std::make_unique<UiDialog>(_x("editor", "New Episode"));
			UiDialog* dialogPtr = dialog.get();
			UiStack* content = dialog->GetContent();
			UiForm* form = content->Add<UiForm>(UiTheme::Spacing);
			UiTextBox* nameBox;
			{
				// TRANSLATORS: Label of the name of the episode shown in the game in the New Episode dialog
				auto* row = form->AddRow(_x("editor", "Name"));
				nameBox = row->Add<UiTextBox>(settings->DisplayName);
				nameBox->Width = 220.0f;
				nameBox->MaxLength = 255;
				nameBox->OnChanged = [settings](StringView text) {
					settings->DisplayName = text;
					if (!settings->NameTyped) {
						settings->Name = MakeEpisodeName(text);
					}
				};
				nameBox->Tooltip = _x("editor", "Name shown in the list of episodes");
			}
			{
				// TRANSLATORS: Label of the name of the folder of the episode in the New Episode dialog
				auto* row = form->AddRow(_x("editor", "Folder"));
				auto* box = row->Add<UiTextBox>();
				box->Width = 220.0f;
				box->MaxLength = std::int32_t(MaxEpisodeNameLength);
				box->TextFunc = [settings]() { return settings->Name; };
				box->OnChanged = [settings](StringView text) {
					settings->Name = StringUtils::lowercase(text.trimmed());
					settings->NameTyped = true;
				};
				box->Tooltip = _x("editor", "The episode is known by it, other episodes link to its levels as \"folder/level\"");
			}
			auto* result = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
			// TRANSLATORS: In the New Episode dialog, the new name of the level, {}/{} are the folder of the episode and the name of the level
			result->TextFunc = [settings, levelName]() { return _fx("editor", "The level becomes \"{}/{}\"", settings->Name, levelName); };

			IEditorContext* s = &session;
			// TRANSLATORS: Button in the New Episode dialog
			dialog->AddButton(_x("editor", "Create"), [s, dialogPtr, settings, directory]() {
				if (settings->Name == "unknown"_s) {
					// TRANSLATORS: "unknown" is the name of a folder, keep it as it is
					s->ShowMessage(_x("editor", "\"unknown\" isn't an episode, it's where the levels of no episode are"));
					return;
				}
				if (!IsValidEpisodeName(settings->Name)) {
					s->ShowMessage(_x("editor", "A folder of an episode may have only letters, numbers, - and _"));
					return;
				}
				SmallVector<UiFileRoot, 4> allRoots;
				s->GetFileRoots(false, allRoots);
				if (EpisodeExists(allRoots, settings->Name)) {
					s->ShowMessage(_x("editor", "There is an episode of that name already"));
					return;
				}
				EpisodeTarget target;
				target.Name = settings->Name;
				target.Directory = fs::CombinePath(directory, settings->Name);
				target.FilePath = fs::CombinePath(directory, String(settings->Name + ".j2e"_s));
				String displayName = settings->DisplayName.trimmed();
				target.DisplayName = (displayName.empty() ? settings->Name : displayName);
				dialogPtr->Close();
				ConfirmMove(*s, Death::move(target), true);
			}, true);
			dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });

			ShowModal(*surface, Death::move(dialog));
			// Typed right away
			surface->SetFocus(nameBox);
			nameBox->BeginEdit();
		}

		void ShowMoveToEpisodeDialog(IEditorContext& session)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			auto targets = std::make_shared<SmallVector<EpisodeTarget, 0>>();
			FindEpisodeTargets(session, *targets);
			IEditorContext* s = &session;
			if (targets->empty()) {
				// TRANSLATORS: Title and buttons of the message when there is no episode the level could be moved into
				UiMessageDialog::Show(*surface, _x("editor", "Move to Episode"), _x("editor", "There is no episode the level could be moved into yet."), { _x("editor", "New Episode"), _x("editor", "Cancel") },
					[s](std::int32_t button) {
					if (button == 0) {
						ShowNewEpisodeDialog(*s);
					}
				});
				return;
			}

			String levelName = StringUtils::lowercase(fs::GetFileNameWithoutExtension(session.GetFilePath()));
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(false, roots);

			// TRANSLATORS: Title of the dialog that moves a level that isn't in any episode into an existing episode
			auto dialog = std::make_unique<UiDialog>(_x("editor", "Move to Episode"));
			UiDialog* dialogPtr = dialog.get();
			// As large as the screen allows, the list scrolls
			dialog->MaxScreenHeightFraction = 0.8f;
			dialog->MinScreenWidthFraction = 0.5f;
			UiStack* content = dialog->GetContent();
			content->Add<UiLabel>(_fx("editor", "Episodes the level \"{}\" can be moved into", levelName), UiTheme::TextHeader);
			auto* scroll = content->Add<UiScrollView>();
			scroll->Flex = 1.0f;
			auto* list = scroll->SetContent<UiStack>(UiOrientation::Vertical, 3.0f, UiThickness(0.0f, 0.0f, 3.0f, 0.0f));
			for (std::size_t i = 0; i < targets->size(); i++) {
				const EpisodeTarget& target = (*targets)[i];
				// The level would hide that one, or be hidden by it
				bool taken = LevelExistsIn(roots, target.Name, levelName);
				auto* row = list->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* label = row->Add<UiLabel>(target.DisplayName.empty() ? target.Name : target.DisplayName, UiTheme::Text);
				label->Flex = 1.0f;
				// TRANSLATORS: Button next to an episode in the Move to Episode dialog, moves the level into it
				auto* move = row->Add<UiButton>(_x("editor", "Move Here"), EditorIcon::None, [s, dialogPtr, targets, i]() {
					EpisodeTarget chosen = (*targets)[i];
					dialogPtr->Close();
					ConfirmMove(*s, Death::move(chosen), false);
				});
				move->IsEnabledFunc = [taken]() { return !taken; };

				// TRANSLATORS: Below an episode in the Move to Episode dialog, {} is the number of levels in it
				String details = String(target.Name + "/ - "_s + _fnx("editor", "{} level", "{} levels", target.LevelCount, target.LevelCount));
				if (!target.Exists) {
					// TRANSLATORS: Below an episode in the Move to Episode dialog that has only a folder of levels, no episode file
					details = String(details + " - "_s + _x("editor", "no episode file yet"));
				}
				if (taken) {
					// TRANSLATORS: Below an episode in the Move to Episode dialog, {} is the name of the level being moved
					details = String(details + " - "_s + _fx("editor", "has a level named \"{}\" already", levelName));
				}
				list->Add<UiLabel>(details, taken ? UiTheme::TextDanger : UiTheme::TextDim);
			}

			ShowModal(*surface, Death::move(dialog));
		}

		// A level of "unknown" isn't in any episode, a new one can be made for it, or it can be moved into one
		void ShowNoEpisodeChoice(IEditorContext& session, const std::shared_ptr<EpisodeState>& state)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the message for a level that isn't in any episode, "unknown" is the name of a folder, keep it as it is
			UiMessageDialog::Show(*surface, _x("editor", "Episode"), _x("editor", "The level is in \"unknown\", where the levels of no episode are.\nDo you want to make a new episode for it, or move it into an existing one?"),
				// TRANSLATORS: Buttons of the message for a level that isn't in any episode, the third one shows the levels that aren't in any episode
				{ _x("editor", "New Episode"), _x("editor", "Move to Episode"), _x("editor", "Levels of No Episode"), _x("editor", "Cancel") }, [s, state](std::int32_t button) {
				if (button == 0) {
					ShowNewEpisodeDialog(*s);
				} else if (button == 1) {
					ShowMoveToEpisodeDialog(*s);
				} else if (button == 2) {
					// The levels of "unknown", to open or rename them
					ShowEpisodeWindow(*s, state);
				}
			});
		}
	}

	void ShowEpisodeDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		StringView filePath = session.GetFilePath();
		if (filePath.empty()) {
			// The episode is the directory the level is saved into, it's shown once it's saved
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the message shown instead of the Episode dialog for a level that wasn't saved yet
			UiMessageDialog::Show(*surface, _x("editor", "Episode"), _x("editor", "The level wasn't saved yet, its episode is the folder it's saved into.\nDo you want to save it now?"),
				// TRANSLATORS: Buttons of the message for a level that wasn't saved yet, the first one opens the Save As dialog
				{ _x("editor", "Save As"), _x("editor", "Cancel") }, [s](std::int32_t button) {
				if (button == 0) {
					s->ShowSaveAsDialog(false, [s]() { ShowEpisodeDialog(*s); });
				}
			});
			return;
		}

		auto state = std::make_shared<EpisodeState>();
		state->Directory = fs::GetDirectoryName(filePath);
		state->Name = fs::GetFileName(state->Directory);
		StringUtils::lowercaseInPlace(state->Name);
		// The levels of no episode, the game looks for a level there when it isn't found in its episode
		state->IsNoEpisode = (state->Name == "unknown"_s);
		if (state->IsNoEpisode) {
			ShowNoEpisodeChoice(session, state);
			return;
		}

		// The episode file is next to the directory of its levels, named as it is
		state->FilePath = fs::CombinePath(fs::GetDirectoryName(state->Directory), String(fs::GetFileName(state->Directory) + ".j2e"_s));
		state->Exists = EpisodeFile::Read(state->FilePath, state->Episode);
		if (!state->Exists) {
			// A new episode starts with the level being edited
			state->Episode = EpisodeFile();
			state->Episode.DisplayName = fs::GetFileName(state->Directory);
			state->Episode.FirstLevel = fs::GetFileNameWithoutExtension(filePath);
			StringUtils::lowercaseInPlace(state->Episode.FirstLevel);
			state->Episode.Position = 100;
		}
		ShowEpisodeWindow(session, state);
	}
}

#endif
